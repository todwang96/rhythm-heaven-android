#!/usr/bin/env python3
"""Generate the host fallback font: U+25A1 WHITE SQUARE and console button icons.

Some games look up the console shared fonts for the box glyph that marks a missing character.
The console fonts are system data and are not part of the game. This font gives the shared-font
interface one valid TrueType face with that glyph. Output: runtime/common/ss_fallback_font.h
"""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
UNITS = 1000
NAME = 'NitenphoneFallback'


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>%dI' % (len(data) // 4), data)) & 0xFFFFFFFF


def simple_glyph(contours):
    """contours: lists of on-curve points. Coordinates are absolute."""
    points = [point for contour in contours for point in contour]
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    data = struct.pack('>hhhhh', len(contours), min(xs), min(ys), max(xs), max(ys))
    end = -1
    for contour in contours:
        end += len(contour)
        data += struct.pack('>H', end)
    data += struct.pack('>H', 0)                      # no instructions
    data += bytes([0x01] * len(points))               # on-curve, 16-bit deltas
    x = y = 0
    dx, dy = b'', b''
    for px, py in points:
        dx += struct.pack('>h', px - x); dy += struct.pack('>h', py - y)
        x, y = px, py
    data += dx + dy
    return data + b'\0' * (-len(data) % 4)


def oriented(points, filled):
    """TrueType fills clockwise contours (y up) and cuts counter-clockwise ones."""
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1]))
    clockwise = area < 0
    return list(points) if clockwise == filled else list(reversed(points))


def circle(cx, cy, r, n=48):
    import math
    return [(round(cx + r * math.cos(2 * math.pi * i / n)), round(cy + r * math.sin(2 * math.pi * i / n))) for i in range(n)]


