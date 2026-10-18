from pathlib import Path
import subprocess,shutil,tempfile,unittest,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Priority(unittest.TestCase):
 def test_original_priority_and_exact_lazy_getter_order(self):
  original=(HERE/'fixtures/native_pie_priority_original_down.cpp').read_text();a=original.index('    // Current set of priorities');b=original.index('    LL_INFOS() << "pick_rigged',a);gold=original[a:b].replace('mPick = transparent_pick;','result=0;').replace('mPick = visible_pick;','result=1;');gold='int reference(MASK mask,LLViewerObject*transp_object,LLViewerObject*visible_object){int result=-1;'+gold+'return result;}';use=(HERE/'fixtures/native_pie_priority_original_use.cpp').read_text().replace('LLToolPie::useClickAction','useClickAction');policy='\n'.join(l for l in(ROOT/'indra/newview/vspieclickpolicy.h').read_text().splitlines()if not l.startswith('#'));peer=(ROOT/'indra/newview/lltoolpie.cpp').read_text();peer=peer[peer.index('struct ReferencePiePriority'):peer.index('#if VS_NATIVE_VULKAN',peer.index('struct ReferencePiePriority'))];peer=peer.replace('    Object object(const LLPickInfo& pick)const{return pick.getObject();}\n','');source=(HERE/'fixtures/native_pie_priority_fixture.cpp').read_text().replace('POLICY',policy).replace('PEER',peer).replace('USE',use).replace('REFERENCE',gold);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_selected_property_readiness_and_retirement(self):
  import importlib.util
  source=(HERE/'fixtures/native_pie_priority_valid_fixture.cpp').read_text().replace('METHOD',method((ROOT/'indra/newview/vsobjectselection.cpp').read_text(),'bool VSObjectSelection::selectGetAllValid()'))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
