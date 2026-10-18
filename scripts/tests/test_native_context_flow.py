from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class ContextFlow(unittest.TestCase):
 def test_original_all_context_and_pie_branch_traces(self):
  source=(ROOT/'indra/newview/lltoolpie.cpp').read_text();peer=method(source,'struct ReferenceContextSelectionPeer')+';'
  kernel='\n'.join(line for line in(ROOT/'indra/newview/vscontextselection.h').read_text().splitlines()if not line.startswith('#'))
  fixture=(HERE/'native_context_flow_fixture.cpp').read_text().replace('ORIGINAL',(HERE/'native_context_flow_reference.cpp').read_text()).replace('PEER',peer).replace('KERNEL',kernel)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'context.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
