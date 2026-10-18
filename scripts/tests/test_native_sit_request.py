from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Request(unittest.TestCase):
 def test_original_sit_authority_standtp_and_exact_wire_order_transport(self):
  s=(ROOT/'indra/newview/llviewermenu.cpp').read_text();shared=method(s,'template<class Peer>void requestObjectSit(');gold=(HERE/'fixtures/native_sit_original_request.cpp').read_text().replace('handle_object_sit(','referenceSit(');source=(HERE/'fixtures/native_sit_request_fixture.cpp').read_text().replace('PEERS',(HERE/'fixtures/native_sit_request_peers.cpp').read_text()).replace('SHARED',shared).replace('REFERENCE',gold);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
