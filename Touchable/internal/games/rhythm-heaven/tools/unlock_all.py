#!/usr/bin/env python3
"""Unlock every stage and menu feature in a Rhythm Heaven Groove save (game_save.bin).

Save layout (main build 03ff4a3b..., found in the save manager at main+0x3ca0 / main+0x3f70):
  0x10  u32 header size, must be 0x34      0x14  u32 file size, must be 0x1188
  The first 16 bytes are not a checksum; the game only checks the two header fields.
  Progress entries are addressed by (kind, index); the first byte of each entry is its state.
  kind 0  menu features (cafe, toys, drum lessons, score, back world ...): 4-byte entries at 0x28,
          indices 1..13. 0 = hidden, 1 = shown as "?", 3 = unlocked (the value the game writes).
  kind 1  normal-world stages and remixes: 16-byte entries at 0x60, indices 0..79.
          0 = hidden, 1 = shown as "?", 2 = unlocked (NEW), 4 and above = played / cleared / medals.
  kind 2  back-world stages: 8-byte entries at 0x560, indices 0..29, same states as kind 1.
Entries already at or above the unlock value keep their state, so cleared stages and medals stay.

--perfect-only raises the 80 normal-world stage states to 7. This opens the P mode selector.
The state also controls the displayed rank. Scores, play counts, and P streaks do not change.
States 8 and 9 keep their P+ access. Back-world stages have no P mode selector.
"""
import argparse
from pathlib import Path
import struct
import sys

SIZE = 0x1188
GROUPS = (  # kind, offset, stride, count, first index, unlocked state
    (0, 0x28, 4, 14, 1, 3),
    (1, 0x60, 16, 80, 0, 2),
    (2, 0x560, 8, 30, 0, 2),
)


def validate(data):
    """Check the v0 save size and header."""
    if len(data) != SIZE or struct.unpack_from('<II', data, 0x10) != (0x34, SIZE):
        raise ValueError('Not a Rhythm Heaven Groove save (size or header does not match).')


def unlock(data):
    """Return (new save bytes, number of entries raised)."""
    validate(data)
    out = bytearray(data)
    raised = 0
    for _, offset, stride, count, first, state in GROUPS:
        for index in range(first, count):
            position = offset + index * stride
            if out[position] < state:
                out[position] = state
                raised += 1
    return bytes(out), raised


def unlock_perfect(data):
    """Open P mode for all 80 normal-world entries. Keep all other bytes."""
    validate(data)
    out = bytearray(data)
    raised = 0
    for index in range(80):
        position = 0x60 + index * 16
        if out[position] < 7:
            out[position] = 7
            raised += 1
    return bytes(out), raised


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('save', type=Path, help='game_save.bin to read')
    parser.add_argument('--output', type=Path, required=True, help='Where to write the unlocked save (must not exist)')
    parser.add_argument('--perfect-only', action='store_true', help='Open P mode only. This also raises the displayed stage rank.')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('The output exists. Choose a new path.')
    try:
        operation = unlock_perfect if args.perfect_only else unlock
        data, raised = operation(args.save.read_bytes())
    except (OSError, ValueError) as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print('Unlocked %d entries -> %s' % (raised, args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
