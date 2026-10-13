"""Package regular qualification files and explicitly report unsupported entries."""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import zipfile


EVIDENCE_DIRECTORIES = ('evidence', 'headless-evidence', 'viewer-evidence',
                        'session-evidence', 'r1-session-evidence', 'parity-evidence',
                        'world-parity-evidence')


def inputs(viewer):
    return [*(f'.ci/{name}' for name in EVIDENCE_DIRECTORIES),
            '.ci/opengl-reference/runtime.json', '.ci/opengl-reference/install/licenses',
            '.ci/audio/*.log', '.ci/audio/*.txt', '.ci/runtime/runtime.json',
            '.ci/runtime/staged/licenses', '.ci/ghi/autobuild.xml',
            '.ci/test-packages/metadata', '.ci/test-packages/LICENSES',
            '.ci/test-build/CMakeCache.txt', f'{viewer}/CMakeCache.txt',
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
    if viewer == 'build-linux-x86_64' and runtime.get('driver') == 'lavapipe':
        for name in ('.ci/opengl-reference/runtime.json', '.ci/parity-evidence/baseline-results.json',
                     '.ci/world-parity-evidence/baseline-results.json', '.ci/parity-evidence/r0-acceptance.json'):
            require(name)
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
    folders = ['.ci/r1-session-evidence'] + [
        '.ci/session-evidence/' + name for name in matrix.get('cases', {})]
    for folder in folders:
        result = read(folder + '/results.json')
        if result.get('passed') is not True or result.get('exit_code') != 0:
            errors.append('Unqualified session evidence: ' + folder)
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
