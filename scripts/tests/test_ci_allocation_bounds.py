"""Exercise production allocation bounds at valid, invalid and extreme sizes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class CIAllocationBoundsTest(unittest.TestCase):
    def test_opengl_loader_probe_without_vulkan_sdk(self):
        source = (ROOT/'indra/newview/llviewerstats.cpp').read_text()
        probe = source[source.index('// "Minimal Vulkan"'):source.index('namespace LLStatViewer')]
        self.compile(r'''
#include <cstdint>
#include <cassert>
#define VS_NATIVE_VULKAN 0
PROBE
int main(){
 const uint32_t api=VK_MAKE_API_VERSION(2,1,3,4095);
 assert(VK_API_VERSION_VARIANT(api)==2&&VK_API_VERSION_MAJOR(api)==1);
 assert(VK_API_VERSION_MINOR(api)==3&&VK_API_VERSION_PATCH(api)==4095);
 VkResult result=VK_SUCCESS;assert(result==0);VkInstance instance=nullptr;
 PFN_vkGetInstanceProcAddr get=nullptr;PFN_vkEnumerateInstanceVersion enumerate=nullptr;
 assert(!instance&&!get&&!enumerate);
}
'''.replace('PROBE', probe))

    def compile(self, body):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'bounds.cpp'
            binary = source.with_suffix('.exe')
            source.write_text(body)
            built = subprocess.run([compiler, '-std=c++17', str(source), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_string_table_rounding_and_rejected_sizes(self):
        constructor = method((ROOT/'indra/llcommon/llstringtable.h').read_text(), 'LLStdStringTable(S32 tablesize')
        self.compile(r'''
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <cassert>
#include <utility>
using S32=int32_t;using U32=uint32_t;
struct LLStdStringTable {
 using string_set_t=int;S32 mTableSize;string_set_t* mStringList;
 CONSTRUCTOR
 ~LLStdStringTable(){delete[] mStringList;}
};
int main(){
 for(auto pair:{std::pair<int,int>{0,256},{1,1},{2,2},{3,4},{5,4},{6,8},{7,8},{300,256},{384,512}}){
  LLStdStringTable table(pair.first);assert(table.mTableSize==pair.second);
 }
 for(int size:{-1,std::numeric_limits<int>::min(),1610612736,std::numeric_limits<int>::max()}){
  bool rejected=false;try{LLStdStringTable table(size);}catch(const std::exception&){rejected=true;}
  assert(rejected);
 }
}
'''.replace('CONSTRUCTOR', constructor))

    def test_terrain_validates_before_signed_arithmetic(self):
        body = method((ROOT/'indra/newview/llsurface.cpp').read_text(), 'void LLSurface::create(')
        guard = body[body.index('{')+1:body.index('// Initialize various constants')]
        self.compile(r'''
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <cassert>
#include <utility>
using S32=int32_t;using U64=uint64_t;
void validate(S32 grids_per_edge,S32 grids_per_patch_edge){GUARD}
int main(){
 for(int edge:{16,32,256,512,1024,4096,32768})for(int patch:{16,32})
  if(patch<=edge)validate(edge,patch);
 for(auto pair:{std::pair<int,int>{0,16},{256,0},{-1,16},{256,-1},{16,32},{257,16},
   {65536,16},{std::numeric_limits<int>::max(),1}}){
  bool rejected=false;try{validate(pair.first,pair.second);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
 }
}
'''.replace('GUARD', guard))

    def test_channel_bounds_preserve_power_of_two_and_count_range(self):
        check = method((ROOT/'indra/llmessage/llcoproceduremanager.cpp').read_text(), 'static size_t checkedQueueSize(')
        self.compile(r'''
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <cassert>
#include <memory>
using S32=int32_t;struct CoprocQueue_t{using value_type=std::shared_ptr<int>;};
CHECK
int main(){
 assert(checkedQueueSize(524288,5)==524288);assert(checkedQueueSize(2,1)==2);
 for(size_t capacity:{size_t(0),size_t(1),size_t(3),size_t(1)<<31,std::numeric_limits<size_t>::max()}){
  bool rejected=false;try{checkedQueueSize(capacity,1);}catch(const std::exception&){rejected=true;}
  assert(rejected);
 }
 for(size_t workers:{size_t(0),size_t(8),size_t(9)}){
  bool rejected=false;try{checkedQueueSize(8,workers);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
 }
}
'''.replace('CHECK', check))


if __name__ == '__main__':
    unittest.main()
