"""Native diagnostic capture and report export, without game files."""
from paths import ROOT, executable
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import mouse_trace


@unittest.skipUnless(sys.platform == 'win32' and executable('mouse-trace-test').exists(), 'Windows native diagnostic test')
class MouseTraceTests(unittest.TestCase):
    def run_native(self, directory, *args):
        result = subprocess.run([str(executable('mouse-trace-test')), *args], cwd=directory,
                                capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_disabled_and_write_failure_do_not_create_capture(self):
        for mode in ('disabled', 'blocked'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                self.run_native(temp, mode)
                self.assertEqual(list(Path(temp).rglob('state.bin')), [])

    def test_bounded_capture_unicode_path_and_export(self):
        with tempfile.TemporaryDirectory(prefix='mouse-\u043a\u0430\u043c\u0435\u0440\u0430-') as temp:
            self.run_native(temp)
            directory = next((Path(temp) / 'logs').iterdir())
            data = (directory / 'state.bin').read_bytes()
            columns, rows = mouse_trace.decode(data)
            self.assertEqual(len(rows), 4096)
            self.assertEqual(rows[0][4:7], [-1.0, 2.0, 125.0])
            self.assertEqual(rows[0][7:11], [1, 1, 0, 1])
            self.assertEqual(rows[1][7:11], [0, 0, 0, 0])
            self.assertEqual(rows[0][columns.index('before_140')], .25)
            self.assertEqual(rows[0][columns.index('after_140')], -.5)
            code = (directory / 'code.txt').read_text()
            self.assertIn('call 0x0000000000000050', code)
            self.assertIn('image+0x50', code)
            self.assertIn('00000066:', code)  # RIP-relative data target
            report = mouse_trace.collect(directory)
            with zipfile.ZipFile(report) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(set(archive.namelist()), {'state.bin', 'state.csv', 'code.txt', 'metadata.json'})
                self.assertEqual(archive.read('state.bin'), data)
                self.assertEqual(len(archive.read('state.csv').splitlines()), 4097)
            with self.assertRaises(FileExistsError):
                mouse_trace.collect(directory)
            # Malformed/truncated reports are rejected without reading past a buffer.
            for invalid in (b'', data[:-1], b'unknown!' + data[8:]):
                with self.assertRaises(ValueError):
                    mouse_trace.decode(invalid)
            bad = bytearray(data)
            struct.pack_into('<Q', bad, 4096, 5000)
            with self.assertRaises(ValueError):
                mouse_trace.decode(bad)

    def test_capture_survives_forced_process_exit_without_explicit_flush(self):
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([str(executable('mouse-trace-test')), 'terminate'], cwd=temp,
                                    capture_output=True, timeout=45)
            self.assertEqual(result.returncode, 8, result.stdout + result.stderr)
            data = next(Path(temp).rglob('state.bin')).read_bytes()
            self.assertEqual(len(mouse_trace.decode(data)[1]), 4096)
            bad = bytearray(data)
            struct.pack_into('<I', bad, 8, 999)
            with self.assertRaises(ValueError):
                mouse_trace.decode(bad)


if __name__ == '__main__':
    unittest.main()
