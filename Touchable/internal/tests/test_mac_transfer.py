"""Check that transfer verification rejects damaged and unsafe inputs."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_mac_transfer import verify_files
from workspace import WorkspaceError


class TransferVerificationTests(unittest.TestCase):
    def test_same_size_damage_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'module.bin'
            path.write_bytes(b'original')
            records = [{'path': path.name, 'bytes': 8, 'sha256': hashlib.sha256(b'original').hexdigest()}]
            self.assertEqual(verify_files(root, records), 8)
            path.write_bytes(b'modified')
            with self.assertRaises(WorkspaceError):
                verify_files(root, records)

    def test_parent_traversal_and_external_link_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / 'package'
            root.mkdir()
            outside = base / 'outside'
            outside.write_bytes(b'keep')
            (root / 'linked').symlink_to(outside)
            for name in ('../outside', str(outside), 'linked'):
                with self.subTest(name=name), self.assertRaises(WorkspaceError):
                    verify_files(root, [{'path': name, 'bytes': 4, 'sha256': hashlib.sha256(b'keep').hexdigest()}])
            self.assertEqual(outside.read_bytes(), b'keep')

    def test_missing_and_duplicate_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'profile.json'
            path.write_bytes(b'{}')
            record = {'path': path.name, 'bytes': 2, 'sha256': hashlib.sha256(b'{}').hexdigest()}
            with self.assertRaises(WorkspaceError):
                verify_files(root, [record, record])
            path.unlink()
            with self.assertRaises(WorkspaceError):
                verify_files(root, [record])


if __name__ == '__main__':
    unittest.main()
