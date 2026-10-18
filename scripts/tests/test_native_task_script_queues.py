"""Actual task script transaction, encoded action and lifetime regressions."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_TASK_QUEUES_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_TASK_QUEUES_PROPOSED'])if 'VS_TASK_QUEUES_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativeTaskScriptQueueTests(unittest.TestCase):
 def test_actual_complete_queue_fetch_failure_reset_wire_and_retirement(self):
  cpp=source('indra/newview/llcompilequeue.cpp');queue=body(cpp,'void LLFloaterScriptQueue::objectScriptProcessingQueueCoro(');action=body(cpp,'bool LLFloaterResetQueue::resetObjectScripts(')
  fixture=source('scripts/tests/native_task_script_queue_fixture.cpp');run(self,fixture.replace('QUEUE}',queue+'}').replace('ACTION}',action+'}'))
 def test_actual_queue_guard_stops_closed_or_old_account_without_retargeting(self):
  cpp=source('indra/newview/llcompilequeue.cpp');guard=body(cpp,'std::function<bool()> LLFloaterScriptQueue::queueGuard(')
  run(self,r'''#include <cassert>
#include <memory>
#include <functional>
struct Queue{std::shared_ptr<bool>mQueueLive=std::make_shared<bool>(true);std::function<bool()>mAccountCurrent;std::function<bool()>queueGuard()const ACTUAL};
int main(){int account=1;Queue q;q.mAccountCurrent=[&]{return account==1;};auto current=q.queueGuard();assert(current());*q.mQueueLive=false;assert(!current());q.mQueueLive=std::make_shared<bool>(true);assert(q.queueGuard()());assert(!current());account=2;assert(!q.queueGuard()());}
'''.replace('ACTUAL',guard))
 def test_actual_upload_dispatch_rejects_mismatched_or_expired_retained_task(self):
  actual=body(source('indra/newview/llviewerassetupload.cpp'),'LLUUID LLViewerAssetUpload::EnqueueInventoryUpload(');prefix=actual[1:actual.index('    std::string procName')]
  run(self,r'''#include <cassert>
#include <functional>
#include <memory>
#define VS_NATIVE_VULKAN 1
struct LLUUID{int value;static const LLUUID null;LLUUID(int v=0):value(v){}bool notNull()const{return value!=0;}bool operator!=(LLUUID other)const{return value!=other.value;}operator bool()const{return value!=0;}};const LLUUID LLUUID::null{};
int lookups=0;bool ownerCurrent=true,itemCurrent=true;
std::function<bool()>vs_native_im_guard(){return[]{return true;};}
struct Base{virtual~Base()=default;};struct LLBufferedAssetUploadInfo:Base{LLUUID getTaskId()const{return 7;}LLUUID getItemId()const{return 9;}};
struct VSTaskInventoryObject{int id=7;bool live=true;static VSTaskInventoryObject find(LLUUID){++lookups;return{};}explicit operator bool()const{return live;}const auto*operator->()const{return this;}LLUUID getID()const{return id;}std::function<bool()>objectGuard()const{return[]{return ownerCurrent;};}std::function<bool()>itemGuard(LLUUID)const{return[]{return itemCurrent;};}};
struct Window{bool isNativeVulkan()const{return true;}}window;Window*gViewerWindow=&window;
bool dispatch(std::shared_ptr<Base>uploadInfo,std::shared_ptr<const VSTaskInventoryObject>source){PREFIX return current();}
int main(){auto task=std::make_shared<LLBufferedAssetUploadInfo>();auto source=std::make_shared<VSTaskInventoryObject>();assert(dispatch(task,source)&&lookups==0);source->id=8;assert(!dispatch(task,source)&&lookups==0);source->id=7;ownerCurrent=false;assert(!dispatch(task,source)&&lookups==0);ownerCurrent=true;itemCurrent=false;assert(!dispatch(task,source)&&lookups==0);itemCurrent=true;assert(dispatch(task,{})&&lookups==1);}
'''.replace('PREFIX',prefix))
 def test_original_capability_permission_preprocessor_and_explicit_source_routes_retained(self):
  cpp=source('indra/newview/llcompilequeue.cpp');self.assertNotIn('&userData);',cpp);self.assertIn('std::unique_ptr<HandleScriptUserData> owner',cpp);self.assertIn('const auto obj = itObj->mSource;',cpp);self.assertIn('std::make_shared<VSTaskInventoryObject>(object)',cpp)
  process=body(cpp,'bool LLFloaterCompileQueue::processScript(');self.assertIn('allowModifyBy(gAgent.getID(), gAgent.getGroupID())',process);self.assertIn('allowCopyBy(gAgent.getID(), gAgent.getGroupID())',process);self.assertIn('userData->current=[waitCurrent,itemCurrent]',process);self.assertIn('getInvItemAsset(object->getHost()',process)
  upload=source('indra/newview/llviewerassetupload.cpp');self.assertIn('source ? *source : VSTaskInventoryObject::find(task->getTaskId())',upload)
if __name__=='__main__':unittest.main()
