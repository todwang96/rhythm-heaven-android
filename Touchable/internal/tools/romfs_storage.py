"""Expand an NCA RomFS compression layer before extracting its files.

Format references:
https://switchbrew.org/wiki/NCA#CompressionInfo
https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/fssystem/fssystem_compressed_storage.hpp
"""

from pathlib import Path
import shutil
import struct

from game_formats import GameError, read_exact

NODE_SIZE = 0x4000
ENTRY_SIZE = 24
ENTRIES_PER_NODE = (NODE_SIZE - 16) // ENTRY_SIZE


def copy_range(source, target, offset, size):
    source.seek(offset)
    while size:
        chunk = read_exact(source, min(size, 4 * 1024 * 1024))
        target.write(chunk)
        size -= len(chunk)


def expand_romfs(section: Path, fs_header: bytes, destination: Path):
    if len(fs_header) != 512 or fs_header[2:4] != bytes((0, 3)) or fs_header[8:12] != b"IVFC":
        raise GameError("Expected an IVFC RomFS section header.")
    levels = struct.unpack_from("<I", fs_header, 0x14)[0]
    if not 2 <= levels <= 7:
        raise GameError("Unsupported IVFC level count.")
    data_offset, data_size = struct.unpack_from("<QQ", fs_header, 0x18 + (levels - 2) * 24)
    if data_offset + data_size > section.stat().st_size:
        raise GameError("IVFC data extends beyond the section.")
    table_offset, table_size, magic, version, count, reserved = struct.unpack_from("<QQ4sIII", fs_header, 0x178)
    with section.open("rb") as source:
        if table_size == 0:
            with destination.open("xb") as target:
                copy_range(source, target, data_offset, data_size)
            return {"compression": "none", "bytes": data_size}
        if magic != b"BKTR" or version != 1 or reserved or not 0 < count <= 1_000_000:
            raise GameError("Invalid compression table header.")
        if table_offset + table_size > data_size:
            raise GameError("Compression table extends beyond the IVFC data.")
        sets = (count + ENTRIES_PER_NODE - 1) // ENTRIES_PER_NODE
        # This profile only needs a single index node. Reject larger trees explicitly.
        if sets > (NODE_SIZE - 16) // 8 or (sets + 1) * NODE_SIZE > table_size:
            raise GameError("Unsupported or truncated compression index.")
        source.seek(data_offset + table_offset)
        root_index, root_count, expanded_size = struct.unpack("<iiq", read_exact(source, 16))
        if root_index != 0 or root_count != sets or not 0 < expanded_size <= 100 * 1024**3:
            raise GameError("Invalid compression root node.")
        if shutil.disk_usage(destination.parent).free < expanded_size + 256 * 1024**2:
            raise GameError("There is not enough disk space for the expanded RomFS.")
        starts = struct.unpack("<" + "q" * sets, read_exact(source, sets * 8))
        entries = []
        for index in range(sets):
            source.seek(data_offset + table_offset + (index + 1) * NODE_SIZE)
            node_index, node_count, node_end = struct.unpack("<iiq", read_exact(source, 16))
            expected = min(ENTRIES_PER_NODE, count - len(entries))
            if node_index != index or node_count != expected:
                raise GameError("Invalid compression entry node.")
            bucket = [struct.unpack("<qqB3xi", read_exact(source, ENTRY_SIZE)) for _ in range(node_count)]
            if bucket[0][0] != starts[index]:
                raise GameError("Compression index does not match its entries.")
            if node_end != (starts[index + 1] if index + 1 < sets else expanded_size):
                raise GameError("Compression entry node has an invalid end offset.")
            entries.extend(bucket)
        if len(entries) != count or entries[0][0] != 0:
            raise GameError("The compression map does not start at zero.")
        try:
            import lz4.block
        except ImportError as exc:
            raise GameError("Run Touchable.command to prepare the local Python environment.") from exc
        kinds = {"plain": 0, "zeros": 0, "lz4": 0}
        # Validate all ranges before creating the output.
        for index, (virtual, physical, kind, stored) in enumerate(entries):
            next_virtual = entries[index + 1][0] if index + 1 < count else expanded_size
            size = next_virtual - virtual
            if size <= 0 or virtual < 0 or next_virtual > expanded_size or not 0 <= physical <= table_offset:
                raise GameError("Invalid compression entry range.")
            physical_size = size if kind == 0 else stored
            if kind not in (0, 1, 3) or (kind != 1 and (physical_size < 0 or physical + physical_size > table_offset)):
                raise GameError("Unsupported or invalid compression entry.")
            if kind == 3 and (size > 65536 or not 0 < stored <= 131072 or physical % 16):
                raise GameError("Invalid LZ4 block bounds.")
        with destination.open("xb") as target:
            for index, (virtual, physical, kind, stored) in enumerate(entries):
                size = (entries[index + 1][0] if index + 1 < count else expanded_size) - virtual
                if kind == 0:
                    copy_range(source, target, data_offset + physical, size)
                    kinds["plain"] += 1
                elif kind == 1:
                    target.seek(size, 1)
                    kinds["zeros"] += 1
                else:
                    source.seek(data_offset + physical)
                    try:
                        decoded = lz4.block.decompress(read_exact(source, stored), uncompressed_size=size)
                    except lz4.block.LZ4BlockError as exc:
                        raise GameError("An LZ4 block could not be decoded.") from exc
                    if len(decoded) != size:
                        raise GameError("An LZ4 block has an invalid expanded size.")
                    target.write(decoded)
                    kinds["lz4"] += 1
            target.truncate(expanded_size)
    return {"compression": "NCA BKTR / LZ4", "entries": count, "bytes": expanded_size, "blocks": kinds}
