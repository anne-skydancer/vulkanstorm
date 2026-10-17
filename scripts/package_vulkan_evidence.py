"""Package regular qualification files and explicitly report unsupported entries."""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import zipfile
from run_vulkan_geometry_tests import required_checks
from run_vulkan_session_replay import r2_contracts_passed, local_image_contracts_passed, local_material_contracts_passed, world_bump_contracts_passed
from run_software_vulkan_tests import MATERIAL_READBACKS
from run_vulkan_texture_cache_tests import CASES as TEXTURE_CACHE_CASES, validate_case as validate_texture_cache_case, texture_requests


EVIDENCE_DIRECTORIES = ('evidence', 'headless-evidence', 'viewer-evidence',
                        'session-evidence', 'r1-session-evidence', 'r2-session-evidence', 'parity-evidence',
                        'world-parity-evidence', 'geometry-evidence', 'r3-texture-cache-evidence')


def texture_cache_completeness(files, source, executable_digest):
    prefix='.ci/r3-texture-cache-evidence'
    summary=files.get(prefix+'/results.json')
    if summary is None:return ['Missing R3 texture-cache restart evidence']
    errors=[];processes=set()
    try:
        matrix=json.loads(summary.read_text())
        if not isinstance(matrix,dict):raise ValueError('Invalid R3 matrix record')
        if (matrix.get('passed') is not True or matrix.get('complete') is not True or
            matrix.get('planned_cases')!=list(TEXTURE_CACHE_CASES) or set(matrix.get('cases',{}))!=set(TEXTURE_CACHE_CASES)):
            return ['Incomplete R3 texture-cache restart matrix']
        for case in TEXTURE_CACHE_CASES:
            row=matrix['cases'][case]
            if not isinstance(row,dict):raise ValueError('Invalid R3 case record')
            relative=str(row['evidence']).replace('\\','/')
            if relative.startswith('/') or ':' in relative or '..' in relative.split('/') or relative.split('/')[-1]!=case:
                raise ValueError('Unsafe R3 case evidence path')
            folder=prefix+'/'+relative
            if row.get('passed') is not True or row.get('source_commit')!=source:
                raise ValueError('Unqualified R3 cache case: '+case)
            for name in ('results.json','session-replay.xml','viewer.log','http-replay.json','vk_layer_settings.txt','r3/texture.j2c'):
                if folder+'/'+name not in files:raise ValueError('Missing R3 cache artifact: '+folder+'/'+name)
            child=json.loads(files[folder+'/results.json'].read_text())
            traffic=json.loads(files[folder+'/http-replay.json'].read_text())
            if not isinstance(child,dict) or not isinstance(traffic,list) or any(not isinstance(item,dict) for item in traffic):
                raise ValueError('Invalid R3 case or HTTP records')
            if child.get('source_commit')!=source or child.get('viewer_executable_sha256')!=executable_digest:
                raise ValueError('R3 cache source/executable differs from build: '+case)
            actual=files[folder+'/results.json'].parent
            cache=actual.parent/('shared-cache' if case in ('cold','warm') else 'cache-'+case)
            validate_texture_cache_case(child,traffic,actual,cache,case=='read-only')
            identity=child['texture_cache']['process_instance']
            if identity in processes or identity!=row['process_instance']:raise ValueError('Reused R3 viewer process identity')
            processes.add(identity)
            count=len(texture_requests(traffic))
            if row['texture_requests']!=count or (case in ('warm','read-only','resize') and count) or (case not in ('warm','read-only','resize') and not count):
                raise ValueError('R3 cache network policy failed: '+case)
            if case=='read-only' and (not row.get('cache_before') or row['cache_before']!=row['cache_files']):
                raise ValueError('Read-only R3 cache file witnesses differ')
            paths=[image['gpu_rgba_file'] for image in child['texture_cache']['images']]
            stages=child['r3_texture_streaming']
            paths.extend(stages[name]['gpu_rgba_file'] for name in ('coarse','full_resolution_preference','coarse_after_preference','resolution_limit','restored'))
            paths.extend(stages['references'].values())
            for path in paths:
                if folder+'/'+str(path).replace('\\','/') not in files:raise ValueError('R3 readback omitted from archive: '+str(path))
            if case=='cold':
                if not local_image_contracts_passed(child,actual) or not local_material_contracts_passed(child,actual) or not world_bump_contracts_passed(child,actual):raise ValueError('Missing R3 local file/GPU evidence')
                for name in ('initial','replaced','refused','recovered'):
                    path=child['r3_local_images'][name]['file']
                    if folder+'/'+str(path).replace('\\','/') not in files:raise ValueError('R3 local image omitted from archive: '+str(path))
                for stage in ('initial','replaced','refused','recovered'):
                    for image in child['r3_local_material'][stage]:
                        path=image['file']
                        if folder+'/'+str(path).replace('\\','/') not in files:raise ValueError('R3 local material omitted from archive: '+str(path))
                for stage in ('initial','source_replaced','scale_changed'):
                    for image in child['r3_world_bumps'][stage]:
                        for key in ('gpu_file','expected_file','source_file'):
                            path=image[key]
                            if folder+'/'+str(path).replace('\\','/') not in files:raise ValueError('R3 bump readback omitted from archive: '+str(path))
    except (OSError,ValueError,KeyError,TypeError) as error:
        errors.append('Invalid R3 texture-cache evidence: '+str(error))
    return errors


