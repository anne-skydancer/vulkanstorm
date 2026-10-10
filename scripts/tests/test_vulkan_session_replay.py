"""Connected-session evidence must fail for missing stages and corrupt diagnostics."""
import sys
import copy
import re
import subprocess
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET
import threading
import tempfile
import os
import http.client
import xmlrpc.client
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import assess, read_llsd, REQUIRED, SimulatorHTTP, append_llsd, INVENTORY_ROOT, favorites_fetch_observed, isolated_profile_environment, conference_start_observed

class SessionEvidenceTests(unittest.TestCase):
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
