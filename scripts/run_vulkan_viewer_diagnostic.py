"""Qualify native presentation through the fully staged viewer executable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess

from run_software_vulkan_tests import windows_manifest_registration, loaded_library_hashes

ROOT = Path(__file__).resolve().parents[1]
STAGES = ['viewer-window-created', 'device-created', 'swapchain-created-320x240',
          'present-initial', 'resize-present-640x360', 'zero-extents-suspended',
          'minimized-suspended', 'restore-present-320x240', 'swapchain-released',
          'viewer-window-destroyed', 'device-context-released']
FAILURES = ('before-window', 'after-window', 'after-device', 'after-swapchain',
            'frame', 'shutdown', 'gl-trap', 'bad-clear')
UI_CASES = ('ui-positive', 'bad-ui', 'ui-orientation')


def runtime_libraries(runtime):
    manifests = [(Path(runtime['icd']), 'ICD'),
                 (Path(runtime['layer_path']) / 'VkLayer_khronos_validation.json', 'layer')]
    result = []
    for manifest, key in manifests:
        library = Path(json.loads(manifest.read_text())[key]['library_path'])
        result.append(library if library.is_absolute() else (manifest.parent / library).resolve())
    return result


def assess(record, code, log, case, system):
    if (record.get('schema') != 1 or record.get('mode') != 'viewer-native-diagnostic'
            or record.get('application_lifecycle') != 'LLAppViewer::init/frame/cleanup'
            or record.get('window_factory') != 'LLWindowManager::createWindow'
            or record.get('window_api') != ('Win32' if system == 'Windows' else 'SDL2/X11')
            or record.get('shutdown_complete') is not True
            or record.get('validation_errors') != 0
            or record.get('stages', [])[-3:] != STAGES[-3:]
            or any(int(n) >= 2 for n in re.findall(r'^DILIGENT (\d+):', log, re.M))):
        return False
    if case not in ('positive', 'ui-positive'):
        if case in UI_CASES and record.get('ui_fixture_enabled') is not True:
            return False
        expected = ('Viewer UI pixel oracle mismatch' if case in ('bad-ui', 'ui-orientation') else
                    'Viewer clear pixel oracle mismatch' if case == 'bad-clear' else
                    'GL presentation in native Vulkan window' if case == 'gl-trap'
                    else 'Injected failure: ' + case)
        return (code == 1 and record.get('passed') is False and record.get('failure') == expected
                and 'FAIL viewer-native-diagnostic' in log)
    if case == 'ui-positive' and (record.get('ui_fixture_enabled') is not True or
                                 record.get('ui_readback_verified') is not True or
                                 record.get('ui_readbacks') != 2):
        return False
    return (code == 0 and record.get('passed') is True and record.get('failure') == ''
            and record.get('stages') == STAGES and record.get('presented_frames') == 9
            and record.get('zero_extent_skips') == 3 and record.get('minimized_skips') == 2
            and type(record.get('native_minimize_observed')) is bool
            and (system != 'Windows' or record['native_minimize_observed'])
            and record.get('resize_events', 0) > 0
            and record.get('clear_readback_verified') is True
            and 'PASS viewer-native-diagnostic' in log)


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
    executable = list(stage.glob('*-bin.exe')) if windows else list((stage / 'bin').glob('*-bin'))
    if len(executable) != 1:
        raise RuntimeError('Expected exactly one fully staged viewer executable')
    executable = executable[0]
    if 'VS_VULKAN_DIAGNOSTICS:BOOL=ON' not in (build / 'CMakeCache.txt').read_text():
        raise RuntimeError('Viewer diagnostic mode is not enabled in this build')
    staging = json.loads((build / 'newview/vulkan-ci-staging.json').read_text())
    if staging['files'].get(str(executable.relative_to(stage))) != hashlib.sha256(executable.read_bytes()).hexdigest():
        raise RuntimeError('Staging record differs from the tested viewer executable')
    runtime = json.loads(args.runtime.read_text())
    if runtime['driver'] not in ('swiftshader', 'lavapipe'):
        raise RuntimeError('This CI launcher requires the pinned software test runtime')
    if runtime['sources'] != json.loads((ROOT / 'scripts/vulkan_ci_dependencies.json').read_text()):
        raise RuntimeError('Runtime pins differ from the CI lock')
    for name, digest in runtime['staged_sha256'].items():
        if hashlib.sha256((args.runtime.resolve().parent / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Runtime checksum mismatch: ' + name)
    harness = json.loads(args.harness_evidence.read_text())
    if not harness.get('passed') or not harness.get('presentation_qualified'):
        raise RuntimeError('Viewer qualification requires successful prerequisite harness evidence')
    expected_device = harness['device']
    evidence = args.evidence.resolve(); evidence.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for key in ('VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES', 'VK_ADD_LAYER_PATH', 'VK_INSTANCE_LAYERS',
                'VK_LOADER_DRIVERS_SELECT', 'VK_LOADER_DRIVERS_DISABLE', 'VK_LAYER_ENABLES', 'VK_LAYER_DISABLES',
                'VS_VULKAN_DIAGNOSTIC', 'VS_VULKAN_DIAGNOSTIC_FAIL', 'VS_VULKAN_DIAGNOSTIC_UI'):
        env.pop(key, None)
    env.update(VK_DRIVER_FILES=runtime['icd'], VK_LAYER_PATH=runtime['layer_path'],
               VK_LOADER_LAYERS_DISABLE='~implicit~', VK_LOADER_DEBUG='error,warn,driver,layer')
    settings = evidence / 'vk_layer_settings.txt'
    settings.write_text('khronos_validation.validate_sync = true\n')
    env['VK_LAYER_SETTINGS_PATH'] = str(settings)
    if not windows:
        env['SDL_VIDEODRIVER'] = 'x11'
        env['LD_LIBRARY_PATH'] = str(stage / 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    results = {'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               'source_worktree_dirty': bool(subprocess.check_output(
                   ['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
               'executable': str(executable), 'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
               'ui_font': 'fonts/DejaVuSans.ttf',
               'ui_font_sha256': hashlib.sha256((stage / 'fonts/DejaVuSans.ttf').read_bytes()).hexdigest(),
               'tests': [], 'ui_chat_qualified': False, 'world_qualified': False}
    with windows_manifest_registration(runtime, args.register_windows_manifests):
        for case in ('positive', *FAILURES, *UI_CASES):
            directory = evidence / case; directory.mkdir(exist_ok=True)
            artifact = directory / 'viewer-presentation.json'; artifact.unlink(missing_ok=True)
            image = directory / 'viewer-clear.ppm'; image.unlink(missing_ok=True)
            for old_image in directory.glob('viewer-ui-*.ppm'): old_image.unlink()
            child_env = env.copy(); child_env['VS_VULKAN_DIAGNOSTIC'] = str(directory)
            if case in UI_CASES: child_env['VS_VULKAN_DIAGNOSTIC_UI'] = '1'
            if case not in ('positive', 'ui-positive'): child_env['VS_VULKAN_DIAGNOSTIC_FAIL'] = case
            record = None; mapped = {}; log = ''; code = None; passed = False
            try:
                process = subprocess.run([str(executable)], cwd=stage, env=child_env,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
                code = process.returncode; log = process.stdout.decode('utf-8', errors='replace')
                record = json.loads(artifact.read_text())
                passed = assess(record, code, log, case, platform.system())
                if case in ('positive', 'ui-positive'):
                    if not image.is_file() or image.stat().st_size != len(b'P6\n320 240\n255\n') + 320 * 240 * 3:
                        raise RuntimeError('Missing or incomplete viewer clear readback')
                if case in UI_CASES:
                    for scale in (('1x', '2x') if case == 'ui-positive' else ('1x',)):
                        for suffix in ('', '-expected'):
                            path = directory / f'viewer-ui-{scale}{suffix}.ppm'
                            if not path.is_file() or path.stat().st_size != len(b'P6\n320 240\n255\n') + 320 * 240 * 3:
                                raise RuntimeError('Missing or incomplete viewer UI readback: ' + path.name)
                if case in ('positive', 'ui-positive') or 'device-created' in record.get('stages', []):
                    device = record['device']
                    if any(device.get(k) != expected_device[k] for k in ('name', 'vendor_id', 'device_id')):
                        raise RuntimeError('Viewer and pinned test runtime selected different devices')
                    mapped = loaded_library_hashes(log)
                    libraries = stage if windows else stage / 'lib'
                    for name, digest in harness['staged_library_sha256'].items():
                        path = str((libraries / name).resolve())
                        if mapped.get(path) != digest:
                            raise RuntimeError('Viewer loaded unexpected GHI library: ' + name)
                    for library in runtime_libraries(runtime):
                        path = str(library.resolve())
                        if mapped.get(path) != hashlib.sha256(Path(path).read_bytes()).hexdigest():
                            raise RuntimeError('Viewer did not load pinned runtime: ' + name)
            except subprocess.TimeoutExpired as exc:
                log = (exc.stdout or b'').decode('utf-8', errors='replace') + '\nVIEWER TIMEOUT\n'; passed = False
            except (RuntimeError, OSError, KeyError, ValueError, TypeError) as exc:
                log += '\nEVIDENCE FAILURE: ' + str(exc) + '\n'; passed = False
            (directory / 'viewer.log').write_text(log, encoding='utf-8')
            results['tests'].append(dict(case=case, returncode=code, passed=passed, presentation=record,
                                         loaded_library_sha256=mapped))
            print(('PASS' if passed else 'FAIL') + ': viewer ' + case)
    results['presentation_qualified'] = all(t['passed'] for t in results['tests'] if t['case'] not in UI_CASES)
    results['ui_substrate_qualified'] = all(t['passed'] for t in results['tests'] if t['case'] in UI_CASES)
    (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if results['presentation_qualified'] and results['ui_substrate_qualified'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
