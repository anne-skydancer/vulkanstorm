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
            args = ([compiler, '/nologo', '/std:c++20', '/W3', '/WX', str(cpp), '/Fe' + str(exe)]
                    if windows else [compiler, '-std=c++20', '-Wall', '-Wextra', '-Wconversion',
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

    def test_all_statistic_handles_preserve_typed_values(self):
        header = (ROOT / 'indra/llcommon/lltrace.h').read_text()
        begin = header.index('template <typename T = F64>\nclass EventStatHandle')
        handles = header[begin:header.index('// measures effective memory footprint', begin)]
        begin = header.index('template<typename T>\nT storage_value(')
        storage = header[begin:header.index('template<typename UNIT_TYPE', begin)]
        self.compile_and_run('''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <initializer_list>
using S32=int32_t; using U32=uint32_t; using S64=int64_t; using U64=uint64_t;
using F32=float; using F64=double;
#define LL_TRACE_ENABLED 1
struct Accumulator {
 F64 last=0, sum=0; unsigned calls=0;
 void record(F64 v) { last=v; ++calls; }
 void sample(F64 v) { last=v; ++calls; }
 void add(F64 v) { sum+=v; ++calls; }
};
using EventAccumulator=Accumulator;
using SampleAccumulator=Accumulator;
using CountAccumulator=Accumulator;
template<typename A> struct StatType {
 StatType(const char*, const char*) {}
 A data; A& getCurrentAccumulator() { return data; }
};
template<typename T> struct LLGetUnitLabel { static const char* getUnitLabel() { return ""; } };
''' + storage + handles + '''
template<typename T> void check(T value) {
 EventStatHandle<T> event("event");
 SampleStatHandle<T> sample_handle("sample");
 CountStatHandle<T> count("count");
 assert(count.getTotalSamples()==T());
 assert(count.getTotalSampleCount()==0);
 record(event,value); sample(sample_handle,value); add(count,value);
 assert(event.data.last==static_cast<F64>(value));
 assert(sample_handle.data.last==static_cast<F64>(value));
 assert(count.data.sum==static_cast<F64>(value));
 assert(count.getTotalSamples()==value);
 assert(count.getTotalSampleCount()==1);
}
int main() {
 check<S32>(-17); check<U32>(42); check<S64>(-17); check<U64>(42);
 check<U64>(U64(1)<<53); check<U64>(std::numeric_limits<U64>::max());
 check<F32>(0.125f); check<F64>(0.125);
 CountStatHandle<S32> count("signed_count");
 add(count,S32(7)); add(count,S32(-2));
 assert(count.getTotalSamples()==5 && count.getTotalSampleCount()==2);
 CountStatHandle<F64> fractional("fractional_count");
 add(fractional,0.125); add(fractional,0.25);
 assert(fractional.getTotalSamples()==0.375 && fractional.getTotalSampleCount()==2);
}
''')

if __name__ == '__main__':
    unittest.main()
