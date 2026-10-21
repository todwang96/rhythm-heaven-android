#!/usr/bin/env python3
"""Check the game's resolution, render texture, and perspective paths.

Run with .nitenphone/venv/bin/python internal/games/silksong/tests/test_world_projection.py. The checks execute
instructions from the pinned game. Unity allocation and API calls are mocked.
They cannot replace the live camera and image checks on the phone.
"""
import argparse
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from workspace import load_game

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as arm


def reg(name):
    return getattr(arm, 'UC_ARM64_REG_' + name.upper())


def put(cpu, address, form, *values):
    cpu.mem_write(address, struct.pack('<' + form, *values))


def get(cpu, address, form):
    return struct.unpack('<' + form, cpu.mem_read(address, struct.calcsize('<' + form)))


def set_float(cpu, name, value):
    cpu.reg_write(reg(name), struct.unpack('<I', struct.pack('<f', value))[0])


def machine(image, start, end):
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    page = start & ~0xfff
    cpu.mem_map(page, ((end + 0xfff) & ~0xfff) - page)
    cpu.mem_write(start, image[start:end])
    cpu.mem_map(0x100000, 0x10000)
    for page in (0x7839000, 0x783d000, 0x783e000, 0x7885000):
        cpu.mem_map(page, 0x1000)
    cpu.mem_map(0x7a70000, 0x11000)
    cpu.reg_write(reg('sp'), 0x10e000)
    cpu.reg_write(reg('x30'), 0x10f000)
    # The game class and UnityEngine.Object class have completed initialization.
    put(cpu, 0x7839dd8, 'Q', 0x102000)
    put(cpu, 0x783ddb0, 'Q', 0x103000)
    put(cpu, 0x1020b8, 'Q', 0x108000)
    put(cpu, 0x1020e4, 'I', 1)
    put(cpu, 0x1030e4, 'I', 1)
    return cpu


def platform_records(image, records):
    """Execute SwitchPlatform's two record loads and object field stores."""
    start, end = 0x24908e0, 0x24908fc
    cpu = machine(image, start, end)
    cpu.mem_map(0x61f4000, 0x1000)
    cpu.mem_map(0x61fd000, 0x1000)
    put(cpu, 0x61f40a0, '4I', *records[:4])
    put(cpu, 0x61fd050, '4I', *records[4:])
    cpu.reg_write(reg('x19'), 0x100000)
    cpu.emu_start(start, end, count=30)
    return get(cpu, 0x1000d0, '8I')


def set_screen(image, records, docked, mode):
    """Execute each platform screen mode, including the static RT size store."""
    start = 0x24902b0 if docked else 0x2490070
    cpu = machine(image, 0x2490070, 0x24904f0)
    put(cpu, 0x1000d0, '8I', *records)
    put(cpu, 0x100040, 'I', mode)
    for address in (0x7a70132, 0x7a70133, 0x7a700cf, 0x7a7200c):
        put(cpu, address, 'B', 1)
    put(cpu, 0x7a7ee78, 'Q', 0x10d000)
    cpu.reg_write(reg('x0'), 0x100000)
    requests = []

    def hook(cpu, address, size, data):
        if address == 0x10d000:
            requests.append((cpu.reg_read(reg('w0')), cpu.reg_read(reg('w1'))))
            cpu.reg_write(reg('pc'), cpu.reg_read(reg('x30')))
        elif address == 0x10f000:
            cpu.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.emu_start(start, 0x10f004, count=300)
    assert len(requests) == 1, requests
    base = 4 if docked else 0
    request = records[base:base + 2] if mode == 1 else records[base + 2:base + 4]
    assert requests[0] == request, (mode, requests, request)
    recommended = get(cpu, 0x108000, '2I')
    expected = records[base:base + 2] if mode == 2 else (0, 0)
    assert recommended == expected, (mode, recommended, expected)
    return recommended, request


