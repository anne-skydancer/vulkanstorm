"""Qualify two real headless viewers sharing the reference cache and marker lock."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from uuid import uuid4
import xml.etree.ElementTree as ET

from run_vulkan_session_replay import read_llsd
from run_vulkan_texture_cache_tests import snapshot, validate_case, texture_requests, PROFILE, STREAM

ROOT = Path(__file__).resolve().parents[1]


def require_live_process(pid):
    if type(pid) is not int or pid<=0:
        raise ValueError('Invalid viewer process identity')
    if os.name != 'nt':
        os.kill(pid, 0)
        return
    # Windows signal-zero behavior varies across Python/CRT versions. Query a
    # read-only process handle instead; this never sends a signal to the viewer.
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
    kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
    kernel.WaitForSingleObject.restype=wintypes.DWORD
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    kernel.CloseHandle.restype=wintypes.BOOL
    handle=kernel.OpenProcess(0x100000,False,pid)  # SYNCHRONIZE only.
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        status=kernel.WaitForSingleObject(handle,0)
        if status==0:
            raise RuntimeError('Primary viewer exited during concurrent qualification')
        if status!=258:  # WAIT_TIMEOUT means the process is still alive.
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)


def validate_instances(primary, sibling, before, after, held):
    if held is not True:
        raise ValueError('Primary viewer was not held alive during sibling execution')
    a, b = primary['texture_cache'], sibling['texture_cache']
    if a.get('second_instance') is not False or a.get('read_only') is not False:
        raise ValueError('Primary did not own the writable cache')
    if b.get('second_instance') is not True or b.get('read_only') is not True:
        raise ValueError('Actual marker lock did not make the sibling cache read-only')
    if a['process_id'] == b['process_id'] or a['process_instance'] == b['process_instance']:
        raise ValueError('Qualification did not use distinct viewer processes')
    if not before or before != after:
        raise ValueError('Sibling mutated the shared texture cache')
    if (primary.get('source_commit') != sibling.get('source_commit') or
            primary.get('viewer_executable_sha256') != sibling.get('viewer_executable_sha256')):
        raise ValueError('Concurrent viewer source/executable differs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build-directory', 'runtime', 'harness-evidence', 'cache-evidence', 'evidence'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--register-windows-manifests', action='store_true')
    args = parser.parse_args()
    directory = args.evidence.resolve(); directory.mkdir(parents=True, exist_ok=True)
    run = directory / ('run-' + uuid4().hex); run.mkdir()
    result = dict(passed=False, run=run.relative_to(directory).as_posix())
    primary = None
    gate = run / 'completion.release'
    try:
        matrix = json.loads((args.cache_evidence / 'results.json').read_text())
        if matrix.get('passed') is not True or matrix.get('complete') is not True:
            raise ValueError('Missing successful persistent-cache seed matrix')
        cold = (args.cache_evidence / matrix['cases']['cold']['evidence']).resolve()
        if not cold.is_relative_to(args.cache_evidence.resolve()) or cold.name != 'cold':
            raise ValueError('Unsafe seed evidence path')
        cache = run / 'shared-cache'; cache.mkdir()
        shutil.copytree(cold.parent / 'shared-cache' / 'texturecache', cache / 'texturecache')
        policy = run / 'cache-policy.json'
        policy.write_text(json.dumps(json.loads((cold / 'results.json').read_text())['texture_cache']['startup_policy']))
        base = [sys.executable, str(ROOT / 'scripts/run_vulkan_session_replay.py'),
                '--build-directory', str(args.build_directory.resolve()), '--runtime', str(args.runtime.resolve()),
                '--harness-evidence', str(args.harness_evidence.resolve()), '--headless', '--texture-cache-contracts',
                '--texture-fixture', str(ROOT / 'indra/newview/skins/default/textures/uv_test1.j2c'),
                '--cache-directory', str(cache), '--cache-policy', str(policy),
                '--profile-directory', str(run / 'profile-shared')]
        if args.register_windows_manifests:
            base.append('--register-windows-manifests')
        primary_log = (run / 'primary-launch.log').open('wb')
        try:
            primary = subprocess.Popen(base + ['--evidence', str(run / 'primary'), '--completion-gate', str(gate)],
                                       stdout=primary_log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            ready = Path(str(gate) + '.ready')
            deadline = time.monotonic() + 120
            while not ready.is_file():
                if primary.poll() is not None:
                    raise RuntimeError('Primary viewer exited before retaining its marker lock')
                if time.monotonic() >= deadline:
                    raise RuntimeError('Primary viewer did not reach the concurrent-instance checkpoint')
                time.sleep(.1)
            checkpoint = read_llsd(ET.parse(ready).getroot())
            if type(checkpoint.get('process_id')) is not int or checkpoint['process_id']<=0 or checkpoint.get('second_instance') is not False:
                raise ValueError('Invalid primary viewer marker checkpoint')
            require_live_process(checkpoint['process_id'])
            before = snapshot(cache)
            with (run / 'sibling-launch.log').open('wb') as sibling_log:
                sibling = subprocess.run(base + ['--evidence', str(run / 'sibling')], timeout=120,
                                         stdout=sibling_log, stderr=subprocess.STDOUT,
                                         creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            held = primary.poll() is None and not gate.exists()
            require_live_process(checkpoint['process_id'])
            after = snapshot(cache)
            gate.touch(); primary_code = primary.wait(timeout=120)
            if primary_code or sibling.returncode:
                raise RuntimeError(f'Concurrent viewer failure: primary={primary_code}, sibling={sibling.returncode}')
        finally:
            gate.touch()
            if primary is not None and primary.poll() is None:
                primary.wait(timeout=540)  # The existing viewer deadline remains decisive.
            primary_log.close()
        reports = {}
        for name, readonly in [('primary', False), ('sibling', True)]:
            folder = run / name
            child = json.loads((folder / 'results.json').read_text())
            requests = json.loads((folder / 'http-replay.json').read_text())
            rows = validate_case(child, requests, folder, cache, readonly)
            if texture_requests(requests):
                raise ValueError('Warm concurrent viewer unexpectedly fetched cached images')
            reports[name] = child
            result[name] = dict(evidence=(folder.relative_to(directory)).as_posix(), process_id=child['texture_cache']['process_id'])
            if name == 'primary':
                pixels = {identity: rows[identity]['gpu_rgba_sha256'] for identity in (PROFILE, STREAM)}
            elif any(rows[identity]['gpu_rgba_sha256'] != pixels[identity] for identity in pixels):
                raise ValueError('Concurrent cache users produced different image pixels')
        validate_instances(reports['primary'], reports['sibling'], before, after, held)
        result.update(passed=True, primary_held=held, cache_before=before, cache_after=after,
                      cache_final=snapshot(cache),
                      source_commit=reports['primary']['source_commit'],
                      viewer_executable_sha256=reports['primary']['viewer_executable_sha256'])
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        result['failure'] = str(error)
    (directory / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(('PASS' if result['passed'] else 'FAIL') + ': actual concurrent viewer cache ownership', flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
