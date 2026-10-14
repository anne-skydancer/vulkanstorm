"""Exercise actual deferred bake completion with content and lifecycle guards."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class NativeBakeCallbackTest(unittest.TestCase):
    def test_cof_change_reuses_current_bake_but_retired_content_is_rejected(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/vsnativeappearance.cpp').read_text()
        production=method(source,'static void uploadCompleted(')
        fixture=r'''
#include "vsresourcebudget.h"
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
using U64=unsigned long long;using S32=int;constexpr int LL_ERR_NOERR=0;
struct LLUUID {int value=0;};
struct LLFrameTimer {static double getTotalSeconds(){return 5;}};
struct LLAppViewer {std::vector<std::function<void()>> queued;
 static LLAppViewer* instance(){static LLAppViewer app;return &app;}
 void postToMainCoro(std::function<void()> work){queued.push_back(std::move(work));}
 void drain(){auto work=std::move(queued);for(auto& callback:work)callback();}};
struct Avatar {U64 owner=1,generation=10;int accepted=0;
 U64 ownerGeneration(){return owner;}bool setLocalBake(unsigned index,U64 content,LLUUID asset){
 if(index!=0||generation!=content||!asset.value)return false;++accepted;return true;}} avatar,*activeAvatar=&avatar;
Avatar* selfAvatar(){return activeAvatar;}
struct Session {U64 region=2;U64 regionEpoch(){return region;}};
struct Impl:std::enable_shared_from_this<Impl>{bool live=true;U64 revision=1;
 std::weak_ptr<Session> session;U64 sentAppearanceRevision=7;std::string error;
 bool current(){return live;}
 struct Upload {U64 generation=10;unsigned attempts=1;double deadline=100;bool pending=true;LLUUID asset;};
 std::map<unsigned,Upload> uploads{{0,{}}};
 struct UploadRequest {std::weak_ptr<Impl> owner;U64 region=2,avatar=1,generation=10;
 unsigned index=0,attempt=1;std::shared_ptr<const VSResourceBudget::Reservation> residency;};
PRODUCTION
};
int main(){auto session=std::make_shared<Session>();auto owner=std::make_shared<Impl>();owner->session=session;
 VSResourceBudget budget{1};auto request=std::make_shared<Impl::UploadRequest>();request->owner=owner;
 request->residency=budget.reserve(1);Impl::uploadCompleted(request,LLUUID{99},0);request.reset();
 assert(budget.used()==1&&avatar.accepted==0);
 owner->revision=2;LLAppViewer::instance()->drain();
 assert(avatar.accepted==1&&owner->uploads[0].asset.value==99&&!owner->uploads[0].pending);
 assert(owner->sentAppearanceRevision==0&&budget.used()==0);
 auto submit=[&](){auto current=std::make_shared<Impl::UploadRequest>();current->owner=owner;
  Impl::uploadCompleted(current,LLUUID{99},0);LLAppViewer::instance()->drain();};
 owner->live=false;submit();owner->live=true;assert(avatar.accepted==1);
 session->region=3;submit();session->region=2;assert(avatar.accepted==1);
 avatar.owner=2;submit();avatar.owner=1;assert(avatar.accepted==1);
 owner->uploads[0].generation=11;submit();owner->uploads[0].generation=10;assert(avatar.accepted==1);
 owner->uploads[0].attempts=2;submit();owner->uploads[0].attempts=1;assert(avatar.accepted==1);
 owner->uploads[0].deadline=1;submit();owner->uploads[0].deadline=100;assert(avatar.accepted==1);
 // The compositor changed before the appearance owner refreshed its upload table.
 avatar.generation=11;owner->error.clear();owner->uploads[0].pending=true;submit();
 assert(avatar.accepted==1&&owner->uploads[0].asset.value==99&&owner->uploads[0].pending&&owner->error.empty());
 avatar.generation=10;owner->uploads[0].pending=true;
 auto failed=std::make_shared<Impl::UploadRequest>();failed->owner=owner;
 Impl::uploadCompleted(failed,LLUUID{},-1);LLAppViewer::instance()->drain();
 assert(!owner->uploads[0].pending&&owner->uploads[0].deadline==10&&!owner->error.empty());
}
'''.replace('PRODUCTION',production)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'native_bake_callback.cpp';exe=Path(directory)/'native_bake_callback.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
