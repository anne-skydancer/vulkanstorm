"""Exercise the production CMake version module for stable and canary builds."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'indra/cmake/BuildVersion.cmake'

class VersionSchemaTest(unittest.TestCase):
    def configure(self, base):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            source = Path(directory)
            (source / 'newview').mkdir()
            (source / 'newview/VIEWER_VERSION_FS.txt').write_text(base + '\n')
            script = source / 'probe.cmake'
            script.write_text(f'''set(CMAKE_CURRENT_SOURCE_DIR "{source.as_posix()}")
include("{MODULE.as_posix()}")
file(WRITE "{source.as_posix()}/result.txt" "${{VIEWER_SHORT_VERSION}}.${{VIEWER_VERSION_REVISION}};${{VIEWER_VERSION_MAJOR}},${{VIEWER_VERSION_MINOR}},${{VIEWER_VERSION_PATCH}};${{VIEWER_VERSION_SUFFIX}}")
''')
            env = dict(os.environ, revision='99999999', AUTOBUILD_BUILD_ID='20261002123456')
            result = subprocess.run(['cmake', '-P', str(script)], text=True, capture_output=True, env=env)
            output = (source / 'result.txt').read_text() if result.returncode == 0 else ''
            return result, output

    def test_stable_and_canary_keep_numeric_components_and_source_count(self):
        count = subprocess.check_output(['git', 'rev-list', '--count', 'HEAD'], cwd=ROOT, text=True).strip()
        for base, suffix in [('1.0.0',''), ('1.0.0-canary','-canary')]:
            with self.subTest(base=base):
                result, output = self.configure(base)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(output, f'{base}.{count};1,0,0;{suffix}')

    def test_rejects_invalid_or_unsupported_version_labels(self):
        for base in ('1.0', '1.0.0-beta', '1.0.0-canary.123'):
            with self.subTest(base=base):
                result, _ = self.configure(base)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Invalid viewer version', result.stderr)

if __name__ == '__main__':
    unittest.main()
