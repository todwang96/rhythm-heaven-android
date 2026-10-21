"""Check the translator identity contract for static and runtime shaders."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from shader_keys import functional_key
from workspace import WorkspaceError


class ShaderKeyTests(unittest.TestCase):
    def fixture(self, stage=1):
        control = bytearray(0x880)
        struct.pack_into('<III', control, 0x700, 0x80, 0x100, 0x20)
        struct.pack_into('<I', control, 0x714, stage)
        struct.pack_into('<III', control, 0x748, 8, 4, 1)
        return control, bytearray((i * 7) % 256 for i in range(0x100))

    def test_untranslated_metadata_does_not_change_identity(self):
        control, code = self.fixture()
        expected = functional_key(control, code)
        control[0x7d0:0x7d8] = b'NVN hash'
        self.assertEqual(functional_key(control, code), expected)
        code[0] ^= 1  # Bytes before the shader header are not sent to the translator.
        self.assertEqual(functional_key(control, code), expected)

    def test_code_constants_stage_and_boundaries_change_identity(self):
        for offset in (0x30, 0xf0):
            control, code = self.fixture()
            expected = functional_key(control, code)
            code[offset] ^= 1
            self.assertNotEqual(functional_key(control, code), expected)
        for offset, value in ((0x700, 0x70), (0x714, 0)):
            control, code = self.fixture()
            expected = functional_key(control, code)
            struct.pack_into('<I', control, offset, value)
            self.assertNotEqual(functional_key(control, code), expected)

    def test_compute_workgroup_is_part_of_identity(self):
        control, code = self.fixture(5)
        expected = functional_key(control, code)
        struct.pack_into('<I', control, 0x748, 16)
        self.assertNotEqual(functional_key(control, code), expected)

    def test_invalid_bounds_are_rejected(self):
        for offset, value in ((0x700, 0x200), (0x708, 0x80), (0x714, 3)):
            control, code = self.fixture()
            struct.pack_into('<I', control, offset, value)
            with self.assertRaises(WorkspaceError):
                functional_key(control, code)


if __name__ == '__main__':
    unittest.main()
