#!/usr/bin/env python3
"""Send a touch or drag to a host started with RH_POINTER_FILE. Coordinates use 1920 x 1080."""
import argparse
import math
import time
from workspace import load_game, WorkspaceError


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True)
    parser.add_argument('action', choices=['tap', 'drag', 'down', 'up', 'cancel', 'shot'])
    parser.add_argument('coordinates', nargs='*', type=float)
    parser.add_argument('--seconds', type=float, default=0.3)
    args = parser.parse_args()
    try:
        game = load_game(args.game)
    except WorkspaceError as error:
        parser.error(str(error))
    needed = 4 if args.action == 'drag' else 0 if args.action in ('cancel', 'shot') else 2
    if len(args.coordinates) != needed or not all(math.isfinite(v) for v in args.coordinates):
        parser.error('Use four coordinates for drag. Use two for other actions.')
    if not math.isfinite(args.seconds) or not 0.04 <= args.seconds <= 10:
        parser.error('--seconds must be from 0.04 to 10.')
    path = game.host_dir / 'pointer.txt'
    path.parent.mkdir(parents=True, exist_ok=True)

    def put(kind, x, y):
        temporary = path.with_suffix('.tmp')
        temporary.write_text(f'{time.time_ns()} {kind} {x:.3f} {y:.3f}\n')
        temporary.replace(path)

    if args.action in ('cancel', 'shot'):
        put(3 if args.action == 'cancel' else 5, 0, 0)
        return
    x, y = args.coordinates[:2]
    if args.action in ('down', 'up'):
        put(0 if args.action == 'down' else 2, x, y)
        return
    put(0, x, y)
    try:
        if args.action == 'tap':
            time.sleep(args.seconds)
        else:
            end_x, end_y = args.coordinates[2:]
            for step in range(1, 21):
                time.sleep(args.seconds / 20)
                put(1, x + (end_x - x) * step / 20, y + (end_y - y) * step / 20)
            x, y = end_x, end_y
    finally:
        put(2, x, y)


if __name__ == '__main__':
    main()
