#!/usr/bin/env python3
"""Package the signed local app as an IPA and record its checksum."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import zipfile

from workspace import LOCAL_ROOT, WorkspaceError, load_game, validate_registry

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
    parser.add_argument('--configuration', choices=['Debug', 'Release'], default='Debug')
    parser.add_argument('--app', type=Path, help='Signed .app; defaults to the selected game build')
    parser.add_argument('--output', type=Path, help='IPA path; existing files are never replaced')
    parser.add_argument('--tested-device', help='Optional device UDID to check against the signing profile')
    args = parser.parse_args()
    try:
        game = load_game(args.game)
        validate_registry(game.root)
        game.require_adapted('ios')
    except WorkspaceError as error:
        parser.error(str(error))
    latest = LOCAL_ROOT / 'build/games' / game.id / 'latest.json'
    prior_app = Path(json.loads(latest.read_text())['app']) if latest.is_file() else None
    app = (args.app or prior_app or game.ios_build_dir / 'Build/Products' / f'{args.configuration}-iphoneos' / f'{game.product_name}.app').resolve()
    if not app.is_dir():
        parser.error('The signed app does not exist: ' + str(app))
    with (app / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('CFBundleIdentifier') != game.bundle_id:
        parser.error('The app bundle ID does not match the selected game.')
    if info.get('NitenGameID', game.id) != game.id:
        parser.error('The app game ID does not match the selected game.')
    output = (args.output or game.ipa_dir / f'{game.product_name}-build{info["CFBundleVersion"]}.ipa').resolve()
    if output.exists():
        parser.error('The output exists. Use a new output path.')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    profile_data = subprocess.check_output(['security', 'cms', '-D', '-i', str(app / 'embedded.mobileprovision')])
    profile = plistlib.loads(profile_data)
    if profile['ExpirationDate'] <= datetime.now(timezone.utc).replace(tzinfo=None):
        parser.error('The signing profile has expired.')
    if args.tested_device and not profile.get('ProvisionsAllDevices') and args.tested_device not in profile.get('ProvisionedDevices', []):
        parser.error('The signing profile does not include the selected device.')
    files = sorted(p for p in app.rglob('*') if p.is_file())
    if any(p.name.lower().endswith(('.keys', '.nsp')) for p in files):
        parser.error('The app contains a key file or source NSP.')
    output.parent.mkdir(parents=True, exist_ok=True)
    staged = output.with_name('.' + output.name + '.partial')
    total = sum(p.stat().st_size for p in files)
    done = 0
    next_report = 1 << 30
    with zipfile.ZipFile(staged, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as archive:
        for path in files:
            archive.write(path, str(Path('Payload') / app.name / path.relative_to(app)))
            done += path.stat().st_size
            if done >= next_report:
                print(f'Packaged {done / (1 << 30):.1f} / {total / (1 << 30):.1f} GiB', flush=True)
                next_report = done + (1 << 30)
    digest = hashlib.sha256()
    with staged.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b''):
            digest.update(chunk)
    with zipfile.ZipFile(staged) as archive:
        prefix = f'Payload/{app.name}/'
        assert all(name.startswith(prefix) for name in archive.namelist())
        assert prefix + 'Info.plist' in archive.namelist()
        assert prefix + info['CFBundleExecutable'] in archive.namelist()
        assert prefix + 'embedded.mobileprovision' in archive.namelist()
        bad = archive.testzip()
        if bad:
            raise RuntimeError(f'Archive CRC check failed: {bad}')
    staged.rename(output)
    report = {
        'ipa': str(output), 'bytes': output.stat().st_size,
        'sha256': digest.hexdigest(), 'files': len(files),
        'game_id': game.id, 'app_name': app.name,
        'bundle_id': info['CFBundleIdentifier'],
        'executable': info['CFBundleExecutable'],
        'build_version': info['CFBundleVersion'],
        'marketing_version': info['CFBundleShortVersionString'],
        'profile_expires_utc': profile['ExpirationDate'].isoformat() + 'Z',
        'profile_device_count': len(profile.get('ProvisionedDevices', [])),
    }
    if args.tested_device:
        report['tested_device_in_profile'] = args.tested_device in profile.get('ProvisionedDevices', [])
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    output.with_suffix('.sha256').write_text(f'{digest.hexdigest()}  {output.name}\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
