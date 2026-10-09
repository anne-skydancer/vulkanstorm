"""Qualify native presentation through the fully staged viewer executable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import xml.etree.ElementTree as ET

from run_software_vulkan_tests import windows_manifest_registration, loaded_library_hashes

ROOT = Path(__file__).resolve().parents[1]
STAGES = ['viewer-window-created', 'device-created', 'swapchain-created-320x240',
          'present-initial', 'resize-present-640x360', 'zero-extents-suspended',
          'minimized-suspended', 'restore-present-320x240', 'swapchain-released',
          'viewer-window-destroyed', 'device-context-released']
FAILURES = ('before-window', 'after-window', 'after-device', 'after-swapchain',
            'frame', 'shutdown', 'gl-trap', 'bad-clear')
STARTUP_CASES = ('startup-positive', 'startup-ui', 'startup-frame', 'startup-cleanup')
UI_CASES = ('ui-positive', 'bad-ui', 'ui-orientation', 'bad-xui', 'ui-construction', 'ui-gl-trap')


def assess_startup(record, code, log, case):
    stages = ['native-viewer-window-created', 'native-startup-progress-created',
              'native-login-controller-created', 'native-startup-ui-released',
              'native-startup-graphics-released', 'native-startup-window-released']
    if (record.get('schema') != 1 or record.get('mode') != 'viewer-native-startup'
        or record.get('shutdown_complete') is not True or record.get('validation_errors') != 0
        or not record.get('login_controls_verified') or not record.get('progress_owner_verified')
        or any(stage not in record.get('stages', []) for stage in stages)):
        return False
    positions = [record['stages'].index(stage) for stage in stages]
    if positions != sorted(positions) or 'DILIGENT 2:' in log or 'Validation Error' in log:
        return False
    if case == 'startup-positive':
        return (code == 0 and record.get('passed') is True and record.get('presented_frames') == 9
                and record.get('readbacks') == 9 and record.get('modal_alert_verified') is True
                and record.get('critical_dialog_verified') is True)
    return code == 1 and record.get('passed') is False and record.get('failure') == 'Injected failure: ' + case and ('Injected failure: ' + case) in log


def skin_cases(stage):
    """Qualify every packaged skin/theme; missing catalog assets cannot fall back unnoticed."""
    def mapping(node):
        children = list(node)
        if node.tag != 'map' or len(children) % 2:
            raise RuntimeError('Malformed packaged skin catalog')
        if any(children[i].tag != 'key' for i in range(0, len(children), 2)):
            raise RuntimeError('Malformed packaged skin catalog keys')
        return {children[i].text: children[i + 1] for i in range(0, len(children), 2)}

    def component(value, optional=False):
        if (not value and not optional) or (value and not re.fullmatch(r'[A-Za-z0-9_-]+', value)):
            raise RuntimeError('Invalid packaged skin folder')
        return value

    catalog = stage / 'skins/skins.xml'
    array = ET.parse(catalog).getroot().find('array')
    if array is None or not len(array):
        raise RuntimeError('Packaged skin catalog is empty')
    result = {'skin-default-base': ('default', '', 'en')}
    for skin in array:
        entry = mapping(skin)
        folder = component(entry['folder'].text or '')
        themes = entry['themes']
        if themes.tag != 'array' or not len(themes):
            raise RuntimeError('Packaged skin has no themes: ' + folder)
        for theme in themes:
            selected = component(mapping(theme)['folder'].text or '', optional=True)
            path = stage / 'skins' / folder
            if selected:
                path = path / 'themes' / selected
            if not path.is_dir():
                raise RuntimeError('Packaged skin/theme directory is missing: ' + str(path))
            name = 'skin-' + folder + '-' + (selected or 'base')
            if name in result:
                raise RuntimeError('Duplicate packaged skin/theme: ' + name)
            result[name] = (folder, selected, 'en')
    if not (stage / 'skins/default').is_dir():
        raise RuntimeError('Packaged base skin is missing')
    # A translated XUI overlay uses the same native owner and base-asset fallback.
    for filename in ('strings.xml', 'panel_progress_mini.xml'):
        if not (stage / 'skins/default/xui/de' / filename).is_file():
            raise RuntimeError('Packaged German XUI overlay is missing: ' + filename)
    result['skin-default-de'] = ('default', '', 'de')
    return result


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
    positive_ui = case == 'ui-positive' or case.startswith('skin-')
    if case != 'positive' and not positive_ui:
        if case in UI_CASES and record.get('ui_fixture_enabled') is not True:
            return False
        expected = ('GL geometry in native UI owner' if case == 'ui-gl-trap' else
                    'Viewer XUI pixel oracle mismatch' if case == 'bad-xui' else
                    'Viewer UI pixel oracle mismatch' if case in ('bad-ui', 'ui-orientation') else
                    'Viewer clear pixel oracle mismatch' if case == 'bad-clear' else
                    'GL presentation in native Vulkan window' if case == 'gl-trap'
                    else 'Injected failure: ' + case)
        return (code == 1 and record.get('passed') is False and record.get('failure') == expected
                and 'FAIL viewer-native-diagnostic' in log)
    if positive_ui and (record.get('ui_fixture_enabled') is not True or
                                 record.get('ui_readback_verified') is not True or
                                 record.get('ui_facade_verified') is not True or
                                 record.get('ui_atlas_verified') is not True or
                                 record.get('ui_font_producer_verified') is not True or
                                 record.get('ui_admission_verified') is not True or
                                 record.get('ui_xui_verified') is not True or
                                 record.get('ui_input_verified') is not True or
                                 record.get('ui_focus_verified') is not True or
                                 record.get('ui_mouse_verified') is not True or
                                 record.get('ui_scroll_verified') is not True or
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
                'VS_VULKAN_DIAGNOSTIC', 'VS_VULKAN_DIAGNOSTIC_FAIL', 'VS_VULKAN_DIAGNOSTIC_UI',
                'VS_VULKAN_DIAGNOSTIC_SKIN', 'VS_VULKAN_DIAGNOSTIC_THEME', 'VS_VULKAN_DIAGNOSTIC_LANGUAGE',
                'VS_VULKAN_DIAGNOSTIC_STARTUP'):
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
               'tests': [], 'ui_chat_qualified': False, 'world_qualified': False,
               'ui_platform_dpi_qualified': False, 'ui_os_ime_qualified': False,
               'ui_event_source': 'synthetic-native-window-events'}
    skins = skin_cases(stage)
    ui_cases = (*UI_CASES, *skins)
    results['skin_cases'] = {name: dict(zip(('skin', 'theme', 'language'), values)) for name, values in skins.items()}
    with windows_manifest_registration(runtime, args.register_windows_manifests):
        for case in ('positive', *FAILURES, *ui_cases, *STARTUP_CASES):
            directory = evidence / case; directory.mkdir(exist_ok=True)
            artifact = directory / ('viewer-startup.json' if case in STARTUP_CASES else 'viewer-presentation.json'); artifact.unlink(missing_ok=True)
            image = directory / 'viewer-clear.ppm'; image.unlink(missing_ok=True)
            for pattern in ('viewer-ui-*.ppm','viewer-xui*.ppm'):
                for old_image in directory.glob(pattern): old_image.unlink()
            child_env = env.copy(); child_env['VS_VULKAN_DIAGNOSTIC'] = str(directory)
            if case in ui_cases: child_env['VS_VULKAN_DIAGNOSTIC_UI'] = '1'
            if case in STARTUP_CASES: child_env['VS_VULKAN_DIAGNOSTIC_STARTUP'] = '1'
            if case in skins:
                for key, value in zip(('SKIN', 'THEME', 'LANGUAGE'), skins[case]):
                    child_env['VS_VULKAN_DIAGNOSTIC_' + key] = value
            if case not in ('positive', 'ui-positive', 'startup-positive') and case not in skins:
                child_env['VS_VULKAN_DIAGNOSTIC_FAIL'] = case
            record = None; mapped = {}; log = ''; code = None; passed = False
            try:
                process = subprocess.run([str(executable)], cwd=stage, env=child_env,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
                code = process.returncode; log = process.stdout.decode('utf-8', errors='replace')
                record = json.loads(artifact.read_text())
                passed = assess_startup(record, code, log, case) if case in STARTUP_CASES else assess(record, code, log, case, platform.system())
                if case == 'startup-positive':
                    for index in range(9):
                        path = directory / f'startup-{index}.ppm'
                        if not path.is_file() or path.stat().st_size < 640*480*3:
                            raise RuntimeError('Missing or incomplete native startup readback')
                    if 'DILIGENT_DEVICE=' + expected_device['name'] not in log:
                        raise RuntimeError('Native startup selected an unexpected device')
                if case in skins and tuple(record.get('ui_' + key) for key in ('skin', 'theme', 'language')) != skins[case]:
                    raise RuntimeError('Viewer skin selection differs from the requested skin/theme/language')
                if case in ('positive', 'ui-positive') or case in skins:
                    if not image.is_file() or image.stat().st_size != len(b'P6\n320 240\n255\n') + 320 * 240 * 3:
                        raise RuntimeError('Missing or incomplete viewer clear readback')
                if case in ui_cases:
                    for scale in (('1x', '2x') if case in ('ui-positive', 'bad-xui', 'ui-construction') or case in skins else ('1x',)):
                        for suffix in ('', '-expected'):
                            path = directory / f'viewer-ui-{scale}{suffix}.ppm'
                            if not path.is_file() or path.stat().st_size != len(b'P6\n320 240\n255\n') + 320 * 240 * 3:
                                raise RuntimeError('Missing or incomplete viewer UI readback: ' + path.name)
                if case in ('ui-positive', 'bad-xui') or case in skins:
                    for suffix in ('', '-expected'):
                        path = directory / f'viewer-xui{suffix}.ppm'
                        if not path.is_file() or path.stat().st_size != len(b'P6\n320 240\n255\n') + 320 * 240 * 3:
                            raise RuntimeError('Missing or incomplete viewer XUI readback: ' + path.name)
                if case in ('positive', 'ui-positive') or 'device-created' in record.get('stages', []) or case in STARTUP_CASES:
                    device = record['device'] if case not in STARTUP_CASES else expected_device
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
    results['presentation_qualified'] = all(t['passed'] for t in results['tests'] if t['case'] not in (*ui_cases, *STARTUP_CASES))
    results['ui_substrate_qualified'] = all(t['passed'] for t in results['tests'] if t['case'] in ui_cases)
    results['skin_fixture_qualified'] = all(t['passed'] for t in results['tests'] if t['case'] in skins)
    results['startup_window_qualified'] = all(t['passed'] for t in results['tests'] if t['case'] in STARTUP_CASES)
    (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if results['presentation_qualified'] and results['ui_substrate_qualified'] and results['startup_window_qualified'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
