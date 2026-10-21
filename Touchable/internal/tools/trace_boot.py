#!/usr/bin/env python3
"""Trace the local development boot with LLDB. No game bytes are changed on disk."""
from pathlib import Path
import argparse
import os
import re
import signal
import subprocess
from workspace import ROOT, WorkspaceError, load_game, validate_registry

p = argparse.ArgumentParser()
p.add_argument('--seconds', type=int, default=15)
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
root = ROOT
host = game.host_dir / (game.id + '-host')
if not host.is_file(): p.error('Build the host first: bash internal/tools/build_host.sh --game ' + game.id)
if not (game.game_dir / 'debug/lldb.json').is_file(): p.error('The game needs debug/lldb.json for LLDB tracing.')
game.host_dir.mkdir(parents=True, exist_ok=True)
game.logs_dir.mkdir(parents=True, exist_ok=True)
save_dir = a.save_dir.resolve() if a.save_dir else game.save_dir
for directory in (save_dir, game.temp_dir, game.capture_dir): directory.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
env.update(game.profile.get('environment', {}))
game.input_file.write_text('0 0 0\n')
env.update(DM_ROMFS=str(game.content_dir/'RomFS'), DM_SAVE=str(save_dir),
           DM_TEMP=str(game.temp_dir), DM_NVN_SHMAP=str(game.shaders_dir/'shader_map.tsv'),
           DM_MSL_DIR=str(game.shaders_dir/'mac'), DM_PSO_MANIFEST='off', DM_NVN_TBDR='0',
           DM_PRESENT_LINK='0', DM_FS='1', SS_SHADER_CAPTURE=str(game.capture_dir),
           SS_INPUT_FILE=str(game.input_file))
commands = ('settings set target.disable-aslr false\nbreakpoint set -n run_module_inits\nrun\n'
            'process handle SIGTRAP -s false -p true -n false\n'
            'script import sys; sys.path.insert(0, '+repr(str(root/'tools'))+'); '
            'import ss_lldb; ss_lldb.install(lldb.debugger, '+repr(game.id)+', '+repr(str(root))+')\ncontinue\n')
path = game.host_dir/'boot.lldb'
path.write_text(commands)
log = game.logs_dir/'lldb-boot.log'
with log.open('w') as f:
    process = subprocess.Popen(['xcrun', 'lldb', '-b', '-s', str(path), '--',
        str(host), str(game.patched_dir)],
        env=env, stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        process.wait(timeout=a.seconds)
    except subprocess.TimeoutExpired:
        # LLDB may detach its target when it exits. Stop only this launch's target.
        text = log.read_text()
        targets = re.findall(r'Process (\d+) launched:.*'+re.escape(host.name), text)
        for target in targets:
            try: os.kill(int(target), signal.SIGKILL)
            except ProcessLookupError: pass
        try: process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait()
print('Trace:', log)
