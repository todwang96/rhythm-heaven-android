#!/usr/bin/env python3
"""Run the simple-camera and main-camera viewport code with several screen sizes.

v0 (main 9049ea1c) needs the full-screen cut in ForceCameraAspectLite. Update
655360 (main fc9ea4cc) clamps both cameras to [1.6, 2.3917] in game code.
Phone aspects are in that range. Its cuts remove only the 1.6 lower limit, so
iPad aspects (3:2 and 4:3) also get a full viewport.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_W0, UC_ARM64_REG_W20, UC_ARM64_REG_X19,
    UC_ARM64_REG_SP, UC_ARM64_REG_PC,
    UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2, UC_ARM64_REG_S3,
    UC_ARM64_REG_S8, UC_ARM64_REG_S9,
)

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from workspace import load_game
START, END, PATCH = 0x2BD3520, 0x2BD35A0, 0x2BD3548
# Update 655360: ForceCameraAspectLite.AutoScaleViewport after the two Screen
# calls, up to its tail call to Camera.set_rect, and ForceCameraAspect's clamp.
V10_LITE_START, V10_LITE_END = 0x2C4C78C, 0x2C4C838
V10_MAIN_START, V10_MAIN_END = 0x2C4BA6C, 0x2C4BB2C
# fcsel <aspect>, s3 (1.6), s2, mi. The cuts select s2 in both operands.
V10_LOWER_LIMIT = {0x2C4BAD8: ('684c221e', '484c221e'), 0x2C4C800: ('614c221e', '414c221e')}


def read_float(cpu, register):
    return struct.unpack('<f', struct.pack('<I', cpu.reg_read(register)))[0]


def run(code, width, height, adjustment):
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(START & ~0xFFF, 0x1000)
    cpu.mem_write(START, code)
    cpu.mem_map(0x100000, 0x3000)
    cpu.mem_write(0x100020, struct.pack('<Q', 0x101000))
    cpu.mem_write(0x100034, struct.pack('<f', adjustment))
    cpu.reg_write(UC_ARM64_REG_X19, 0x100000)
    cpu.reg_write(UC_ARM64_REG_SP, 0x102800)
    cpu.reg_write(UC_ARM64_REG_W20, width)
    cpu.reg_write(UC_ARM64_REG_W0, height)
    result = {}

    def call(cpu, address, size, data):
        if address == 0x2BD3550:
            # Camera.get_rect returns a Rect in s0..s3. It does not set the
            # camera aspect or the projection matrix.
            for register, value in zip((UC_ARM64_REG_S0, UC_ARM64_REG_S1,
                                        UC_ARM64_REG_S2, UC_ARM64_REG_S3),
                                       (0.0, 0.0, 1.0, 1.0)):
                bits = struct.unpack('<I', struct.pack('<f', value))[0]
                cpu.reg_write(register, bits)
            cpu.reg_write(UC_ARM64_REG_PC, address + 4)
        if address == 0x2BD359C:
            result['rect'] = tuple(read_float(cpu, r) for r in (
                UC_ARM64_REG_S0, UC_ARM64_REG_S1,
                UC_ARM64_REG_S2, UC_ARM64_REG_S3))
            cpu.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, call)
    cpu.emu_start(START, END, count=100)
    assert 'rect' in result, result
    return result


def write_float(cpu, register, value):
    cpu.reg_write(register, struct.unpack('<I', struct.pack('<f', value))[0])


def run_v10_lite(image, width, height, adjustment):
    """Run the unmodified update code. s8 holds the overscan adjustment."""
    start, end = V10_LITE_START, V10_LITE_END
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(start & ~0xFFF, 0x2000)
    cpu.mem_write(start, image[start:end])
    cpu.mem_map(0x100000, 0x3000)
    cpu.mem_write(0x100020, struct.pack('<Q', 0x101000))
    cpu.reg_write(UC_ARM64_REG_X19, 0x100000)
    cpu.reg_write(UC_ARM64_REG_SP, 0x102800)
    cpu.reg_write(UC_ARM64_REG_W20, width)
    cpu.reg_write(UC_ARM64_REG_W0, height)
    write_float(cpu, UC_ARM64_REG_S8, adjustment)
    cpu.emu_start(start, end, count=100)
    return tuple(read_float(cpu, r) for r in (UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2, UC_ARM64_REG_S3))


def run_v10_main_viewport(image, width, height):
    """Check the update's main-camera clamp. This does not test projection."""
    start, end = V10_MAIN_START, V10_MAIN_END
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(start & ~0xFFF, 0x1000)
    cpu.mem_write(start, image[start:end])
    cpu.mem_map(0x100000, 0x3000)
    cpu.mem_write(0x100030, struct.pack('<Q', 0x101000))
    cpu.mem_write(0x101028, struct.pack('<Q', 0x101800))
    cpu.reg_write(UC_ARM64_REG_X19, 0x100000)
    cpu.reg_write(UC_ARM64_REG_W20, width)
    cpu.reg_write(UC_ARM64_REG_W0, height)
    write_float(cpu, UC_ARM64_REG_S9, 0.0)  # Overscan field loaded before the Screen calls.
    cpu.emu_start(start, end, count=100)
    aspect = read_float(cpu, UC_ARM64_REG_S8)
    return aspect, struct.unpack('<ffff', cpu.mem_read(0x10182C, 16))


