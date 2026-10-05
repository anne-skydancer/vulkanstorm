"""Run staged Diligent tests with one software ICD and mandatory validation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def assess(mode, code, log):
    if mode == 'invalid':
        return code == 1 and 'VUID-VkBufferCreateInfo-size-00912' in log and 'Validation reported an error' in log
    if mode == 'invalid-sync':
        return code == 1 and 'SYNC-HAZARD-WRITE-AFTER-WRITE' in log and 'Validation reported an error' in log
    if mode == 'bad-pixels':
        return code == 1 and 'Pixel oracle mismatch' in log
    return code == 0 and f'PASS {mode}' in log and 'DILIGENT_DEVICE=' in log and 'LOADED=' in log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve()
    runtime = json.loads(args.runtime.read_text())
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
    env['VK_DRIVER_FILES'] = runtime['icd']
    env['VK_LAYER_PATH'] = runtime['layer_path']
    env['VK_LOADER_DEBUG'] = 'error,warn,driver,layer'
    env['SDL_VIDEODRIVER'] = 'x11'
    settings = evidence / 'vk_layer_settings.txt'
    settings.write_text('khronos_validation.validate_sync = true\n')
    env['VK_LAYER_SETTINGS_PATH'] = str(settings)
    if platform.system() != 'Windows':
        env['LD_LIBRARY_PATH'] = str(executable.parent) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    expected = 'SwiftShader' if runtime['driver'] == 'swiftshader' else 'llvmpipe'
    result = {'runtime': runtime, 'executable': str(executable),
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'staged_library_sha256': {name: hashlib.sha256((executable.parent / name).read_bytes()).hexdigest() for name in libraries},
              'tests': []}
    failed = False
    for mode in ('offscreen', 'present', 'invalid', 'invalid-sync', 'bad-pixels'):
        directory = evidence / mode; directory.mkdir(exist_ok=True)
        try:
            process = subprocess.run([str(executable), expected, mode], cwd=directory, env=env,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
            log = process.stdout
            passed = assess(mode, process.returncode, log)
            code = process.returncode
            if mode in ('offscreen', 'present'):
                normalized = log.replace('\\', '/').lower()
                passed &= all(str(executable.parent / name).replace('\\', '/').lower() in normalized for name in libraries)
        except subprocess.TimeoutExpired as error:
            log = (error.stdout or b'').decode(errors='replace') if isinstance(error.stdout, bytes) else (error.stdout or '')
            log += '\nTIMEOUT\n'; passed = False; code = None
        (directory / 'test.log').write_text(log, encoding='utf-8')
        if mode in ('offscreen', 'present'):
            passed &= all((directory / name).is_file() for name in ('first.ppm', 'replacement.ppm'))
        result['tests'].append({'mode': mode, 'passed': passed, 'exit_code': code})
        print(f'{mode}: {"PASS" if passed else "FAIL"}', flush=True)
        if not passed:
            print(log[-8000:], flush=True)
        failed |= not passed
    result['passed'] = not failed
    (evidence / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
