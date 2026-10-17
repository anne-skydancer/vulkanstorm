from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute actual production Access/Dwell replies and selection owner guards."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest
class ParcelReplies(unittest.TestCase):
 def test_actual_access_chunks_dwell_and_selection_retirement(self):
  source=compose()['indra/newview/llviewerparcelmgr.cpp']
  methods='\n'.join(method(source,sig) for sig in ['void LLViewerParcelMgr::processParcelAccessListReply(', 'void LLViewerParcelMgr::processParcelDwellReply(', 'std::function<bool()> LLViewerParcelMgr::selectionGuard() const'])
  names=sorted(set(re.findall(r'_PREHASH_\w+',methods)));prehash='\n'.join('constexpr const char* '+n+'="'+n[len('_PREHASH_'):]+'";' for n in names)
  fixture=(HERE/'native_parcel_replies_fixture.cpp').read_text().replace('PREHASH',prehash).replace('PRODUCTION',methods)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'parcel.cpp';exe=Path(tmp)/'parcel.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
