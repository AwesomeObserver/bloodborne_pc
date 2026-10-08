"""Verify a real access violation retains its exception and modules in a Windows minidump."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_probe import package


with tempfile.TemporaryDirectory(prefix='bbport краш проверка ') as directory:
    work = Path(directory)
    image = work / 'boot.bin'
    # xor eax,eax; mov dword ptr [rax],1 (guest access violation, no unwind metadata).
    image.write_bytes(package(bytes.fromhex('31c0c70001000000c3'), [], []))
    env = {**os.environ, 'BB_CRASH_DIR': str(work), 'BB_CRASH_DUMP': '1'}
    result = subprocess.run([sys.argv[1], str(image), '--cpu-only'], env=env,
        capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
    print(result.stdout)
    print(result.stderr, file=sys.stderr)
    assert result.returncode == 3, result.returncode
    assert 'Runtime build: PE timestamp=' in result.stdout
    dumps = list(work.glob('bb-crash-*.dmp'))
    assert len(dumps) == 1, dumps
    data = dumps[0].read_bytes()
    signature, _, count, directory_rva = struct.unpack_from('<4I', data)
    assert signature == 0x504d444d, hex(signature)  # MDMP
    streams = {kind: (size, rva) for kind, size, rva in
        (struct.unpack_from('<3I', data, directory_rva + i*12) for i in range(count))}
    assert 3 in streams and 4 in streams and 6 in streams  # threads, modules, exception
    _, exception_rva = streams[6]
    code = struct.unpack_from('<I', data, exception_rva + 8)[0]
    address = struct.unpack_from('<Q', data, exception_rva + 24)[0]
    assert code == 0xc0000005, hex(code)
    assert address == 0x800000002, hex(address)
    assert 'Crash dump:' in result.stderr
    print('PASS: original exception, threads, modules and Unicode dump path')
    # Diagnostics must preserve exit 3 even when disabled or unable to create the file.
    for extra, expected in [({'BB_CRASH_DUMP': '0'}, None),
            ({'BB_CRASH_DIR': str(work / 'missing' / 'child')}, 'Crash dump: failed')]:
        retry = subprocess.run([sys.argv[1], str(image), '--cpu-only'], env={**env, **extra},
            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)
        assert retry.returncode == 3, retry.returncode
        assert len(list(work.rglob('*.dmp'))) == 1
        if expected:
            assert expected in retry.stderr, retry.stderr
        else:
            assert 'Crash dump:' not in retry.stderr
    print('PASS: disabled and failed dump writes retain the original exit code')
