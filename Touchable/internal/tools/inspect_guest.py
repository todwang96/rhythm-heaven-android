#!/usr/bin/env python3
"""Show guest symbols and ARM64 code without executing a module."""
import argparse
from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from workspace import WorkspaceError, load_game

p = argparse.ArgumentParser()
p.add_argument('module', help='Module name from the selected game profile')
p.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
p.add_argument('start', help='Offset in hex or exact exported symbol')
p.add_argument('size', nargs='?', default='100', help='Byte count in hex')
a = p.parse_args()
try:
    game = load_game(a.game)
    game.require_adapted()
except WorkspaceError as error:
    p.error(str(error))
if a.module not in game.module_names: p.error('Unknown game module: ' + a.module)
data = (game.patched_dir / (a.module + '.patched.bin')).read_bytes()
mod0 = struct.unpack_from('<I', data, 4)[0]
position = mod0 + struct.unpack_from('<i', data, mod0 + 4)[0]
tags = {}
while True:
    tag, value = struct.unpack_from('<QQ', data, position)
    position += 16
    if not tag:
        break
    tags[tag] = value
symbols = []
for offset in range(tags[6], tags[5], 24):
    name, info, other, section, value, size = struct.unpack_from('<IBBHQQ', data, offset)
    pos = tags[5] + name
    symbols.append((data[pos:data.find(b'\0', pos)].decode(), value, section))
relocs = {}
for offset, length in [(tags.get(7, 0), tags.get(8, 0)), (tags.get(23, 0), tags.get(2, 0))]:
    for pos in range(offset, offset + length, 24):
        dest, info, addend = struct.unpack_from('<QQq', data, pos)
        relocs[dest] = symbols[info >> 32][0]
def label(offset):
    near = sorted((s for s in symbols if s[2] and s[1] <= offset), key=lambda s: s[1])
    if not near: return ''
    return near[-1][0] + '+0x%x' % (offset - near[-1][1])
def plt(offset):
    if offset < 0 or offset + 8 > len(data): return ''
    w, ldr = struct.unpack_from('<II', data, offset)
    if w & 0x9f00001f != 0x90000010 or ldr & 0xffc003ff != 0xf9400211: return ''
    imm = ((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2)
    if imm & 0x100000: imm -= 0x200000
    got = (offset & ~0xfff) + (imm << 12) + ((ldr >> 10) & 0xfff) * 8
    return relocs.get(got, '')
try: start = int(a.start, 16)
except ValueError: start = next(s[1] for s in symbols if s[0] == a.start and s[2])
print(a.module, hex(start), label(start))
cs = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
cs.skipdata = True
for i in cs.disasm(data[start:start + int(a.size, 16)], start):
    extra = ''
    if i.mnemonic in ('bl', 'b'):
        target = int(i.op_str.lstrip('#'), 16)
        extra = plt(target) or label(target)
    print('%08x  %-8s %-36s %s' % (i.address, i.mnemonic, i.op_str, extra))
