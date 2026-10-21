"""Identify the exact inputs passed to the shader translator."""
import hashlib
import struct
from game_formats import GameError


def functional_key(control, code):
    if len(control) != 0x880:
        raise GameError('Invalid shader control size.')
    constant, end, start = struct.unpack_from('<III', control, 0x700)
    stage = struct.unpack_from('<I', control, 0x714)[0]
    if stage not in (0, 1, 5) or not 0 <= start < constant <= end <= len(code):
        raise GameError('Invalid shader translation bounds.')
    local = struct.unpack_from('<III', control, 0x748) if stage == 5 else (0, 0, 0)
    header = struct.pack('<6I', stage, constant - start, end - constant, *local)
    return hashlib.sha256(header + code[start:end]).hexdigest()
