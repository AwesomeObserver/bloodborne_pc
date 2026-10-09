from paths import ROOT
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('rtx_trace', ROOT / 'scripts/rtx_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class RtxTraceTests(unittest.TestCase):
    def test_report_keeps_only_session_evidence_and_native_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / 'logs/rtx-trace-session'
            report.mkdir(parents=True)
            (report / 'session.log').write_text('RTX path tracing active\n', encoding='utf-8')
            (report / 'bb-crash-test.dmp').write_bytes(b'dump fixture')
            for name in ('eboot.bin', 'bbport.ini', 'settings.json'):
                (report / name).write_text('private')
            (root / 'Bloodborne.exe').write_bytes(b'launcher fixture')
            result = trace.collect(report, -1073741819, root, 0)
            with zipfile.ZipFile(result) as archive:
                self.assertEqual(set(archive.namelist()), {'metadata.json', 'session.log', 'bb-crash-test.dmp'})
                metadata = json.loads(archive.read('metadata.json'))
                self.assertEqual(metadata['launch_status_hex'], '0xc0000005')
                self.assertEqual(metadata['gpu_preflight_status'], 0)
                self.assertEqual(len(metadata['binary_sha256']['Bloodborne.exe']), 64)
            with self.assertRaises(FileExistsError):
                trace.collect(report, 0, root, None)
