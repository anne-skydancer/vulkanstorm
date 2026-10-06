"""Run staged Diligent tests against a selected software or installed vendor ICD."""
import argparse
from contextlib import contextmanager, ExitStack
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def windows_manifest_registration(runtime, enabled):
    """Elevated hosted Windows loaders ignore environment search overrides."""
    if not enabled:
        yield
        return
    if (platform.system() != 'Windows' or os.environ.get('GITHUB_ACTIONS') != 'true'
            or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted'):
        raise RuntimeError('Manifest registration is restricted to hosted Windows CI')
    import winreg
    manifests = [('Drivers', Path(runtime['icd'])),
                 ('ExplicitLayers', Path(runtime['layer_path']) / 'VkLayer_khronos_validation.json')]
    access = winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY

    def restore(key_path, name, previous):
        with winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, key_path, 0, access) as key:
            if previous is not None:
                winreg.SetValueEx(key, name, 0, previous[1], previous[0])
            else:
                try:
                    winreg.DeleteValue(key, name)
                except FileNotFoundError:
                    pass

    with ExitStack() as cleanup:
        for category, manifest in manifests:
            if not manifest.is_file():
                raise RuntimeError(f'Missing registered manifest: {manifest}')
            key_path = 'SOFTWARE\\Khronos\\Vulkan\\' + category
            name = str(manifest.resolve())
            with winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, key_path, 0, access) as key:
                try:
                    previous = winreg.QueryValueEx(key, name)
                except FileNotFoundError:
                    previous = None
                cleanup.callback(restore, key_path, name, previous)
                winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, 0)
        yield


def assess(mode, code, log):
    expected_validation = {'invalid': 'VUID-VkBufferCreateInfo-size-00912',
                           'invalid-sync': 'SYNC-HAZARD-WRITE-AFTER-WRITE'}.get(mode)
    for identifier in re.findall(r'^VALIDATION ([^:]+):', log, re.MULTILINE):
        if identifier.startswith(('VUID-', 'SYNC-', 'UNASSIGNED-')) and identifier != expected_validation:
            return False
    if any(int(severity) >= 2 for severity in re.findall(r'^DILIGENT (\d+):', log, re.MULTILINE)):
        return False
    if mode == 'invalid':
        return code == 1 and 'VUID-VkBufferCreateInfo-size-00912' in log and 'Validation reported an error' in log
    if mode == 'invalid-sync':
        return code == 1 and 'SYNC-HAZARD-WRITE-AFTER-WRITE' in log and 'Validation reported an error' in log
    if mode in ('bad-pixels', 'bad-orientation'):
        return code == 1 and 'Pixel oracle mismatch' in log
    return code == 0 and f'PASS {mode}' in log and 'DILIGENT_DEVICE=' in log and 'LOADED=' in log


def device_evidence(log, vendor=None):
    match = re.search(r'^ICD_DEVICE=(.+) API=(\d+) DRIVER=(\d+) VENDOR=(\d+) DEVICE=(\d+) TYPE=(\d+)$', log, re.MULTILINE)
    if not match:
        raise RuntimeError('Missing Vulkan device identity')
    name, api, driver, vendor_id, device_id, kind = match.groups()
    if vendor and (int(vendor_id) != {'amd': 0x1002, 'nvidia': 0x10de}[vendor] or int(kind) not in (1, 2)):
        raise RuntimeError('Selected device is not the requested hardware vendor')
    return dict(name=name, api_version=int(api), driver_version=int(driver),
                vendor_id=int(vendor_id), device_id=int(device_id), device_type=int(kind))


def verify_installed_driver(runtime):
    if not runtime.get('installed_driver'):
        return
    for name in ('manifest', 'library'):
        entry = runtime['installed_driver']
        if hashlib.sha256(Path(entry[name]).read_bytes()).hexdigest() != entry[name + '_sha256']:
            raise RuntimeError(f'Installed driver changed: {name}')


