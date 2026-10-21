#!/usr/bin/env python3
"""List the guest call sites of an imported or exported symbol. This does not execute a module."""
import argparse
import struct
import subprocess
import sys

from workspace import WorkspaceError, load_game


def tables(data):
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
        start = tags[5] + name
        symbols.append((data[start:data.find(b'\0', start)].decode(), value, section))
    relocations = {}
    for offset, length in [(tags.get(7, 0), tags.get(8, 0)), (tags.get(23, 0), tags.get(2, 0))]:
        for position in range(offset, offset + length, 24):
            destination, info, addend = struct.unpack_from('<QQq', data, position)
            relocations[destination] = symbols[info >> 32][0]
    return symbols, relocations


def plt_symbol(data, offset, relocations):
    if offset < 0 or offset + 8 > len(data):
        return ''
    adrp, load = struct.unpack_from('<II', data, offset)
    if adrp & 0x9f00001f != 0x90000010 or load & 0xffc003ff != 0xf9400211:
        return ''
    immediate = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
    if immediate & 0x100000:
        immediate -= 0x200000
    slot = (offset & ~0xfff) + (immediate << 12) + ((load >> 10) & 0xfff) * 8
    return relocations.get(slot, '')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', help='Module name from the selected game profile')
    parser.add_argument('symbol', help='Mangled symbol name, or a substring with --match')
    parser.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
    parser.add_argument('--match', action='store_true', help='Treat symbol as a substring of the mangled name')
    args = parser.parse_args()
    try:
        game = load_game(args.game)
        game.require_adapted()
    except WorkspaceError as error:
        parser.error(str(error))
    if args.module not in game.module_names:
        parser.error('Unknown game module: ' + args.module)
    data = (game.patched_dir / (args.module + '.patched.bin')).read_bytes()
    symbols, relocations = tables(data)
    wanted = (lambda name: args.symbol in name) if args.match else (lambda name: name == args.symbol)
    targets = {value: name for name, value, section in symbols if section and wanted(name)}
    labels = sorted((value, name) for name, value, section in symbols if section)
    import bisect

    def label(offset):
        index = bisect.bisect_right(labels, (offset, '￿')) - 1
        return labels[index][1] + '+0x%x' % (offset - labels[index][0]) if index >= 0 else ''

    count = 0
    for offset in range(0, len(data) - 3, 4):
        word = struct.unpack_from('<I', data, offset)[0]
        # bl, and b for tail calls.
        if word & 0x7c000000 != 0x14000000:
            continue
        immediate = word & 0x3ffffff
        if immediate & 0x2000000:
            immediate -= 0x4000000
        target = offset + 4 * immediate
        name = targets.get(target) or plt_symbol(data, target, relocations)
        if name and wanted(name):
            demangled = subprocess.run(['c++filt', name], capture_output=True, text=True).stdout.strip()
            kind = 'bl' if word & 0x80000000 else 'b '
            print('%s+%#x  %s  %-48s -> %s' % (args.module, offset, kind, label(offset), demangled))
            count += 1
    print('call sites:', count, file=sys.stderr)


if __name__ == '__main__':
    main()
