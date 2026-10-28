"""Production-extracted recovery ownership and original-file identity regressions."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'indra/newview/llfloaterscriptrecover.cpp'
def function(source,name):
 start=source.index(name);brace=source.index('{',start);level=1;end=brace+1
 while level:
  level+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
class ScriptRecoveryTest(unittest.TestCase):
 def test_source_routes_and_file_identity(self):
  s=SOURCE.read_text();create=function(s,'void LLScriptRecoverQueue::onCreateScript(')
  self.assertIn('m_FileQueue.find(path)',create)
  self.assertNotIn('pItem->getName()',create)
  self.assertLess(create.index('!current()'),create.index('gInventory.getItem'))
  self.assertIn('LLFile::fopen(path, "rb")',create)
  self.assertIn('read != buffer.size()',create)
  self.assertIn('LLScriptAssetUpload(idItem, target, buffer, complete, failure)',create)
  cancel=function(s,'void LLFloaterScriptRecover::onBtnCancel()')
  self.assertIn('getValue()["path"].asString()',cancel)
  self.assertLess(cancel.index('!mAccountCurrent()'),cancel.index('LLFile::remove'))
  self.assertIn('std::weak_ptr<LLScriptRecoverQueue>',s)
  self.assertNotIn('delete this',s)
 def test_actual_queue_transactions_failure_and_retirement(self):
  source=SOURCE.read_text(); header=SOURCE.with_suffix('.h').read_text()
  start=header.index('class LLScriptRecoverQueue :'); end=header.index('\n};',start)+3
  declaration=header[start:end]
  begin=source.index('class LLCreateRecoverScriptCallback :');end=source.index('\n};',begin)+3
  callback=source[begin:end]
  methods=source[source.index('LLScriptRecoverQueue::LLScriptRecoverQueue(const LLSD&'):]
  fixture=(Path(__file__).with_name('script_recovery_fixture.hpp').read_text()+ '\n'+
           function(source,'std::function<bool()> recoveryAccount()')+'\n'+declaration+
           '\nstd::set<std::shared_ptr<LLScriptRecoverQueue>> sRecoverQueues;\n'+callback+'\n'+methods)
  fixture+=r"""
LLSD one(const char* path,const char* name="Repeated"){LLSD files,file;file["path"]=path;file["name"]=name;files.array.push_back(file);return files;}
LLSD complete(){LLSD r;r["http_result"].number=200;r["state"]="complete";return r;}
void fire(size_t index,int id){gInventory.items[id].name="New_Script";creates.at(index)->fire({id});}
void saved(size_t index){auto u=uploads.at(index);u->done(u->item,{500},{},complete());}
void clean(){LLScriptRecoverQueue::resetAccount();creates.clear();uploads.clear();files.clear();removed.clear();nativeCurrent=true;gAgent.cap="upload";afterUpdateServer={};afterUpdateItem={};afterNotify={};shortRead=false;}
int main(){
 clean();files["a"]="alpha";files["b"]="beta";auto list=one("a");list.array.push_back(one("b").array[0]);
 LLScriptRecoverQueue::start(list);assert(creates.size()==1);fire(0,10);assert(uploads.size()==1&&uploads[0]->buffer=="alpha");
 saved(0);assert(!files.count("a")&&files.count("b")&&creates.size()==2);fire(1,11);assert(uploads[1]->buffer=="beta");saved(1);assert(files.empty()&&sRecoverQueues.empty());
 clean();files["late"]="x";LLScriptRecoverQueue::start(one("late"));auto retained=*sRecoverQueues.begin();LLScriptRecoverQueue::resetAccount();fire(0,12);assert(uploads.empty()&&files.count("late"));
 clean();files["upload-late"]="x";LLScriptRecoverQueue::start(one("upload-late"));fire(0,13);retained=*sRecoverQueues.begin();LLScriptRecoverQueue::resetAccount();saved(0);assert(files.count("upload-late"));
 for(int stage=0;stage<3;++stage){clean();files["reentrant"]="x";LLScriptRecoverQueue::start(one("reentrant","New Script"));fire(0,20+stage);
 auto reset=[](){LLScriptRecoverQueue::resetAccount();};if(stage==0)afterUpdateServer=reset;else if(stage==1)afterUpdateItem=reset;else afterNotify=reset;
 saved(0);assert(files.count("reentrant")&&removed.empty()&&sRecoverQueues.empty());}
 clean();files["failed"]="x";LLScriptRecoverQueue::start(one("failed"));fire(0,30);uploads[0]->failure({30},{},{},"network");assert(files.count("failed")&&sRecoverQueues.empty());
 clean();files["missing-cap"]="x";gAgent.cap="";LLScriptRecoverQueue::start(one("missing-cap"));fire(0,31);assert(uploads.empty()&&files.count("missing-cap")&&sRecoverQueues.empty());
 clean();files["short"]="abc";shortRead=true;LLScriptRecoverQueue::start(one("short"));fire(0,32);assert(uploads.empty()&&files.count("short")&&sRecoverQueues.empty());
 clean();files["wrong-account"]="abc";LLScriptRecoverQueue::start(one("wrong-account"));++gAgentSessionID.id;fire(0,33);assert(uploads.empty()&&files.count("wrong-account")&&sRecoverQueues.empty());
}
"""
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'transactions.cpp';exe=Path(directory)/'transactions.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   self.assertEqual(subprocess.run([str(exe)]).returncode,0)
 def test_real_account_guard_and_weak_inventory_callback(self):
  s=SOURCE.read_text();guard=function(s,'std::function<bool()> recoveryAccount()')
  begin=s.index('class LLCreateRecoverScriptCallback :');end=s.index('\n};',begin)+3
  callback=s[begin:end]
  fixture=r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
struct LLUUID{int value=0;bool notNull()const{return value!=0;}bool operator==(const LLUUID& b)const{return value==b.value;}};
LLUUID gAgentID{1},gAgentSessionID{2};bool nativeCurrent=true;
std::function<bool()> vs_native_im_guard(){return []{return nativeCurrent;};}
''' + guard + r'''
struct LLInventoryCallback{virtual ~LLInventoryCallback()=default;virtual void fire(const LLUUID&)=0;};
struct LLScriptRecoverQueue{int calls=0;std::string path;std::function<bool()> current=recoveryAccount();
 void onCreateScript(const LLUUID&,const std::string& original){if(current()){++calls;path=original;}}};
''' + callback + r'''
int main(){auto current=recoveryAccount();assert(current());gAgentSessionID={3};assert(!current());gAgentSessionID={2};
 nativeCurrent=false;assert(!current());nativeCurrent=true;gAgentID={0};assert(!current());gAgentID={1};
 auto q=std::make_shared<LLScriptRecoverQueue>();LLCreateRecoverScriptCallback first(q,"same-name-A"),second(q,"same-name-B");
 first.fire({9});assert(q->calls==1&&q->path=="same-name-A");second.fire({10});assert(q->calls==2&&q->path=="same-name-B");
 nativeCurrent=false;first.fire({11});assert(q->calls==2);q.reset();second.fire({12});}
'''
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'recovery.cpp';exe=Path(directory)/'recovery.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   self.assertEqual(subprocess.run([str(exe)]).returncode,0)
if __name__=='__main__':unittest.main()
