#!/usr/bin/env python3
"""Create a private Mac transfer package for one configured game."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import zipfile

from analyze_game import load_nso
from verify_mac_transfer import sha256
from workspace import ROOT, PROJECT_ROOT, DEPENDENCIES, UPSTREAM, WorkspaceError, check_game, load_game, validate_registry

SKIP_NAMES = {'.git', '__pycache__', '.DS_Store', 'xcuserdata'}
PRIVATE_SUFFIXES = {'.keys', '.nsp', '.nsz', '.xci', '.nca', '.tik', '.cert', '.ipa',
                    '.p12', '.pfx', '.mobileprovision'}


def tree_files(directory):
    """Use an explicit source tree. Do not follow links or copy local caches."""
    for path in sorted(directory.rglob('*')):
        relative = path.relative_to(directory)
        if any(part in SKIP_NAMES for part in relative.parts):
            continue
        if directory == ROOT / 'tools' and relative.parts[0] in ('vendor', 'dotnet'):
            continue
        if path.is_symlink():
            raise WorkspaceError('A selected path is a symlink: ' + str(path))
        if path.is_file():
            if path.suffix.lower() in PRIVATE_SUFFIXES:
                raise WorkspaceError('A selected source tree contains a private file: ' + str(path))
            yield path


def check_inputs(game):
    game.require_adapted('ios')
    errors = check_game(game)
    if errors:
        raise WorkspaceError('\n'.join(errors))
    upstream = UPSTREAM
    revision = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != game.profile.get('upstream_revision'):
        raise WorkspaceError('The upstream revision does not match the game profile.')
    current_patch = subprocess.check_output(['git', '-C', str(upstream), 'diff', '--binary', '--full-index', '--no-ext-diff', '--no-color', 'HEAD'])
    if current_patch != (ROOT / 'patches/d2r-mobile.patch').read_bytes():
        raise WorkspaceError('The current upstream diff does not match patches/d2r-mobile.patch.')
    if subprocess.check_output(['git', '-C', str(upstream), 'ls-files', '--others', '--exclude-standard']):
        raise WorkspaceError('Record the untracked upstream files before packaging.')
    for name, expected in game.profile['modules'].items():
        actual = load_nso(game.content_dir / 'ExeFS' / name)
        if any(actual[field] != expected[field] for field in ('sha256', 'build_id')):
            raise WorkspaceError('The input module does not match the profile: ' + name)
    verification = json.loads((game.patched_dir / 'verification.json').read_text())
    for name in game.module_names:
        record = verification['modules'][name]
        if record.get('source_sha256') != game.profile['modules'][name]['sha256']:
            raise WorkspaceError('The rewrite report has a different input module: ' + name)
        if record.get('structure') != 'passed' or record.get('equivalence') != 'passed':
            raise WorkspaceError('The rewrite checks did not pass: ' + name)
        expected_files = record['output_sha256']
        if name + '.patched.bin' not in expected_files or name + '.fixups.tsv' not in expected_files:
            raise WorkspaceError('The rewrite report is incomplete: ' + name)
        for filename, digest in expected_files.items():
            if Path(filename).name != filename or sha256(game.patched_dir / filename) != digest:
                raise WorkspaceError('A rewrite output does not match its report: ' + filename)
    chain = json.loads((game.shaders_dir / 'chain.json').read_text())['chain']
    if not isinstance(chain, str) or not re.fullmatch(r'[0-9a-f]+', chain):
        raise WorkspaceError('The shader chain is invalid.')
    shader_dir = game.shaders_dir / 'ios' / ('b-' + chain)
    if not list(shader_dir.glob('*.metallib')):
        raise WorkspaceError('The active iOS shader libraries are missing.')
    return upstream, revision, chain, shader_dir


def write_readme(game, root):
    content = f'''# Touchable: {game.display_name} Mac transfer package

Use this package to move the project to another Mac that you own.
This package contains game code and game assets. Keep it as a private transfer file.
This package does not contain the original NSP, keys, signing certificates, provisioning profiles, IPA files, or personal save data.

## Requirements for the new Mac

- Use an Apple Silicon Mac.
- Install the full Xcode. The source Mac uses Xcode 27.0.
- Install the iPhone SDK. The minimum system version of the app is iOS {game.minimum_ios_version}.
- In Xcode, sign in to the original Apple Account.
- In Xcode Settings > Locations, select the Command Line Tools.
- Keep at least 20 GiB of free space for extraction and the first build.

## Open the project

1. Copy the full ZIP file to the new Mac.
2. Extract the ZIP file to a local disk.
3. Double-click `Open-{game.product_name}.command`.
4. Wait for the file check to complete.
5. Wait for Xcode to open the project.

The script creates a local `.nitenphone/venv` with the Python that Xcode supplies.
The script uses only the Python standard library.
The script does not download dependencies. It does not extract the game again, rewrite the modules again, or compile the shaders again.
The script generates the project for the new paths. If you move the extracted folder, run the script again.

If macOS blocks the double-click, type `bash ` in Terminal, then drag the `.command` file into Terminal.
Press Return to run it.

## Install on the iPhone

1. At the top of Xcode, select the `{game.product_name}` scheme.
2. Connect the iPhone.
3. Select this iPhone as the run destination. Do not select a Simulator.
4. In Target > Signing & Capabilities, select the original development team.
5. Keep Automatically manage signing enabled.
6. Keep the Bundle ID `{game.bundle_id}`.
7. On the iPhone, complete the trust and Developer Mode settings.
8. Back up the save data on the iPhone before you install.
9. Click the Run button in Xcode.

A different development team or Bundle ID can make a different app identity.
The old save data on the iPhone does not move to a new identity automatically.
If Xcode reports that a certificate has no private key, export the signing identity with its private key from the old Mac. Then import it on the new Mac.
A copy of the provisioning profile alone does not recover the signing private key.

## Current version

- Game ID: `{game.id}`.
- Game package version: `{game.profile.get('package_version')}`.
- App build: `{game.build_version}`.
- Bundle ID: `{game.bundle_id}`.
- Save directory on the iPhone: `Documents/Saves/{game.save_directory}`.
- The save data stays in the app container on the iPhone. This package did not read or change the save data on the iPhone.

This package did not change the adaptation status of the game.
The full game flow is not verified.
Read the known limits in `internal/games/{game.id}/README.md`.

## File check

`mac-transfer.json` records the size and SHA-256 of each transfer file.
The launch script checks all transfer files.
If the check fails, copy and extract the original ZIP file again.
The `.sha256` file outside the package checks the full ZIP file.

To check the transfer package without opening Xcode:

```sh
bash Open-{game.product_name}.command --check-only
```

Signing, installation, and gameplay on the new Mac must be verified on the new Mac and the target iPhone.
'''
    (root / 'README-FIRST.md').write_text(content)


def archive_package(root, archive):
    if archive.exists():
        raise WorkspaceError('The archive already exists: ' + str(archive))
    total = sum(path.stat().st_size for path in root.rglob('*') if path.is_file())
    done, next_report = 0, 256 << 20
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as output:
        for path in sorted(root.rglob('*')):
            if not path.is_file():
                continue
            output.write(path, str(Path(root.name) / path.relative_to(root)))
            done += path.stat().st_size
            if done >= next_report:
                print(f'Archived {done / 2**30:.2f} / {total / 2**30:.2f} GiB', flush=True)
                next_report = done + (256 << 20)
    digest = sha256(archive)
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n')
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, help='One registered game ID.')
    parser.add_argument('--output', type=Path, required=True, help='A new output directory.')
    args = parser.parse_args()
    try:
        validate_registry(ROOT)
        game = load_game(args.game)
        output = args.output.resolve()
        if output.exists():
            raise WorkspaceError('The output exists. Select a new directory.')
        selected_trees = [ROOT / name for name in ('runtime', 'platform', 'tools', 'tests', 'patches')]
        selected_trees.append(PROJECT_ROOT / 'docs')
        selected_trees += [game.game_dir, game.content_dir, game.patched_dir]
        if any(output.is_relative_to(tree) for tree in (*selected_trees, game.shaders_dir)):
            raise WorkspaceError('The output must be outside the selected source and game directories.')
        upstream, revision, chain, shader_dir = check_inputs(game)
        selected = set()
        for tree in (*selected_trees, shader_dir):
            selected.update(tree_files(tree))
        selected.update(PROJECT_ROOT / name for name in ('AGENTS.md', '.gitignore', 'Touchable.command'))
        selected.add(ROOT / 'requirements.txt')
        selected.update(game.shaders_dir / name for name in ('chain.json', 'shader_map.tsv'))
        tracked = subprocess.check_output(['git', '-C', str(upstream), 'ls-files', '-z'])
        for relative in tracked.split(b'\0'):
            if relative:
                path = upstream / relative.decode()
                if path.is_symlink() or path.suffix.lower() in PRIVATE_SUFFIXES or not path.is_file():
                    raise WorkspaceError('An upstream file cannot be copied: ' + str(path))
                selected.add(path)
        total = sum(path.stat().st_size for path in selected)
        output.parent.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(output.parent).free < 2 * total + (2 << 30):
            raise WorkspaceError('The output disk needs space for the folder and ZIP.')
        output.mkdir()
        root = output / ('Touchable-' + game.product_name)
        root.mkdir()
        print(f'Copying {len(selected)} files ({total / 2**30:.2f} GiB).', flush=True)
        for source in sorted(selected):
            target = root / source.relative_to(PROJECT_ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        launcher = (ROOT / 'tools/mac_transfer_launcher.command').read_text()
        launcher = launcher.replace('__TRANSFER_GAME__', game.id).replace('__TRANSFER_PRODUCT__', game.product_name)
        launch_path = root / ('Open-' + game.product_name + '.command')
        launch_path.write_text(launcher)
        launch_path.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        write_readme(game, root)
        # Keep the selected local identity. Do not copy account or certificate settings.
        local_config = root / '.nitenphone/config.json'
        local_config.parent.mkdir(parents=True, exist_ok=True)
        local_config.write_text(json.dumps({'schema_version': 1, 'games': {
            game.id: {'bundle_id': game.bundle_id}}}, indent=2) + '\n')
        local_config.chmod(0o600)
        (root / 'UPSTREAM-REVISION.txt').write_text(revision + '\n')
        records = []
        for path in sorted(root.rglob('*')):
            if path.is_file():
                records.append({'path': path.relative_to(root).as_posix(), 'bytes': path.stat().st_size,
                                'sha256': sha256(path)})
        manifest = {'schema_version': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
                    'game_id': game.id, 'bundle_id': game.bundle_id, 'build_version': game.build_version,
                    'save_directory': game.save_directory, 'package_version': game.profile.get('package_version'),
                    'upstream_revision': revision, 'shader_chain': chain, 'files': records}
        (root / 'mac-transfer.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        archive = output / f'Touchable-{game.product_name}-build{game.build_version}-Mac.zip'
        digest = archive_package(root, archive)
        report = {'game_id': game.id, 'build_version': game.build_version, 'folder': str(root),
                  'archive': str(archive), 'archive_bytes': archive.stat().st_size, 'sha256': digest,
                  'files': len(records), 'input_bytes': total, 'upstream_revision': revision,
                  'shader_chain': chain, 'status': 'Packaged. Relocation and Xcode checks are separate.'}
        (output / 'package-report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2), flush=True)
        return 0
    except (WorkspaceError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print('Error:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