def inputs(viewer):
    return [*(f'.ci/{name}' for name in EVIDENCE_DIRECTORIES),
            '.ci/opengl-reference/runtime.json', '.ci/opengl-reference/install/licenses',
            '.ci/audio/*.log', '.ci/audio/*.txt', '.ci/runtime/runtime.json',
            '.ci/runtime/staged/licenses', '.ci/ghi/autobuild.xml',
            '.ci/test-packages/metadata', '.ci/test-packages/LICENSES',
            '.ci/test-build/CMakeCache.txt', f'{viewer}/CMakeCache.txt',
            f'{viewer}/parity-build-provenance.json',
            f'{viewer}/newview/vulkan-ci-staging.json']


def collect(root, patterns):
    files, excluded = {}, []

    def visit(path):
        relative = path.relative_to(root).as_posix()
        mode = path.lstat().st_mode
        if stat.S_ISREG(mode):
            files[relative] = path
        elif stat.S_ISDIR(mode):
            for child in sorted(path.iterdir()):
                visit(child)
        else:
            kind = ('symlink' if stat.S_ISLNK(mode) else 'socket' if stat.S_ISSOCK(mode)
                    else 'fifo' if stat.S_ISFIFO(mode) else 'device-or-special')
            excluded.append(dict(path=relative, type=kind))

    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            visit(path)
    return files, sorted(excluded, key=lambda item: item['path'])


