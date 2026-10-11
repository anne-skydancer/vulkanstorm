"""Compile actual bridge retirement; never fabricate an attached native bridge."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeBridgeRetirementTest(unittest.TestCase):
    def test_actual_retirement_preserves_legacy_availability(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'indra/newview/fslslbridge.cpp').read_text()
        definitions = method(source, 'static bool fs_bridge_scene_owner_available()') + '\n' + method(source, 'std::function<bool()> fs_bridge_account_guard()') + '\n' + method(source, 'void FSLSLBridge::resetNativeAccount()') + '\n' + method(source, 'bool FSLSLBridge::canUseBridge()')
        fixture = r'''
#include <cassert>
#include <string>
#include <vector>
#include <functional>
#define VS_NATIVE_VULKAN 1
struct UUID{int id=1;bool null=false;void setNull(){null=true;id=0;}bool operator==(const UUID& rhs)const{return id==rhs.id;}};
using LLUUID=UUID;LLUUID gAgentID,gAgentSessionID;
std::function<bool()> vs_native_im_guard(){return []{return true;};}
struct Settings{bool enable=true;}gSavedSettings;
template<class T>struct LLCachedControl{Settings& settings;LLCachedControl(Settings& s,const char*):settings(s){}operator bool()const{return settings.enable;}};
struct VSNativeSession{static bool native;static VSNativeSession* active(){static VSNativeSession s;return native?&s:nullptr;}};bool VSNativeSession::native=false;
struct ViewerWindow{bool native=false;bool isNativeVulkan(){return native;}} window,*gViewerWindow=&window;
struct FSLSLBridge{
 enum TimerResult{NO_TIMER,START_CREATION_FINISHED};
 std::string mCurrentURL="https://old-region.invalid/bridge";void* mpBridge=reinterpret_cast<void*>(1);
 UUID mScriptItemID,mBridgeFolderID,mBridgeContainerFolderID,mBridgeUUID,mReattachBridgeUUID;
 std::vector<int> mAllowedDetachables{1,2};bool mBridgeCreating=true,mAllowDetach=true,mFinishCreation=true,mIsFirstCallDone=true;
 TimerResult mTimerResult=START_CREATION_FINISHED;
 bool isBridgeValid(){return mpBridge!=nullptr;}bool canUseBridge();void resetNativeAccount();
};
DEFINITIONS
int main(){
 FSLSLBridge bridge;assert(bridge.canUseBridge());auto current=fs_bridge_account_guard();assert(current());++gAgentSessionID.id;assert(!current());--gAgentSessionID.id;assert(current());
 VSNativeSession::native=true;assert(!bridge.canUseBridge());
 bridge.resetNativeAccount();assert(bridge.mCurrentURL.empty()&&!bridge.mpBridge);
 assert(bridge.mScriptItemID.null&&bridge.mBridgeFolderID.null&&bridge.mBridgeContainerFolderID.null&&bridge.mBridgeUUID.null&&bridge.mReattachBridgeUUID.null);
 assert(bridge.mAllowedDetachables.empty()&&!bridge.mBridgeCreating&&!bridge.mAllowDetach&&!bridge.mFinishCreation&&!bridge.mIsFirstCallDone&&bridge.mTimerResult==FSLSLBridge::NO_TIMER);
 VSNativeSession::native=false;assert(!bridge.canUseBridge());bridge.mpBridge=reinterpret_cast<void*>(2);bridge.mCurrentURL="https://new-region.invalid/bridge";
 assert(bridge.canUseBridge());window.native=true;assert(!bridge.canUseBridge()&&!current());window.native=false;gViewerWindow=nullptr;assert(!bridge.canUseBridge()&&!current());gViewerWindow=&window;gSavedSettings.enable=false;assert(!bridge.canUseBridge());
}
'''.replace('DEFINITIONS', definitions)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'bridge_retirement.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_late_callbacks_and_scene_entrypoints_are_guarded(self):
        source = (ROOT / 'indra/newview/fslslbridge.cpp').read_text()
        for signature in ('void FSLSLBridge::initBridge()', 'void FSLSLBridge::recreateBridge()',
                          'void FSLSLBridge::startCreation()', 'void FSLSLBridge::processAttach(',
                          'void FSLSLBridge::processDetach(', 'void FSLSLBridge::onIdle(',
                          'void FSLSLBridgeRezCallback::fire(', 'void FSLSLBridgeScriptCallback::fire('):
            body = method(source, signature).split('#endif', 1)[0]
            self.assertIn('fs_bridge_scene_owner_available()', body)
            self.assertIn('return;', body)
        session = (ROOT / 'indra/newview/vsnativesession.cpp').read_text()
        for signature in ('void VSNativeSession::reset()', 'bool VSNativeSession::changeRegion('):
            body = method(session, signature)
            self.assertIn('FSLSLBridge::instanceExists()', body)
            self.assertIn('resetNativeAccount()', body)
        header = (ROOT / 'indra/newview/fslslbridge.h').read_text()
        self.assertEqual(header.count('mAccountCurrent = fs_bridge_account_guard()'), 7)
        self.assertEqual(header.count('if (mAccountCurrent()) FSLSLBridge::instance()'), 2)
        for signature in ('bool FSLSLBridgeCleanupTimer::tick()', 'bool FSLSLBridgeReAttachTimer::tick()', 'bool FSLSLBridgeStartCreationTimer::tick()'):
            self.assertIn('if (!mAccountCurrent()', method(source, signature))


if __name__ == '__main__':
    unittest.main()
