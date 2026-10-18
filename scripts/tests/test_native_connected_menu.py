"""Actual resident-menu and group-inspector intent/account controller lifetimes."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_connected_menu'
class NativeConnectedMenu(unittest.TestCase):
 def run_cpp(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'menu.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_actual_primary_selection_drives_world_avatar_call_and_current_identity(self):
  selection=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();source=(ROOT/'indra/newview/llviewermenu.cpp').read_text()
  start=source.index('class LLAvatarCall :');end=source.index('// [RLVa:KB] - Checked: RLVa-1.2.1',start)
  call=source[start:end].replace('class LLAvatarCall : public view_listener_t\n{','class LLAvatarCall : public view_listener_t\n{\npublic:')
  self.run_cpp((FIXTURES/'primary_fixture.cpp').read_text().replace('PRIMARY',method(selection,'std::optional<VSWorldScene::ObjectHandle> VSObjectSelection::primaryObject(')).replace('CALL',call).replace('ENABLE',method(source,'bool enable_avatar_call(')))
 def test_real_simulator_url_floater_preserves_internal_trust_geometry_and_controls(self):
  source=(ROOT/'indra/newview/llurlfloaterdispatchhandler.cpp').read_text()
  constants=source[source.index('const std::string MESSAGE_URL_FLOATER'):source.index('LLUrlFloaterDispatchHandler LLUrlFloaterDispatchHandler::sUrlDispatchhandler;')]
  self.run_cpp((FIXTURES/'url_fixture.cpp').read_text().replace('CONSTANTS',constants).replace('METHOD',method(source,'bool LLUrlFloaterDispatchHandler::operator()(')))
 def test_real_namelist_callbacks_match_shipped_menus_and_retire_original_intent(self):
  source=(ROOT/'indra/newview/fsnamelistavatarmenu.cpp').read_text();header=(ROOT/'indra/newview/vsnativeim.h').read_text()
  policy=[]
  for name in ['menu_fs_namelist_avatar.xml','menu_fs_namelist_avatar_multiselect.xml']:
   for element in ET.parse(ROOT/'indra/newview/skins/default/xui/en'/name).getroot().iter():
    function=element.get('function','')
    if function.startswith('Namelist.'):
     import json
     policy.append('assert(FSNameListAvatarMenu::isNativeCallback('+json.dumps(function)+',{' +json.dumps(element.get('parameter',''))+'}));')
  code=(FIXTURES/'namelist_fixture.cpp').read_text().replace('ACCOUNT',method(header,'template<class Callback>\nauto vs_native_im_account_callback(')).replace('LIFETIME',method(header,'template<class Current, class Callback>\nauto vs_native_im_lifetime_callback(')).replace('CREATE',method(source,'LLContextMenu* FSNameListAvatarMenu::createMenu(')).replace('RESET',method(source,'void FSNameListAvatarMenu::resetAccount(')).replace('GATE_ASSERTIONS','\n'.join(policy)).replace('GATE',method(source,'bool FSNameListAvatarMenu::isNativeCallback('))
  self.run_cpp(code)
 def test_real_group_inspector_rejects_old_group_account_and_owner_and_survives_close(self):
  source=(ROOT/'indra/newview/llinspectgroup.cpp').read_text()
  self.run_cpp((FIXTURES/'group_fixture.cpp').read_text().replace('REQUEST',method(source,'void LLInspectGroup::requestUpdate(')).replace('NAME',method(source,'void LLInspectGroup::nameUpdatedCallback(')).replace('JOIN',method(source,'void LLInspectGroup::onClickJoin(')))
if __name__=='__main__':unittest.main()
