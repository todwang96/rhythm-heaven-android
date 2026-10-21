#!/usr/bin/env python3
"""Run a bounded development boot and keep the complete log on disk."""
import argparse
from datetime import datetime
import os
from pathlib import Path
import subprocess
import shutil
from workspace import WorkspaceError, load_game, validate_registry

p = argparse.ArgumentParser()
p.add_argument('--seconds', type=int, default=10)
p.add_argument('--trace', action='store_true')
p.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
p.add_argument('--save-dir', type=Path, help='Use a separate save directory for a development test')
a = p.parse_args()
if a.seconds < 1: p.error('--seconds must be positive')
try:
    game = load_game(a.game)
    validate_registry(game.root)
    game.require_adapted()
except WorkspaceError as error:
    p.error(str(error))
host = game.host_dir / (game.id + '-host')
if not host.is_file(): p.error('Build the host first: bash internal/tools/build_host.sh --game ' + game.id)
if not game.patched_dir.is_dir(): p.error('Rewritten modules do not exist: ' + str(game.patched_dir))
environment = dict(os.environ)
environment.update(game.profile.get('environment', {}))
input_file = game.input_file
input_file.parent.mkdir(parents=True, exist_ok=True)
input_file.write_text('0 0 0\n')
environment['SS_INPUT_FILE'] = str(input_file)
environment.update(DM_ROMFS=str(game.content_dir / 'RomFS'),
                   DM_SAVE=str(a.save_dir.resolve() if a.save_dir else game.save_dir),
                   DM_TEMP=str(game.temp_dir))
capture = game.capture_dir
capture.mkdir(parents=True, exist_ok=True)
mapping = capture.parent / 'shader_map.tsv'
if not mapping.exists(): mapping.write_text('package\tname\tstage\tkey\n')
environment.update(SS_SHADER_CAPTURE=str(capture), DM_NVN_SHMAP=str(mapping),
                   DM_MSL_DIR=str(capture.parent / 'mac'), DM_PSO_MANIFEST='off')
# Defaults that a game profile may override, as in the iOS app.
environment.setdefault('DM_NVN_TBDR', '0')
environment.setdefault('DM_PRESENT_LINK', '0')
for key in ['DM_SAVE', 'DM_TEMP']:
    Path(environment[key]).mkdir(parents=True, exist_ok=True)
if a.trace:
    environment.update(DM_SVC='1', DM_THR='1', DM_FS='1')
game.logs_dir.mkdir(parents=True, exist_ok=True)
log = game.logs_dir / 'host-boot.log'
if log.exists():
    shutil.copy2(log, log.with_name('host-boot-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.log'))
with log.open('w') as output:
    process = subprocess.Popen([str(host), str(game.patched_dir)],
                               env=environment, stdout=output, stderr=subprocess.STDOUT)
    try:
        result = process.wait(timeout=a.seconds)
        print('Boot exit:', result)
    except subprocess.TimeoutExpired:
        process.terminate()
        try: process.wait(timeout=3)
        except subprocess.TimeoutExpired: process.kill(); process.wait()
        print('Boot time limit reached:', a.seconds, 'seconds')
print('Log:', log)
