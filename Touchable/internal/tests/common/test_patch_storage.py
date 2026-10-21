"""Use synthetic inputs only. Never load the user's game or keys in unit tests."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from game_formats import GameError
from nca_header import _ENCRYPT, _aes_ecb, xts_aes128
from patch_storage import (NODE_SIZE, Nca, PatchSection, aes_ctr, bucket_tree, counter,
                           read_exefs, verify_ivfc)

HEADER_KEY = bytes(range(32))
TITLEKEK = bytes(range(100, 116))
TITLE_KEY = bytes(range(200, 216))
SECTION_OFFSET = 0xC00


def tree(entries, end, entry_size, pack):
    """Build a one-node bucket tree table and its 16-byte header."""
    per_set = (NODE_SIZE - 16) // entry_size
    sets = [entries[i:i + per_set] for i in range(0, len(entries), per_set)]
    root = struct.pack("<iiq", 0, len(sets), end) + b"".join(struct.pack("<q", s[0][0]) for s in sets)
    table = root.ljust(NODE_SIZE, b"\0")
    for index, bucket in enumerate(sets):
        node_end = sets[index + 1][0][0] if index + 1 < len(sets) else end
        node = struct.pack("<iiq", index, len(bucket), node_end) + b"".join(pack(e) for e in bucket)
        table += node.ljust(NODE_SIZE, b"\0")
    return table, struct.pack("<4sIii", b"BKTR", 1, len(entries), 0)


def ctr_encrypt(key, secure, generation, absolute, data):
    # CTR encryption and decryption are the same operation.
    return aes_ctr(key, counter(secure, generation, absolute), data)


def build_nca(path, sections):
    """sections: list of (fs header without hashes, encrypted section bytes)."""
    header = bytearray(0xC00)
    header[0x200:0x204] = b"NCA3"
    header[0x205] = 0
    header[0x230:0x240] = bytes(range(1, 17))
    offset, body = SECTION_OFFSET, b""
    for index, (fs, data) in enumerate(sections):
        assert offset % 0x200 == 0 and len(data) % 0x200 == 0
        struct.pack_into("<II", header, 0x240 + 16 * index, offset // 0x200, (offset + len(data)) // 0x200)
        header[0x400 + 0x200 * index:0x600 + 0x200 * index] = fs
        header[0x280 + 32 * index:0x2a0 + 32 * index] = hashlib.sha256(fs).digest()
        body += data
        offset += len(data)
    encrypted = b"".join(xts_aes128(_ENCRYPT, HEADER_KEY, bytes(header[i * 0x200:(i + 1) * 0x200]), i) for i in range(6))
    path.write_bytes(encrypted + body)
    keys = {"header_key": HEADER_KEY, "titlekek_00": TITLEKEK}
    return Nca(path, keys, _aes_ecb(_ENCRYPT, TITLEKEK, TITLE_KEY).hex())


def ivfc_section(data, block_shift=9):
    """Return (section bytes, fs header) for a three-level IVFC tree over data."""
    block = 1 << block_shift
    data_level = data.ljust((len(data) + block - 1) // block * block, b"\0")
    hashes = b"".join(hashlib.sha256(data_level[i:i + block]).digest() for i in range(0, len(data_level), block))
    level1 = hashes.ljust(block, b"\0")
    master = hashlib.sha256(level1).digest()
    fs = bytearray(0x200)
    fs[2], fs[3] = 0, 3
    fs[8:12] = b"IVFC"
    struct.pack_into("<I", fs, 0x14, 3)
    struct.pack_into("<QQI", fs, 0x18, 0, len(hashes), block_shift)
    struct.pack_into("<QQI", fs, 0x30, block, len(data), block_shift)
    fs[0xC8:0xE8] = master
    return level1 + data, fs


class BucketTreeTests(unittest.TestCase):
    def test_round_trip_and_bounds(self):
        entries = [(i * 0x40, i * 0x10, i % 2) for i in range(1000)]
        table, header = tree(entries, 1000 * 0x40, 20, lambda e: struct.pack("<qqi", *e))
        parsed, end = bucket_tree(table, header, 20, lambda b, o: struct.unpack_from("<qqi", b, o))
        self.assertEqual(parsed, entries)
        self.assertEqual(end, 64000)
        with self.assertRaises(GameError):
            bucket_tree(table[:NODE_SIZE * 2], header, 20, lambda b, o: struct.unpack_from("<qqi", b, o))
        unordered = entries[:]
        unordered[5] = (unordered[4][0], 0, 0)
        table, header = tree(unordered, 64000, 20, lambda e: struct.pack("<qqi", *e))
        with self.assertRaises(GameError):
            bucket_tree(table, header, 20, lambda b, o: struct.unpack_from("<qqi", b, o))


class PatchSectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def patch(self, generation=7, secure=2):
        base = bytes((i * 7 + 3) & 0xFF for i in range(0x400))
        patch_plain = bytes((i * 13 + 1) & 0xFF for i in range(0x80))
        # Virtual: base 0x200..0x300, patch 0x00..0x80, base 0x000..0x080.
        indirect = [(0, 0x200, 0), (0x100, 0, 1), (0x180, 0, 0)]
        expected = base[0x200:0x300] + patch_plain + base[:0x80]
        indirect_offset = 0x80
        indirect_table, indirect_header = tree(indirect, 0x200, 20, lambda e: struct.pack("<qqi", *e))
        aes_offset = indirect_offset + len(indirect_table)
        # Patch data: [0, 0x40) uses generation 5, [0x40, 0x80) is stored in plain text.
        aes_entries = [(0, 0, 5), (0x40, 1, 0)]
        aes_table, aes_header = tree(aes_entries, indirect_offset, 16, lambda e: struct.pack("<qB3xi", *e))
        data = ctr_encrypt(TITLE_KEY, secure, 5, SECTION_OFFSET, patch_plain[:0x40]) + patch_plain[0x40:]
        tables = indirect_table + aes_table
        data += ctr_encrypt(TITLE_KEY, secure, generation, SECTION_OFFSET + indirect_offset, tables)
        data = data.ljust((len(data) + 0x1FF) // 0x200 * 0x200, b"\0")
        fs = bytearray(0x200)
        fs[2], fs[3], fs[4] = 0, 3, 4
        struct.pack_into("<QQ", fs, 0x100, indirect_offset, len(indirect_table))
        fs[0x110:0x120] = indirect_header
        struct.pack_into("<QQ", fs, 0x120, aes_offset, len(aes_table))
        fs[0x130:0x140] = aes_header
        struct.pack_into("<II", fs, 0x140, generation, secure)
        nca = build_nca(self.root / "patch.nca", [(bytes(fs), data)])
        base_path = self.root / "base.section"
        base_path.write_bytes(base)
        return nca, base_path, expected

    def test_virtual_section_uses_both_sources_and_counters(self):
        nca, base, expected = self.patch()
        section = PatchSection(nca, 0)
        output = self.root / "virtual.section"
        self.assertEqual(section.write_virtual(base, output, chunk=0x30), {"base": 2, "patch": 1})
        self.assertEqual(output.read_bytes(), expected)

    def test_wrong_title_key_does_not_decode(self):
        nca, base, expected = self.patch()
        nca.key = bytes(16)
        with self.assertRaises(GameError):
            PatchSection(nca, 0)

    def test_base_range_must_exist(self):
        nca, base, _ = self.patch()
        base.write_bytes(base.read_bytes()[:0x100])
        with self.assertRaises(GameError):
            PatchSection(nca, 0).write_virtual(base, self.root / "short.section")


class IvfcTests(unittest.TestCase):
    def test_every_level_is_checked(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "section"
            section, fs = ivfc_section(bytes(range(256)) * 9)
            path.write_bytes(section)
            self.assertEqual(verify_ivfc(path, fs), 1 + 5)
            for position in (0, 512 + 1000, len(section) - 1):
                changed = bytearray(section)
                changed[position] ^= 1
                path.write_bytes(changed)
                with self.assertRaises(GameError):
                    verify_ivfc(path, fs)


class ExeFsTests(unittest.TestCase):
    def test_hashed_pfs0_is_checked_and_written(self):
        files = {"main": b"NSO0" + bytes(300), "main.npdm": b"META" + bytes(40)}
        names = b"".join(name.encode() + b"\0" for name in files)
        records, blobs, offset, name_offset = b"", b"", 0, 0
        for name, blob in files.items():
            records += struct.pack("<QQII", offset, len(blob), name_offset, 0)
            offset += len(blob)
            name_offset += len(name) + 1
            blobs += blob
        pfs0 = struct.pack("<4sIII", b"PFS0", len(files), len(names), 0) + records + names + blobs
        block = 0x100
        table = b"".join(hashlib.sha256(pfs0[i:i + block]).digest() for i in range(0, len(pfs0), block))
        plain = table.ljust(0x200, b"\0") + pfs0
        plain = plain.ljust((len(plain) + 0x1FF) // 0x200 * 0x200, b"\0")
        fs = bytearray(0x200)
        fs[2], fs[3], fs[4] = 1, 2, 3
        fs[8:0x28] = hashlib.sha256(table).digest()
        struct.pack_into("<IIQQQQ", fs, 0x28, block, 2, 0, len(table), 0x200, len(pfs0))
        struct.pack_into("<II", fs, 0x140, 0, 1)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data = ctr_encrypt(TITLE_KEY, 1, 0, SECTION_OFFSET, plain)
            nca = build_nca(root / "exefs.nca", [(bytes(fs), data)])
            self.assertEqual(read_exefs(nca, 0, root / "ExeFS"), sorted(files))
            for name, blob in files.items():
                self.assertEqual((root / "ExeFS" / name).read_bytes(), blob)
            changed = bytearray(data)
            changed[0x200 + 40] ^= 1
            nca = build_nca(root / "bad.nca", [(bytes(fs), bytes(changed))])
            with self.assertRaises(GameError):
                read_exefs(nca, 0, root / "Bad")


if __name__ == "__main__":
    unittest.main()
