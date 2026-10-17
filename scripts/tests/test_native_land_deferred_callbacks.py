from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute actual deferred land-picker/file/pass closures with their real arities."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
def closure(source,marker):
 a=source.index(marker);brace=source.index('{',a);depth=0
 for b in range(brace,len(source)):
  if source[b]=='{':depth+=1
  elif source[b]=='}':
   depth-=1
   if not depth:return source[a:b+2]
 raise AssertionError(marker)
class LandDeferredCallbacks(unittest.TestCase):
 def test_actual_pick_file_and_pass_lifetimes(self):
  source=compose()['indra/newview/llfloaterland.cpp']
  closures={}
  for name,signature in [('GROUP','void LLPanelLandGeneral::onClickSetGroup()'),('ACCESS','void LLPanelLandAccess::onClickAddAccess()'),('BANNED','void LLPanelLandAccess::onClickAddBanned()'),('DURATION','void LLPanelLandAccess::callbackAvatarCBBanned('),('IMPORT','void LLPanelLandAccess::onClickImportList('),('EXPORT','void LLPanelLandAccess::onClickExportList(')]:
   closures[name]=closure(method(source,signature),'vs_native_im_account_callback(')
  body=method(source,'void LLPanelLandGeneral::onClickBuyPass(');a=body.index('[selection,current,account,pass_price,pass_hours]');closures['PASS']=method(body[a:],'[selection,current,account,pass_price,pass_hours]')
  fixture=(HERE/'native_land_deferred_fixture.cpp').read_text()
  for name,value in closures.items():fixture=fixture.replace(name+'_CLOSURE',value)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'land.cpp';exe=Path(directory)/'land.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
