from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeScriptIcon(unittest.TestCase):
 def test_real_identity_retirement_and_legacy_source_unchanged(self):
  s=(ROOT/'indra/newview/lltoolselect.cpp').read_text();native=s[s.index('struct NativeSelectionPeer'):]
  methods='\n'.join(method(native,sig)for sig in ['bool scriptIcon(', 'void showScriptDebug('])
  fixture=r"""
#include <cassert>
#include <functional>
struct LLUUID{int id=0;bool notNull()const{return id!=0;}};
struct Icon{std::function<LLUUID()> query;LLUUID getSourceID(){return query();}};
struct LLPickInfo{Icon* mHUDIcon=nullptr;};
struct LLFloaterScriptDebug{static inline int opens=0,last=0;static void show(LLUUID id){++opens;last=id.id;}};
struct Peer{bool* alive;bool current()const{return *alive;}
METHODS
};
int main(){
 bool alive=true;Peer p{&alive};LLPickInfo pick;assert(!p.scriptIcon(pick));p.showScriptDebug(pick);assert(!LLFloaterScriptDebug::opens);
 Icon icon;pick.mHUDIcon=&icon;icon.query=[] {return LLUUID{7};};assert(p.scriptIcon(pick));p.showScriptDebug(pick);assert(LLFloaterScriptDebug::opens==1&&LLFloaterScriptDebug::last==7);
 icon.query=[] {return LLUUID{};};assert(!p.scriptIcon(pick));p.showScriptDebug(pick);assert(LLFloaterScriptDebug::opens==1);
 icon.query=[&] {alive=false;return LLUUID{8};};assert(!p.scriptIcon(pick));alive=true;p.showScriptDebug(pick);assert(!alive&&LLFloaterScriptDebug::opens==1);
 unsigned queries=0;icon.query=[&] {++queries;return LLUUID{9};};assert(!p.scriptIcon(pick));p.showScriptDebug(pick);assert(queries==0&&LLFloaterScriptDebug::opens==1);
}
""".replace('METHODS',methods)
  legacy=s[:s.index('struct NativeSelectionPeer')];self.assertIn('pick.mHUDIcon->getSourceObject()->getID()',legacy)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'icons.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
