from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute production actual shared confirmation callbacks; no purchases or network."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class LandLifetime(unittest.TestCase):
 def test_actual_purchase_and_sale_confirmation_ownership(self):
  changes=compose()
  buy=changes['indra/newview/llfloaterbuyland.cpp'];a=buy.index('    class Confirmation final');b=buy.index('    LLConfirmationManager::confirm(mSiteConfirm,action,new Confirmation(*this));',a)+len('    LLConfirmationManager::confirm(mSiteConfirm,action,new Confirmation(*this));');buy=buy[a:b]
  sell=changes['indra/newview/llfloatersellland.cpp'];a=sell.index('vs_native_im_ui_callback(self,',sell.index('.functor.function('));b=sell.index('}));',a)+2;sell=sell[a:b]
  fixture=(HERE/'native_land_lifecycle_fixture.cpp').read_text().replace('BUY_CALLBACK',buy).replace('SELL_CALLBACK',sell)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'land.cpp';exe=Path(tmp)/'land.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
