"""Capture actual peer viewer frames without credentials or foreground fallback."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import shutil
import subprocess
import sys
import threading
from itertools import product
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

from run_vulkan_session_replay import isolated_profile_environment, read_llsd, append_llsd
from vulkan_viewer_process import run_viewer_process, process_isolation_evidence
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class BaselineLoginPage:
    """Serve identical immutable browser input to the actual peer viewers."""
    def __init__(self, evidence, contract):
        self.evidence = Path(evidence)
        self.spec = contract['login_page_fixture']
        self.content = (ROOT/'doc/vulkan/parity'/self.spec['file']).read_bytes().replace(b'\r\n', b'\n')
        if hashlib.sha256(self.content).hexdigest() != self.spec['sha256']:
            raise RuntimeError('Changed baseline login page fixture')
        (self.evidence/'login-page.html').write_bytes(self.content)
        self.requests = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'
            def do_GET(self):
                if urlsplit(self.path).path != '/login-page.html':
                    self.send_error(404)
                    return
                owner.requests.append(self.path)
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(owner.content)))
                self.send_header('Cache-Control', 'no-store')
                self.end_headers()
                self.wfile.write(owner.content)
                self.wfile.flush()

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = 'http://127.0.0.1:%d/login-page.html' % self.server.server_port
        self.thread = threading.Thread(target=lambda: self.server.serve_forever(poll_interval=.05), daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        (self.evidence/'login-page-requests.json').write_text(json.dumps(
            dict(url=self.url, sha256=self.spec['sha256'], requests=self.requests), indent=2)+'\n')
        if self.thread.is_alive():
            raise RuntimeError('Baseline login page server did not stop')


def startup_page_pixels(path, spec):
    """Require the actual browser paint, not a stable blank or error page."""
    (width, height), pixels = read_ppm(path)
    counts = dict(background=0, upper=0, lower=0)
    colors = {bytes(color):name for name in counts for color in
              product(*(range(max(0,c-1),min(255,c+1)+1) for c in spec[name]))}
    for y in range(height):
        for x in range(width//4, width):
            offset = (y*width+x)*3
            name = colors.get(pixels[offset:offset+3])
            if name is not None and not (name == 'upper' and y >= height//2 or name == 'lower' and y < height//2):
                counts[name] += 1
    if counts['background'] < 8192 or counts['upper'] < 1024 or counts['lower'] < 1024:
        raise ValueError('Missing painted baseline login page witnesses: '+str(counts))
    return counts


def read_ppm(path):
    data = Path(path).read_bytes()
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not match: raise ValueError('Missing canonical RGB8 PPM header')
    width, height = map(int, match.groups())
    pixels = data[match.end():]
    if width < 1 or height < 1 or len(pixels) != width * height * 3:
        raise ValueError('Incomplete RGB8 readback')
    return (width, height), pixels


def compare(left, right, tolerance):
    shape, a = read_ppm(left)
    other, b = read_ppm(right)
    if shape != other: raise ValueError('Readback extents differ')
    error = [abs(x-y) for x, y in zip(a, b)]
    return dict(width=shape[0], height=shape[1],
                maximum_channel_error=max(error), mean_absolute_error=sum(error)/len(error),
                channels_over_tolerance=sum(e > tolerance for e in error),
                matched=all(e <= tolerance for e in error))


def fixture_pixels(path):
    """Require each known tint in its projected half, independent of draw counters."""
    (width,height),pixels=read_ppm(path)
    counts={'red_right':0,'green_left':0}
    for y in range(height//3,2*height//3):
        for x in range(width):
            i=(y*width+x)*3;r,g,b=pixels[i:i+3]
            if x>width//2 and r>=40 and r>1.25*g and r>b+8: counts['red_right']+=1
            if x<width//2 and g>=40 and g>1.25*r and g>b+8: counts['green_left']+=1
    if min(counts.values())<32: raise ValueError('Missing visible projected red/green fixture objects: '+str(counts))
    return counts


def verify_vulkan_driver(runtime, loaded_paths):
    """Require this viewer to load the checksum-pinned ICD, not just a matching name."""
    icd=Path(runtime['icd']).resolve()
    library=Path(json.loads(icd.read_text())['ICD']['library_path'])
    if not library.is_absolute(): library=icd.parent/library
    library=library.resolve()
    root=icd.parents[2]
    if not library.is_relative_to(root): raise RuntimeError('Selected ICD is outside the staged runtime')
    expected=runtime['staged_sha256'].get(library.relative_to(root).as_posix())
    if not expected or hashlib.sha256(library.read_bytes()).hexdigest()!=expected:
        raise RuntimeError('Selected Vulkan driver differs from its immutable runtime identity')
    selected=str(library).casefold()
    if not any(line.casefold()==selected or line.casefold().endswith(' '+selected)
               for line in loaded_paths.splitlines()):
        raise RuntimeError('Viewer did not load the selected pinned Vulkan driver')


def metrics(frames):
    result = {}
    for name in ('pre_render_cpu_ms', 'render_submit_cpu_ms', 'capture_wait_and_io_ms'):
        values = [float(f[name]) for f in frames]
        if not values or not all(math.isfinite(v) and v >= 0 for v in values):
            raise ValueError('Invalid/missing metric: ' + name)
        ordered = sorted(values)
        result[name] = dict(count=len(values), median=statistics.median(values),
            p95=ordered[math.ceil(.95*len(values))-1], maximum=max(values))
    result['gpu_ms'] = None
    for name in ('presentation_cpu_ms','observed_total_frame_cpu_ms'):
        if all(name in f for f in frames):
            values=[float(f[name]) for f in frames]
            if not all(math.isfinite(v) and v>=0 for v in values): raise ValueError('Invalid '+name)
            result[name]=dict(count=len(values),median=statistics.median(values),maximum=max(values))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-directory', required=True, type=Path)
    p.add_argument('--runtime', required=True, type=Path)
    p.add_argument('--evidence', required=True, type=Path)
    p.add_argument('--opengl-runtime', type=Path,
                   help='Checksum-verified separately built llvmpipe runtime (required on Linux)')
    p.add_argument('--scene', choices=('startup','world'), default='startup')
    args = p.parse_args()
    contract_path = ROOT/'doc/vulkan/parity/baseline-contract.json'
    contract = json.loads(contract_path.read_text())
    scene_path=ROOT/'doc/vulkan/parity/static-world-scene.json' if args.scene=='world' else None
    build = args.build_directory.resolve()
    stage = build/'newview'/('RelWithDebInfo' if os.name == 'nt' else 'packaged')
    executables = list(stage.glob('*-bin.exe')) if os.name == 'nt' else list((stage/'bin').glob('*-bin'))
    if len(executables) != 1: raise RuntimeError('Exactly one completely staged viewer is required')
    runtime = json.loads(args.runtime.read_text())
    gl_runtime = json.loads(args.opengl_runtime.read_text()) if args.opengl_runtime else None
    if os.name != 'nt' and gl_runtime is None:
        raise RuntimeError('Pinned independent software OpenGL runtime required')
    if gl_runtime:
        pin=json.loads((ROOT/'scripts/vulkan_ci_dependencies.json').read_text())['lavapipe']
        if gl_runtime['source']!=pin: raise RuntimeError('Wrong Mesa reference revision')
        for name,expected in gl_runtime['libraries'].items():
            actual=Path(gl_runtime['install'])/name
            if hashlib.sha256(actual.read_bytes()).hexdigest()!=expected:
                raise RuntimeError('Changed reference library: '+name)
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    parent=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    changed=subprocess.check_output(['git','diff','--name-only','HEAD','--','indra','scripts'],cwd=ROOT,text=True).splitlines()
    changed+=subprocess.check_output(['git','ls-files','--others','--exclude-standard','indra','scripts'],cwd=ROOT,text=True).splitlines()
    source_hashes={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sorted(set(changed)) if (ROOT/s).is_file()}
    provenance_path=build/'parity-build-provenance.json'
    provenance=json.loads(provenance_path.read_text())
    executable_hash=hashlib.sha256(executables[0].read_bytes()).hexdigest()
    if not provenance.get('complete') or provenance.get('source_commit')!=parent or source_hashes or provenance.get('executable_sha256')!=executable_hash:
        raise RuntimeError('Staged executable is not bound to this committed source build')
    shutil.copy2(provenance_path,evidence/'build-provenance.json')
    output = dict(schema=1, stage=str(stage), build_provenance=provenance, source_commit=None if source_hashes else parent,
        working_tree_parent=parent, working_tree_source_sha256=source_hashes,
        contract_sha256=hashlib.sha256(contract_path.read_bytes()).hexdigest(),
        executable_sha256=hashlib.sha256(executables[0].read_bytes()).hexdigest(),
        runtime=runtime, opengl_runtime=gl_runtime, cases=[], r0_complete=False,
        qualification_limits=['Combined startup and offline-world evidence must pass the separate R0 gate; neither runner alone closes R0.'])
    assets={str(f.relative_to(stage)):hashlib.sha256(f.read_bytes()).hexdigest()
            for folder in ('skins','fonts','app_settings','lib','llplugin','bin/llplugin')
            for f in (stage/folder).rglob('*') if f.is_file()}
    if not assets: raise RuntimeError('Missing staged baseline assets')
    output['asset_sha256']=assets
    with BaselineLoginPage(evidence, contract) as login_page:
        for backend in ('OpenGL', 'Vulkan'):
            directory = evidence/backend.lower()
            directory.mkdir(exist_ok=True)
            # Reject old evidence rather than accepting it after a failed process.
            for pattern in ('frame-*.ppm', 'viewer.xml', 'loaded-library-paths.txt','scene-input.json','scene-messages.xml'):
                for old in directory.glob(pattern): old.unlink()
            env = {k:v for k,v in os.environ.items() if not k.startswith(('VS_VULKAN_DIAGNOSTIC', 'VS_NATIVE_REPLAY', 'VS_PARITY_'))}
            env, profile = isolated_profile_environment(env, directory, os.name == 'nt')
            env['VS_PARITY_BASELINE_DIRECTORY'] = str(directory)
            env['VS_PARITY_BASELINE_LOGIN_URL'] = login_page.url
            page_requests_before = len(login_page.requests)
            if scene_path: env['VS_PARITY_SCENE_FILE']=str(scene_path)
            if os.name != 'nt':
                env['LD_LIBRARY_PATH'] = str(stage/'lib') + os.pathsep + env.get('LD_LIBRARY_PATH','')
                env['SDL_VIDEODRIVER'] = 'x11'
            if backend == 'OpenGL':
                env.update(LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe')
                if gl_runtime:
                    env['LD_LIBRARY_PATH']=gl_runtime['library_directory']+os.pathsep+env.get('LD_LIBRARY_PATH','')
                    env['LIBGL_DRIVERS_PATH']=gl_runtime['dri_directory']
            else:
                env.update(VK_DRIVER_FILES=runtime['icd'], VK_LAYER_PATH=runtime['layer_path'],
                    VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LOADER_DEBUG='error,warn,driver,layer')
                (directory/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n')
                env['VK_LAYER_SETTINGS_PATH'] = str(directory)
            command = [str(executables[0]), '--settings', 'vs_parity_baseline.xml',
                '--set', 'ClientSettingsFile', str(directory/'settings.xml'),
                '--set', 'RenderBackend', backend, '--set', 'AutoLogin', 'false',
                '--set', 'RenderDebugGLSession', 'true',
                '--set', 'QuitAfterSeconds', '170', '--set', 'RenderVSyncEnable', 'false',
                '--set', 'UpdaterShowReleaseNotes', '0', '--set', 'FSShowWhitelistReminder', 'false',
                '--set', 'CacheLocation', str(profile/'cache'), '--set', 'UIScaleFactor', '1.0',
                '--set', 'WindowWidth', '1024', '--set', 'WindowHeight', '768']
            selection=ET.Element('llsd')
            append_llsd(selection,{name:dict(Type='String',Persist=0,Comment='Baseline fixed UI input',Value=value)
                for name,value in [('SkinCurrent',contract['skin']),('SkinCurrentTheme',contract['theme']),('Language',contract['language']),('ForceLoginURL',login_page.url)]})
            selection_file=directory/'ui-selection.xml'
            selection_file.write_bytes(ET.tostring(selection,encoding='utf-8'))
            command.extend(['--sessionsettings',str(selection_file)])
            case = dict(backend=backend, passed=False, login_page_url=login_page.url)
            try:
                process = run_viewer_process(command, cwd=stage, env=env, timeout=contract['timeout_seconds'])
                case['returncode'] = process.returncode
                case['process_isolation'] = process_isolation_evidence(process)
                log = process.stdout.decode('utf-8', errors='replace')
                (directory/'viewer.log').write_text(log, encoding='utf-8')
                if process.returncode: raise RuntimeError('Viewer process failed')
                viewer = read_llsd(ET.parse(directory/'viewer.xml').getroot())
                if viewer['backend'] != backend: raise RuntimeError('Unexpected viewer backend')
                if backend=='Vulkan':
                    card=viewer['device']['GRAPHICS_CARD'].lower()
                    expected='swiftshader' if runtime['driver']=='swiftshader' else 'llvmpipe'
                    if expected not in card: raise RuntimeError('Unexpected software Vulkan device')
                    if re.search(r'Validation Error|SYNC-HAZARD|VUID-',log): raise RuntimeError('Vulkan validation failure')
                    paths=(directory/'loaded-library-paths.txt').read_text()
                    verify_vulkan_driver(runtime,paths)
                    if 'VkLayer_khronos_validation' not in paths or not viewer['device'].get('VALIDATION_ENABLED'):
                        raise RuntimeError('Required validation layer/configuration witness missing')
                    if 'VK_LAYER_KHRONOS_validation' not in log:
                        raise RuntimeError('Missing loader validation-layer diagnostics')
                if backend == 'OpenGL' and 'llvmpipe' not in viewer['device']['renderer'].lower():
                    raise RuntimeError('Reference did not use software OpenGL')
                if backend == 'OpenGL' and gl_runtime:
                    if gl_runtime['mesa_version'] not in viewer['device']['version']:
                        raise RuntimeError('Unexpected software OpenGL version')
                    paths=(directory/'loaded-library-paths.txt').read_text()
                    if gl_runtime['install'] not in paths: raise RuntimeError('Pinned GL runtime not loaded')
                if not scene_path:
                    if (not viewer.get('login_page_fixture_ready') or len(login_page.requests) <= page_requests_before
                            or viewer['settings'].get('ForceLoginURL') != login_page.url):
                        raise RuntimeError('Missing actual painted loopback login page readiness')
                frames = viewer['frames']
                if len(frames) != contract['capture_frames']: raise RuntimeError('Missing baseline frames')
                if not all('presentation_cpu_ms' in f and 'observed_total_frame_cpu_ms' in f for f in frames):
                    raise RuntimeError('Missing presentation/frame measurements')
                if scene_path:
                    if viewer['scene']!='static-world-primitives-v1' or not viewer['fixed_world_clock_applied']:
                        raise RuntimeError('World fixture or fixed clock missing')
                    if viewer['render_clock']!={'elapsed_seconds':1000,'motion_delta_seconds':0,'frame':60,'epoch_seconds':1700000000}:
                        raise RuntimeError('Observed render clock differs from fixed replay clock')
                    if len(viewer['world_projection'])!=16 or not all(math.isfinite(v) for v in viewer['world_projection']):
                        raise RuntimeError('Missing observed world projection')
                    applied=viewer['effective_scene_inputs']
                    requested=json.loads(scene_path.read_text())
                    if applied['camera']!=requested['camera'] or applied['object_count']!=requested['simulation']['object_count'] or applied['motion_delta_seconds']!=0:
                        raise RuntimeError('Effective production scene inputs differ from declared fixture')
                    if (directory/'scene-input.json').read_bytes()!=scene_path.read_bytes():
                        raise RuntimeError('World input snapshot changed')
                    corpus=read_llsd(ET.parse(directory/'scene-messages.xml').getroot())
                    if len(corpus['objects'])!=2: raise RuntimeError('Missing actual simulator replay objects')
                    case['scene_corpus_sha256']=hashlib.sha256((directory/'scene-messages.xml').read_bytes()).hexdigest()
                    for f in frames:
                        draws=f['counters']['draw_calls'] if backend=='OpenGL' else f['stages']['draw_calls']
                        if draws<1: raise RuntimeError('Production viewer issued no scene draws')
                images = [directory/f['image'] for f in frames]
                for image in images:
                    if read_ppm(image)[0] != tuple(contract['viewport']): raise RuntimeError('Unexpected viewport')
                    if scene_path:
                        case.setdefault('fixture_pixel_witnesses',{})[image.name]=fixture_pixels(image)
                    else:
                        case.setdefault('login_page_pixel_witnesses',{})[image.name]=startup_page_pixels(image,contract['login_page_fixture'])
                comparisons = [compare(images[0], image, contract['comparison']['same_backend_max_channel_error']) for image in images[1:]]
                case.update(viewer=viewer, metrics=metrics(frames), repeatability=comparisons,
                    image_sha256={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in images},
                    passed=all(c['matched'] for c in comparisons))
            except (OSError, RuntimeError, ValueError, KeyError, ET.ParseError, subprocess.TimeoutExpired) as exc:
                case['failure'] = str(exc)
            output['cases'].append(case)
            print(('PASS' if case['passed'] else 'FAIL') + ': actual viewer ' + backend, flush=True)
    if all(c['passed'] for c in output['cases']):
        if scene_path and output['cases'][0]['scene_corpus_sha256']!=output['cases'][1]['scene_corpus_sha256']:
            output['cases'][1].update(passed=False,failure='Peer renderers received different actual scene data')
        output['reference_comparison'] = compare(evidence/'opengl/frame-00.ppm',
            evidence/'vulkan/frame-00.ppm', contract['comparison']['cross_backend_max_channel_error'])
        shape,left=read_ppm(evidence/'opengl/frame-00.ppm')
        _,right=read_ppm(evidence/'vulkan/frame-00.ppm')
        (evidence/'difference.ppm').write_bytes(f'P6\n{shape[0]} {shape[1]}\n255\n'.encode()+bytes(abs(a-b) for a,b in zip(left,right)))
    output['startup_baseline_measured'] = all(c['passed'] for c in output['cases'])
    output['world_baseline_measured']=bool(scene_path) and output['startup_baseline_measured']
    output['artifact_sha256']={str(f.relative_to(evidence)):hashlib.sha256(f.read_bytes()).hexdigest() for f in evidence.rglob('*') if f.is_file() and f.name!='baseline-results.json' and not any(part.startswith('profile-') for part in f.relative_to(evidence).parts)}
    (evidence/'baseline-results.json').write_text(json.dumps(output,indent=2)+'\n')
    return 0 if output['startup_baseline_measured'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError,RuntimeError,ValueError,KeyError,ET.ParseError) as exc:
        # Initialization failures must leave decisive structured evidence too.
        if '--evidence' in sys.argv:
            target=Path(sys.argv[sys.argv.index('--evidence')+1]);target.mkdir(parents=True,exist_ok=True)
            (target/'baseline-results.json').write_text(json.dumps(dict(schema=1,r0_complete=False,
                startup_baseline_measured=False,world_baseline_measured=False,cases=[],initialization_failure=str(exc)),indent=2)+'\n')
        print('FAIL: production baseline initialization: '+str(exc),file=sys.stderr)
        sys.exit(1)
