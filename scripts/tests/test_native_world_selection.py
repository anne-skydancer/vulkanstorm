"""Reference selection policy, simulator grab fields and bounded scene lookup."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from native_source_fixture import ROOT, method
HERE=Path(__file__).resolve().parent
class WorldSelectionTest(unittest.TestCase):
 def compile_fixture(self,stem,replacements):
  source=(HERE/('native_'+stem+'_fixture.cpp')).read_text(encoding='utf-8')
  replacements['COMMON']=(HERE/('native_'+stem+'_common.cpp')).read_text(encoding='utf-8')
  for marker,value in replacements.items(): source=source.replace(marker,value)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'fixture.cpp';exe=Path(tmp)/'fixture.exe';cpp.write_text(source,encoding='utf-8')
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_selection_permission_and_lazy_getter_order(self):
  current=(ROOT/'indra/newview/llselectmgr.cpp').read_text(encoding='utf-8')
  start=current.index('namespace\n{\ntemplate<class SelectionType>')
  stop=current.index('bool LLSelectMgr::setForceSelection',start)
  shared='\n'.join(line for line in(ROOT/'indra/newview/vsselectionpolicy.h').read_text(encoding='utf-8').splitlines()if not line.startswith('#'))
  self.compile_fixture('selection',{'REFERENCE_METHOD':(HERE/'native_selection_gl_reference.cpp').read_text(encoding='utf-8'),'CURRENT_WRAPPER':current[start:stop],'SHARED':shared})
 def test_native_permission_mapping_and_owner_retirement(self):
  current=(ROOT/'indra/newview/vsobjectselection.cpp').read_text(encoding='utf-8')
  shared='\n'.join(line for line in(ROOT/'indra/newview/vsselectionpolicy.h').read_text(encoding='utf-8').splitlines()if not line.startswith('#'))
  source=(HERE/'native_selection_peer_fixture.cpp').read_text(encoding='utf-8').replace('REFERENCE_METHOD',(HERE/'native_selection_gl_reference.cpp').read_text(encoding='utf-8')).replace('SHARED',shared).replace('CURRENT_METHOD',method(current,'bool VSObjectSelection::canSelect('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'peer.cpp';exe=Path(tmp)/'peer.exe';cpp.write_text(source,encoding='utf-8')
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_grab_surface_fields_and_simulator_destination(self):
  current=(ROOT/'indra/newview/lltoolgrab.cpp').read_text(encoding='utf-8')
  start=current.index('namespace\n{\nstruct ReferenceGrabObject')
  helpers=current[start:current.index('void send_ObjectGrab_message(',start)]
  wrappers='\n'.join(method(current,'void '+name+'('+obj)for name in ['send_ObjectGrab_message','send_ObjectDeGrab_message']for obj in ['LLViewerObject* object','const VSObjectContext& object'])
  self.compile_fixture('grab',{'ORIGINAL':(HERE/'native_grab_gl_reference.cpp').read_text(encoding='utf-8'),'HELPERS':helpers,'WRAPPERS':wrappers})
 def test_index_identity_order_and_retirement(self):
  current=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
  index='\n'.join(line for line in(ROOT/'indra/newview/vsobjectindex.h').read_text(encoding='utf-8').splitlines()if not line.startswith('#'))
  self.compile_fixture('object_index',{'INDEX':index,'REFERENCE_METHOD':(HERE/'native_object_index_gl_reference.cpp').read_text(encoding='utf-8'),'CURRENT_METHOD':method(current,'std::optional<VSWorldScene::ObjectHandle> VSWorldScene::object(const LLUUID& identity)')})
if __name__=='__main__':unittest.main()
