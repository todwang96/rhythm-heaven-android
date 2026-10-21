"""Use synthetic inputs only. Never load the user's game or keys in unit tests."""
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import lz4.block

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from game_formats import GameError, read_exact
from prepare_game import inspect, read_pfs0, run_hactool, write_nca_keyset
from nca_header import gamecard_header, xts_aes128
from romfs_storage import expand_romfs, NODE_SIZE


def package(names, blobs, offsets=None):
    names_blob = b""
    records = []
    running = 0
    for i, (name, data) in enumerate(zip(names, blobs)):
        records.append(struct.pack("<QQII", running if offsets is None else offsets[i], len(data), len(names_blob), 0))
        names_blob += name.encode() + b"\0"
        running += len(data)
    return struct.pack("<4sIII", b"PFS0", len(names), len(names_blob), 0) + b"".join(records) + names_blob + b"".join(blobs)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, data):
        path = self.root / "synthetic.nsp"
        path.write_bytes(data)
        return path

    def test_valid_ranges_and_truncation(self):
        data = package(["first", "second"], [b"abc", b"1234"])
        rows = read_pfs0(self.write(data))
        self.assertEqual([r["size"] for r in rows], [3, 4])
        self.assertEqual(rows[0]["offset"] + 3, rows[1]["offset"])
        with self.assertRaises(GameError) as failure: read_pfs0(self.write(data[:-1]))
        self.assertIn("needs " + str(len(data)) + " bytes, but the file has " + str(len(data) - 1), str(failure.exception))
        with self.assertRaises(GameError): read_exact(io.BytesIO(b"abc"), 4)

    def test_unsafe_duplicate_and_overlap(self):
        for names in (["../outside"], ["a/b"], ["A", "a"], ["a\\b"], ["."]):
            with self.subTest(names=names), self.assertRaises(GameError):
                read_pfs0(self.write(package(names, [b"12"] * len(names))))
        with self.assertRaises(GameError):
            read_pfs0(self.write(package(["a", "b"], [b"12", b"34"], [0, 1])))

    def test_report_omits_ticket_secret(self):
        ticket = bytearray(0x2b0)
        struct.pack_into("<I", ticket, 0, 0x10004)
        secret = bytes(range(16))
        ticket[0x180:0x190] = secret
        report = inspect(self.write(package(["test.tik"], [ticket])))
        self.assertNotIn("encrypted_title_key", report["tickets"][0])
        self.assertNotIn(secret.hex(), str(report))

    def test_key_filter_permissions_and_redacted_errors(self):
        source, output = self.root / "input.keys", self.root / "filtered.keys"
        source.write_text("header_key = " + "0" * 64 + "\nmaster_key_12 = " + "1" * 32 + "\nunrelated_console_secret = invalid\n")
        write_nca_keyset(source, output)
        self.assertEqual(output.stat().st_mode & 0o777, 0o600)
        self.assertNotIn("unrelated", output.read_text())
        result = type("Result", (), {"returncode": 1, "stdout": "private-key-material"})()
        with patch("prepare_game.subprocess.run", return_value=result), self.assertRaises(GameError) as failure:
            run_hactool(Path("unused"), output, [])
        self.assertNotIn("private-key-material", str(failure.exception))

    def test_xts_known_answers(self):
        # IEEE P1619 XTS-AES-128 vectors 1 and 2. Nintendo writes the sector number big-endian.
        self.assertEqual(xts_aes128(0, bytes(32), bytes(32), 0).hex(),
                         "917cf69ebd68b2ec9b9fe9a3eadda692cd43d2f59598ed858c02c2652fbf922e")
        sector = int.from_bytes(bytes.fromhex("3333333333") + bytes(11), "big")
        self.assertEqual(xts_aes128(0, b"\x11" * 16 + b"\x22" * 16, b"\x44" * 32, sector).hex(),
                         "c454185e6a16936e39334038acef838bfb186fff7480adc4289382ecd6d394f0")

    def test_gamecard_header_restores_only_the_distribution_type(self):
        key = bytes(range(32))
        plain = bytearray(0x200)
        plain[:5] = b"NCA3\x01"
        encrypt = lambda data: xts_aes128(0, key, bytes(data), 1)
        original = b"S" * 0x200 + encrypt(plain) + b"F" * 0x800
        converted = bytearray(plain)
        converted[4] = 0
        self.assertEqual(gamecard_header(original[:0x200] + encrypt(converted) + original[0x400:], key), original)
        self.assertIsNone(gamecard_header(original, key))
        plain[:4] = b"NCA2"
        plain[4] = 0
        self.assertIsNone(gamecard_header(b"S" * 0x200 + encrypt(plain), key))

    def compressed_fixture(self, invalid=False):
        decoded = bytes(range(32))
        compressed = lz4.block.compress(decoded, store_size=False)
        data_offset, table_offset = 0x100, 0x100
        table = bytearray(NODE_SIZE * 2)
        struct.pack_into("<iiqq", table, 0, 0, 1, 48, 0)
        struct.pack_into("<iiq", table, NODE_SIZE, 0, 3, 48)
        for i, row in enumerate([(0, 0, 0, 0), (4, 0, 1, 0), (16, 17 if invalid else 16, 3, len(compressed))]):
            struct.pack_into("<qqB3xi", table, NODE_SIZE + 16 + 24 * i, *row)
        raw = bytearray(data_offset + table_offset)
        raw[data_offset:data_offset + 4] = b"abcd"
        raw[data_offset + 16:data_offset + 16 + len(compressed)] = compressed
        raw.extend(table)
        header = bytearray(512)
        header[2:4], header[8:12] = b"\0\3", b"IVFC"
        struct.pack_into("<I", header, 0x14, 2)
        struct.pack_into("<QQ", header, 0x18, data_offset, len(raw) - data_offset)
        struct.pack_into("<QQ4sIII", header, 0x178, table_offset, len(table), b"BKTR", 1, 3, 0)
        return self.write(raw), header, b"abcd" + bytes(12) + decoded

    def test_plain_zero_and_lz4_storage(self):
        source, header, expected = self.compressed_fixture()
        target = self.root / "romfs.bin"
        report = expand_romfs(source, header, target)
        self.assertEqual(target.read_bytes(), expected)
        self.assertEqual(report["blocks"], {"plain": 1, "zeros": 1, "lz4": 1})

    def test_reject_bad_compression_before_output(self):
        source, header, _ = self.compressed_fixture(invalid=True)
        target = self.root / "romfs.bin"
        with self.assertRaises(GameError): expand_romfs(source, header, target)
        self.assertFalse(target.exists())


if __name__ == "__main__": unittest.main()
