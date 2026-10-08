"""Collect the bounded, opt-in Windows mouse-camera trace without a Python install."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import zipfile


def decode(data):
    if len(data) < 4096 or data[:8] != b'BBMOUSE1':
        raise ValueError('Not a mouse-camera trace')
    version, header, record_size, capacity, frequency, before_count, after_count = struct.unpack_from('<4IQ2I', data, 8)
    if (version, header, record_size, capacity, before_count, after_count) != (1, 4096, 336, 4096, 64, 9):
        raise ValueError('Unsupported mouse-camera trace layout')
    if not frequency or len(data) != header + record_size * capacity:
        raise ValueError('Truncated mouse-camera trace')
    offsets = struct.unpack_from('<73I', data, 64)
    if len(set(offsets[:64])) != 64 or any(n > 0x294 or n % 4 for n in offsets):
        raise ValueError('Invalid camera field offsets')
    names = [f'before_{n:03x}' for n in offsets[:64]] + [f'after_{n:03x}' for n in offsets[64:]]
    rows, origin, previous = [], None, None
    for index in range(capacity):
        values = struct.unpack_from('<3Q2ifI73f', data, header + record_size * index)
        sequence, ticks, camera, dx, dy, sensitivity, flags, *fields = values
        if not sequence:  # unfilled or interrupted record
            continue
        if sequence != index + 1 or flags & ~15:
            raise ValueError('Invalid mouse-camera record')
        if origin is None:
            origin = previous = ticks
        rows.append([sequence, (ticks - origin) / frequency, (ticks - previous) / frequency,
                     hex(camera), dx / 256, dy / 256, sensitivity,
                     int(bool(flags & 1)), int(bool(flags & 2)), int(bool(flags & 4)), int(bool(flags & 8)), *fields])
        previous = ticks
    columns = ['sequence', 'seconds', 'update_seconds', 'camera', 'raw_dx', 'raw_dy', 'sensitivity',
               'capture', 'mouse_owner', 'stick_moving', 'invert_y', *names]
    return columns, rows


def collect(directory):
    state = (directory / 'state.bin').read_bytes()
    columns, rows = decode(state)
    if not rows:
        raise ValueError('No mouse samples: enable F4 and move the mouse after loading a character')
    # All cells are numeric or hex identifiers, so CSV quoting is unnecessary.
    csv = '\n'.join([','.join(columns), *[','.join(map(str, row)) for row in rows]]) + '\n'
    metadata = {
        'format': 'BBPORT_MOUSE_REPORT_V1', 'samples': len(rows),
        'seconds': rows[-1][1], 'capacity': 4096,
        'meaning': 'Before fields are sampled at camera-update entry; after fields follow native mouse writes. '
                   'Camera position at entry reflects the preceding game update. Different camera addresses are separate objects.',
        'limits': 'This report diagnoses a gameplay issue; collecting it does not fix camera behavior. '
                  'Code contains the unmodified camera routine, bounded direct callees and RIP-relative constants.',
        'privacy': 'No game assets, saves, player names, launcher settings or game logs are collected.',
    }
    target = directory.with_suffix('.zip')
    code = (directory / 'code.txt').read_bytes()
    # Avoid replacing a previously collected report, even when called twice.
    with zipfile.ZipFile(target, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        archive.writestr('state.bin', state)
        archive.writestr('state.csv', csv)
        archive.writestr('metadata.json', json.dumps(metadata, indent=2) + '\n')
        archive.writestr('code.txt', code)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, nargs='?', help='A logs/mouse-camera-* directory')
    parser.add_argument('--play', action='store_true', help='Run the configured game, then collect this session')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if args.play:
        if args.directory:
            parser.error('Use --play or a trace directory')
        started = time.time()
        print('Load a character, enable F4 and record left/right reversals, up/down sweeps and stops for about 15 seconds. '
              'Then close the game. Recording stops after 4096 camera updates.', flush=True)
        subprocess.run([str(root / 'Play Bloodborne.exe')], cwd=root,
                       env={**os.environ, 'BB_MOUSE_TRACE': '1'}, check=False)
        directories = sorted(p.parent for p in (root / 'logs').glob('mouse-camera-*/state.bin')
                             if p.stat().st_mtime >= started)
    else:
        if not args.directory:
            parser.error('Provide a trace directory or use --play')
        directories = [args.directory]
    if not directories:
        raise ValueError('No trace was created. Use the diagnostic bb-probe build and a supported camera hook.')
    for directory in directories:
        print(f'Report: {collect(directory)}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f'Mouse trace: {error}', file=sys.stderr)
        raise SystemExit(1)
