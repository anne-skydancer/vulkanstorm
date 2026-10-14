"""Exercise actual ordered removal admission/drain without a window or network."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class NativeAppearanceQueueTest(unittest.TestCase):
    def test_ordering_fence_scope_capacity_and_drain(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/vsnativeappearance.cpp').read_text()
        remove=method(source,'bool VSNativeAppearance::remove(')
        prefix=remove[:re.search(r'\bbool\s+accepted\s*=',remove).start()]+'++direct;return true;}'
        tick=method(source,'void VSNativeAppearance::tick()')
        drain=method(tick,'if (!state.pendingOrder && !state.orderRecovery && !state.queuedRemovals.empty())')
        fixture=r'''
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <vector>
struct LLUUID {int value;bool operator==(LLUUID b)const{return value==b.value;}};
struct LLInventoryCallback {};
template<class T>using LLPointer=std::shared_ptr<T>;
struct VSNativeSession {struct AccountHandle{bool live=true;};struct InventoryHandle {LLUUID item;unsigned revision;AccountHandle account;LLUUID asset;};
 std::map<int,unsigned> revisions{{1,1},{2,1}};std::map<int,int> assets{{1,10},{2,20}};
 std::optional<InventoryHandle> inventory(LLUUID id){auto i=revisions.find(id.value);
 return i==revisions.end()?std::nullopt:std::optional<InventoryHandle>{{id,i->second,{},LLUUID{assets.at(id.value)}}};}
 bool current(AccountHandle account){return account.live;}
 bool current(InventoryHandle token){auto i=revisions.find(token.item.value);return i!=revisions.end()&&i->second==token.revision;}};
struct Inventory {LLUUID getLinkedItemID(LLUUID id){return id;}} gInventory;
struct VSNativeAppearance {struct Impl {
 bool live=true,orderRecovery=false;unsigned pendingOrder=2;std::weak_ptr<VSNativeSession> session;
 bool current(){return live;}struct QueuedRemoval {std::vector<VSNativeSession::InventoryHandle> items;LLPointer<LLInventoryCallback> callback;};
 std::vector<QueuedRemoval> queuedRemovals;};
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();int direct=0;
 bool remove(const std::vector<LLUUID>&,LLPointer<LLInventoryCallback>);
 void drain(){auto& state=*mImpl;auto session=state.session.lock();DRAIN}
};
REMOVE
int main(){auto session=std::make_shared<VSNativeSession>();VSNativeAppearance appearance;appearance.mImpl->session=session;
 auto callback=std::make_shared<LLInventoryCallback>();std::weak_ptr<LLInventoryCallback> held=callback;
 assert(appearance.remove({{1}},callback));callback.reset();assert(!held.expired());
 appearance.drain();assert(!appearance.direct&&appearance.mImpl->queuedRemovals.size()==1);
 appearance.mImpl->pendingOrder=0;appearance.mImpl->orderRecovery=true;
 appearance.drain();assert(!appearance.direct);
 appearance.mImpl->orderRecovery=false;appearance.drain();assert(appearance.direct==1&&held.expired());
 appearance.mImpl->pendingOrder=1;assert(appearance.remove({{1}},{}));session->revisions[1]=2;
 appearance.mImpl->pendingOrder=0;appearance.drain();assert(appearance.direct==2);
 appearance.mImpl->pendingOrder=1;assert(appearance.remove({{1}},{}));session->assets[1]=11;
 appearance.mImpl->pendingOrder=0;appearance.drain();assert(appearance.direct==2);
 appearance.mImpl->pendingOrder=1;assert(!appearance.remove({{9}},{}));
 for(unsigned i=0;i<64;++i)assert(appearance.remove({{2}},{}));
 assert(!appearance.remove({{2}},{}));appearance.mImpl->live=false;assert(!appearance.remove({{2}},{}));
}
'''.replace('DRAIN',drain).replace('REMOVE',prefix)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'native_appearance_queue.cpp';exe=Path(directory)/'native_appearance_queue.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
