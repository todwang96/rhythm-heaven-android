#!/usr/bin/env python3
"""Check the files in a local Mac transfer package. Use only the standard library."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

from workspace import ROOT, PROJECT_ROOT, WorkspaceError, load_game, validate_registry


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def verify_files(root, records):
    """Reject missing files, changed files, and paths outside the package."""
    root = Path(root).resolve()
    if not isinstance(records, list) or not records:
        raise WorkspaceError('The transfer file list is empty or invalid.')
    seen = set()
    total = 0
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get('path'), str):
            raise WorkspaceError('A transfer file record is invalid.')
        relative = PurePosixPath(record['path'])
        if relative.is_absolute() or '..' in relative.parts or str(relative) in ('', '.'):
            raise WorkspaceError('A transfer path is invalid: ' + str(relative))
        if str(relative) in seen:
            raise WorkspaceError('A transfer path is repeated: ' + str(relative))
        seen.add(str(relative))
        path = root / relative
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise WorkspaceError('A transfer path leaves the package: ' + str(relative))
        if not path.is_file():
            raise WorkspaceError('A transfer file is missing: ' + str(relative))
        size = path.stat().st_size
        if size != record.get('bytes') or sha256(path) != record.get('sha256'):
            raise WorkspaceError('A transfer file changed: ' + str(relative))
        total += size
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, help='Game ID in the transfer package.')
    args = parser.parse_args()
    try:
        manifest = json.loads((PROJECT_ROOT / 'mac-transfer.json').read_text())
        if manifest.get('schema_version') != 1 or manifest.get('game_id') != args.game:
            raise WorkspaceError('The transfer manifest does not match the selected game.')
        total = verify_files(PROJECT_ROOT, manifest.get('files'))
        validate_registry(ROOT)
        game = load_game(args.game)
        for field, expected in (('bundle_id', game.bundle_id), ('build_version', game.build_version),
                                ('save_directory', game.save_directory)):
            if manifest.get(field) != expected:
                raise WorkspaceError('The transfer identity changed: ' + field)
        print(f'Verified {len(manifest["files"])} files ({total / 2**30:.2f} GiB).')
        return 0
    except (WorkspaceError, OSError, ValueError, TypeError) as error:
        print('Error:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