def pre_cull(image, recommended, screen, force_instance=False,
             force_global=False, existing=None):
    """Execute OnPreCull from the size selector through RT creation/binding."""
    start, end = 0x2a8c60c, 0x2a8c9c8
    cpu = machine(image, start, end)
    put(cpu, 0x108000, '2I', *recommended)
    put(cpu, 0x108008, 'B', force_global)
    put(cpu, 0x100038, 'B', force_instance)
    put(cpu, 0x100030, 'Q', 0x106000)  # Managed source camera.
    if existing:
        put(cpu, 0x100028, 'Q', 0x105000)
        put(cpu, 0x105000, 'Q', 0x107000)
        put(cpu, 0x105010, 'Q', 0x105800)  # Live Unity native object.
        put(cpu, 0x107178, '2Q', 0x10d030, 0)
        put(cpu, 0x107198, '2Q', 0x10d040, 0)
    for address in (0x7a807b0, 0x7a80772):
        put(cpu, address, 'B', 1)
    put(cpu, 0x7a7ee30, 'Q', 0x10d010)
    put(cpu, 0x7a7ee38, 'Q', 0x10d020)
    cpu.reg_write(reg('x19'), 0x100000)
    cpu.reg_write(reg('x23'), 0x783d000)
    cpu.reg_write(reg('x24'), 0x7a80000)
    cpu.reg_write(reg('x29'), 0x10d800)
    result = {'created': None, 'bound': False, 'destroyed': False}

    def hook(cpu, address, size, data):
        if address in (0x2a8c680, 0x2a8c7fc):
            result['selected'] = (cpu.reg_read(reg('w20')), cpu.reg_read(reg('w21')))
        elif address in (0x10d010, 0x10d020, 0x10d030, 0x10d040):
            value = {0x10d010: screen[0], 0x10d020: screen[1],
                     0x10d030: existing[0] if existing else 0,
                     0x10d040: existing[1] if existing else 0}[address]
            cpu.reg_write(reg('w0'), value)
            cpu.reg_write(reg('pc'), cpu.reg_read(reg('x30')))
        elif address == 0x2a8c8fc:  # Unity allocation.
            cpu.reg_write(reg('x0'), 0x105000)
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x2a8c918:  # RenderTexture(width, height, depth, format).
            result['created'] = (cpu.reg_read(reg('w1')), cpu.reg_read(reg('w2')))
            assert cpu.reg_read(reg('w3')) == 32
            assert cpu.reg_read(reg('w4')) == 7
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x2a8c978:  # Camera.set_targetTexture.
            assert cpu.reg_read(reg('x0')) == 0x106000
            assert cpu.reg_read(reg('x1')) == 0x105000
            result['bound'] = True
            cpu.emu_stop()
        elif address in (0x2a8c77c, 0x2a8c828):
            # Destroy the old RT after a size change or full-resolution switch.
            result['destroyed'] = True
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x2a8c84c:  # No separate apply component in this check.
            cpu.reg_write(reg('w0'), 0)
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x2a8c85c:  # Full resolution uses the Screen target.
            result['direct'] = result['selected']
            cpu.emu_stop()
        elif address == 0x2a8c9ac:  # Reuse an existing RT of the same size.
            result['reused'] = existing
            cpu.emu_stop()
        elif address in (0x2a8c928, 0x2a8c934, 0x2a8c944,
                         0x2a8c958, 0x2a8c964):
            # Object metadata calls do not select the RT size.
            cpu.reg_write(reg('pc'), address + 4)

    cpu.hook_add(UC_HOOK_CODE, hook)
    try:
        cpu.emu_start(start, end, count=300)
    except Exception as error:
        raise AssertionError(f'OnPreCull failed at {cpu.reg_read(reg("pc")):x}: {result}') from error
    return result


def clear_render_texture(image):
    """Check that the full-resolution path restores the default camera target."""
    start, end = 0x2a8c130, 0x2a8c190
    cpu = machine(image, start, end)
    put(cpu, 0x100030, 'Q', 0x106000)
    for address in (0x7a807b0, 0x7a80772):
        put(cpu, address, 'B', 1)
    cpu.reg_write(reg('x19'), 0x100000)
    result = {}

    def hook(cpu, address, size, data):
        if address == 0x2a8c13c:
            assert cpu.reg_read(reg('x0')) == 0x106000
            assert cpu.reg_read(reg('x1')) == 0
            result['default_target'] = True
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x2a8c17c:
            cpu.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.emu_start(start, end, count=60)
    assert result.get('default_target'), result


def reset_aspect(image, size):
    """Execute Unity's native ResetAspect with the selected target dimensions."""
    start, end = 0xc07714, 0xc078c0
    cpu = machine(image, start, end)
    put(cpu, 0x10041c, '4f', 0, 0, 1, 1)
    put(cpu, 0x100504, 'I', 1)  # Default perspective, not physical camera.
    cpu.reg_write(reg('x0'), 0x100000)

    def hook(cpu, address, length, data):
        if address == 0xc0772c:  # Target flags.
            cpu.reg_write(reg('w0'), 0)
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0xc07740:  # Native target size resolver C08740.
            set_float(cpu, 's2', size[0])
            set_float(cpu, 's3', size[1])
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x10f000:
            cpu.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.emu_start(start, 0x10f004, count=150)
    aspect, = get(cpu, 0x1004e0, 'f')
    assert abs(aspect - size[0] / size[1]) < 1e-6, (aspect, size)
    assert get(cpu, 0x100512, 'B') == (1,)  # Automatic aspect remains on.
    return aspect


