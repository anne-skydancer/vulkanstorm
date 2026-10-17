"""Image CPU/GPU accounting follows the last retained generation owner."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeImageUsageTests(unittest.TestCase):
    def test_charges_survive_cache_eviction_and_balance_after_retirement(self):
        source = r'''
#include "vsimageusage.h"
#include <cassert>
struct Generation {VSImageUsage::Charge usage;};
int main(){
 auto state=std::make_shared<VSImageUsage::State>();
 auto image=std::make_shared<Generation>();image->usage.attach(state,112,256);
 assert(state->gpu==112 && state->snapshot==256);
 auto submitted=image;image.reset();assert(state->gpu==112 && state->snapshot==256);
 auto replacement=std::make_shared<Generation>();replacement->usage.attach(state,340,256);
 assert(state->gpu==452 && state->snapshot==512);
 bool refused=false;try{replacement->usage.attach(state,1,1);}catch(const std::logic_error&){refused=true;}
 assert(refused && state->gpu==452 && state->snapshot==512);
 submitted.reset();assert(state->gpu==340 && state->snapshot==256);
 replacement.reset();assert(state->gpu==0 && state->snapshot==0);
 auto late=std::make_shared<Generation>();late->usage.attach(state,16,4);
 std::weak_ptr<VSImageUsage::State> weak=state;state.reset();assert(!weak.expired());
 late.reset();assert(weak.expired());
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cpp = directory / 'image_usage.cpp'
            cpp.write_text(source)
            result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'indra/newview'), str(cpp), '-o', str(directory / 'usage.exe')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(directory / 'usage.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
