"""Camera effect owners must release cached settings before native teardown."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method

class NativeCameraRetirement(unittest.TestCase):
 def test_real_cleanup_at_account_and_application_boundaries(self):
  cleanup=method((ROOT/'indra/newview/llagentcamera.cpp').read_text(encoding='utf-8'),'void LLAgentCamera::cleanup(')
  reset=method((ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8'),'void VSNativeSession::reset(')
  start=reset.index('    if (gViewerWindow && gViewerWindow->nativeContext()) gViewerWindow->nativeContext()->world().reset();')
  end=reset.index('    gAgentCamera.cleanup();',start)+len('    gAgentCamera.cleanup();')
  account=reset[start:end]
  native=method((ROOT/'indra/newview/llappviewer.cpp').read_text(encoding='utf-8'),'bool LLAppViewer::cleanupNativeVulkanLogin(')
  end=native.index('    LLSingletonBase::deleteAll();')+len('    LLSingletonBase::deleteAll();')
  start=native.rfind('    gAgentCamera.cleanup();',0,end)
  self.assertGreaterEqual(start,0,'Camera cached-control owners survived singleton destruction')
  application=native[start:end]
  fixture=r'''
#include <cassert>
#include <memory>
#undef NULL
#define NULL nullptr
bool registryAlive=true;int destroyed=0,marked=0;
struct Effect{~Effect(){assert(registryAlive);++destroyed;}void markDead(){assert(registryAlive);++marked;}};
struct LLUUID{static const LLUUID null;};const LLUUID LLUUID::null{};
struct LLAgentCamera{
 std::shared_ptr<Effect>mLookAt,mPointAt;bool focus=false,sit=false;
 void setSitCamera(LLUUID){sit=false;}void setFocusObject(void*){focus=false;}
 void cleanup();~LLAgentCamera(){cleanup();}
 void beginAccount(){assert(registryAlive&&!mLookAt&&!mPointAt);mLookAt=std::make_shared<Effect>();mPointAt=std::make_shared<Effect>();focus=sit=true;}
};
CLEANUP
struct World{int resets=0;void reset(){++resets;}};
struct Context{World value;World&world(){return value;}};
struct Window{Context value;Context*nativeContext(){return &value;}}window,*gViewerWindow=&window;
struct LLSingletonBase{static void deleteAll(){assert(registryAlive);registryAlive=false;}};
void accountCleanup(LLAgentCamera&gAgentCamera){ACCOUNT}
void applicationCleanup(LLAgentCamera&gAgentCamera){APPLICATION}
int main(){
 {LLAgentCamera camera;
  camera.beginAccount();accountCleanup(camera);assert(destroyed==2&&marked==2&&!camera.focus&&!camera.sit&&window.value.value.resets==1);
  camera.beginAccount();gViewerWindow=nullptr;accountCleanup(camera);assert(destroyed==4&&marked==4);
  // Partial initialization and ordinary reentry remain idempotent.
  accountCleanup(camera);assert(destroyed==4&&marked==4);
  camera.beginAccount();applicationCleanup(camera);assert(destroyed==6&&marked==6&&!registryAlive);
  // Actual global destructor repeats cleanup after the cached registry is gone.
 }
 assert(destroyed==6&&marked==6);
}
'''.replace('CLEANUP',cleanup).replace('ACCOUNT',account).replace('APPLICATION',application)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp)/'retirement.cpp';exe=p.with_suffix('.exe');p.write_text(fixture,encoding='utf-8')
   built=subprocess.run([compiler,'-std=c++17',str(p),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

if __name__=='__main__':unittest.main()
