"""Exercise actual peer object adapters and delayed-action ownership guards."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def clean(path):
    return '\n'.join(line for line in path.read_text(encoding='utf-8').splitlines()
                     if not line.startswith(('#include', '#pragma')))


class ObjectContext(unittest.TestCase):
    def test_real_peer_resolution_retirement_reparenting_and_owner_policy(self):
        source = (HERE / 'object_context_fixture.cpp').read_text(encoding='utf-8')
        source = source.replace('CONTEXT_HEADER', clean(ROOT / 'indra/newview/vsobjectcontext.h'))
        source = source.replace('CONTEXT_SOURCE', clean(ROOT / 'indra/newview/vsobjectcontext.cpp'))
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        for options in ([], ['-DTOGGLE_HACKED_GODLIKE_VIEWER'], ['-DHACKED_GODLIKE_VIEWER']):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temporary:
                cpp = Path(temporary) / 'contexts.cpp'
                exe = cpp.with_suffix('.exe')
                cpp.write_text(source, encoding='utf-8')
                result = subprocess.run([compiler, '-std=c++17', *options, str(cpp), '-o', str(exe)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(exe)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