def completeness(files, viewer):
    errors = []

    def require(name):
        if name not in files or files[name].stat().st_size == 0:
            errors.append('Missing or empty required evidence: ' + name)
            return False
        return True

    def read(name):
        if not require(name):
            return {}
        try:
            result = json.loads(files[name].read_text(encoding='utf-8'))
            if not isinstance(result, dict):
                raise ValueError('Expected an evidence object')
            return result
        except (ValueError, OSError) as error:
            errors.append(f'Invalid evidence {name}: {error}')
            return {}

    for name in ('.ci/evidence/source.txt', '.ci/evidence/toolchain.txt',
                 '.ci/runtime/runtime.json', '.ci/ghi/autobuild.xml',
                 '.ci/test-build/CMakeCache.txt', f'{viewer}/CMakeCache.txt',
                 f'{viewer}/newview/vulkan-ci-staging.json'):
        require(name)
    for prefix in ('.ci/runtime/staged/licenses/', '.ci/test-packages/metadata/',
                   '.ci/test-packages/LICENSES/'):
        if not any(name.startswith(prefix) for name in files):
            errors.append('Missing required evidence group: ' + prefix)
    runtime = read('.ci/runtime/runtime.json')
    provenance = read(f'{viewer}/parity-build-provenance.json')
    source_file = files.get('.ci/evidence/source.txt')
    source = source_file.read_text().strip() if source_file else ''
    errors.extend(texture_cache_completeness(files,source,provenance.get('executable_sha256')))
    if (provenance.get('complete') is not True or provenance.get('source_commit') != source or not source or
            len(provenance.get('executable_sha256', '')) != 64):
        errors.append('Incomplete or mismatched source/build provenance')
    staging = read(f'{viewer}/newview/vulkan-ci-staging.json')
    executable = Path(str(provenance.get('executable', '')).replace('\\', '/')).name
    staged_hashes = {Path(name.replace('\\', '/')).name: digest for name, digest in staging.get('files', {}).items()}
    if not executable or staged_hashes.get(executable) != provenance.get('executable_sha256'):
        errors.append('Staged executable does not match source/build provenance')
    geometry=read('.ci/geometry-evidence/results.json')
    if geometry.get('source_commit') != source:
        errors.append('CPU producer source does not match viewer source/build provenance')
    log_name='.ci/geometry-evidence/geometry.log'
    count=geometry.get('tree_species')
    checks=required_checks(count) if isinstance(count,int) and count>0 else []
    if (geometry.get('passed') is not True or not checks or geometry.get('checks')!=checks or
            not geometry.get('source_commit') or len(geometry.get('executable_sha256', ''))!=64 or
            len(geometry.get('tree_species_sha256', ''))!=64):
        errors.append('Incomplete CPU geometry qualification evidence')
    if require(log_name):
        log=files[log_name].read_bytes()
        if hashlib.sha256(log).hexdigest()!=geometry.get('log_sha256'):
            errors.append('CPU geometry log checksum mismatch')
        text=log.decode('utf-8',errors='replace')
        if any(check not in text for check in checks) or 'FAIL:' in text:
            errors.append('CPU geometry log is missing successful producer checks')
    if viewer == 'build-linux-x86_64' and runtime.get('driver') == 'lavapipe':
        for name in ('.ci/opengl-reference/runtime.json', '.ci/parity-evidence/baseline-results.json',
                     '.ci/world-parity-evidence/baseline-results.json', '.ci/parity-evidence/r0-acceptance.json'):
            require(name)
        for folder in ('.ci/parity-evidence', '.ci/world-parity-evidence'):
            baseline = read(folder + '/baseline-results.json')
            artifacts = baseline.get('artifact_sha256', {})
            if not artifacts:
                errors.append('Missing R0 artifact hash manifest: ' + folder)
            for relative, digest in artifacts.items():
                # Reject traversal and absolute identities; archives use relative paths.
                relative = relative.replace('\\', '/')
                if relative.startswith('/') or ':' in relative or '..' in relative.split('/'):
                    errors.append('Invalid R0 artifact path: ' + relative)
                    continue
                name = folder + '/' + relative
                if require(name) and hashlib.sha256(files[name].read_bytes()).hexdigest() != digest:
                    errors.append('R0 artifact checksum mismatch: ' + name)
        if not any(name.startswith('.ci/opengl-reference/install/licenses/') for name in files):
            errors.append('Missing OpenGL reference licenses')
    for prefix in ('.ci/evidence', '.ci/headless-evidence'):
        result = read(prefix + '/results.json')
        if result.get('passed') is not True or not result.get('tests'):
            errors.append('Unqualified graphics evidence: ' + prefix)
        for test in result.get('tests', []):
            mode = test.get('mode', '')
            require(prefix + '/' + mode + '/test.log')
            if test.get('passed') is not True:
                errors.append('Failed graphics case: ' + prefix + '/' + mode)
            if mode in ('world', 'offscreen', 'present'):
                for image in ('first.ppm', 'replacement.ppm'):
                    require(prefix + '/' + mode + '/' + image)
            if mode=='world':
                for image in MATERIAL_READBACKS:require(prefix+'/world/'+image)
    viewer_result = read('.ci/viewer-evidence/results.json')
    if not viewer_result.get('tests'):
        errors.append('Missing viewer presentation cases')
    for test in viewer_result.get('tests', []):
        case = test.get('case', '')
        folder = '.ci/viewer-evidence/' + case
        require(folder + '/viewer.log')
        require(folder + ('/result.json' if case == 'normal-login' else
                          '/viewer-startup.json' if case.startswith('startup-') else
                          '/viewer-presentation.json'))
        if test.get('passed') is not True:
            errors.append('Failed viewer case: ' + case)
    matrix = read('.ci/session-evidence/results.json')
    if (matrix.get('passed') is not True or matrix.get('complete') is not True
            or not matrix.get('planned_cases')
            or set(matrix.get('cases', {})) != set(matrix.get('planned_cases', []))):
        errors.append('Incomplete connected skin matrix')
    folders = ['.ci/r1-session-evidence', '.ci/r2-session-evidence'] + [
        '.ci/session-evidence/' + name for name in matrix.get('cases', {})]
    for folder in folders:
        result = read(folder + '/results.json')
        if result.get('passed') is not True or result.get('exit_code') != 0:
            errors.append('Unqualified session evidence: ' + folder)
        if (result.get('source_qualified') is not True or result.get('source_commit') != source or
                result.get('viewer_executable_sha256') != provenance.get('executable_sha256')):
            errors.append('Session source/executable does not match build provenance: ' + folder)
        for name in ('session-replay.xml', 'viewer.log', 'http-replay.json', 'vk_layer_settings.txt'):
            require(folder + '/' + name)
        hashes = result.get('readback_sha256', {})
        if len(hashes) != 10 or not result.get('loaded_library_sha256'):
            errors.append('Missing readback/library witnesses: ' + folder)
        for name, digest in hashes.items():
            name = folder + '/' + name.replace('\\', '/')
            if require(name) and hashlib.sha256(files[name].read_bytes()).hexdigest() != digest:
                errors.append('Readback checksum mismatch: ' + name)
        if folder.endswith('r1-session-evidence'):
            for name in ('r1/contracts.xml', 'r1/mesh-1.bin', 'r1/mesh-2.bin', 'r1/mesh-generation.txt'):
                require(folder + '/' + name)
        if folder.endswith('r2-session-evidence'):
            manifest = read(folder + '/r2/artifact-hashes.json')
            artifacts = manifest.get('artifacts', [])
            if not isinstance(artifacts, list): artifacts = []
            valid = bool(artifacts)
            for artifact in artifacts:
                if not isinstance(artifact, dict):
                    valid = False
                    continue
                relative = artifact.get('path', '')
                if not relative or '/' in relative or '\\' in relative or relative in ('.', '..'):
                    errors.append('Invalid R2 artifact path: ' + relative)
                    valid = False
                    continue
                valid = require(folder + '/r2/' + relative) and valid
            valid = require(folder + '/r2/contracts.xml') and valid
            if valid:
                try:
                    requests = json.loads(files[folder + '/http-replay.json'].read_text())
                    valid = r2_contracts_passed(result, requests, files[folder + '/results.json'].parent, write_manifest=False)
                except (KeyError, ValueError, OSError, TypeError):
                    valid = False
            if not valid:
                errors.append('Incomplete or inconsistent R2 geometry/appearance evidence')
    return errors


