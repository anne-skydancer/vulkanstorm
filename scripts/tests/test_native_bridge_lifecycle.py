"""Compile actual bridge guards and attachment event delivery without a device."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(source, signature):
    start = source.index(signature)
    start += re.search(r"\n[ \t]*\{", source[start:]).end() - 1
    depth = 0
    for index in range(start, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if not depth:
            return source[start:index + 1]
    raise AssertionError(signature)


class NativeBridgeLifecycleTest(unittest.TestCase):
    def compile_run(self, fixture):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / "bridge.cpp"
            executable = Path(temporary) / "bridge.exe"
            cpp.write_text(fixture)
            result = subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_folder_creation_retires_before_mutating_bridge_or_creating_children(self):
        source = (ROOT / "indra/newview/fslslbridge.cpp").read_text()
        fixture = (ROOT / "scripts/tests/native_bridge_folder_fixture.cpp").read_text()
        self.compile_run(fixture.replace("METHOD", body(source, "void FSLSLBridge::setupFSCategory(")))

    def test_actual_bridge_account_region_and_operation_guard(self):
        source = (ROOT / "indra/newview/fslslbridge.cpp").read_text()
        fixture = r'''
#include <cassert>
#include <functional>
#include <memory>
using LLUUID=int;using U64=unsigned long long;
#define VS_NATIVE_VULKAN 1
int gAgentID=1,gAgentSessionID=2;bool account=true,scene=true;
auto vs_native_im_guard(){return []{return account;};}
bool fs_bridge_scene_owner_available(){return scene;}
struct FSLSLBridge {
 bool exists=true;U64 epoch=7;
 static FSLSLBridge& instance(){static FSLSLBridge bridge;return bridge;}
 static bool instanceExists(){return instance().exists;}
 U64 lifecycle(){return epoch;}
};
struct VSNativeSession {
 U64 region=5;U64 regionEpoch(){return region;}
 static std::shared_ptr<VSNativeSession> active(){static auto session=std::make_shared<VSNativeSession>();return session;}
};
std::function<bool()> fs_bridge_account_guard() GUARD
int main(){auto guard=fs_bridge_account_guard();assert(guard());
 account=false;assert(!guard());account=true;assert(guard());
 scene=false;assert(!guard());scene=true;
 gAgentID=3;assert(!guard());gAgentID=1;
 gAgentSessionID=4;assert(!guard());gAgentSessionID=2;
 VSNativeSession::active()->region++;assert(!guard());guard=fs_bridge_account_guard();assert(guard());
 FSLSLBridge::instance().epoch++;assert(!guard());guard=fs_bridge_account_guard();assert(guard());
 FSLSLBridge::instance().exists=false;assert(!guard());
}
'''.replace("GUARD", body(source, "std::function<bool()> fs_bridge_account_guard()"))
        self.compile_run(fixture)

    def test_actual_attachment_events_snapshot_before_reentrant_callbacks(self):
        source = (ROOT / "indra/newview/vsbridgeadapter.cpp").read_text()
        fixture = r'''
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <utility>
using U8=unsigned char;using U64=unsigned long long;
struct LLUUID {int value=0;bool notNull()const{return value!=0;}bool operator<(LLUUID b)const{return value<b.value;}bool operator==(LLUUID b)const{return value==b.value;}};
struct VSWorldScene {
 struct ObjectHandle {int host=1;LLUUID id{10};int local=1;U64 region=1,generation=1;LLUUID account{1};U64 accountGeneration=1;};
 struct Attachment {ObjectHandle object;LLUUID item{20};U8 point=31;};
 std::vector<Attachment> rows;auto attachments(){return rows;}
};
struct Context {VSWorldScene scene;auto& world(){return scene;}};
struct Window {Context context;auto* nativeContext(){return &context;}} window;
auto* gViewerWindow=&window;
struct VSNativeSession {bool ready=true;std::shared_ptr<bool> valid=std::make_shared<bool>(true);
 bool regionReady(){return ready;}auto accountGuard(){return [v=valid]{return *v;};}};
bool sameObject(const VSWorldScene::ObjectHandle& a,const VSWorldScene::ObjectHandle& b) SAME
struct VSBridgeAdapter {
 std::weak_ptr<VSNativeSession> mSession;
 std::function<void(const VSWorldScene::Attachment&)> mAttach;
 std::function<void(const LLUUID&,U8)> mDetach;
 std::map<LLUUID,VSWorldScene::Attachment> mAttachments;
 void tick() TICK
};
int main(){auto session=std::make_shared<VSNativeSession>();VSBridgeAdapter adapter;adapter.mSession=session;
 int attached=0,detached=0;adapter.mAttach=[&](const auto& row){assert(row.item.value==20);++attached;};
 adapter.mDetach=[&](const auto&,U8 point){assert(point==31);++detached;};
 window.context.scene.rows.push_back({});adapter.tick();assert(attached==1&&detached==0);
 adapter.tick();assert(attached==1);session->ready=false;window.context.scene.rows.clear();adapter.tick();assert(detached==0);
 session->ready=true;adapter.tick();assert(detached==1&&adapter.mAttachments.empty());
 window.context.scene.rows.push_back({});adapter.tick();assert(attached==2);
 window.context.scene.rows[0].object.generation++;adapter.tick();assert(attached==3&&detached==1);
 window.context.scene.rows.clear();adapter.mDetach=[&](const auto&,U8){adapter.mAttachments.clear();*session->valid=false;++detached;};
 window.context.scene.rows.push_back({});window.context.scene.rows[0].item={30};adapter.tick();
 assert(detached==2&&attached==3); // account retirement suppresses subsequent attach in the captured batch
}
'''
        fixture = fixture.replace("SAME", body(source, "bool sameObject("))
        fixture = fixture.replace("TICK", body(source, "void VSBridgeAdapter::tick()"))
        self.compile_run(fixture)


if __name__ == "__main__":
    unittest.main()
