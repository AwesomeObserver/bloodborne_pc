#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Collect an opt-in native RTX session, GPU preflight, dumps and binary fingerprints."""
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile


def collect(directory, status, root, preflight):
    binaries = {}
    for name in ('Bloodborne.exe', 'bin/bb-probe.exe', 'bin/bb-rtx-check.exe'):
        path = root / name
        if path.is_file():
            binaries[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    metadata = {
        'format': 'BBPORT_RTX_TRACE_V1',
        'launch_status_hex': f'0x{status & 0xffffffff:08x}',
        'gpu_preflight_status': preflight,
        'binary_sha256': binaries,
        'renderer': 'Experimental native Vulkan hardware ray query, hybrid indirect mode.',
        'contents': 'This session log, crash dumps and binary fingerprints. No game, save or settings files are copied.',
    }
    archive = directory.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as report:
        report.writestr('metadata.json', json.dumps(metadata, indent=2) + '\n')
        report.write(directory / 'session.log', 'session.log')
        for path in sorted(directory.glob('bb-crash-*.dmp')):
            report.write(path, path.name)
    return archive


def main():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
        except (AttributeError, ValueError):
            pass
    root = Path(__file__).resolve().parent.parent
    data = Path(os.environ.get('BB_DATA_DIR', root)).resolve()
    directory = data / 'logs' / ('rtx-trace-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    directory.mkdir(parents=True)
    env = {**os.environ, 'BB_RTX_PATH_TRACE': '1', 'BB_RTX_MODE': '2',
           'BB_CRASH_DUMP': '1', 'BB_CRASH_DIR': str(directory)}
    env.pop('BB_CRASH_MONITOR', None)
    flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    print('RTX diagnostics: GPU preflight, then gameplay in hybrid mode. Exit the game to collect the report.', flush=True)
    preflight = None
    checker = root / 'bin/bb-rtx-check.exe'
    with (directory / 'session.log').open('w', encoding='utf-8') as log:
        if checker.is_file():
            try:
                result = subprocess.run([str(checker)], cwd=root, env=env, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                    timeout=120, creationflags=flags)
                preflight = result.returncode
                log.write(result.stdout)
                print(result.stdout, end='', flush=True)
                log.write(f'RTX GPU preflight: 0x{preflight & 0xffffffff:08x}\n')
            except subprocess.TimeoutExpired:
                preflight = 'timeout'
                log.write('RTX GPU preflight timed out.\n')
            log.flush()
        with subprocess.Popen([str(root / 'Bloodborne.exe'), '--play'], cwd=root, env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding='utf-8', errors='replace', creationflags=flags) as process:
            for line in process.stdout:
                log.write(line); log.flush(); print(line, end='', flush=True)
            status = process.wait()
        log.write(f'Launch exit: 0x{status & 0xffffffff:08x}\n')
    print(f'Report: {collect(directory, status, root, preflight)}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f'RTX diagnostics: {error}', file=sys.stderr)
        raise SystemExit(1)
