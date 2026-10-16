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

    def test_integer_and_fractional_samples_use_declared_storage(self):
        header = (ROOT / 'indra/llcommon/lltrace.h').read_text()
        begin = header.index('template<typename T, typename VALUE_T>\nvoid sample(')
        sample = header[begin:header.index('template <typename T = F64>', begin)]
        begin = header.index('template<typename T>\nT storage_value(')
        storage = header[begin:header.index('template<typename UNIT_TYPE', begin)]
        begin = header.index('class SampleStatHandle')
        storage_type = re.search(r'typedef\s+\w+\s+storage_t;', header[begin:]).group(0)
        self.compile_and_run('''
#include <cassert>
#include <cstdint>
#include <limits>
#include <initializer_list>
using U64=uint64_t; using S64=int64_t; using F64=double;
#define LL_TRACE_ENABLED 1
struct Accumulator { F64 last=0; unsigned samples=0; void sample(F64 v) { last=v; ++samples; } };
template<typename T> struct SampleStatHandle {
''' + storage_type + '''
 Accumulator data; Accumulator& getCurrentAccumulator() { return data; }
};
''' + storage + sample + '''
int main() {
 SampleStatHandle<U64> elapsed;
 for (U64 value : {U64(0),U64(14000),U64(1)<<53,std::numeric_limits<U64>::max()}) {
  sample(elapsed,value); assert(elapsed.data.last==static_cast<F64>(value));
 }
 assert(elapsed.data.samples==4);
 SampleStatHandle<S64> signed_value; sample(signed_value,S64(-17));
 assert(signed_value.data.last==-17.0);
 SampleStatHandle<F64> fraction; sample(fraction,0.125);
 assert(fraction.data.last==0.125);
}
''')


if __name__ == '__main__':
    unittest.main()
