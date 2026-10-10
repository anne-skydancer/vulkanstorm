"""Replay the native CPU session through the fully staged viewer, without grid credentials."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import xml.etree.ElementTree as ET
import xmlrpc.client
from urllib.parse import urlsplit, parse_qs

from run_software_vulkan_tests import windows_manifest_registration, loaded_library_hashes
from run_vulkan_viewer_diagnostic import runtime_libraries, skin_cases

INVENTORY_ROOT = '70000000-0000-0000-0000-000000000001'
LIBRARY_ROOT = '72000000-0000-0000-0000-000000000001'
LIBRARY_OWNER = '72000000-0000-0000-0000-000000000002'
SYSTEM_FOLDERS = [(0, 'Textures', 3), (7, 'Notecards', 2), (1, 'Sounds', 4),
                  (2, 'Calling Cards', 5), (3, 'Landmarks', 6), (5, 'Clothing', 7),
                  (6, 'Objects', 8), (10, 'Scripts', 9), (13, 'Body Parts', 10),
                  (14, 'Trash', 11), (15, 'Photo Album', 12), (16, 'Lost And Found', 13),
                  (20, 'Animations', 14), (21, 'Gestures', 15), (23, 'Favorites', 16),
                  (46, 'Current Outfit', 17), (48, 'My Outfits', 18), (50, 'Received Items', 19),
                  (56, 'Settings', 20), (57, 'Materials', 21)]
INVENTORY_SKELETON = [dict(folder_id=INVENTORY_ROOT,
                          parent_id='00000000-0000-0000-0000-000000000000',
                          name='My Inventory', type_default=8, version=1)] + [
    dict(folder_id=f'70000000-0000-0000-0000-{suffix:012d}', parent_id=INVENTORY_ROOT,
         name=name, type_default=kind, version=1) for kind, name, suffix in SYSTEM_FOLDERS]


def append_llsd(parent, value):
    if isinstance(value, dict):
        result = ET.SubElement(parent, 'map')
        for key, item in value.items():
            ET.SubElement(result, 'key').text = key
            append_llsd(result, item)
    elif isinstance(value, list):
        result = ET.SubElement(parent, 'array')
        for item in value:
            append_llsd(result, item)
    elif isinstance(value, bool):
        ET.SubElement(parent, 'boolean').text = str(value).lower()
    elif isinstance(value, datetime):
        ET.SubElement(parent, 'date').text = value.isoformat().replace('+00:00', 'Z')
    elif isinstance(value, int):
        ET.SubElement(parent, 'integer').text = str(value)
    else:
        ET.SubElement(parent, 'string').text = str(value)

REQUIRED = ('connected_skin_geometry', 'connected_account_benefits', 'connected_inventory_script_edit_save', 'connected_inventory_properties', 'connected_inventory_sound_preview',
            'connected_inventory_texture_upload', 'connected_inventory_sound_upload',
            'connected_group_titles', 'connected_group_directory_search',
            'connected_inventory_share', 'connected_resident_pay', 'connected_payment_balance_gate',
            'connected_display_name_update',
            'connected_profile_second_life_image_save', 'connected_profile_first_life_image_save',
            'connected_resident_profile_notes', 'connected_group_profile_members_roles',
            'connected_preferences_apply_cancel', 'preferences_repeated_tabs', 'preferences_graphics_persistence', 'connected_contact_set_edit', 'connected_blocklist_edit',
            'connected_transcript_preview', 'im_inventory_offer', 'im_group_attachment', 'im_inventory_preview',
            'connected_ui_image_decoded', 'im_friendship_notification', 'im_friend_status_history', 'im_group_notice', 'im_group_invitation_decline_encoded',
            'im_contacts_ui', 'im_readback', 'im_direct_encoded', 'im_direct_incoming_offline', 'im_typing', 'im_mute',
            'im_group_start', 'im_group_encoded', 'im_group_incoming', 'im_group_participants',
            'im_group_leave', 'im_group_reopen', 'im_group_invitation', 'im_group_start_error',
            'im_group_event_error', 'im_group_force_close', 'im_callback_expired',
            'login_authentication', 'login_challenge', 'login_account_setup', 'notification_storage', 'malformed', 'unknown', 'wrong_host', 'udp_chat', 'http_gate', 'udp_gate',
            'queued_expired', 'relogin', 'chat_submit', 'typing', 'settings_gate', 'readback',
            'partial_init_cleanup', 'connection_timeout', 'encoded_chat', 'encoded_typing', 'ui_gate', 'logout_timeout', 'crossing_disconnect', 'encoded_channels', 'queued_http_chat', 'mute_request', 'mute_transfer_cleanup', 'input_history', 'status_messages', 'connected_timeout', 'reliable_failure')


def read_llsd(element):
    if element.tag == 'llsd':
        return read_llsd(list(element)[0])
    if element.tag == 'map':
        children = list(element)
        if len(children) % 2 or any(children[i].tag != 'key' for i in range(0, len(children), 2)):
            raise ValueError('Malformed LLSD evidence map')
        return {children[i].text: read_llsd(children[i + 1]) for i in range(0, len(children), 2)}
    if element.tag == 'array':
        return [read_llsd(item) for item in element]
    if element.tag == 'boolean':
        return element.text in ('true', '1')
    if element.tag == 'integer':
        return int(element.text or '0')
    return element.text or ''


def assess(record, returncode, log):
    return (returncode == 0 and record.get('schema') == 1
            and record.get('mode') == 'viewer-native-session-replay'
            and record.get('passed') is True and record.get('world_owners') == 0
            and record.get('live_server_qualified') is False
            and all(record.get(key) is True for key in REQUIRED)
            and record.get('sent', 0) >= 4 and record.get('expired', 0) >= 1
            and 'Native Vulkan normal login shut down' in log
            and not any(text in log for text in ('Validation Error', 'VUID-', 'DILIGENT 2:',
                                                 'Forbidden GL path', 'Rejected native widget type:', 'Failed to create dummy',
                                                 'Making dummy ', 'is not a valid child', 'Could not create widget', "Failed to assign '", 'VIEWER TIMEOUT'))
            and not re.search(r'with no handler function received: \w+Reply\b', log))


class SimulatorHTTP(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.auth_requests = 0
        self.seed_requests = 0
        self.event_requests = 0
        self.requests = []
        self.profile_updates = {}
        self.upload_requests = {}
        self.upload_directory = None

    @property
    def url(self):
        return 'http://127.0.0.1:' + str(self.server_address[1])


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
            # Viewer shutdown closes outstanding event-poll HTTP connections.
            pass

    def log_message(self, *args):
        pass

    def respond_llsd(self, body):
        root = ET.Element('llsd'); append_llsd(root, body)
        data = ET.tostring(root, encoding='utf-8')
        self.send_response(200)
        self.send_header('Content-Type', 'application/llsd+xml')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path.rstrip('/') == '/asset':
            query = parse_qs(urlsplit(self.path).query)
            if query.get('lsltext_id') == ['60000000-0000-0000-0000-000000000009']:
                data = b'default { state_entry() { llOwnerSay("Native initial script"); } }'
            elif (query.get('sound_id') == ['60000000-0000-0000-0000-000000000008']
                  and self.server.upload_directory is not None
                  and (self.server.upload_directory / 'sound.ogg').is_file()):
                data = (self.server.upload_directory / 'sound.ogg').read_bytes()
            else:
                self.send_error(404)
                return
            self.server.requests.append(dict(path=path, method='GET', query=query, bytes=len(data)))
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if path.startswith('/profile/'):
            agent = path.removeprefix('/profile/')
            self.server.requests.append({'path': path, 'method': 'GET'})
            profile = dict(id=agent, sl_image_id='60000000-0000-0000-0000-000000000006',
                fl_image_id='60000000-0000-0000-0000-000000000006', partner_id='00000000-0000-0000-0000-000000000000',
                sl_about_text='Native resident profile', fl_about_text='Native first life',
                member_since=datetime(2020, 1, 1, tzinfo=timezone.utc), hide_age=False, customer_type='Resident',
                notes='Native profile notes', online=True, allow_publish=False, identified=False, transacted=False,
                caption='Native test resident', groups=[dict(id='80000000-0000-0000-0000-000000000008',
                    name='Native replay group', image_id='60000000-0000-0000-0000-000000000006')], picks=[])
            if agent == '10000000-0000-0000-0000-000000000001':
                profile['sl_image_id'] = profile['fl_image_id'] = '00000000-0000-0000-0000-000000000000'
            profile.update(self.server.profile_updates.get(agent, {}))
            self.respond_llsd(profile)
            return
        if path == '/avatar-picker/':
            self.server.requests.append({'path': path, 'method': 'GET'})
            self.respond_llsd(dict(agents=[dict(id='50000000-0000-0000-0000-000000000005',
                username='cpu.sender', display_name='CPU Sender', legacy_first_name='CPU', legacy_last_name='Sender',
                is_display_name_default=True)]))
            return
        if urlsplit(self.path).path == '/user-info':
            self.server.requests.append({'path': '/user-info', 'method': 'GET'})
            root = ET.Element('llsd')
            append_llsd(root, dict(success=True, email='fixture@example.invalid',
                                  directory_visibility='default', im_via_email=False))
            data = ET.tostring(root, encoding='utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/llsd+xml')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if urlsplit(self.path).path == '/names/':
            ids = parse_qs(urlsplit(self.path).query).get('ids', [])
            self.server.requests.append({'path': '/names/', 'ids': ids})
            root = ET.Element('llsd')
            append_llsd(root, {'agents': [dict(id=agent, username='cpu.sender',
                         display_name='CPU Sender', legacy_first_name='CPU', legacy_last_name='Sender',
                         is_display_name_default=True) for agent in ids], 'bad_ids': []})
            data = ET.tostring(root, encoding='utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/llsd+xml')
            self.send_header('Cache-Control', 'max-age=3600')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if urlsplit(self.path).path != '/texture':
            self.send_error(404)
            return
        # Existing lossless CPU skin asset provides reproducible image data.
        data = (Path(__file__).resolve().parents[1] / 'indra/newview/skins/default/textures/transparent.j2c').read_bytes()
        self.server.requests.append({'path': '/texture', 'texture_id': parse_qs(urlsplit(self.path).query).get('texture_id', [])})
        self.send_response(200)
        self.send_header('Content-Type', 'image/jp2')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_PUT(self):
        path = urlsplit(self.path).path
        size = int(self.headers.get('Content-Length', '0'))
        if path != '/profile/10000000-0000-0000-0000-000000000001' or not 0 < size <= 65536:
            self.send_error(400)
            return
        request = read_llsd(ET.fromstring(self.rfile.read(size)))
        if not isinstance(request, dict) or not request or not set(request).issubset(
                {'sl_image_id', 'fl_image_id', 'notes', 'sl_about_text', 'fl_about_text', 'allow_publish', 'hide_age'}):
            self.send_error(400)
            return
        self.server.requests.append({'path': path, 'method': 'PUT', 'body': request})
        self.server.profile_updates.setdefault(path.removeprefix('/profile/'), {}).update(request)
        self.respond_llsd({'success': True})

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        if size < 0 or size > 65536:
            self.send_error(413)
            return
        payload = self.rfile.read(size)
        path = urlsplit(self.path).path
        if path == '/script-data':
            expected = b'default { state_entry() { llOwnerSay("Native edited script"); }}'
            metadata = self.server.upload_requests.get('script')
            if not metadata or payload.rstrip(b'\0') != expected:
                self.send_error(400)
                return
            if self.server.upload_directory:
                self.server.upload_directory.mkdir(parents=True, exist_ok=True)
                (self.server.upload_directory / 'script.lsl').write_bytes(payload)
            self.server.requests.append(dict(path=path, method='POST', bytes=len(payload),
                sha256=hashlib.sha256(payload).hexdigest(), metadata=metadata))
            self.respond_llsd(dict(state='complete', new_asset='60000000-0000-0000-0000-000000000010',
                compiled=True, errors=[]))
            return
        if path.startswith('/upload-data/'):
            kind = path.removeprefix('/upload-data/')
            metadata = self.server.upload_requests.get(kind)
            valid_signature = (kind == 'texture' and payload.startswith(bytes.fromhex('ff4fff51'))) or (
                kind == 'sound' and payload.startswith(b'OggS') and b'vorbis' in payload[:128])
            if not metadata or not valid_signature:
                self.send_error(400)
                return
            if self.server.upload_directory:
                self.server.upload_directory.mkdir(parents=True, exist_ok=True)
                (self.server.upload_directory / (kind + ('.j2c' if kind == 'texture' else '.ogg'))).write_bytes(payload)
            self.server.requests.append(dict(path=path, method='POST', bytes=len(payload),
                                             sha256=hashlib.sha256(payload).hexdigest(),
                                             signature=payload[:16].hex(), metadata=metadata))
            suffix = '002' if kind == 'texture' else '003'
            asset_suffix = '007' if kind == 'texture' else '008'
            self.respond_llsd(dict(state='complete', new_inventory_item='71000000-0000-0000-0000-000000000' + suffix,
                                   new_asset='60000000-0000-0000-0000-000000000' + asset_suffix,
                                   upload_price=0, new_next_owner_mask=2147483647,
                                   new_group_mask=0, new_everyone_mask=0))
            return
        if path == '/login':
            params, method = xmlrpc.client.loads(payload)
            if method != 'login_to_simulator' or len(params) != 1:
                self.send_error(400)
                return
            query = parse_qs(urlsplit(self.path).query)
            port = int(query['sim_port'][0])
            if not 0 < port < 65536:
                self.send_error(400)
                return
            self.server.auth_requests += 1
            # Keep fixture credentials/tokens out of archived request evidence.
            self.server.requests.append({'path': '/login', 'method': method,
                                         'attempt': self.server.auth_requests})
            response = dict(login='true',
                            agent_id='10000000-0000-0000-0000-000000000001',
                            session_id='20000000-0000-0000-0000-000000000002',
                            secure_session_id='70000000-0000-0000-0000-000000000007',
                            first_name='Native', last_name='Replay',
                            sim_ip='127.0.0.1', sim_port=port,
                            region_x=256, region_y=512, circuit_code=123456,
                            seed_capability=self.server.url + '/seed', message='Loopback login',
                            account_type='Basic',
                            account_level_benefits=dict(animated_object_limit=2, animation_upload_cost=0,
                                attachment_limit=38, create_group_cost=0, group_membership_limit=42,
                                picks_limit=10, sound_upload_cost=0, texture_upload_cost=0),
                            premium_packages={name: dict(benefits=dict(animated_object_limit=2,
                                animation_upload_cost=0, attachment_limit=38, create_group_cost=0,
                                group_membership_limit=42, picks_limit=10, sound_upload_cost=0,
                                texture_upload_cost=0)) for name in ('Base', 'Premium')},
                            **{'inventory-root': [{'folder_id': INVENTORY_ROOT}],
                               'inventory-skeleton': INVENTORY_SKELETON,
                               'inventory-lib-root': [{'folder_id': LIBRARY_ROOT}],
                               'inventory-lib-owner': [{'agent_id': LIBRARY_OWNER}],
                               'inventory-skel-lib': [dict(folder_id=LIBRARY_ROOT,
                                   parent_id='00000000-0000-0000-0000-000000000000',
                                   name='Library', type_default=8, version=1)],
                               'buddy-list': [{'buddy_id': '50000000-0000-0000-0000-000000000005',
                                               'buddy_rights_given': 1, 'buddy_rights_has': 1}]})
            if self.server.auth_requests == 1 or params[0].get('read_critical') not in (True, 1):
                response = dict(login='false', reason='critical', message='Loopback authentication policy acknowledgement')
            data = xmlrpc.client.dumps((response,), methodresponse=True).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/xml')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        request = read_llsd(ET.fromstring(payload))
        self.server.requests.append({'path': self.path, 'request': request})
        if path == '/metadata':
            if request.get('fields') != ['experience'] or not request.get('item-id'):
                self.send_error(400)
                return
            # This fixture's inventory scripts have no associated experience.
            self.respond_llsd({})
            return
        if path == '/script-request':
            if request.get('item_id') != '71000000-0000-0000-0000-000000000004' or request.get('target') not in ('mono', 'lsl2'):
                self.send_error(400)
                return
            self.server.upload_requests['script'] = request
            self.respond_llsd(dict(state='upload', uploader=self.server.url + '/script-data'))
            return
        if path == '/upload-request':
            kind = request.get('asset_type')
            expected_name = 'Native uploaded ' + str(kind)
            if kind not in ('texture', 'sound') or request.get('inventory_type') != kind or (
                    request.get('folder_id') != '70000000-0000-0000-0000-000000000003') or (
                    request.get('name') != expected_name) or not isinstance(request.get('expected_upload_cost'), int) or (
                    request['expected_upload_cost'] < 0):
                self.send_error(400)
                return
            self.server.upload_requests[kind] = request
            self.respond_llsd(dict(state='upload', uploader=self.server.url + '/upload-data/' + kind))
            return
        if self.path == '/seed':
            self.server.seed_requests += 1
            if not {'EventQueueGet', 'GetDisplayNames', 'ChatSessionRequest', 'GetTexture', 'AvatarPickerSearch'}.issubset(request):
                self.send_error(400)
                return
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'GetDisplayNames'
            ET.SubElement(mapping, 'string').text = self.server.url + '/names/'
            ET.SubElement(mapping, 'key').text = 'UntrustedSimulatorMessage'
            ET.SubElement(mapping, 'string').text = self.server.url + '/message'
            for name, route in [('UserInfo', '/user-info'), ('UpdateAgentInformation', '/agent-info'),
                                ('UpdateAgentLanguage', '/agent-language'), ('AgentProfile', '/profile'),
                                ('AvatarPickerSearch', '/avatar-picker'),
                                ('UploadAgentProfileImage', '/profile-upload'),
                                ('NewFileAgentInventory', '/upload-request'),
                                ('UpdateScriptAgent', '/script-request'), ('ViewerAsset', '/asset'),
                                ('GetMetadata', '/metadata')]:
                ET.SubElement(mapping, 'key').text = name
                ET.SubElement(mapping, 'string').text = self.server.url + route
            ET.SubElement(mapping, 'key').text = 'EventQueueGet'
            ET.SubElement(mapping, 'string').text = self.server.url + '/events'
            ET.SubElement(mapping, 'key').text = 'FetchInventory2'
            ET.SubElement(mapping, 'string').text = self.server.url + '/items'
            ET.SubElement(mapping, 'key').text = 'FetchInventoryDescendents2'
            ET.SubElement(mapping, 'string').text = self.server.url + '/inventory'
            ET.SubElement(mapping, 'key').text = 'GetTexture'
            ET.SubElement(mapping, 'string').text = self.server.url + '/texture'
            ET.SubElement(mapping, 'key').text = 'ChatSessionRequest'
            ET.SubElement(mapping, 'string').text = self.server.url + '/chat'
        elif self.path == '/items':
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'agent_id'
            ET.SubElement(mapping, 'uuid').text = '10000000-0000-0000-0000-000000000001'
            ET.SubElement(mapping, 'key').text = 'items'
            items = ET.SubElement(mapping, 'array')
            for item in request.get('items', []):
                script = item['item_id'] == '71000000-0000-0000-0000-000000000004'
                result = ET.SubElement(items, 'map')
                for key, tag, value in [('item_id', 'uuid', item['item_id']),
                                        ('parent_id', 'uuid', '70000000-0000-0000-0000-000000000009' if script else '70000000-0000-0000-0000-000000000003'),
                                        ('asset_id', 'uuid', '60000000-0000-0000-0000-000000000009' if script else '60000000-0000-0000-0000-000000000006'),
                                        ('type', 'string', 'lsltext' if script else 'texture'), ('inv_type', 'string', 'lsl' if script else 'texture'),
                                        ('name', 'string', 'Native script' if script else 'Native received texture'), ('desc', 'string', 'Loopback inventory asset'),
                                        ('flags', 'integer', '0'), ('created_at', 'integer', '0')]:
                    ET.SubElement(result, 'key').text = key; ET.SubElement(result, tag).text = value
                ET.SubElement(result, 'key').text = 'permissions'; permissions = ET.SubElement(result, 'map')
                for key in ('creator_id', 'owner_id', 'last_owner_id'):
                    ET.SubElement(permissions, 'key').text = key
                    ET.SubElement(permissions, 'uuid').text = '10000000-0000-0000-0000-000000000001'
                for key, value in [('base_mask', '2147483647'), ('owner_mask', '2147483647'),
                                   ('next_owner_mask', '2147483647'), ('group_mask', '0'), ('everyone_mask', '0')]:
                    ET.SubElement(permissions, 'key').text = key; ET.SubElement(permissions, 'integer').text = value
                ET.SubElement(result, 'key').text = 'sale_info'; sale = ET.SubElement(result, 'map')
                ET.SubElement(sale, 'key').text = 'sale_type'; ET.SubElement(sale, 'string').text = 'not'
                ET.SubElement(sale, 'key').text = 'sale_price'; ET.SubElement(sale, 'integer').text = '0'
        elif self.path == '/inventory':
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'folders'
            folders = ET.SubElement(mapping, 'array')
            for folder in request.get('folders', []):
                children = [dict(folder_id=item['folder_id'], parent_id=item['parent_id'],
                                 name=item['name'], type=item['type_default'], version=1)
                            for item in INVENTORY_SKELETON if item['parent_id'] == folder['folder_id']]
                result = ET.SubElement(folders, 'map')
                for key, tag, value in [('folder_id', 'uuid', folder['folder_id']),
                                        ('owner_id', 'uuid', folder['owner_id']),
                                        ('version', 'integer', '1'), ('descendents', 'integer', str(len(children)))]:
                    ET.SubElement(result, 'key').text = key; ET.SubElement(result, tag).text = value
                ET.SubElement(result, 'key').text = 'categories'; append_llsd(result, children)
                ET.SubElement(result, 'key').text = 'items'; ET.SubElement(result, 'array')
        elif self.path == '/message':
            root = ET.Element('llsd'); append_llsd(root, {})
        elif self.path in ('/user-info', '/agent-info', '/agent-language'):
            root = ET.Element('llsd'); append_llsd(root, dict(request, success=True))
        elif self.path == '/chat':
            root = ET.Element('llsd')
            if request.get('method') == 'fetch history':
                ET.SubElement(root, 'array')
            else:
                mapping = ET.SubElement(root, 'map')
                ET.SubElement(mapping, 'key').text = 'agents'
                agents = ET.SubElement(mapping, 'array')
                for agent in ('10000000-0000-0000-0000-000000000001', '50000000-0000-0000-0000-000000000005'):
                    ET.SubElement(agents, 'uuid').text = agent
                ET.SubElement(mapping, 'key').text = 'session_info'
                ET.SubElement(mapping, 'map')
        elif self.path == '/events':
            self.server.event_requests += 1
            time.sleep(.1)
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'id'
            ET.SubElement(mapping, 'integer').text = str(self.server.event_requests)
            ET.SubElement(mapping, 'key').text = 'events'
            events = ET.SubElement(mapping, 'array')
            if self.server.event_requests == 1:
                event = ET.SubElement(events, 'map')
                ET.SubElement(event, 'key').text = 'message'
                ET.SubElement(event, 'string').text = 'ObjectUpdate'
                ET.SubElement(event, 'key').text = 'body'
                ET.SubElement(event, 'map')
            if self.server.event_requests >= 1:
                event = ET.SubElement(events, 'map')
                ET.SubElement(event, 'key').text = 'message'
                ET.SubElement(event, 'string').text = 'ChatFromSimulator'
                ET.SubElement(event, 'key').text = 'body'
                body = ET.SubElement(event, 'map')
                ET.SubElement(body, 'key').text = 'ChatData'
                row = ET.SubElement(ET.SubElement(body, 'array'), 'map')
                for name, tag, value in (
                    ('FromName', 'string', 'HTTP sender'),
                    ('SourceID', 'uuid', '60000000-0000-0000-0000-000000000006'),
                    ('OwnerID', 'uuid', '60000000-0000-0000-0000-000000000006'),
                    ('SourceType', 'integer', '1'), ('ChatType', 'integer', '1'),
                    ('Audible', 'integer', '1'), ('Message', 'string', 'Native event queue chat')):
                    ET.SubElement(row, 'key').text = name
                    ET.SubElement(row, tag).text = value
        else:
            self.send_error(404)
            return
        data = ET.tostring(root, encoding='utf-8')
        try:
            self.send_response(200)
            self.send_header('Content-Type', 'application/llsd+xml')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # The production owner cancels suspended HTTP during reset.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--harness-evidence', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--register-windows-manifests', action='store_true')
    parser.add_argument('--skin', default='default')
    parser.add_argument('--theme', default='')
    parser.add_argument('--language', default='en')
    parser.add_argument('--ui-scale', type=float, default=1.0,
                        help='Viewer UI scale used for connected skin/readback qualification')
    parser.add_argument('--all-skins', action='store_true', help='Run the complete connected replay for every packaged skin/theme and German overlay')
    args = parser.parse_args()
    if not .75 <= args.ui_scale <= 4:
        parser.error('--ui-scale must be between 0.75 and 4')
    windows = platform.system() == 'Windows'
    build = args.build_directory.resolve()
    stage = build / 'newview' / ('RelWithDebInfo' if windows else 'packaged')
    if args.all_skins:
        directory = args.evidence.resolve()
        directory.mkdir(parents=True, exist_ok=True)
        results = {}
        cases = {name: (*selection, 1.0) for name, selection in skin_cases(stage).items()}
        cases['skin-ansastorm_modern-125percent'] = ('ansastorm_modern', '', 'en', 1.25)
        cases['skin-ansastorm_modern-150percent'] = ('ansastorm_modern', '', 'en', 1.5)
        for name, (skin, theme, language, ui_scale) in cases.items():
            child = directory / name
            command = [sys.executable, str(Path(__file__).resolve()),
                       '--build-directory', str(build), '--runtime', str(args.runtime.resolve()),
                       '--harness-evidence', str(args.harness_evidence.resolve()), '--evidence', str(child),
                       '--skin', skin, '--theme', theme, '--language', language,
                       '--ui-scale', str(ui_scale)]
            if args.register_windows_manifests:
                command.append('--register-windows-manifests')
            try:
                code = subprocess.run(command, timeout=230).returncode
                record = json.loads((child / 'results.json').read_text())
                results[name] = dict(passed=code == 0 and record.get('passed') is True,
                                     exit_code=code, evidence=str(child))
            except (subprocess.TimeoutExpired, OSError, ValueError) as error:
                results[name] = dict(passed=False, error=str(error), evidence=str(child))
        passed = bool(results) and all(record['passed'] for record in results.values())
        (directory / 'results.json').write_text(json.dumps(dict(passed=passed, cases=results,
                                                               live_server_qualified=False), indent=2) + '\n')
        print(('PASS' if passed else 'FAIL') + ': connected UI skin matrix')
        return 0 if passed else 1
    binaries = list(stage.glob('*-bin.exe')) if windows else list((stage / 'bin').glob('*-bin'))
    if len(binaries) != 1 or 'VS_VULKAN_DIAGNOSTICS:BOOL=ON' not in (build / 'CMakeCache.txt').read_text():
        raise RuntimeError('Expected one diagnostic-enabled fully staged viewer')
    executable = binaries[0]
    staging = json.loads((build / 'newview/vulkan-ci-staging.json').read_text())
    if staging['files'].get(str(executable.relative_to(stage))) != hashlib.sha256(executable.read_bytes()).hexdigest():
        raise RuntimeError('Viewer staging checksum differs')
    runtime = json.loads(args.runtime.read_text())
    root = Path(__file__).resolve().parents[1]
    if runtime['driver'] not in ('swiftshader', 'lavapipe') or runtime['sources'] != json.loads((root / 'scripts/vulkan_ci_dependencies.json').read_text()):
        raise RuntimeError('Runtime pins differ from the software CI lock')
    for name, digest in runtime['staged_sha256'].items():
        if hashlib.sha256((args.runtime.resolve().parent / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Runtime checksum mismatch: ' + name)
    harness = json.loads(args.harness_evidence.read_text())
    if not harness.get('passed') or not harness.get('presentation_qualified'):
        raise RuntimeError('Missing prerequisite native presentation evidence')
    directory = args.evidence.resolve(); directory.mkdir(parents=True, exist_ok=True)
    for name in ('session-replay.xml', 'session-chat.ppm', 'session-chat-expected.ppm',
                 'im/session-chat.ppm', 'im/session-chat-expected.ppm', 'results.json'):
        (directory / name).unlink(missing_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('VS_VULKAN_DIAGNOSTIC')}
    for key in ('VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES', 'VK_ADD_LAYER_PATH', 'VK_INSTANCE_LAYERS',
                'VK_LOADER_DRIVERS_SELECT', 'VK_LOADER_DRIVERS_DISABLE', 'VK_LAYER_ENABLES', 'VK_LAYER_DISABLES'):
        env.pop(key, None)
    if not windows:
        env['SDL_VIDEODRIVER'] = 'x11'
        env['LD_LIBRARY_PATH'] = str(stage / 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    env.update(VK_DRIVER_FILES=runtime['icd'], VK_LAYER_PATH=runtime['layer_path'],
               VK_LOADER_LAYERS_DISABLE='~implicit~', VK_LOADER_DEBUG='error,warn,driver,layer',
               VS_VULKAN_DIAGNOSTIC_REPLAY=str(directory))
    layer = directory / 'vk_layer_settings.txt'; layer.write_text('khronos_validation.validate_sync = true\n')
    env['VK_LAYER_SETTINGS_PATH'] = str(layer)
    command = [str(executable), '--settings', 'vs_native_session_ci.xml', '--set', 'ClientSettingsFile',
               str(directory / 'settings.xml'), '--set', 'RenderBackend', 'Vulkan', '--set', 'AutoLogin', 'false',
               '--set', 'QuitAfterSeconds', '180', '--set', 'RenderDebugGLSession', 'true',
               '--set', 'UpdaterShowReleaseNotes', '0', '--set', 'FSShowWhitelistReminder', 'false']
    selection = ET.Element('llsd')
    append_llsd(selection, {name: dict(Type='String', Persist=0, Comment='Replay skin selection', Value=value)
                           for name, value in [('SkinCurrent', args.skin),
                                               ('SkinCurrentTheme', args.theme), ('Language', args.language)]})
    selection_file = directory / 'ui-selection.xml'
    selection_file.write_bytes(ET.tostring(selection, encoding='utf-8'))
    # Empty theme strings cannot survive the viewer's command-line tokenizer.
    command.extend(['--sessionsettings', str(selection_file)])
    command.extend(['--set', 'UIScaleFactor', str(args.ui_scale)])
    report = {}; log = ''; code = None; passed = False
    with SimulatorHTTP() as server:
        server.upload_directory = directory / 'upload-bodies'
        worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
        env['VS_VULKAN_REPLAY_SEED'] = server.url + '/seed'
        env['VS_VULKAN_REPLAY_LOGIN'] = server.url + '/login'
        try:
            with windows_manifest_registration(runtime, args.register_windows_manifests):
                process = subprocess.run(command, cwd=stage, env=env, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, timeout=200)
            code = process.returncode; log = process.stdout.decode('utf-8', errors='replace')
            report = read_llsd(ET.parse(directory / 'session-replay.xml').getroot())
            passed = assess(report, code, log)
            for suffix in ('', '-expected'):
                if (directory / ('session-chat' + suffix + '.ppm')).stat().st_size < 640 * 480 * 3:
                    raise RuntimeError('Missing native chat readback')
                if (directory / 'im' / ('session-chat' + suffix + '.ppm')).stat().st_size < 640 * 480 * 3:
                    raise RuntimeError('Missing native conversation UI readback')
            if 'Machine Vulkan adapter: ' + harness['device']['name'] not in log:
                raise RuntimeError('Unexpected Vulkan adapter')
            libraries = loaded_library_hashes(log)
            for name, expected in harness['staged_library_sha256'].items():
                path = str(((stage if windows else stage / 'lib') / name).resolve())
                if libraries.get(path) != expected:
                    raise RuntimeError('Unexpected GHI dependency: ' + name)
            for library in runtime_libraries(runtime):
                if libraries.get(str(library.resolve())) != hashlib.sha256(library.read_bytes()).hexdigest():
                    raise RuntimeError('Viewer did not load the pinned ICD/validation library: ' + str(library))
            report['loaded_library_sha256'] = libraries
            profile_writes = [request.get('body', {}) for request in server.requests
                              if request.get('method') == 'PUT'
                              and request.get('path') == '/profile/10000000-0000-0000-0000-000000000001']
            report['profile_image_writes'] = {field: any(body.get(field) == '60000000-0000-0000-0000-000000000006'
                                                         for body in profile_writes)
                                             for field in ('sl_image_id', 'fl_image_id')}
            if not all(report['profile_image_writes'].values()):
                raise RuntimeError('Missing real self-profile image update requests')
            report['inventory_uploads'] = {}
            for kind, extension in (('texture', '.j2c'), ('sound', '.ogg')):
                receipts = [record for record in server.requests if record['path'] == '/upload-data/' + kind]
                body_path = server.upload_directory / (kind + extension)
                if len(receipts) != 1 or not body_path.is_file() or (
                        hashlib.sha256(body_path.read_bytes()).hexdigest() != receipts[0]['sha256']):
                    raise RuntimeError('Missing or inconsistent actual inventory upload bytes: ' + kind)
                report['inventory_uploads'][kind] = dict(bytes=receipts[0]['bytes'], sha256=receipts[0]['sha256'])
            script_receipts = [record for record in server.requests if record['path'] == '/script-data']
            script_path = server.upload_directory / 'script.lsl'
            if len(script_receipts) != 1 or not script_path.is_file() or (
                    hashlib.sha256(script_path.read_bytes()).hexdigest() != script_receipts[0]['sha256']):
                raise RuntimeError('Missing or inconsistent actual script upload bytes')
            report['inventory_uploads']['script'] = dict(bytes=script_receipts[0]['bytes'], sha256=script_receipts[0]['sha256'])

            passed = passed and server.auth_requests >= 3 and server.seed_requests >= 2 and server.event_requests >= 1
        except Exception as error:
            if isinstance(error, subprocess.TimeoutExpired):
                log = (error.stdout or b'').decode('utf-8', errors='replace') + '\nVIEWER TIMEOUT\n'
            log += '\nREPLAY EVIDENCE FAILURE: ' + repr(error)
            passed = False
        finally:
            server.shutdown(); worker.join()
        (directory / 'http-replay.json').write_text(json.dumps(server.requests, indent=2) + '\n')
    (directory / 'viewer.log').write_text(log, encoding='utf-8')
    report.update(passed=passed, exit_code=code, live_server_qualified=False)
    (directory / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(('PASS' if passed else 'FAIL') + ': viewer connected-session replay')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
