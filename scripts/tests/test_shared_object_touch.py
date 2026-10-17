"""Execute the shared touch policy against its original OpenGL implementation."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def method(source, signature):
    first = source.index(signature)
    opening = source.index('{', first)
    depth = 0
    for last in range(opening, len(source)):
        depth += (source[last] == '{') - (source[last] == '}')
        if not depth:
            return source[first:last + 1]
    raise AssertionError(signature)


class SharedObjectTouch(unittest.TestCase):
    def test_reference_restrictions_exceptions_ancestry_and_distance(self):
        current = (ROOT / 'indra/newview/rlvactions.cpp').read_text(encoding='utf-8')
        first = current.index('namespace\n{\nconst RlvCachedBehaviourModifier<float>& touchDistance()')
        functions = current[first:current.index('\nbool RlvActions::canStand()', first)]
        fixture = (HERE / 'object_touch_fixture.cpp').read_text(encoding='utf-8')
        fixture = fixture.replace('REFERENCE', (HERE / 'touch_policy_reference.cpp').read_text(encoding='utf-8'))
        fixture = fixture.replace('CURRENT', functions)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'touch.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', '-O2', str(cpp), '-o', str(exe)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