def check_v10(image):
    sizes = [(1280, 720), (1564, 720), (1600, 720), (1520, 720), (1430, 660),
             (1920, 900), (2868, 1320), (2796, 1290), (2556, 1179)]
    checked = 0
    for width, height in sizes:
        for adjustment in (0.0, -0.05, 0.1):
            rect = run_v10_lite(image, width, height, adjustment)
            for actual, target in zip(rect, (-adjustment / 2, -adjustment / 2, 1 + adjustment, 1 + adjustment)):
                close(actual, target)
            checked += 1
        aspect, rect = run_v10_main_viewport(image, width, height)
        close(aspect, width / height)
        for actual, expected in zip(rect, (0, 0, 1, 1)): close(actual, expected)
    # Negative control: a 21:9+ screen beyond the game's 2.3917 limit keeps pillars.
    # Rects are (x, y, width, height).
    rect = run_v10_lite(image, 2000, 720, 0.0)
    assert rect[2] < 0.87 and rect[3] == 1.0 and abs(rect[0] - (1 - rect[2]) / 2) < 1e-6, rect
    aspect, rect = run_v10_main_viewport(image, 2000, 720)
    assert abs(aspect - 2.391667) < 0.0001 and rect[2] < 0.87 and rect[3] == 1.0, (aspect, rect)
    states = {image[a:a + 4].hex() for a in V10_LOWER_LIMIT}
    cut = all(image[a:a + 4].hex() == new for a, (_, new) in V10_LOWER_LIMIT.items())
    assert cut or all(image[a:a + 4].hex() == old for a, (old, _) in V10_LOWER_LIMIT.items()), states
    original = bytearray(image)
    for address, (old, _) in V10_LOWER_LIMIT.items(): original[address:address + 4] = bytes.fromhex(old)
    # iPad mini (A17 Pro) at 720 and 1080 lines, its native size, the 1.6 limit, and 4:3.
    ipad_sizes = [(1096, 720), (1644, 1080), (2266, 1488), (1728, 1080), (1024, 768), (1440, 1080)]
    if cut:
        for width, height in ipad_sizes:
            for adjustment in (0.0, -0.05, 0.1):
                rect = run_v10_lite(image, width, height, adjustment)
                for actual, target in zip(rect, (-adjustment / 2, -adjustment / 2, 1 + adjustment, 1 + adjustment)):
                    close(actual, target)
                checked += 1
            aspect, rect = run_v10_main_viewport(image, width, height)
            close(aspect, width / height)
            for actual, expected in zip(rect, (0, 0, 1, 1)): close(actual, expected)
    # Negative control: the original lower limit letterboxes a 3:2 screen.
    rect = run_v10_lite(bytes(original), 1096, 720, 0.0)
    assert rect[2] == 1.0 and rect[3] < 0.96 and abs(rect[1] - (1 - rect[3]) / 2) < 1e-6, rect
    aspect, rect = run_v10_main_viewport(bytes(original), 1096, 720)
    assert abs(aspect - 1.6) < 1e-6 and rect[2] == 1.0 and rect[3] < 0.96, (aspect, rect)
    if cut:
        print(f'PASS (update 655360): {checked} simple-camera cases and {len(sizes) + len(ipad_sizes)} main-camera cases '
              'fill phone and iPad screens; the wide and original 3:2 negative controls keep bars. Projection is not covered.')
    else:
        print(f'PASS (update 655360, no lower-limit cut): {checked} simple-camera cases and {len(sizes)} main-camera cases '
              'fill phone screens; iPad aspects below 1.6 letterbox. Projection is not covered.')


