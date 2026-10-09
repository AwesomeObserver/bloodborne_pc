#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check official Remix rendered pixels and Vulkan interop, without starting the game."""
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
        except (AttributeError, ValueError):
            pass
    root = Path(__file__).resolve().parents[1]
    runtime = root / 'remix-runtime-1.5.2' / '.trex'
    checker = root / 'bin' / 'bb-remix-check.exe'
    if not checker.is_file():
        raise SystemExit('bb-remix-check.exe is missing; rebuild or extract the complete Windows package.')
    if not (runtime / 'd3d9.dll').is_file():
        raise SystemExit('Official runtime is missing. Run Bloodborne.exe --script scripts/install_remix.py first.')
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    report = Path(os.environ.get('BB_DATA_DIR', root)) / 'logs' / f'remix-sdk-{stamp}'
    report.mkdir(parents=True)
    report = report.resolve()
    env = dict(os.environ, DXVK_LOG_PATH=str(report), BB_CRASH_DIR=str(report))
    command = [str(checker), str(runtime / 'd3d9.dll'), str(report / 'sdk-output.ppm')]
    monitor = root / 'bin' / 'bb-crash-monitor.exe'
    if monitor.is_file():
        command.insert(0, str(monitor))
    print('Official RTX Remix SDK diagnostic. This does not start Bloodborne or enable game path tracing.', flush=True)
    with (report / 'session.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, cwd=runtime, env=env, stdin=subprocess.DEVNULL,
            stdout=log, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            status = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            # Terminate this diagnostic process tree only; no game process was started.
            subprocess.run(['taskkill.exe', '/PID', str(process.pid), '/T', '/F'],
                           stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), timeout=15)
            process.wait(timeout=15)
            status = 124
    output = (report / 'session.log').read_text(encoding='utf-8', errors='replace')
    success = status == 0 and 'RTX Remix SDK PASS:' in output
    metadata = {'test': 'Official SDK synthetic scene, not Bloodborne gameplay',
                'success': success, 'exit_code': status & 0xffffffff,
                'runtime': '1.5.2', 'sdk_api': '0.6.4'}
    (report / 'result.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(f'{"PASS" if success else "FAILED"}: official SDK pixel and shared-output check.\nReport: {report}', flush=True)
    return 0 if success else 1


if __name__ == '__main__':
    raise SystemExit(main())
