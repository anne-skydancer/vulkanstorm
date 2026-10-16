"""Compile real numeric-boundary code with warnings treated as errors."""
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NumericBoundaryTest(unittest.TestCase):
    def compile_and_run(self, source):
        windows = platform.system() == 'Windows'
        compiler = shutil.which('cl') if windows else shutil.which('g++')
        self.assertIsNotNone(compiler, 'Run in the platform compiler environment')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cpp, exe = root / 'probe.cpp', root / 'probe.exe'
            cpp.write_text(source, encoding='utf-8')
            args = ([compiler, '/nologo', '/std:c++17', '/W3', '/WX', str(cpp), '/Fe' + str(exe)]
                    if windows else [compiler, '-std=c++17', '-Wall', '-Wextra', '-Wconversion',
                                     '-Werror', str(cpp), '-o', str(exe)])
            result = subprocess.run(args, cwd=root, text=True, capture_output=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], cwd=root, text=True, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_reflection_probe_limits(self):
        definitions = (ROOT / 'indra/llcommon/lldefs.h').read_text()
        begin = definitions.index('// recursion tail')
        templates = definitions[begin:definitions.index('template <typename A, typename MIN', begin)]
        manager = (ROOT / 'indra/newview/llreflectionmapmanager.cpp').read_text()
        floor = re.search(r'mDynamicProbeCount = (llmax\([^;]+);', manager).group(1)
        ceiling = re.search(r'mDynamicProbeCount = (llmin\([^;]+);', manager).group(1)
        limit = re.search(r'^#define LL_MAX_REFLECTION_PROBE_COUNT .+$',
                          (ROOT / 'indra/newview/llreflectionmapmanager.h').read_text(), re.M).group(0)
        self.compile_and_run('''
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <cstddef>
using U32=uint32_t;
''' + templates + limit + '''
U32 lower(U32 count) {
 struct Probes { size_t count; size_t size() const { return count; } } mProbes{count};
 return ''' + floor + ''';
}
U32 upper(U32 mDynamicProbeCount) { return ''' + ceiling + '''; }
int main() {
 for (U32 count : std::array<U32,8>{0U,1U,127U,128U,129U,255U,256U,std::numeric_limits<U32>::max()}) {
  assert(lower(count)==(count<128U?128U:count));
  assert(upper(count)==(count>256U?256U:count));
 }
}
''')

if __name__ == '__main__':
    unittest.main()