def perspective_path(image, size, aspect):
    """Execute tk2d's perspective branch with the actual camera target size."""
    start, end = 0x5816328, 0x5816588
    cpu = machine(image, start, end)
    put(cpu, 0x10006c, 'f', 1)  # Zoom.
    put(cpu, 0x101010, 'Q', 0x106000)  # Managed Camera native pointer.
    put(cpu, 0x104010, 'I', 1)  # tk2dCameraSettings.Perspective.
    put(cpu, 0x104028, 'f', 24)  # Main camera's serialized vertical FOV.
    for address in (0x7a7e41a, 0x7a7e415, 0x7a7e420, 0x7a7e439):
        put(cpu, address, 'B', 1)
    for address in (0x7a7e4d0, 0x7a7e4a8, 0x7a7e500, 0x7a7e5c8):
        put(cpu, address, 'Q', 0x10d000)
    for name, value in [('x20', 0x100000), ('x19', 0x101000),
                        ('x25', 0x104000), ('w22', size[0]), ('w0', size[1])]:
        cpu.reg_write(reg(name), value)
    result = {}

    def hook(cpu, address, length, data):
        if address == 0x5816378:  # Camera.get_orthographic.
            cpu.reg_write(reg('w0'), 0)
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x581645c:  # Camera.get_fieldOfView.
            set_float(cpu, 's0', 24)
            cpu.reg_write(reg('pc'), address + 4)
        elif address in (0x58164a8, 0x5816504):  # Camera.get_aspect.
            set_float(cpu, 's0', aspect)
            cpu.reg_write(reg('pc'), address + 4)
        elif address == 0x5816584:
            result['reset_projection'] = True
            cpu.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.emu_start(start, end, count=200)
    assert result.get('reset_projection'), result
    assert get(cpu, 0x100064, '2f') == tuple(map(float, size))
    extents = get(cpu, 0x10007c, '4f')
    assert all(abs(a - b) < 1e-6 for a, b in zip(extents, (-aspect, -1, 2 * aspect, 2)))


def projection_model(aspect, output, reference_aspect):
    """Check image scale and visible width for a fixed vertical FOV.

    This is a projection model. The native live camera check must confirm
    these inputs. In particular, the old 16:9 RT must fail the scale check.
    """
    tangent = math.tan(math.radians(24) / 2)
    horizontal_scale = output[0] / (2 * tangent * aspect)
    vertical_scale = output[1] / (2 * tangent)
    stretch = horizontal_scale / vertical_scale
    visible_width_gain = aspect / reference_aspect
    return stretch, visible_width_gain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modules', type=Path, default=load_game('silksong').work_dir / 'phone-features')
    args = parser.parse_args()
    image = (args.modules / 'main.patched.bin').read_bytes()
    original = (1024, 576, 1280, 720, 1280, 720, 1920, 1080)
    assert struct.unpack_from('<4I', image, 0x61f40a0) == original[:4]
    assert struct.unpack_from('<4I', image, 0x61fd050) == original[4:]
    adapted = (1252, 576, 1564, 720, 1564, 720, 1920, 1080)
    assert platform_records(image, adapted) == adapted

    cases = 0
    for docked in (False, True):
        for mode in (0, 1, 2):
            recommended, request = set_screen(image, adapted, docked, mode)
            # The production SetResolution wrapper is tested in test_display.c.
            screen = (1252, 576) if request[1] == 576 else (1564, 720)
            result = pre_cull(image, recommended, screen)
            selected = recommended if mode == 2 else screen
            if mode == 2:
                assert result['created'] == selected and result['bound'], result
            else:
                assert result.get('direct') == selected and not result['bound'], result
            aspect = reset_aspect(image, selected)
            perspective_path(image, selected, aspect)
            stretch, gain = projection_model(aspect, (1564, 720), 1280 / 720)
            assert abs(stretch - 1) < 0.001, stretch  # Four-pixel rounding error.
            assert gain > 1.22, gain  # The visible world is at least 22% wider.
            cases += 1

    for force_instance, force_global in ((True, False), (False, True), (True, True)):
        result = pre_cull(image, (1252, 576), (1564, 720), force_instance,
                          force_global, existing=(1252, 576))
        assert result.get('direct') == (1564, 720) and not result['bound'], result
        assert result['destroyed'], result
        clear_render_texture(image)
        cases += 1
    resize = pre_cull(image, (1252, 576), (1564, 720), existing=(1024, 576))
    assert resize['destroyed'] and resize['created'] == (1252, 576) and resize['bound'], resize
    reuse = pre_cull(image, (1252, 576), (1564, 720), existing=(1252, 576))
    assert reuse.get('reused') == (1252, 576) and not reuse['created'], reuse
    cases += 2

    # Keep the original record as a negative control: its RT stays 16:9 even
    # when the separate Screen target is wide. This is the reported v5 bug.
    recommended, _ = set_screen(image, original, False, 2)
    old = pre_cull(image, recommended, (1564, 720))
    assert old['created'] == (1024, 576) and old['bound'], old
    old_aspect = reset_aspect(image, old['created'])
    stretch, gain = projection_model(old_aspect, (1564, 720), 1280 / 720)
    assert stretch > 1.22 and abs(gain - 1) < 1e-6, (stretch, gain)
    print(f'PASS: {cases} actual game resolution/RT branches, native automatic aspect, tk2d perspective reset, image-scale model, expanded-view model, and old 16:9 RT negative control. Live image validation remains required.')


if __name__ == '__main__':
    main()
