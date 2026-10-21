#!/usr/bin/env python3
"""Drive Silksong's title screen for a bounded cutscene test."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from workspace import load_game


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wait', type=int, default=90)
    parser.add_argument('--presses', type=int, default=8)
    args = parser.parse_args()
    if not 0 <= args.wait <= 300 or not 0 <= args.presses <= 30:
        parser.error('Use wait 0..300 and presses 0..30.')
    game = load_game('silksong')
    game.require_adapted()
    host = game.host_dir / 'silksong-host'
    if not host.is_file(): parser.error('Build the Silksong host first.')
    for directory in (game.save_dir, game.temp_dir, game.logs_dir):
        directory.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.update(game.profile.get('environment', {}))
    env.update(DM_ROMFS=str(game.base_dir / 'RomFS'), DM_SAVE=str(game.save_dir),
               DM_TEMP=str(game.temp_dir), SS_INPUT_FILE=str(game.input_file),
               SS_SHADER_CAPTURE=str(game.capture_dir),
               DM_NVN_SHMAP=str(game.shaders_dir / 'shader_map.tsv'),
               DM_MSL_DIR=str(game.shaders_dir / 'mac'), DM_PSO_MANIFEST='off',
               DM_NVN_TBDR='0', DM_PRESENT_LINK='0')

    def input_value(value):
        temporary = game.input_file.with_suffix('.tmp')
        temporary.write_text(f'{value} 0 0\n')
        temporary.replace(game.input_file)

    def press(duration):
        input_value(1)
        try: time.sleep(duration)
        finally: input_value(0)

    input_value(0)
    log = game.logs_dir / 'movie-test.log'
    with log.open('w') as output:
        process = subprocess.Popen([str(host), str(game.patched_dir)], cwd=ROOT,
                                   env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            time.sleep(25)
            for index in range(3):
                if process.poll() is not None: break
                press(0.2)
                if index < 2: time.sleep(5)
            for _ in range(args.presses):
                if process.poll() is not None: break
                time.sleep(6)
                press(0.4)
            if process.poll() is None:
                try: process.wait(timeout=max(1, args.wait))
                except subprocess.TimeoutExpired: pass
        finally:
            input_value(0)
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
    print('Log:', log)


if __name__ == '__main__': main()
