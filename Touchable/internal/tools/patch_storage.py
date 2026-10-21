"""Read a Switch update (patch) Program NCA on top of its base Program.

The update RomFS section is a virtual section. An indirect bucket tree maps each
range to the decrypted base section or to the patch data. Patch data uses
AES-CTR with a per-range counter generation. The result must match the patch's
own IVFC hash tree before the RomFS compression layer is expanded.

Format references:
https://switchbrew.org/wiki/NCA#PatchInfo
https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/fssystem/fssystem_indirect_storage.hpp
https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/fssystem/fssystem_aes_ctr_counter_extended_storage.hpp
"""

import bisect
import ctypes
import hashlib
from pathlib import Path
import struct

from game_formats import GameError, read_exact
from nca_header import SECTOR_SIZE, _DECRYPT, _aes_ecb, _crypto, xts_aes128

NODE_SIZE = 0x4000
HEADER_SIZE = 0xC00

_crypto.CCCryptorCreateWithMode.argtypes = [
    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
_crypto.CCCryptorCreateWithMode.restype = ctypes.c_int32
_crypto.CCCryptorUpdate.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                                    ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
_crypto.CCCryptorUpdate.restype = ctypes.c_int32
_crypto.CCCryptorRelease.argtypes = [ctypes.c_void_p]


def aes_ctr(key, counter, data):
    """AES-128-CTR with a 128-bit big-endian counter."""
    reference = ctypes.c_void_p()
    # kCCDecrypt = 1, kCCModeCTR = 4, kCCAlgorithmAES = 0, ccNoPadding = 0, kCCModeOptionCTR_BE = 2.
    if _crypto.CCCryptorCreateWithMode(1, 4, 0, 0, counter, key, len(key), None, 0, 0, 2, ctypes.byref(reference)):
        raise GameError("AES-CTR setup failed.")
    try:
        output, moved = ctypes.create_string_buffer(len(data)), ctypes.c_size_t()
        if _crypto.CCCryptorUpdate(reference, data, len(data), output, len(data), ctypes.byref(moved)) \
                or moved.value != len(data):
            raise GameError("AES-CTR decryption failed.")
        return output.raw
    finally:
        _crypto.CCCryptorRelease(reference)


def counter(secure_value, generation, offset):
    return struct.pack(">IIQ", secure_value, generation, offset >> 4)


def read_keys(path):
    keys = {}
    for line in path.read_text().splitlines():
        name, separator, value = line.partition("=")
        if separator:
            keys[name.strip()] = bytes.fromhex(value.strip())
    return keys


def decrypt_header(path, header_key):
    with path.open("rb") as stream:
        raw = read_exact(stream, HEADER_SIZE)
    header = b"".join(xts_aes128(_DECRYPT, header_key, raw[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE], i)
                      for i in range(HEADER_SIZE // SECTOR_SIZE))
    if header[0x200:0x204] != b"NCA3":
        raise GameError("The NCA header could not be decrypted. Check prod.keys.")
    return header


class Nca:
    """A Program NCA that uses a title key (rights ID) for its sections."""

    def __init__(self, path, keys, encrypted_title_key):
        self.path = path
        self.header = decrypt_header(path, keys["header_key"])
        if not any(self.header[0x230:0x240]):
            raise GameError("Expected a Program NCA that uses a title key.")
        generation = max(self.header[0x206], self.header[0x220])
        name = "titlekek_%02x" % (generation - 1 if generation else 0)
        if name not in keys:
            raise GameError("The key set has no " + name + ".")
        self.key = _aes_ecb(_DECRYPT, keys[name], bytes.fromhex(encrypted_title_key))

    def sections(self):
        """Return (index, filesystem type, hash type, encryption type) for each present section."""
        result = []
        for index in range(4):
            start, end = struct.unpack_from("<II", self.header, 0x240 + 16 * index)
            if end > start:
                fs = self.header[0x400 + 0x200 * index:0x600 + 0x200 * index]
                result.append((index, fs[2], fs[3], fs[4]))
        return result

    def section(self, index):
        start, end = struct.unpack_from("<II", self.header, 0x240 + 16 * index)
        if not end > start:
            raise GameError("The NCA has no section " + str(index) + ".")
        fs = self.header[0x400 + 0x200 * index:0x600 + 0x200 * index]
        if hashlib.sha256(fs).digest() != self.header[0x280 + 32 * index:0x2a0 + 32 * index]:
            raise GameError("An NCA filesystem header hash does not match.")
        return start * 0x200, (end - start) * 0x200, fs

    def read_ctr(self, stream, section_offset, generation, secure_value, offset, size):
        """Read plain AES-CTR data at a section-relative offset."""
        start, end = offset & ~15, (offset + size + 15) & ~15
        stream.seek(section_offset + start)
        data = aes_ctr(self.key, counter(secure_value, generation, section_offset + start), read_exact(stream, end - start))
        return data[offset - start:offset - start + size]


def bucket_tree(table, header, entry_size, unpack):
    """Return (entries, end offset) for a bucket tree with one L1 node."""
    magic, version, count, reserved = struct.unpack("<4sIii", header)
    if magic != b"BKTR" or version != 1 or reserved or not 0 < count <= 10_000_000:
        raise GameError("Invalid bucket tree header.")
    per_set = (NODE_SIZE - 16) // entry_size
    sets = (count + per_set - 1) // per_set
    if sets > (NODE_SIZE - 16) // 8:
        raise GameError("Bucket trees with L2 nodes are not supported.")
    if len(table) < (1 + sets) * NODE_SIZE:
        raise GameError("The bucket tree is truncated.")
    index, node_count, end = struct.unpack_from("<iiq", table, 0)
    starts = struct.unpack_from("<%dq" % sets, table, 16)
    if index != 0 or node_count != sets or end <= 0:
        raise GameError("Invalid bucket tree root node.")
    entries = []
    for number in range(sets):
        node = NODE_SIZE * (1 + number)
        index, node_count, node_end = struct.unpack_from("<iiq", table, node)
        if index != number or node_count != min(per_set, count - len(entries)):
            raise GameError("Invalid bucket tree entry node.")
        bucket = [unpack(table, node + 16 + i * entry_size) for i in range(node_count)]
        if bucket[0][0] != starts[number] or node_end != (starts[number + 1] if number + 1 < sets else end):
            raise GameError("Bucket tree offsets do not match.")
        entries.extend(bucket)
    offsets = [entry[0] for entry in entries]
    if offsets[0] != 0 or any(b <= a for a, b in zip(offsets, offsets[1:])) or offsets[-1] >= end:
        raise GameError("Bucket tree entries are not ordered.")
    return entries, end


def ivfc_levels(fs):
    if fs[8:12] != b"IVFC":
        raise GameError("Expected an IVFC section.")
    levels = struct.unpack_from("<I", fs, 0x14)[0]
    if not 2 <= levels <= 7:
        raise GameError("Unsupported IVFC level count.")
    result = [struct.unpack_from("<QQI", fs, 0x18 + 24 * i) for i in range(levels - 1)]
    master = fs[0xC8:0xE8]
    return result, master


def verify_ivfc(path, fs, progress=None):
    """Check every IVFC level and the master hash. Return the checked block count."""
    levels, master = ivfc_levels(fs)
    checked = 0
    with path.open("rb") as stream:
        for number, (offset, size, block_shift) in enumerate(levels):
            block = 1 << block_shift
            if number == 0:
                expected = master
            else:
                previous = levels[number - 1]
                stream.seek(previous[0])
                expected = read_exact(stream, previous[1])
            count = (size + block - 1) // block
            if len(expected) < count * 32:
                raise GameError("IVFC hash level is too small.")
            stream.seek(offset)
            for index in range(count):
                data = read_exact(stream, min(block, size - index * block))
                # IVFC hashes a final partial block with zero padding.
                if hashlib.sha256(data.ljust(block, b"\0")).digest() != expected[index * 32:index * 32 + 32]:
                    raise GameError("IVFC level %d block %d does not match its hash." % (number + 1, index))
                checked += 1
            if progress:
                progress(number + 1, len(levels))
    return checked


class PatchSection:
    """Build the virtual RomFS section of an update Program NCA."""

    def __init__(self, nca, index=1):
        self.nca = nca
        self.offset, self.size, self.fs = nca.section(index)
        fs = self.fs
        if fs[3] != 3 or fs[4] != 4:
            raise GameError("Expected an IVFC RomFS section with AES-CTR-EX patch encryption.")
        self.generation, self.secure_value = struct.unpack_from("<II", fs, 0x140)
        indirect_offset, indirect_size = struct.unpack_from("<QQ", fs, 0x100)
        aes_offset, aes_size = struct.unpack_from("<QQ", fs, 0x120)
        if not (0 < indirect_offset < indirect_offset + indirect_size <= aes_offset
                and aes_offset + aes_size <= self.size):
            raise GameError("Unsupported patch table layout.")
        with nca.path.open("rb") as stream:
            aes_table = nca.read_ctr(stream, self.offset, self.generation, self.secure_value, aes_offset, aes_size)
            self.aes_entries, aes_end = bucket_tree(aes_table, fs[0x130:0x140], 16,
                                                    lambda b, o: struct.unpack_from("<qB3xi", b, o))
            # The counter table covers the patch data. Both tables use the section counter.
            if aes_end != indirect_offset:
                raise GameError("The AES-CTR-EX table does not end at the indirect table.")
            self.aes_starts = [entry[0] for entry in self.aes_entries]
            self.aes_end = aes_end
            table = self.read_patch(stream, indirect_offset, indirect_size)
        self.indirect, self.virtual_size = bucket_tree(table, fs[0x110:0x120], 20,
                                                       lambda b, o: struct.unpack_from("<qqi", b, o))
        if any(entry[2] not in (0, 1) for entry in self.indirect):
            raise GameError("Unknown indirect storage index.")

    def read_patch(self, stream, offset, size):
        """Read decrypted patch data. Each range uses its own counter generation."""
        parts = []
        while size:
            if offset >= self.aes_end:
                # Tables after the AES-CTR-EX data use the section counter.
                parts.append(self.nca.read_ctr(stream, self.offset, self.generation, self.secure_value, offset, size))
                break
            index = bisect.bisect_right(self.aes_starts, offset) - 1
            limit = self.aes_starts[index + 1] if index + 1 < len(self.aes_starts) else self.aes_end
            length = min(size, limit - offset)
            start, encryption, generation = self.aes_entries[index]
            if encryption == 0:
                parts.append(self.nca.read_ctr(stream, self.offset, generation, self.secure_value, offset, length))
            elif encryption == 1:
                stream.seek(self.offset + offset)
                parts.append(read_exact(stream, length))
            else:
                raise GameError("Unknown AES-CTR-EX encryption value.")
            offset += length
            size -= length
        return b"".join(parts)

    def write_virtual(self, base_section, destination, chunk=8 << 20):
        """Write the full virtual section. base_section is the decrypted base RomFS section."""
        base_size = base_section.stat().st_size
        counts = {"base": 0, "patch": 0}
        with self.nca.path.open("rb") as patch, base_section.open("rb") as base, destination.open("xb") as target:
            for number, (virtual, physical, storage) in enumerate(self.indirect):
                end = self.indirect[number + 1][0] if number + 1 < len(self.indirect) else self.virtual_size
                if target.tell() != virtual:
                    raise GameError("Indirect storage entries are not contiguous.")
                size = end - virtual
                if storage == 0:
                    if physical < 0 or physical + size > base_size:
                        raise GameError("An indirect entry reads beyond the base section.")
                    base.seek(physical)
                    while size:
                        target.write(read_exact(base, min(chunk, size)))
                        size -= min(chunk, size)
                    counts["base"] += 1
                else:
                    if physical < 0 or physical + size > self.aes_end:
                        raise GameError("An indirect entry reads beyond the patch data.")
                    while size:
                        length = min(chunk, size)
                        target.write(self.read_patch(patch, physical, length))
                        physical += length
                        size -= length
                    counts["patch"] += 1
        return counts


def read_exefs(nca, index, destination):
    """Decrypt a plain AES-CTR PFS0 section, check its SHA-256 layers, and write its files."""
    offset, size, fs = nca.section(index)
    if fs[3] != 2 or fs[4] != 3:
        raise GameError("Expected a hashed PFS0 section with AES-CTR encryption.")
    generation, secure_value = struct.unpack_from("<II", fs, 0x140)
    master, block, layers = fs[8:0x28], *struct.unpack_from("<II", fs, 0x28)
    hash_offset, hash_size, data_offset, data_size = struct.unpack_from("<QQQQ", fs, 0x30)
    if layers != 2 or not block or hash_offset + hash_size > size or data_offset + data_size > size:
        raise GameError("Unsupported PFS0 hash layout.")
    with nca.path.open("rb") as stream:
        table = nca.read_ctr(stream, offset, generation, secure_value, hash_offset, hash_size)
        data = nca.read_ctr(stream, offset, generation, secure_value, data_offset, data_size)
    if hashlib.sha256(table).digest() != master:
        raise GameError("The ExeFS hash table does not match its master hash.")
    for number in range(0, data_size, block):
        if hashlib.sha256(data[number:number + block]).digest() != table[number // block * 32:number // block * 32 + 32]:
            raise GameError("An ExeFS block does not match its hash.")
    magic, count, strings_size, _ = struct.unpack_from("<4sIII", data, 0)
    if magic != b"PFS0" or not 0 < count <= 64:
        raise GameError("The ExeFS is not a PFS0 file system.")
    strings = data[16 + 24 * count:16 + 24 * count + strings_size]
    base = 16 + 24 * count + strings_size
    destination.mkdir(parents=True)
    names = []
    for number in range(count):
        file_offset, file_size, name_offset, _ = struct.unpack_from("<QQII", data, 16 + 24 * number)
        name = strings[name_offset:strings.index(b"\0", name_offset)].decode()
        if not name or "/" in name or name in (".", "..") or base + file_offset + file_size > len(data):
            raise GameError("The ExeFS has an unsafe or truncated entry.")
        (destination / name).write_bytes(data[base + file_offset:base + file_offset + file_size])
        names.append(name)
    return sorted(names)
