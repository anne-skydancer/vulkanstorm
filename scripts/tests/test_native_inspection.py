"""Production Inspect policy, unchanged XUI rows and actual image residency checks."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_INSPECT_TEST_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_INSPECT_PROPOSED']) if 'VS_INSPECT_PROPOSED' in os.environ else None

def source(path):
 q=OVERLAY/path if OVERLAY else ROOT/path
 return(q if q.exists()else ROOT/path).read_text()
class NativeInspectionTests(unittest.TestCase):
 def test_real_profile_and_rlva_callbacks_preserve_reference_decisions(self):
  cpp=source('indra/newview/llfloaterinspect.cpp');reference=source('scripts/tests/inspect_policy_reference.cpp')
  for name in ('onClickCreatorProfile','onClickOwnerProfile','onSelectObject'):
   actual=body(cpp,'void LLFloaterInspect::'+name+'()')
   expected=body(reference,'void LLFloaterInspect::'+name+'()').replace('const LLSelectNode* node = getSelectedNode()','const auto* node = getSelectedNode()')
   self.assertEqual(actual,expected)
  original=body(reference,'void LLFloaterInspect::refresh()');actual=body(cpp,'void LLFloaterInspect::refresh()')
  # The genuine controller retains all sixteen existing selected-skin columns,
  # including sorting values and anonymized owner/creator strings.
  a=original.index('        row["id"]');b=original.index('        primcount =',a)
  self.assertIn(original[a:b],actual)
 def test_actual_native_texture_cache_counts_unique_current_sources_and_gpu_bytes(self):
  actual=body(source('indra/newview/llfloaterinspect.cpp'),'void LLFloaterInspect::calculateNativeTextureMemory(')
  run(self,r"""
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <algorithm>
#include <vector>
#include <map>
#include <utility>
using U32=unsigned;using U64=unsigned long long;struct LLUUID{int value=0;bool isNull()const{return!value;}bool operator==(LLUUID b)const{return value==b.value;}};using uuid_vec_t=std::vector<LLUUID>;
struct Texture{int width=0,height=0,components=0;U64 gpuBytes=0;bool ready=false;};std::map<int,Texture>images;
Texture*vs_ui_image_snapshot(LLUUID id){auto&i=images[id.value];return i.ready?&i:nullptr;}
auto vs_ui_image_dimensions(LLUUID id){auto&i=images[id.value];return std::pair{i.width,i.height};}
int vs_ui_image_components(LLUUID id){return images[id.value].components;}
struct VSUIRenderer{static Texture imageDescription(Texture*texture){return *texture;}};
struct Inspect{uuid_vec_t mTextureList;U32 mTextureMemory=0,mTextureVRAMMemory=0;void calculateNativeTextureMemory(const LLUUID&id,uuid_vec_t&textures,U32&bytes,U32&gpuBytes)ACTUAL};
int main(){Inspect view;uuid_vec_t a,b;U32 cpuA=0,gpuA=0,cpuB=0,gpuB=0;images[1]={64,32,3,2048,true};images[2]={16,16,4,32,false};view.calculateNativeTextureMemory({1},a,cpuA,gpuA);view.calculateNativeTextureMemory({1},a,cpuA,gpuA);assert(cpuA==6144&&gpuA==2048&&view.mTextureMemory==6144&&view.mTextureVRAMMemory==2048);view.calculateNativeTextureMemory({1},b,cpuB,gpuB);assert(cpuB==6144&&gpuB==2048&&view.mTextureMemory==6144);view.calculateNativeTextureMemory({2},a,cpuA,gpuA);assert(cpuA==6144&&gpuA==2048);images[2].ready=true;view.calculateNativeTextureMemory({2},a,cpuA,gpuA);assert(cpuA==7168&&gpuA==2080&&view.mTextureMemory==7168&&view.mTextureVRAMMemory==2080);view.calculateNativeTextureMemory({},a,cpuA,gpuA);assert(view.mTextureList.size()==2&&a.size()==2&&b.size()==1);}
""".replace('ACTUAL',actual))
 def test_actual_exact_graph_and_retirement_retain_production_controller(self):
  ui=source('indra/newview/vsstartupui.cpp');reset=source('indra/newview/vsnativeim.cpp');cpp=source('indra/newview/llfloaterinspect.cpp')
  self.assertIn('t == typeid(LLFloaterInspect)',ui);self.assertIn('name == "inspect"',ui);self.assertIn('LLFloaterReg::destroyInstance("inspect")',reset)
  self.assertIn('mNativeSelectionConnection = owner->objectSelection()->changed',cpp)
  self.assertIn('vs_appearance_ui_callback(this, boost::bind(&LLFloaterInspect::onEnableColumnVisibilityChecked',cpp)
if __name__=='__main__':unittest.main()
