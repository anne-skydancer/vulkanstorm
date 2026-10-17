from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute real production transaction serializers against an in-memory transport."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
class ParcelTransactions(unittest.TestCase):
 def test_real_wire_fields_and_stale_intent_refusal(self):
  source=compose()['indra/newview/llviewerparcelmgr.cpp']
  definitions='\n'.join(method(source,name) for name in ['class LLViewerParcelMgr::ParcelBuyInfo','void LLViewerParcelMgr::sendParcelBuy(','void LLViewerParcelMgr::deleteParcelBuy(','void LLViewerParcelMgr::sendParcelDeed(','void LLViewerParcelMgr::sendParcelRelease('])
  definitions=definitions.replace('class LLViewerParcelMgr::ParcelBuyInfo\n','class LLViewerParcelMgr::ParcelBuyInfo\n',1)
  # The class terminator is outside the method scanner's balanced braces.
  definitions=definitions.replace('\n}\nvoid LLViewerParcelMgr::sendParcelBuy','\n};\nvoid LLViewerParcelMgr::sendParcelBuy',1)
  fixture=(HERE/'native_parcel_transactions_fixture.cpp').read_text().replace('METHODS',definitions)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'transactions.cpp';exe=Path(directory)/'transactions.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
