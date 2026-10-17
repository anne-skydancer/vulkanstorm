"""Connected-session evidence must fail for missing stages and corrupt diagnostics."""
import sys
import copy
import json
import re
import subprocess
from pathlib import Path
import unittest
from unittest.mock import patch
import time
import shutil
import xml.etree.ElementTree as ET
import threading
import tempfile
import os
import http.client
import xmlrpc.client
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_vulkan_session_replay as session_runner
from run_vulkan_session_replay import assess, read_llsd, REQUIRED, SimulatorHTTP, append_llsd, INVENTORY_ROOT, favorites_fetch_observed, isolated_profile_environment, conference_start_observed

class SessionEvidenceTests(unittest.TestCase):
    def test_headless_viewer_retains_xvfb_for_real_cef_without_enabling_presentation(self):
        env = {'DISPLAY': ':99', 'WAYLAND_DISPLAY': 'wayland-0', 'XDG_SESSION_TYPE': 'wayland'}
        session_runner.configure_replay_window_environment(env, headless=True, windows=False, require_media=True)
        self.assertEqual(env['DISPLAY'], ':99')
        self.assertEqual(env['XDG_SESSION_TYPE'], 'x11')
        self.assertNotIn('WAYLAND_DISPLAY', env)
        self.assertEqual(env['VS_VULKAN_DIAGNOSTIC_HEADLESS'], '1')
        self.assertFalse(session_runner.harness_prerequisite_passed({'passed': True}, False))

    def test_headless_cef_qualification_fails_early_without_its_display_service(self):
        with self.assertRaisesRegex(ValueError, 'xvfb-run'):
            session_runner.configure_replay_window_environment({}, headless=True, windows=False, require_media=True)
        env = {}
        session_runner.configure_replay_window_environment(env, headless=True, windows=False, require_media=False)
        self.assertEqual(env, {'VS_VULKAN_DIAGNOSTIC_HEADLESS': '1'})

    def test_headless_windows_and_native_presentation_keep_their_distinct_window_paths(self):
        env = {'DISPLAY': ':99', 'WAYLAND_DISPLAY': 'wayland-0'}
        session_runner.configure_replay_window_environment(env, headless=True, windows=True, require_media=True)
        self.assertEqual(env, {'VS_VULKAN_DIAGNOSTIC_HEADLESS': '1'})
        env = {'DISPLAY': ':99', 'VS_VULKAN_DIAGNOSTIC_HEADLESS': '1'}
        session_runner.configure_replay_window_environment(env, headless=False, windows=False, require_media=True)
        self.assertEqual(env, {'DISPLAY': ':99'})

    def test_shared_profiles_require_a_dedicated_credential_free_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);profile=root/'profile-shared'
            first,one=isolated_profile_environment({},root/'primary',True,profile)
            second,two=isolated_profile_environment({},root/'sibling',True,profile)
            self.assertEqual(one,two)
            self.assertEqual(first,second)
            with self.assertRaisesRegex(ValueError,'dedicated siblings'):
                isolated_profile_environment({},root/'primary',True,root/'outside'/'profile-shared')
            (profile/'native-replay-profile.json').unlink()
            with self.assertRaisesRegex(ValueError,'ordinary viewer profile'):
                isolated_profile_environment({},root/'sibling',True,profile)

    def test_viewer_failure_is_not_masked_by_missing_success_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, 'Viewer exited with code -11'):
                session_runner.read_session_evidence(directory, -11)
            failure = directory / 'r2/failure.xml'
            failure.parent.mkdir()
            root = ET.Element('llsd')
            append_llsd(root, {'step': 1, 'failure': 'R2 production replay timed out'})
            failure.write_bytes(ET.tostring(root))
            with self.assertRaisesRegex(RuntimeError, 'R2 step 1: R2 production replay timed out'):
                session_runner.read_session_evidence(directory, -11)
            # A successful exit still requires the actual evidence file.
            with self.assertRaises(FileNotFoundError) as missing:
                session_runner.read_session_evidence(directory, 0)
            self.assertEqual(Path(missing.exception.filename), directory / 'session-replay.xml')

    def test_matrix_progress_is_immediate_and_first_failure_stops_cases(self):
        for outcome in ('pass', 'fail', 'timeout'):
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                evidence = directory / 'evidence'
                command = ['replay', '--build-directory', str(directory), '--runtime', str(directory / 'runtime.json'),
                           '--harness-evidence', str(directory / 'harness.json'), '--evidence', str(evidence),
                           '--all-skins']
                def execute(child_command, timeout):
                    # The parent must announce a case before starting its child.
                    self.assertTrue(output.call_args.args[0].startswith('RUN: connected UI '))
                    self.assertTrue(output.call_args.kwargs['flush'])
                    self.assertEqual(timeout, session_runner.CASE_REPLAY_SECONDS)
                    if outcome == 'timeout':
                        raise subprocess.TimeoutExpired(child_command, timeout)
                    child = Path(child_command[child_command.index('--evidence') + 1])
                    child.mkdir(parents=True)
                    (child / 'results.json').write_text(json.dumps({'passed': outcome == 'pass'}))
                    return subprocess.CompletedProcess(child_command, 0 if outcome == 'pass' else 1)
                with patch.object(sys, 'argv', command), \
                        patch.object(session_runner.platform, 'system', return_value='Linux'), \
                        patch.object(session_runner, 'prepare_native_debugger'), \
                        patch.object(session_runner, 'skin_cases', return_value={'skin-default-base': ('default', '', 'en')}), \
                        patch.object(session_runner.subprocess, 'run', side_effect=execute) as launch, \
                        patch('builtins.print') as output:
                    code = session_runner.main()
                self.assertEqual(code, 0 if outcome == 'pass' else 1)
                self.assertEqual(launch.call_count, 3 if outcome == 'pass' else 1)
                summary = json.loads((evidence / 'results.json').read_text())
                self.assertEqual(summary['passed'], outcome == 'pass')
                self.assertEqual(summary['complete'], outcome == 'pass')
                messages = [call.args[0] for call in output.call_args_list]
                self.assertTrue(all(call.kwargs.get('flush') is True for call in output.call_args_list))
                self.assertTrue(any(message.startswith(('PASS: connected UI skin-default-base',
                                                        'FAIL: connected UI skin-default-base'))
                                    and 's); log:' in message for message in messages))
                if outcome == 'timeout':
                    self.assertIn('timed out', '\n'.join(messages))

    def test_r1_requires_owner_retirement_and_both_actual_asset_revisions(self):
        keys=('host_ids','object_face_retirement','variable_terrain','asset_replacement',
              'crossing_continuity','circuit_retirement','inventory_scope','inventory_revision','scene_owner_kinds','self_continuity')
        record={'r1_contracts':{key:True for key in keys}}
        record['r1_contracts'].update(
            motion_selection={'authoritative_position':[15,20,30],'predicted_position':[15.1,20,30],
                              'predicted_rotation':[0,0,.1,.99],'selected':False},
            task_inventory={'known':True,'pending':False,'serial':42,'filename':'r1-task.txt'},
            appearance_startup=[{'type':0}],
            logout_retirement={'owners':[],'neighbours':[],'owned_object_slots':0,'owned_avatar_slots':0,
                               'assets':{'requests':0,'resident_bytes':'AAAAAAAAAAA='}},
            mesh_before={'asset_generation':'AAAAAAAAAAE=','asset_third_vertex':[32768/65535-.5,.5,0]},
            mesh_after={'asset_generation':'AAAAAAAAAAI=','asset_third_vertex':[45000/65535-.5,.5,0]},
            initial_scene={'neighbours':[{'width':1024,'height':512,'terrain_patches':1}]})
        requests=[{'path':'/r1-mesh','revision':1},{'path':'/r1-mesh','revision':2}]
        self.assertTrue(session_runner.r1_contracts_passed(record,requests))
        self.assertFalse(session_runner.r1_contracts_passed(record,requests[:1]))
        for key in keys+('logout_retirement',):
            broken=copy.deepcopy(record);del broken['r1_contracts'][key]
            self.assertFalse(session_runner.r1_contracts_passed(broken,requests),key)
        for field,value in (('owned_object_slots',1),('owned_avatar_slots',1)):
            broken=copy.deepcopy(record);broken['r1_contracts']['logout_retirement'][field]=value
            self.assertFalse(session_runner.r1_contracts_passed(broken,requests))
        broken=copy.deepcopy(record);broken['r1_contracts']['mesh_after']['asset_third_vertex'][0]=32768/65535-.5
        self.assertFalse(session_runner.r1_contracts_passed(broken,requests))
    def test_headless_prerequisite_does_not_accept_presentation(self):
        graphics={'passed':True,'tests':[{'mode':'offscreen','passed':True},{'mode':'world','passed':True}]}
        self.assertTrue(session_runner.harness_prerequisite_passed(graphics,True))
        self.assertFalse(session_runner.harness_prerequisite_passed(graphics,False))
        graphics['tests'][1]['passed']=False
        self.assertFalse(session_runner.harness_prerequisite_passed(graphics,True))
        graphics['presentation_qualified']=True
        self.assertTrue(session_runner.harness_prerequisite_passed(graphics,False))
        graphics['passed']=False
        self.assertFalse(session_runner.harness_prerequisite_passed(graphics,False))
    def test_linux_ci_debugger_preserves_exit_failure_and_process_deadline(self):
        command = ['viewer', '--settings', 'ci.xml']
        environment = {'GITHUB_ACTIONS': 'true', 'VS_VULKAN_REPLAY_GDB': '1'}
        for code in (0, 139):
            result = subprocess.CompletedProcess(command, code, stdout=b'native trace')
            with patch.object(session_runner.platform, 'system', return_value='Linux'), \
                    patch.object(session_runner.shutil, 'which', return_value='/usr/bin/gdb'), \
                    patch.object(session_runner, 'run_viewer_process', return_value=result) as execute:
                actual = session_runner.run_staged_viewer(command, Path('stage'), environment)
                invocation = execute.call_args.args[0]
                self.assertEqual(invocation[0], '/usr/bin/gdb')
                self.assertIn('--return-child-result', invocation)
                self.assertIn('thread apply all bt', invocation)
                self.assertEqual(invocation[-len(command):], command)
                self.assertEqual(execute.call_args.kwargs['timeout'], session_runner.PROCESS_REPLAY_SECONDS)
                self.assertEqual(actual.returncode, code)
                self.assertEqual(actual.native_debugger, 'gdb')
        with patch.object(session_runner.platform, 'system', return_value='Linux'), \
                patch.object(session_runner.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'requires GDB'):
                session_runner.run_staged_viewer(command, Path('stage'), environment)

    def test_missing_ci_debugger_is_installed_once_before_skin_cases(self):
        with patch.object(session_runner.platform, 'system', return_value='Linux'), \
                patch.dict(session_runner.os.environ, {'GITHUB_ACTIONS': 'true', 'VS_VULKAN_REPLAY_GDB': '1'}), \
                patch.object(session_runner.shutil, 'which', side_effect=[None, '/usr/bin/gdb']), \
                patch.object(session_runner.subprocess, 'run') as install:
            session_runner.prepare_native_debugger()
            install.assert_called_once_with(
                ['sudo', 'apt-get', '-o', 'Acquire::Retries=3', 'install', '-y', 'gdb'], check=True, timeout=120)

    def test_ordinary_linux_ci_qualification_runs_without_debugger(self):
        command = ['viewer'];environment = {'GITHUB_ACTIONS': 'true'}
        result = subprocess.CompletedProcess(command, 0, stdout=b'clean exit')
        with patch.object(session_runner.platform, 'system', return_value='Linux'), \
                patch.object(session_runner, 'run_viewer_process', return_value=result) as execute:
            actual = session_runner.run_staged_viewer(command, Path('stage'), environment)
            execute.assert_called_once_with(command, cwd=Path('stage'), env=environment,
                                           timeout=session_runner.PROCESS_REPLAY_SECONDS)
            self.assertIsNone(actual.native_debugger)

    def test_first_skin_failure_and_incomplete_matrix_cannot_pass(self):
        planned = {'default', 'firestorm'}
        self.assertFalse(session_runner.skin_matrix_passed({'default': {'passed': True}}, planned))
        self.assertFalse(session_runner.skin_matrix_passed(
            {'default': {'passed': True}, 'firestorm': {'passed': False}}, planned))
        self.assertFalse(session_runner.skin_matrix_passed(
            {'default': {'passed': True}, 'unexpected': {'passed': True}}, planned))
        self.assertTrue(session_runner.skin_matrix_passed(
            {name: {'passed': True} for name in planned}, planned))

    def test_actual_script_notification_widget_admission(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/vsstartupui.cpp').read_text(encoding='utf-8')
        expression = re.search(r'const bool admitted\s*=\s*([\s\S]*?);', source)[1]
        types = set(re.findall(r'typeid\(([\w:]+)\)', expression))
        calls = re.findall(r'(\w+)::(\w+)\(t\)', expression)
        declarations = {name: set() for name in types}
        for owner, method in calls:
            declarations.setdefault(owner, set()).add(method)
        fixture = '#include <typeinfo>\n#include <cassert>\n'
        for owner, methods in declarations.items():
            namespace, _, name = owner.rpartition('::')
            if namespace:
                fixture += 'namespace ' + namespace + '{'
            fixture += 'struct ' + name + '{' + ''.join(
                'static bool ' + method + '(const std::type_info&){return false;}' for method in methods)
            if owner == 'LLRadioGroup':
                fixture += 'static const std::type_info& itemType(){return typeid(int);}'
            fixture += '};' + ('}' if namespace else '') + '\n'
        fixture += 'bool admitted(const std::type_info& t){return ' + expression + ';}\n'
        fixture += '''struct SceneWidget{};
int main(){assert(admitted(typeid(LLScriptFloater)));assert(admitted(typeid(LLToastScriptTextbox)));
assert(admitted(typeid(LLToastNotifyPanel)));assert(!admitted(typeid(SceneWidget)));}'''
        self.compile_script_fixture(fixture)

    def compile_script_fixture(self, fixture):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'script_notification.cpp'
            executable = Path(directory) / 'script_notification.exe'
            source.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_script_show_handles_failed_construction_before_dereference(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llscriptfloater.cpp').read_text(encoding='utf-8')
        # The earlier implementation is commented out; exercise the active one.
        method = source.rsplit('LLScriptFloater* LLScriptFloater::show(', 1)[1]
        prefix = 'LLScriptFloater* LLScriptFloater::show(' + method.split('    floater->setNotificationId', 1)[0]
        fixture = '''#include <cassert>
#include <sstream>
using LLUUID=int;
#define LL_WARNS() std::ostringstream()
#define LL_ENDL ""
struct LLScriptFloater {static LLScriptFloater* show(const LLUUID&);};
LLScriptFloater* instance=nullptr;
struct LLFloaterReg {template<class T>static T* getTypedInstance(const char*,const LLUUID&){return instance;}};
''' + prefix + '''return floater;}
int main(){assert(LLScriptFloater::show(1)==nullptr);LLScriptFloater real;instance=&real;
assert(LLScriptFloater::show(1)==&real);}'''
        self.compile_script_fixture(fixture)

    def test_stalled_viewer_is_killed_and_timeout_remains_failed_evidence(self):
        self.assertGreater(session_runner.VIEWER_REPLAY_SECONDS, 0)
        self.assertLess(session_runner.VIEWER_REPLAY_SECONDS, session_runner.PROCESS_REPLAY_SECONDS)
        self.assertLess(session_runner.PROCESS_REPLAY_SECONDS, session_runner.CASE_REPLAY_SECONDS)
        command = ['viewer', '--replay']
        environment = {'MODE': 'test'}
        failure = subprocess.TimeoutExpired(command, .5, output=b'viewer-started')
        with patch.object(session_runner, 'PROCESS_REPLAY_SECONDS', .5), patch.object(
                session_runner, 'run_viewer_process', side_effect=failure) as execute:
            with self.assertRaises(subprocess.TimeoutExpired) as timeout:
                session_runner.run_staged_viewer(command, Path('stage'), environment)
            execute.assert_called_once_with(command, cwd=Path('stage'), env=environment, timeout=.5)
            self.assertIn(b'viewer-started', timeout.exception.stdout)
        self.assertFalse(assess(self.record(), 124, 'VIEWER TIMEOUT'))
        self.assertFalse(assess({}, 0, 'VIEWER TIMEOUT'))

    def test_actual_toolbar_chat_contract_accepts_vintage_and_voice_control(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/vsnativereplay.cpp').read_text()
        lookup = re.search(r'auto\* bottom = gViewerWindow->getRootView\(\)->findChild<[^>]+>\("default_chat_bar"\);', source)[0]
        fixture = r"""#include <cassert>
struct LLView {virtual ~LLView()=default;};
struct FSNearbyChatControl:LLView{};
struct FSNearbyChatVoiceControl:FSNearbyChatControl{};
struct Root {LLView* entry;template<class T>T* findChild(const char*){return dynamic_cast<T*>(entry);}};
struct Window {Root* root;Root* getRootView(){return root;}};
Window* gViewerWindow;
int main(){CONTROL entry;Root root{&entry};Window window{&root};gViewerWindow=&window;
 LOOKUP assert(bottom==&entry);}
"""
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        for skin in ['vintage', 'default']:
            xml = ET.parse(root / ('indra/newview/skins/' + skin + '/xui/en/panel_toolbar_view.xml'))
            node = next(v for v in xml.getroot().iter() if v.attrib.get('name') == 'default_chat_bar')
            control = {'fs_nearby_chat_control':'FSNearbyChatControl',
                       'fs_nearby_chat_voice_monitor':'FSNearbyChatVoiceControl'}[node.tag]
            with self.subTest(skin=skin, actual_widget=node.tag), tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'toolbar.cpp';path.write_text(fixture.replace('CONTROL',control).replace('LOOKUP',lookup))
                exe=Path(directory)/'toolbar.exe'
                built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                run=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_conference_start_requires_actual_selected_recipient_and_temporary_identity(self):
        start = dict(path='/chat', request={
            'method': 'start conference',
            'session-id': '85000000-0000-0000-0000-000000000009',
            'params': ['50000000-0000-0000-0000-000000000005']})
        self.assertTrue(conference_start_observed([start]))
        self.assertTrue(conference_start_observed([dict(path='/chat', request={'method': 'accept invitation'}), start]))
        self.assertFalse(conference_start_observed([]))
        self.assertFalse(conference_start_observed([start, copy.deepcopy(start)]))
        for params in [[], ['10000000-0000-0000-0000-000000000001'],
                       ['50000000-0000-0000-0000-000000000005'] * 2]:
            with self.subTest(params=params):
                altered = copy.deepcopy(start); altered['request']['params'] = params
                self.assertFalse(conference_start_observed([altered]))
        for identity in ['', None, 'not-a-uuid', '00000000-0000-0000-0000-000000000000',
                         '84000000-0000-0000-0000-000000000008']:
            with self.subTest(identity=identity):
                altered = copy.deepcopy(start); altered['request']['session-id'] = identity
                self.assertFalse(conference_start_observed([altered]))
        altered = copy.deepcopy(start); del altered['request']['session-id']
        self.assertFalse(conference_start_observed([altered]))
        altered = copy.deepcopy(start); altered['path'] = '/unrelated'
        self.assertFalse(conference_start_observed([altered]))
        # Diagnostic-body records cannot substitute for the actual POST receipt.
        self.assertFalse(conference_start_observed([dict(path='/chat', body=start['request'])]))

    def test_replay_profile_is_fresh_and_does_not_mutate_parent_environment(self):
        parent = dict(os.environ)
        with tempfile.TemporaryDirectory() as directory:
            source = dict(APPDATA='user-roaming', LOCALAPPDATA='user-local',
                          VULKANSTORM_X64_USER_DIR='user-linux', OTHER_SETTING='preserved')
            original = dict(source)
            windows, first = isolated_profile_environment(source, directory, True)
            repeated, second = isolated_profile_environment(source, directory, True)
            linux, third = isolated_profile_environment(source, directory, False)
            self.assertEqual(len({first, second, third}), 3, 'Retries must not inherit aborted-run settings')
            for profile in [first, second, third]:
                self.assertTrue(profile.is_relative_to(Path(directory).resolve()))
                self.assertTrue((profile / 'cache').is_dir())
            for env in [windows, repeated]:
                self.assertTrue(Path(env['APPDATA']).is_dir())
                self.assertTrue(Path(env['LOCALAPPDATA']).is_dir())
            self.assertTrue(Path(linux['VULKANSTORM_X64_USER_DIR']).is_dir())
            self.assertEqual(linux['VULKANSTORM_USER_DIR'], linux['VULKANSTORM_X64_USER_DIR'])
            self.assertEqual(windows['OTHER_SETTING'], 'preserved')
            self.assertEqual(source, original)
        self.assertEqual(dict(os.environ), parent)

    def test_native_transport_handlers_match_packaged_wire_schema(self):
        root = Path(__file__).resolve().parents[2]
        template = (root / 'scripts/messages/message_template.msg').read_text()
        messages = set(re.findall(r'\{\s*(\w+)\s+(?:Low|Medium|High|Fixed)\s+', template))
        source = (root / 'indra/newview/vsnativesession.cpp').read_text()
        install = source.split('void VSNativeSession::install(', 1)[1].split('void VSNativeSession::', 1)[0]
        handlers = re.findall(r'setHandlerFunc\("(\w+)"', install)
        for names in re.findall(r'for \(const char\* name : \{([^}]+)\}\)', install):
            handlers.extend(re.findall(r'"(\w+)"', names))
        self.assertTrue(handlers, 'Production native transport handlers must be surveyed')
        for name in handlers:
            with self.subTest(handler=name):
                self.assertIn(name, messages, 'Native handler registration must use a real packaged message')
        reply = re.search(r'RegionIDAndHandleReply\s+Low\s+310\s+Trusted\s+Unencoded\s*\{\s*ReplyBlock\s+Single\s*\{\s*RegionID\s+LLUUID\s*\}\s*\{\s*RegionHandle\s+U64\s*\}', template)
        self.assertIsNotNone(reply, 'Landmark reply decoder requires UUID/U64 ReplyBlock schema')
        self.assertIn('msg->getUUID("ReplyBlock", "RegionID", id)', source)
        self.assertIn('msg->getU64("ReplyBlock", "RegionHandle", handle)', source)

    def test_server_im_fixtures_include_packaged_required_tail(self):
        root = Path(__file__).resolve().parents[2]
        template = (root / 'scripts/messages/message_template.msg').read_text()
        schema = template.split('ImprovedInstantMessage Low 254', 1)[1].split('// RetrieveInstantMessages', 1)[0]
        self.assertRegex(schema, r'EstateBlock\s+Single\s*\{\s*EstateID\s+U32\s*\}')
        self.assertRegex(schema, r'MetaData\s+Variable\s*\{\s*Data\s+Variable\s+2\s*\}')
        replay = (root / 'indra/newview/vsnativeimreplay.cpp').read_text()
        server_packets = re.findall(r'pack_instant_message\(.*?sendReliable\(owner.host\(\)\);', replay, re.S)
        self.assertTrue(server_packets)
        for packet in server_packets:
            self.assertIn('finishServerIM();', packet, 'Server fixture packets must include required EstateBlock tail')
        tail = replay.split('void finishServerIM()', 1)[1].split('void incoming(', 1)[0]
        self.assertIn('nextBlock("EstateBlock")', tail)
        self.assertIn('addU32("EstateID", 0)', tail)

    def test_native_notifications_have_packaged_templates(self):
        root = Path(__file__).resolve().parents[2]
        notifications = ET.parse(root / 'indra/newview/skins/default/xui/en/notifications.xml')
        names = {node.get('name') for node in notifications.getroot().iter('notification')}
        for path in (root / 'indra/newview').glob('*.cpp'):
            for name in re.findall(r'LLNotificationsUtil::add\("(Native\w+)"', path.read_text(encoding='utf-8')):
                with self.subTest(path=path.name, notification=name):
                    self.assertIn(name, names, 'Native UI actions require real packaged notification templates')

    def test_native_viewer_includes_match_tracked_header_case(self):
        root = Path(__file__).resolve().parents[2]
        tracked = subprocess.check_output(['git', 'ls-files', 'indra'], cwd=root, text=True).splitlines()
        names = {Path(path).name for path in tracked}
        folded = {name.lower() for name in names}
        for path in tracked:
            if not path.startswith('indra/newview/vs') or not path.endswith(('.cpp', '.h')):
                continue
            for header in re.findall(r'#include\s+"([^"/]+)"', (root / path).read_text(encoding='utf-8')):
                if header.lower() in folded:
                    with self.subTest(path=path, header=header):
                        self.assertIn(header, names, 'Windows can hide include case errors that fail Linux builds')

    def record(self):
        return dict(schema=1, mode='viewer-native-session-replay', passed=True,
                    world_owners=0, live_server_qualified=False, sent=8, expired=2,
                    **{name: True for name in REQUIRED})

    def test_complete_replay(self):
        self.assertTrue(assess(self.record(), 0, 'Native Vulkan normal login shut down'))

    def test_every_stage_is_decisive(self):
        for name in REQUIRED:
            record = self.record(); del record[name]
            with self.subTest(name=name):
                self.assertFalse(assess(record, 0, 'Native Vulkan normal login shut down'))

    def test_validation_crash_and_unobserved_shutdown_fail(self):
        for code, log in ((143, ''), (0, ''), (0, 'VUID-test Native Vulkan normal login shut down'),
                          (0, 'Forbidden GL path Native Vulkan normal login shut down'),
                          (0, 'Rejected native widget type: Native Vulkan normal login shut down'),
                          (0, 'Making dummy class LLButton Native Vulkan normal login shut down'),
                          (0, 'panel is not a valid child of floater Native Vulkan normal login shut down'),
                          (0, 'Could not create widget named script_editor Native Vulkan normal login shut down'),
                          (0, "Failed to assign 'disabled' control variable Native Vulkan normal login shut down"),
                          (0, 'with no handler function received: GroupProfileReply Native Vulkan normal login shut down')):
            self.assertFalse(assess(self.record(), code, log))

    def test_replay_cannot_claim_live_acceptance(self):
        record = self.record(); record['live_server_qualified'] = True
        self.assertFalse(assess(record, 0, 'Native Vulkan normal login shut down'))

    def test_malformed_map_fails(self):
        with self.assertRaises(ValueError):
            read_llsd(ET.fromstring('<llsd><map><key>passed</key></map></llsd>'))

    def test_real_xmlrpc_integer_consent_and_inventory_contract(self):
        with SimulatorHTTP() as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                proxy = xmlrpc.client.ServerProxy(server.url + '/login?sim_port=13001')
                params = dict(read_critical=False)
                self.assertEqual(proxy.login_to_simulator(params)['reason'], 'critical')
                # The viewer XML-RPC adapter represents LLSD booleans as integers.
                response = proxy.login_to_simulator(dict(read_critical=1))
                self.assertEqual(response['login'], 'true')
                self.assertEqual(response['account_type'], 'Basic')
                benefits = response['account_level_benefits']
                self.assertEqual(benefits['texture_upload_cost'], 0)
                self.assertEqual(benefits['sound_upload_cost'], 0)
                self.assertTrue({'Base', 'Premium'}.issubset(response['premium_packages']))
                root = response['inventory-root'][0]['folder_id']
                self.assertEqual(root, INVENTORY_ROOT)
                children = [folder for folder in response['inventory-skeleton'] if folder['parent_id'] == root]
                self.assertTrue({0, 1, 5, 6, 7, 10, 13, 15, 16, 20, 21}.issubset({folder['type_default'] for folder in children}))
                self.assertTrue(response['inventory-lib-root'][0]['folder_id'])
                request = ET.Element('llsd')
                append_llsd(request, dict(folders=[dict(folder_id=root, owner_id=response['agent_id'])]))
                connection = http.client.HTTPConnection(*server.server_address, timeout=5)
                try:
                    connection.request('POST', '/inventory', ET.tostring(request))
                    result = connection.getresponse()
                    self.assertEqual(result.status, 200)
                    fetched = read_llsd(ET.fromstring(result.read()))['folders'][0]
                finally:
                    connection.close()
                self.assertEqual(fetched['descendents'], len(children))
                self.assertEqual({folder['folder_id'] for folder in fetched['categories']}, {folder['folder_id'] for folder in children})
            finally:
                proxy.__exit__(None, None, None)
                server.shutdown()
                worker.join()

    def test_profile_updates_are_observed_by_subsequent_requests(self):
        with SimulatorHTTP() as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                path = '/profile/10000000-0000-0000-0000-000000000001'
                connection.request('GET', path)
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                initial = read_llsd(ET.fromstring(response.read()))
                self.assertEqual(initial['sl_image_id'], '00000000-0000-0000-0000-000000000000')
                updates = dict(sl_image_id='60000000-0000-0000-0000-000000000006', notes='Edited notes')
                root = ET.Element('llsd'); append_llsd(root, updates)
                connection.request('PUT', path, ET.tostring(root))
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertTrue(read_llsd(ET.fromstring(response.read()))['success'])
                connection.request('GET', path)
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                saved = read_llsd(ET.fromstring(response.read()))
                self.assertEqual({key: saved[key] for key in updates}, updates)
                self.assertEqual(server.requests[-2]['body'], updates)
            finally:
                connection.close()
                server.shutdown()
                worker.join()

    def test_favorites_transport_cannot_be_replaced_by_other_inventory(self):
        real = dict(path='/inventory', request=dict(folders=[dict(
            folder_id='70000000-0000-0000-0000-000000000016', fetch_items=True)]))
        self.assertTrue(favorites_fetch_observed([real]))
        self.assertFalse(favorites_fetch_observed([]))
        real['request']['folders'][0]['folder_id'] = INVENTORY_ROOT
        self.assertFalse(favorites_fetch_observed([real]))
        real['request']['folders'][0]['folder_id'] = '70000000-0000-0000-0000-000000000016'
        real['request']['folders'][0]['fetch_items'] = False
        self.assertFalse(favorites_fetch_observed([real]))

    def test_favorite_inventory_and_landmark_asset_are_real_fixture_data(self):
        with SimulatorHTTP() as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                request = dict(folders=[dict(folder_id='70000000-0000-0000-0000-000000000016',
                    owner_id='10000000-0000-0000-0000-000000000001', fetch_items=True, fetch_folders=True)])
                root = ET.Element('llsd'); append_llsd(root, request)
                connection.request('POST', '/inventory', ET.tostring(root))
                response = connection.getresponse(); self.assertEqual(response.status, 200)
                folder = read_llsd(ET.fromstring(response.read()))['folders'][0]
                self.assertEqual(folder['descendents'], len(folder['items']) + len(folder['categories']))
                item = folder['items'][0]
                self.assertEqual(item['type'], 'landmark')
                self.assertEqual(item['inv_type'], 'landmark')
                self.assertEqual(item['parent_id'], folder['folder_id'])
                connection.request('GET', '/asset/?landmark_id=' + item['asset_id'])
                response = connection.getresponse(); self.assertEqual(response.status, 200)
                self.assertEqual(response.read(), b'Landmark version 2\nregion_id 40000000-0000-0000-0000-000000000004\nlocal_pos 128 128 25\n')
            finally:
                connection.close(); server.shutdown(); worker.join()

    def test_inventory_upload_requires_handshake_and_encoded_image(self):
        with SimulatorHTTP() as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                connection.request('POST', '/upload-data/texture', bytes.fromhex('ff4fff51'))
                response = connection.getresponse()
                self.assertEqual(response.status, 400)
                self.assertEqual(read_llsd(ET.fromstring(response.read()))['state'], 'error')
                self.assertEqual(server.requests, [], 'Upload bytes bypassed required handshake')
                metadata = dict(folder_id='70000000-0000-0000-0000-000000000003',
                                asset_type='texture', inventory_type='texture',
                                name='Native uploaded texture', description='Fixture upload',
                                next_owner_mask=2147483647, group_mask=0, everyone_mask=0,
                                expected_upload_cost=0)
                root = ET.Element('llsd'); append_llsd(root, metadata)
                connection.request('POST', '/upload-request', ET.tostring(root))
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(read_llsd(ET.fromstring(response.read()))['state'], 'upload')
                connection.request('POST', '/upload-data/texture', b'invalid image')
                response = connection.getresponse()
                self.assertEqual(response.status, 400)
                self.assertEqual(read_llsd(ET.fromstring(response.read()))['state'], 'error')
                self.assertEqual(server.requests[-1]['path'], '/upload-request',
                                 'Rejected image must not produce an accepted upload receipt')
                # Retrying on the same connection proves that the rejected body
                # was consumed and its HTTP response remained correctly framed.
                data = (Path(__file__).resolve().parents[2] / 'indra/newview/skins/default/textures/transparent.j2c').read_bytes()
                connection.request('POST', '/upload-data/texture', data)
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                completed = read_llsd(ET.fromstring(response.read()))
                self.assertEqual(completed['new_inventory_item'], '71000000-0000-0000-0000-000000000002')
                receipt = server.requests[-1]
                self.assertEqual(receipt['bytes'], len(data))
                self.assertEqual(receipt['metadata'], metadata)
            finally:
                connection.close()
                server.shutdown()
                worker.join()

    def test_script_asset_and_save_require_actual_source_bytes(self):
        with SimulatorHTTP() as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                connection.request('GET', '/asset/?lsltext_id=60000000-0000-0000-0000-000000000009')
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertIn(b'Native initial script', response.read())
                metadata = dict(item_id='71000000-0000-0000-0000-000000000004', target='mono')
                root = ET.Element('llsd'); append_llsd(root, metadata)
                connection.request('POST', '/script-request', ET.tostring(root))
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(read_llsd(ET.fromstring(response.read()))['state'], 'upload')
                data = b'default { state_entry() { llOwnerSay("Native edited script"); }}\0'
                connection.request('POST', '/script-data', data)
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                completed = read_llsd(ET.fromstring(response.read()))
                self.assertTrue(completed['compiled'])
                self.assertEqual(completed['new_asset'], '60000000-0000-0000-0000-000000000010')
                self.assertEqual(server.requests[-1]['bytes'], len(data))
                self.assertEqual(server.requests[-1]['metadata'], metadata)
            finally:
                connection.close()
                server.shutdown()
                worker.join()

if __name__ == '__main__':
    unittest.main()