def close(actual, expected):
    assert abs(actual - expected) < 0.000002, (actual, expected)


def run_main_viewport(image, width, height):
    """Check the game's viewport clamp. This does not test projection."""
    start, end = 0x2BD2868, 0x2BD291C
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(start & ~0xFFF, 0x1000)
    cpu.mem_write(start, image[start:end])
    cpu.mem_map(0x100000, 0x3000)
    cpu.mem_write(0x100030, struct.pack('<Q', 0x101000))
    cpu.mem_write(0x101028, struct.pack('<Q', 0x101800))
    cpu.reg_write(UC_ARM64_REG_X19, 0x100000)
    cpu.reg_write(UC_ARM64_REG_W20, width)
    cpu.reg_write(UC_ARM64_REG_W0, height)
    cpu.emu_start(start, end, count=100)
    close(read_float(cpu, UC_ARM64_REG_S8), width / height)
    rect = struct.unpack('<ffff', cpu.mem_read(0x10182C, 16))
    for actual, expected in zip(rect, (0, 0, 1, 1)): close(actual, expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modules', type=Path, default=load_game('silksong').patched_dir)
    args = parser.parse_args()
    image = (args.modules / 'main.patched.bin').read_bytes()
    if image[V10_LITE_START + 0x24:V10_LITE_START + 0x28] == bytes.fromhex('0101271e') and \
            image[V10_LITE_START + 0x4:V10_LITE_START + 0xc] == bytes.fromhex('a899995288f9b772'):
        return check_v10(image)
    assert image[PATCH:PATCH + 4] == bytes.fromhex('0140201e'), 'The full-screen cut is absent.'
    code = image[START:END]
    sizes = [(1280, 720), (1564, 720), (1920, 1080), (1600, 720),
             (1520, 720), (1430, 660), (1920, 800), (1024, 768)]
    checked = 0
    for width, height in sizes:
        for adjustment in (0.0, -0.05, 0.1):
            result = run(code, width, height, adjustment)
            expected = (-adjustment / 2, -adjustment / 2,
                        1 + adjustment, 1 + adjustment)
            for actual, target in zip(result['rect'], expected): close(actual, target)
            checked += 1
    original = bytearray(code)
    original[PATCH - START:PATCH - START + 4] = bytes.fromhex('0101271e')
    old = run(bytes(original), 1564, 720, 0.0)
    assert old['rect'][2] < 0.85, 'The negative control did not retain the old bars.'
    game_sizes = [(1280, 720), (1564, 720), (1600, 720),
                  (1520, 720), (1430, 660), (1920, 900)]
    for width, height in game_sizes: run_main_viewport(image, width, height)
    print(f'PASS: {checked} simple-camera cases, {len(game_sizes)} game viewport cases; viewport, overscan, and old-bar negative control. Projection is not covered.')


if __name__ == '__main__': main()
