"""Execute every retained Region/Estate avatar-picker lifetime route."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionAvatarPickerTest(unittest.TestCase):
 def source(self):
  folder=Path(os.environ['VS_REGION_PICKER_SOURCE'])if 'VS_REGION_PICKER_SOURCE'in os.environ else ROOT/'indra/newview'
  return(folder/'llfloaterregioninfo.cpp').read_text()
 def test_actual_registration_account_region_view_authority_retirement(self):
  source=self.source();header=(ROOT/'indra/newview/vsnativeim.h').read_text();a=header.index('template<class Callback>');b=header.index('// Notification responders',a)
  methods='\n'.join(method(source,s)for s in ['void LLPanelRegionDebugInfo::onClickChooseAvatar(', 'void LLPanelEstateInfo::onClickKickUser('])
  fixture=(HERE/'native_region_avatar_picker_fixture.cpp').read_text().replace('GUARD',method(source,'std::function<bool()> region_info_guard(')).replace('WRAPPER',header[a:b]).replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'picker.cpp';exe=Path(tmp)/'picker.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_all_region_avatar_picker_registrations_have_current_owner_guard(self):
  matches=re.findall(r'LLFloaterAvatarPicker::show\(\s*([^\n]+)',self.source());self.assertEqual(len(matches),4)
  for expression in matches:self.assertTrue(expression.startswith('vs_native_im_lifetime_callback(region_info_guard('),expression)
if __name__=='__main__':unittest.main()
