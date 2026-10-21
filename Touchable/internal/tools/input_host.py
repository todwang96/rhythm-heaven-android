#!/usr/bin/env python3
"""Press a Switch button in the local development host for a bounded time."""
import argparse
from pathlib import Path
import time
from workspace import WorkspaceError, load_game, validate_registry
p = argparse.ArgumentParser()
p.add_argument('buttons', help='Comma-separated Switch button names')
p.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
p.add_argument('--seconds', type=float, default=0.15)
a = p.parse_args()
try:
    game = load_game(a.game)
    validate_registry(game.root)
    game.require_adapted()
except WorkspaceError as error:
    p.error(str(error))
names = ['A','B','X','Y','LS','RS','L','R','ZL','ZR','PLUS','MINUS','LEFT','UP','RIGHT','DOWN']
mask = 0
for name in a.buttons.upper().split(','):
    if name not in names: p.error('Unknown Switch button: ' + name)
    mask |= 1 << names.index(name)
path = game.input_file
path.parent.mkdir(parents=True, exist_ok=True)
temporary = path.with_suffix('.tmp')
def put(value):
    temporary.write_text(f'{value:x} 0 0\n'); temporary.replace(path)
try:
    put(mask); time.sleep(max(0.02, min(a.seconds, 60)))
finally: put(0)