def package(root, output, patterns, viewer, require_complete):
    root = root.resolve()
    output = output.resolve()
    # An output inside any selected directory would recursively package itself.
    for pattern in patterns:
        for path in root.glob(pattern):
            if path.is_dir() and (output == path.resolve() or path.resolve() in output.parents):
                raise ValueError('Package output overlaps an input directory')
    files, excluded = collect(root, patterns)
    errors = completeness(files, viewer) if require_complete else []
    output.mkdir(parents=True, exist_ok=True)
    manifest = dict(schema=1, require_complete=require_complete,
                    complete=not errors if require_complete else None,
                    excluded=excluded, errors=errors, files={})
    with zipfile.ZipFile(output / 'qualification-evidence.zip', 'w',
                         compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, path in sorted(files.items()):
            # Recheck before opening: special files must never reach the archiver.
            if not stat.S_ISREG(path.lstat().st_mode):
                raise RuntimeError('Evidence entry changed type: ' + name)
            with path.open('rb') as source, archive.open(name, 'w', force_zip64=True) as target:
                digest = hashlib.sha256()
                size = 0
                while chunk := source.read(1024 * 1024):
                    target.write(chunk); digest.update(chunk); size += len(chunk)
            manifest['files'][name] = dict(bytes=size, sha256=digest.hexdigest())
    with (output / 'qualification-evidence.zip').open('rb') as source:
        manifest['archive_sha256'] = hashlib.file_digest(source, 'sha256').hexdigest()
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    if errors:
        raise RuntimeError('Qualification evidence incomplete: ' + '; '.join(errors))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path, default=Path('.ci/artifact-upload'))
    parser.add_argument('--viewer-directory', required=True)
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    result = package(args.root, args.output, inputs(args.viewer_directory),
                     args.viewer_directory, args.require_complete)
    print(f"Packaged {len(result['files'])} regular files; recorded {len(result['excluded'])} excluded entries")


if __name__ == '__main__':
    main()