def rounded_rect(x0, y0, x1, y1, r, n=8):
    import math
    points = []
    for cx, cy, a0 in ((x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            points.append((round(cx + r * math.cos(a)), round(cy + r * math.sin(a))))
    return points


# Letter shapes in a 1000-unit em, cap height 140..560, centred on x = 500: (outline, [counters]).
LETTERS = {
    'A': ([(330, 140), (460, 560), (540, 560), (670, 140), (585, 140), (560, 225), (440, 225), (415, 140)],
          [[(462, 300), (538, 300), (500, 440)]]),
    'B': ([(360, 140), (360, 560), (560, 560), (620, 530), (640, 460), (610, 385), (650, 335), (660, 250), (620, 170), (560, 140)],
          [[(445, 395), (445, 485), (535, 485), (555, 440), (535, 395)], [(445, 215), (445, 320), (555, 320), (575, 270), (555, 215)]]),
    'X': ([(330, 140), (445, 350), (330, 560), (425, 560), (500, 415), (575, 560), (670, 560), (555, 350), (670, 140), (575, 140), (500, 285), (425, 140)], []),
    'Y': ([(330, 560), (425, 560), (500, 415), (575, 560), (670, 560), (545, 330), (545, 140), (455, 140), (455, 330)], []),
    'L': ([(385, 140), (385, 560), (475, 560), (475, 225), (630, 225), (630, 140)], []),
    'R': ([(370, 140), (370, 560), (560, 560), (620, 530), (645, 460), (620, 390), (570, 360), (655, 140), (560, 140), (485, 345), (460, 345), (460, 140)],
          [[(460, 420), (460, 490), (540, 490), (560, 455), (540, 420)]]),
    'Z': ([(360, 560), (640, 560), (640, 480), (465, 225), (645, 225), (645, 140), (355, 140), (355, 220), (530, 475), (360, 475)], []),
    '+': ([(455, 160), (455, 305), (310, 305), (310, 395), (455, 395), (455, 540), (545, 540), (545, 395), (690, 395), (690, 305), (545, 305), (545, 160)], []),
    '-': ([(310, 305), (310, 395), (690, 395), (690, 305)], []),
}


def letter(name, scale=1.0, shift=0):
    outline, counters = LETTERS[name]
    move = lambda pts: [(round(500 + (x - 500) * scale + shift), y) for x, y in pts]
    return [oriented(move(outline), False)] + [oriented(move(c), True) for c in counters]


def badge(shape, letters):
    """A filled badge with letters cut out of it, like the console button icons."""
    contours = [oriented(shape, True)]
    if len(letters) == 1: contours += letter(letters[0])
    else: contours += letter(letters[0], 0.62, -115) + letter(letters[1], 0.62, 115)
    return contours


def dpad(arm):
    w = 130
    cross = [(500 - w, 750), (500 + w, 750), (500 + w, 350 + w), (900, 350 + w), (900, 350 - w), (500 + w, 350 - w), (500 + w, -50),
             (500 - w, -50), (500 - w, 350 - w), (100, 350 - w), (100, 350 + w), (500 - w, 350 + w)]
    contours = [oriented(cross, True)]
    arrows = {'up': [(430, 520), (570, 520), (500, 650)], 'down': [(430, 180), (570, 180), (500, 50)],
              'left': [(330, 280), (330, 420), (200, 350)], 'right': [(670, 280), (670, 420), (800, 350)]}
    if arm in arrows: contours.append(oriented(arrows[arm], False))
    return contours


def build():
    face = circle(500, 350, 400)
    shoulder = rounded_rect(80, 80, 920, 620, 160)
    shapes = {
        0x25A1: [oriented([(100, -50), (100, 750), (900, 750), (900, -50)], True), oriented([(170, 20), (830, 20), (830, 680), (170, 680)], False)],
        0xE0E0: badge(face, 'A'), 0xE0E1: badge(face, 'B'), 0xE0E2: badge(face, 'X'), 0xE0E3: badge(face, 'Y'),
        0xE0E4: badge(shoulder, 'L'), 0xE0E5: badge(shoulder, 'R'), 0xE0E6: badge(shoulder, 'ZL'), 0xE0E7: badge(shoulder, 'ZR'),
        0xE0E8: badge(shoulder, 'L'), 0xE0E9: badge(shoulder, 'R'),
        0xE0EA: dpad(None), 0xE0EB: dpad('up'), 0xE0EC: dpad('down'), 0xE0ED: dpad('left'), 0xE0EE: dpad('right'),
        0xE0EF: badge(face, '+'), 0xE0F0: badge(face, '-'),
        0xE0A0: badge(face, 'A'), 0xE0A1: badge(face, 'B'), 0xE0A2: badge(face, 'X'), 0xE0A3: badge(face, 'Y'),
        0xE0A4: badge(shoulder, 'L'), 0xE0A5: badge(shoulder, 'R'), 0xE0A6: badge(shoulder, 'ZL'), 0xE0A7: badge(shoulder, 'ZR'),
        0xE0AB: dpad(None), 0xE0AC: dpad('up'), 0xE0AD: dpad('down'), 0xE0AE: dpad('left'), 0xE0AF: dpad('right'),
    }
    codes = sorted(shapes)
    glyphs = [b''] + [simple_glyph(shapes[code]) for code in codes]
    loca, glyf = [], b''
    for glyph in glyphs:
        loca.append(len(glyf)); glyf += glyph
    loca.append(len(glyf))
    max_points = max(sum(len(c) for c in shapes[code]) for code in codes)
    max_contours = max(len(shapes[code]) for code in codes)
    tables = {}
    tables[b'glyf'] = glyf
    tables[b'loca'] = struct.pack('>%dI' % len(loca), *loca)
    tables[b'head'] = struct.pack('>IIIIHHqqhhhhHHhhh', 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, UNITS,
                                  0, 0, 100, -50, 900, 750, 0, 8, 2, 1, 0)
    tables[b'hhea'] = struct.pack('>IhhhHhhhhhhhhhhhH', 0x00010000, 880, -120, 0, 1000, 0, 0, 900, 1, 0, 0,
                                  0, 0, 0, 0, 0, len(glyphs))
    tables[b'maxp'] = struct.pack('>IHHHHHHHHHHHHHH', 0x00010000, len(glyphs), max_points, max_contours, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    tables[b'hmtx'] = struct.pack('>HhHh', 500, 0, 1000, 80) + struct.pack('>Hh', 1000, 80) * (len(glyphs) - 2)
    # cmap format 4: U+25A1 -> glyph 1.
    segments = [(code, code, index + 1 - code) for index, code in enumerate(codes)] + [(0xFFFF, 0xFFFF, 1)]
    count = len(segments)
    search = 2 ** (count.bit_length() - 1) * 2
    subtable = struct.pack('>HHHHHHH', 4, 0, 0, count * 2, search, (search // 2).bit_length() - 1, count * 2 - search)
    subtable += struct.pack('>%dH' % count, *[s[1] for s in segments]) + struct.pack('>H', 0)
    subtable += struct.pack('>%dH' % count, *[s[0] for s in segments])
    subtable += struct.pack('>%dH' % count, *[s[2] % 65536 for s in segments])
    subtable += struct.pack('>%dH' % count, *[0] * count)
    subtable = subtable[:2] + struct.pack('>H', len(subtable)) + subtable[4:]
    tables[b'cmap'] = struct.pack('>HHHHI', 0, 1, 3, 1, 12) + subtable
    strings = [(1, NAME), (2, 'Regular'), (3, NAME + '-Regular'), (4, NAME), (6, NAME)]
    storage, records = b'', b''
    for name_id, text in strings:
        encoded = text.encode('utf-16-be')
        records += struct.pack('>HHHHHH', 3, 1, 0x409, name_id, len(encoded), len(storage))
        storage += encoded
    tables[b'name'] = struct.pack('>HHH', 0, len(strings), 6 + len(records)) + records + storage
    tables[b'OS/2'] = struct.pack('>HhHHHhhhhhhhhhhh10sIIIIIHHHhhhHHII', 3, 1000, 400, 5, 0,
                                  650, 600, 0, 75, 650, 600, 0, 350, 50, 250, 0,
                                  bytes(10), 0, 0, 0, 0, 0, 0x40, 0x25A1, 0xE0F0,
                                  880, -120, 0, 880, 120, 1, 0)
    tables[b'OS/2'] += struct.pack('>hhHHH', 500, 700, 0, 0x25A1, 0)
    tables[b'post'] = struct.pack('>IIhhIIIII', 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
    tags = sorted(tables)
    count = len(tags)
    search = 2 ** (count.bit_length() - 1) * 16
    header = struct.pack('>IHHHH', 0x00010000, count, search, (search // 16).bit_length() - 1, count * 16 - search)
    offset = 12 + 16 * count
    directory, body = b'', b''
    for tag in tags:
        data = tables[tag]
        directory += struct.pack('>4sIII', tag, checksum(data), offset + len(body), len(data))
        body += data + b'\0' * (-len(data) % 4)
    font = bytearray(header + directory + body)
    head = offset + sum(len(tables[t]) + (-len(tables[t]) % 4) for t in tags[:tags.index(b'head')])
    struct.pack_into('>I', font, head + 8, (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF)
    return bytes(font)


def main():
    font = build()
    lines = ['/* Generated by tools/make_fallback_font.py. U+25A1 and console button icons (U+E0A0-E0F0). */',
             '#pragma once', '#include <stdint.h>', 'static const uint8_t ss_fallback_font[%d] = {' % len(font)]
    for start in range(0, len(font), 16):
        lines.append('    ' + ', '.join('0x%02x' % b for b in font[start:start + 16]) + ',')
    lines.append('};')
    (ROOT / 'runtime/common/ss_fallback_font.h').write_text('\n'.join(lines) + '\n')
    print('Wrote', len(font), 'bytes')


if __name__ == '__main__':
    main()
