"""Exercise the production CMake version module for stable and canary builds."""
import os
import plistlib
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'indra/cmake/BuildVersion.cmake'

class VersionSchemaTest(unittest.TestCase):
    def configure(self, base, repository=None):
        with tempfile.TemporaryDirectory(dir=repository or ROOT) as directory:
            source = Path(directory)
            if repository is None:
                subprocess.run(['git', 'init', str(source)], check=True, capture_output=True)
                for message in ('first', 'second'):
                    subprocess.run(['git', '-C', str(source), '-c', 'user.name=VersionTest',
                        '-c', 'user.email=version-test@example.invalid', 'commit', '--allow-empty',
                        '-m', message], check=True, capture_output=True)
            (source / 'newview').mkdir()
            (source / 'newview/VIEWER_VERSION_FS.txt').write_text(base + '\n')
            script = source / 'probe.cmake'
            script.write_text(f'''set(CMAKE_CURRENT_SOURCE_DIR "{source.as_posix()}")
include("{MODULE.as_posix()}")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${{VIEWER_VERSION_MAJOR}}.${{VIEWER_VERSION_MINOR}}.${{VIEWER_VERSION_PATCH}}")
set(MACOSX_BUNDLE_LONG_VERSION_STRING "${{VIEWER_SHORT_VERSION}}.${{VIEWER_VERSION_REVISION}}")
foreach(template Info-Firestorm.plist Info-SecondLife.plist)
  configure_file("{ROOT.as_posix()}/indra/newview/${{template}}" "{source.as_posix()}/${{template}}")
endforeach()
file(WRITE "{source.as_posix()}/result.txt" "${{VIEWER_SHORT_VERSION}}.${{VIEWER_VERSION_REVISION}};${{VIEWER_VERSION_MAJOR}},${{VIEWER_VERSION_MINOR}},${{VIEWER_VERSION_PATCH}};${{VIEWER_VERSION_SUFFIX}}")
''')
            env = dict(os.environ, revision='99999999', AUTOBUILD_BUILD_ID='20261002123456')
            result = subprocess.run(['cmake', '-P', str(script)], text=True, capture_output=True, env=env)
            output = (source / 'result.txt').read_text() if result.returncode == 0 else ''
            if result.returncode == 0:
                for template in ('Info-Firestorm.plist', 'Info-SecondLife.plist'):
                    info = plistlib.loads((source / template).read_bytes())
                    self.assertEqual(info['CFBundleVersion'], '1.0.0')
                    self.assertEqual(info['CFBundleShortVersionString'], '1.0.0')
                    self.assertEqual(info['CFBundleLongVersionString'], output.split(';')[0])
            return result, output

    def test_stable_and_canary_keep_numeric_components_and_source_count(self):
        count = '2'  # The complete fixture repository has exactly two commits.
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

    def test_rejects_shallow_history(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            fixture = Path(directory)
            repository = fixture / 'source'
            repository.mkdir()
            subprocess.run(['git', 'init', str(repository)], check=True, capture_output=True)
            for message in ('first', 'second'):
                subprocess.run(['git', '-C', str(repository), '-c', 'user.name=VersionTest',
                    '-c', 'user.email=version-test@example.invalid', 'commit', '--allow-empty',
                    '-m', message], check=True, capture_output=True)
            shallow = fixture / 'shallow'
            subprocess.run(['git', 'clone', '--depth', '1', repository.as_uri(), str(shallow)],
                           check=True, capture_output=True)
            result, _ = self.configure('1.0.0', shallow)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Full Git history is required', result.stderr)

if __name__ == '__main__':
    unittest.main()
