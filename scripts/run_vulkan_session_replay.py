"""Replay the native CPU session through the fully staged viewer, without grid credentials."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import xml.etree.ElementTree as ET

from run_software_vulkan_tests import windows_manifest_registration, loaded_library_hashes
from run_vulkan_viewer_diagnostic import runtime_libraries

REQUIRED = ('malformed', 'unknown', 'wrong_host', 'udp_chat', 'http_gate', 'udp_gate',
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
                                                 'Forbidden GL path', 'VIEWER TIMEOUT')))


class SimulatorHTTP(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.seed_requests = 0
        self.event_requests = 0
        self.requests = []

    @property
    def url(self):
        return 'http://127.0.0.1:' + str(self.server_address[1])


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        if size < 0 or size > 65536:
            self.send_error(413)
            return
        request = read_llsd(ET.fromstring(self.rfile.read(size)))
        self.server.requests.append({'path': self.path, 'request': request})
        if self.path == '/seed':
            self.server.seed_requests += 1
            if request != ['EventQueueGet', 'GetDisplayNames']:
                self.send_error(400)
                return
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'EventQueueGet'
            ET.SubElement(mapping, 'string').text = self.server.url + '/events'
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
    args = parser.parse_args()
    windows = platform.system() == 'Windows'
    build = args.build_directory.resolve()
    stage = build / 'newview' / ('RelWithDebInfo' if windows else 'packaged')
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
    for name in ('session-replay.xml', 'session-chat.ppm', 'session-chat-expected.ppm', 'results.json'):
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
               '--set', 'QuitAfterSeconds', '80', '--set', 'RenderDebugGLSession', 'true',
               '--set', 'UpdaterShowReleaseNotes', '0', '--set', 'FSShowWhitelistReminder', 'false']
    report = {}; log = ''; code = None; passed = False
    with SimulatorHTTP() as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
        env['VS_VULKAN_REPLAY_SEED'] = server.url + '/seed'
        try:
            with windows_manifest_registration(runtime, args.register_windows_manifests):
                process = subprocess.run(command, cwd=stage, env=env, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, timeout=100)
            code = process.returncode; log = process.stdout.decode('utf-8', errors='replace')
            report = read_llsd(ET.parse(directory / 'session-replay.xml').getroot())
            passed = assess(report, code, log)
            for suffix in ('', '-expected'):
                if (directory / ('session-chat' + suffix + '.ppm')).stat().st_size < 640 * 480 * 3:
                    raise RuntimeError('Missing native chat readback')
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
            passed = passed and server.seed_requests >= 2 and server.event_requests >= 1
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