def loaded_library_hashes(log):
    """Archive actual process mappings, including normally discovered vendor libraries."""
    files = {}
    for line in log.splitlines():
        if not line.startswith('LOADED='):
            continue
        value = line.removeprefix('LOADED=')
        if re.match(r'^[0-9a-f]+-[0-9a-f]+\s', value):
            fields = value.split(None, 5)
            value = fields[5] if len(fields) == 6 else ''
        path = Path(value)
        if path.is_file():
            files[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--headless', action='store_true', help='No display or window; omit presentation testing')
    parser.add_argument('--register-windows-manifests', action='store_true',
                        help='Temporarily register pinned manifests on an elevated hosted Windows runner')
    args = parser.parse_args()
    executable = args.executable.resolve()
    runtime = json.loads(args.runtime.read_text())
    system_driver = runtime['driver'] == 'installed'
    if system_driver and (not args.headless or args.register_windows_manifests):
        parser.error('Installed-driver testing requires --headless without hosted manifest registration')
    if system_driver:
        verify_installed_driver(runtime)
    if runtime['sources'] != json.loads((ROOT / 'scripts/vulkan_ci_dependencies.json').read_text()):
        raise RuntimeError('Cached runtime source pins differ from the CI lock')
    for name, expected_hash in runtime['staged_sha256'].items():
        if hashlib.sha256((args.runtime.resolve().parent / name).read_bytes()).hexdigest() != expected_hash:
            raise RuntimeError(f'Cached runtime checksum mismatch: {name}')
    libraries = ['vulkan-1.dll', 'GraphicsEngineVk_64r.dll'] if platform.system() == 'Windows' else ['libvulkan.so.1', 'libGraphicsEngineVk.so']
    evidence = args.evidence.resolve(); evidence.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    # Prevent inherited driver/layer configuration from broadening the selection.
    for name in ('VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES', 'VK_ADD_LAYER_PATH', 'VK_INSTANCE_LAYERS',
                 'VK_LOADER_DRIVERS_SELECT', 'VK_LOADER_DRIVERS_DISABLE', 'VK_LAYER_ENABLES', 'VK_LAYER_DISABLES'):
        env.pop(name, None)
    if runtime['icd']:
        env['VK_DRIVER_FILES'] = runtime['icd']
    else:
        env.pop('VK_DRIVER_FILES', None)
    env['VK_LAYER_PATH'] = runtime['layer_path']
    env['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'
    env['VK_LOADER_DEBUG'] = 'error,warn,driver,layer'
    env['SDL_VIDEODRIVER'] = 'x11'
    if args.headless:
        for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'SDL_VIDEODRIVER'):
            env.pop(name, None)
    settings = evidence / 'vk_layer_settings.txt'
    settings.write_text('khronos_validation.validate_sync = true\n')
    env['VK_LAYER_SETTINGS_PATH'] = str(settings)
    if platform.system() != 'Windows':
        env['LD_LIBRARY_PATH'] = str(executable.parent) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    expected = (runtime.get('vendor') or 'auto') if system_driver else 'auto'
    result = {'runtime': runtime, 'executable': str(executable),
              'windows_manifest_registration': args.register_windows_manifests,
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'staged_library_sha256': {name: hashlib.sha256((executable.parent / name).read_bytes()).hexdigest() for name in libraries},
              'headless': args.headless, 'presentation_qualified': not args.headless,
              'tests': []}
    failed = False
    with windows_manifest_registration(runtime, args.register_windows_manifests):
        modes = ('offscreen', 'invalid', 'invalid-sync', 'bad-pixels', 'bad-orientation') if args.headless else (
            'offscreen', 'present', 'invalid', 'invalid-sync', 'bad-pixels', 'bad-orientation')
        selected_device = None
        for mode in modes:
            directory = evidence / mode; directory.mkdir(exist_ok=True)
            for name in ('first.ppm', 'replacement.ppm'):
                (directory / name).unlink(missing_ok=True)
            mapped_libraries = {}
            try:
                command = [str(executable), expected, mode]
                if system_driver and runtime.get('device_name'):
                    command.append(runtime['device_name'])
                process = subprocess.run(command, cwd=directory, env=env,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
                log = process.stdout
                passed = assess(mode, process.returncode, log)
                code = process.returncode
                identity = device_evidence(log, runtime.get('vendor') if system_driver else None)
                if not system_driver:
                    software_name = 'SwiftShader' if runtime['driver'] == 'swiftshader' else 'llvmpipe'
                    passed &= identity['device_type'] == 4 and software_name in identity['name']
                if selected_device is not None and identity != selected_device:
                    passed = False
                selected_device = identity
                if mode in ('offscreen', 'present'):
                    normalized = log.replace('\\', '/').lower()
                    passed &= all(str(executable.parent / name).replace('\\', '/').lower() in normalized for name in libraries)
                    if system_driver and runtime.get('installed_driver'):
                        passed &= runtime['installed_driver']['library'].replace('\\', '/').lower() in normalized
                    mapped_libraries = loaded_library_hashes(log)
            except RuntimeError as error:
                log += f'\nIDENTITY FAILURE: {error}\n'; passed = False
            except subprocess.TimeoutExpired as error:
                log = (error.stdout or b'').decode(errors='replace') if isinstance(error.stdout, bytes) else (error.stdout or '')
                log += '\nTIMEOUT\n'; passed = False; code = None
            (directory / 'test.log').write_text(log, encoding='utf-8')
            if mode in ('offscreen', 'present'):
                passed &= all((directory / name).is_file() for name in ('first.ppm', 'replacement.ppm'))
            result['tests'].append({'mode': mode, 'passed': passed, 'exit_code': code,
                                    'loaded_library_sha256': mapped_libraries})
            print(f'{mode}: {"PASS" if passed else "FAIL"}', flush=True)
            if not passed:
                print(log[-8000:], flush=True)
            failed |= not passed
    result['passed'] = not failed
    result['presentation_qualified'] = not failed and not args.headless
    result['device'] = selected_device
    if system_driver:
        verify_installed_driver(runtime)
    (evidence / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
