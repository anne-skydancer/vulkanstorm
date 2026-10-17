"""Use actual avatar/account ownership for shared stand and sit restrictions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_camera_publication import method

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


class NativeAvatarStanding(unittest.TestCase):
    def test_reference_permission_and_real_native_avatar_owner(self):
        source = (ROOT / 'indra/newview/rlvactions.cpp').read_text(encoding='utf-8')
        functions = '\n'.join(method(source, signature) for signature in (
            'bool agentAvatarStanding()', 'bool RlvActions::canStand()',
            'bool RlvActions::canStand(const LLUUID&'))
        fixture = (HERE / 'avatar_standing_fixture.cpp').read_text(encoding='utf-8')
        fixture = fixture.replace('REFERENCE', (HERE / 'avatar_standing_reference.cpp').read_text(encoding='utf-8'))
        fixture = fixture.replace('CURRENT', functions)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'standing.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
