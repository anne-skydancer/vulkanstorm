from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class Continuation(unittest.TestCase):
 def test_retained_controller_cannot_continue_after_retirement_or_replacement(self):
  source=(ROOT/'indra/newview/lltoolselect.cpp').read_text();source=source[source.index('struct NativeSelectionPeer'):];current=method(source,'bool current(')
  fixture=r"""
#include <cassert>
#include <memory>
#include <functional>
struct Model{std::shared_ptr<const void> marker=std::make_shared<int>(1);auto retainSelection(){return marker;}};
struct VSNativeSession{enum class Phase{Connected,Disconnected};Phase value=Phase::Connected;Model* selected=nullptr;auto phase(){return value;}Model* existingObjectSelection(){return selected;}};
struct Peer{std::shared_ptr<VSNativeSession> session;std::shared_ptr<Model> lifetime;Model& owner;std::function<bool()> account;
METHOD
};
int main(){
 auto session=std::make_shared<VSNativeSession>();auto old=std::make_shared<Model>();session->selected=old.get();bool account=true;Peer peer{session,old,*old,[&]{return account;}};
 assert(peer.current());auto oldUILease=old->retainSelection();old->marker.reset();assert(!peer.current()&&oldUILease);
 old->marker=std::make_shared<int>(2);auto next=std::make_shared<Model>();session->selected=next.get();assert(!peer.current());
 session->selected=old.get();account=false;assert(!peer.current());account=true;session->value=VSNativeSession::Phase::Disconnected;assert(!peer.current());
 session->value=VSNativeSession::Phase::Connected;assert(peer.current());
}
""".replace('METHOD',current)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'continuation.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
