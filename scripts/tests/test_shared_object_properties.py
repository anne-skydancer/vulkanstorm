"""Compile the actual shared wire decoder, checking metadata and permission ordering."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

class SharedObjectPropertiesTest(unittest.TestCase):
    def test_full_and_family_wire_publications(self):
        header = (ROOT / 'indra/newview/vsobjectproperties.h').read_text()
        source = (ROOT / 'indra/newview/vsobjectproperties.cpp').read_text()
        header = '\n'.join(line for line in header.splitlines() if not line.startswith(('#include', '#pragma')))
        source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
        prehash = '\n'.join('constexpr const char* ' + name + '= "' + name.removeprefix('_PREHASH_') + '";'
                            for name in sorted(set(re.findall(r'_PREHASH_\w+', source))))
        fixture = (HERE / 'object_properties_fixture.cpp').read_text().replace('PREHASH', prehash).replace('HEADER', header).replace('DEFINITIONS', source)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'properties.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++20', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

if __name__ == '__main__':
    unittest.main()
