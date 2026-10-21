#!/usr/bin/env python3
"""Checks for games/rhythm-heaven/tools/unlock_all.py on a synthetic save."""
import importlib.util
from pathlib import Path
import struct
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'unlock_all.py'
spec = importlib.util.spec_from_file_location('unlock_all', TOOL)
unlock_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(unlock_all)


def blank_save():
    data = bytearray(unlock_all.SIZE)
    struct.pack_into('<II', data, 0x10, 0x34, unlock_all.SIZE)
    return data


class UnlockAllTest(unittest.TestCase):
    def test_unlocks_every_group(self):
        data = blank_save()
        data[0x2e0] = 6          # stage index 40 already cleared
        data[0x2e4] = 0x71       # its other fields
        data[0x2c] = 3           # menu feature already unlocked
        out, raised = unlock_all.unlock(bytes(data))
        self.assertEqual(out[0x28], 0)                     # kind 0 index 0 is not a feature
        self.assertTrue(all(out[0x28 + 4 * i] == 3 for i in range(1, 14)))
        self.assertTrue(all(out[0x60 + 16 * i] >= 2 for i in range(80)))
        self.assertTrue(all(out[0x560 + 8 * i] == 2 for i in range(30)))
        self.assertEqual(out[0x2e0], 6)
        self.assertEqual(out[0x2e4], 0x71)
        self.assertEqual(raised, 12 + 79 + 30)
        self.assertEqual(out[:0x28], bytes(data[:0x28]))
        changed = {i for i in range(len(out)) if out[i] != data[i]}
        allowed = {0x28 + 4 * i for i in range(14)} | {0x60 + 16 * i for i in range(80)} | {0x560 + 8 * i for i in range(30)}
        self.assertTrue(changed <= allowed)

    def test_rejects_other_files(self):
        with self.assertRaises(ValueError):
            unlock_all.unlock(bytes(100))
        data = blank_save()
        struct.pack_into('<I', data, 0x10, 0x30)
        with self.assertRaises(ValueError):
            unlock_all.unlock(bytes(data))

    def test_idempotent(self):
        once, _ = unlock_all.unlock(bytes(blank_save()))
        twice, raised = unlock_all.unlock(once)
        self.assertEqual(once, twice)
        self.assertEqual(raised, 0)

    def test_perfect_keeps_scores_counts_and_other_progress(self):
        data = blank_save()
        for index in range(80):
            position = 0x60 + index * 16
            data[position] = index % 10
            struct.pack_into('<III', data, position + 4, 895 + index, index + 1, index % 4)
        data[0x2c] = 1
        data[0x560] = 6
        data[0x66a] = 3
        out, raised = unlock_all.unlock_perfect(bytes(data))
        positions = {0x60 + index * 16 for index in range(80)}
        self.assertEqual(raised, 56)
        for index in range(80):
            position = 0x60 + index * 16
            self.assertEqual(out[position], max(7, data[position]))
        self.assertEqual(bytes(value for i, value in enumerate(out) if i not in positions),
                         bytes(value for i, value in enumerate(data) if i not in positions))
        twice, raised = unlock_all.unlock_perfect(out)
        self.assertEqual(twice, out)
        self.assertEqual(raised, 0)

    def test_perfect_rejects_other_files(self):
        for data in (bytes(100), bytes(unlock_all.SIZE)):
            with self.assertRaises(ValueError):
                unlock_all.unlock_perfect(data)


if __name__ == '__main__':
    unittest.main()
