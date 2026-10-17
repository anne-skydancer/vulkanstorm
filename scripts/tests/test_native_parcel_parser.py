from native_source_fixture import production_sources as compose,ROOT,HERE,method
from pathlib import Path
import subprocess,shutil,tempfile,unittest,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class ParserTest(unittest.TestCase):
 def test_actual_complete_shared_metadata_channels_and_refusal(self):
  changes=compose()
  original=method((ROOT/'indra/newview/llviewerparcelmgr.cpp').read_text(encoding='utf-8-sig'),'void LLViewerParcelMgr::processParcelProperties(')
  proposed=method(changes['indra/newview/llviewerparcelmgr.cpp'],'void LLViewerParcelMgr::processParcelProperties(')
  setters=sorted(set(re.findall(r'parcel->(set\w+)\(',original)) - {'setLocalID','setRegionID','setParcelEnvironmentVersion'})
  setters='\n'.join(' template<class T>void '+name+'(T value){record("'+name+'",value);}' for name in setters)
  names=sorted(set(re.findall(r'_PREHASH_\w+',original+proposed)));prehash='\n'.join('constexpr const char* '+n+'="'+n[len('_PREHASH_'):]+'";' for n in names)
  constants='\n'.join(line for line in (ROOT/'indra/llinventory/llparcel.h').read_text().splitlines() if re.match(r'constexpr S32 (?:PARCEL_RESULT_|SELECTED_PARCEL_SEQ_ID|COLLISION_\w+SEQ_ID|HOVERED_PARCEL_SEQ_ID)',line))
  fixture=(HERE/'native_parcel_parser_fixture.cpp').read_text().replace('PREHASH',prehash).replace('CONSTANTS',constants).replace('SETTERS',setters).replace('REFERENCE',original.replace('LLViewerParcelMgr::processParcelProperties(','LLViewerParcelMgr::processParcelPropertiesReference(',1)).replace('PRODUCTION',proposed)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'parcel.cpp';exe=Path(tmp)/'parcel.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(HERE),'-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
