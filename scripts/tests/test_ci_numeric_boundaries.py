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
        # llmax/llmin and, since Firestorm 7.3, the llless() comparison they
        # call (declared just above them).
        begin = definitions.find('// llless(d0, d1)')
        if begin < 0:
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
#include <type_traits>
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


    def test_production_frame_and_voice_sample_arguments(self):
        header = (ROOT / 'indra/llcommon/lltrace.h').read_text()
        begin = header.index('template<typename T, typename VALUE_T>\nvoid sample(')
        sample = header[begin:header.index('template <typename T = F64>', begin)]
        begin = header.index('template<typename T>\nT storage_value(')
        storage = header[begin:header.index('template<typename UNIT_TYPE', begin)]
        stats = (ROOT / 'indra/newview/llviewerstats.cpp').read_text().splitlines()
        def argument(lines, name, last=False):
            matches = [line.strip() for line in lines if 'sample(LLStatViewer::' + name + ',' in line]
            line = matches[-1 if last else 0]
            return line.split(',', 1)[1].strip().removesuffix(');')
        inputs = ['sample(events, ' + argument(stats, 'FRAMETIME_JITTER_EVENTS_PER_MINUTE') + ');',
                  'assert(events.data.last==static_cast<F64>(static_cast<U32>(frame_time_events_per_minute)));']
        for name, variable in [('SCENERY', 'scene_render'), ('AVATAR', 'avatar'), ('HUDS', 'huds'),
                               ('UI', 'ui'), ('SWAP', 'swap'), ('IDLE', 'idle')]:
            inputs += ['const F64 pct_' + variable + '_time=37.6;',
                       'sample(percent, ' + argument(stats, name + '_FRAME_PCT') + ');',
                       'assert(percent.data.last==38.0);']
        voice = (ROOT / 'indra/newview/llvoicewebrtc.cpp').read_text().splitlines()
        if any('sample(LLStatViewer::WEBRTC_' in line for line in voice):
            for name in ['JITTER_BUFFER', 'JITTER_IN', 'JITTER_OUT', 'LATENCY', 'UPLOAD_BANDWIDTH']:
                inputs += ['sample(timing, ' + argument(voice, 'WEBRTC_' + name, last=True) + ');',
                           'assert(timing.data.last==0.0);']
            for name in ['PACKETS_IN_LOST', 'PACKETS_IN_RECEIVED', 'PACKETS_OUT_SENT', 'PACKETS_OUT_LOST']:
                inputs += ['sample(events, ' + argument(voice, 'WEBRTC_' + name, last=True) + ');',
                           'assert(events.data.last==0.0);']
        self.compile_and_run('''
#include <cassert>
#include <cstdint>
#include <cmath>
#include <limits>
using U32=uint32_t; using U64=uint64_t; using F32=float; using F64=double;
#define LL_TRACE_ENABLED 1
struct F32Percent {
 explicit F32Percent(F32 value): data(value) {}
 operator F32() const { return data; }
 F32 data;
};
struct Accumulator { F64 last=0; void sample(F64 value) { last=value; } };
template<typename T> struct SampleStatHandle {
 using storage_t=F64;
 Accumulator data; Accumulator& getCurrentAccumulator() { return data; }
};
''' + storage + sample + '''
int main() {
 SampleStatHandle<U32> events;
 SampleStatHandle<F32Percent> percent;
 SampleStatHandle<F32> timing;
 (void)timing;
 const U64 frame_time_events_per_minute=std::numeric_limits<U64>::max();
''' + '\n'.join(inputs) + '\n}\n')

if __name__ == '__main__':
    unittest.main()
