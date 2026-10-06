"""Build isolated, pinned test ICDs and validation layers; never install globally."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'scripts/vulkan_ci_dependencies.json').read_text())


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def checkout(destination, pin):
    if not destination.exists():
        run('git', 'init', destination)
        run('git', '-C', destination, 'remote', 'add', 'origin', pin['url'])
        run('git', '-C', destination, 'fetch', '--depth=1', 'origin', pin['revision'])
        run('git', '-C', destination, 'checkout', '--detach', 'FETCH_HEAD')
    actual = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != pin['revision']:
        raise RuntimeError(f'Unexpected source revision in {destination}: {actual}')
    if subprocess.check_output(['git', '-C', str(destination), 'status', '--porcelain'], text=True).strip():
        raise RuntimeError(f'Dirty dependency source: {destination}')
    if pin.get('recursive'):
        run('git', '-C', destination, 'submodule', 'update', '--init', '--recursive', '--depth=1')


def stage_manifest(manifest, destination, key, installed_root=None):
    """Relocate a manifest and its binary together; cache no build/source trees."""
    data = json.loads(manifest.read_text())
    path = Path(data[key]['library_path'])
    source = path if path.is_absolute() else manifest.parent / path
    # Installed Linux layer manifests name their library by basename, leaving
    # loader search paths to resolve it. Relocation must use our own install,
    # never an ambient system library with the same name.
    if not source.is_file() and installed_root is not None and path.name == str(path):
        matches = [candidate for candidate in installed_root.rglob(path.name) if candidate.is_file()]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one installed manifest library: {path}, got {matches}')
        source = matches[0]
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination / source.name)
    data[key]['library_path'] = '.' + os.sep + source.name
    target = destination / manifest.name
    target.write_text(json.dumps(data, indent=2) + '\n')
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--driver', choices=['swiftshader', 'lavapipe'], required=True)
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    work = args.work_dir.resolve()
    windows = platform.system() == 'Windows'
    if windows and args.driver == 'lavapipe':
        parser.error('Lavapipe job is Linux only')
    work.mkdir(parents=True, exist_ok=True)
    generator = ['-G', 'Visual Studio 17 2022', '-A', 'x64', '-Thost=x64'] if windows else ['-G', 'Ninja']

    def build(name, options, target=None, install=False):
        source = work / 'source' / name
        directory = work / 'build' / name
        run('cmake', '-S', source, '-B', directory, *generator,
            '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', *options)
        command = ['cmake', '--build', directory, '--config', 'RelWithDebInfo', '--parallel', args.jobs]
        if target:
            command += ['--target', target]
        run(*command)
        if install:
            run('cmake', '--install', directory, '--config', 'RelWithDebInfo')
        return directory

    checkout(work / 'source/validation', LOCK['validation'])
    known_good = json.loads((work / 'source/validation/scripts/known_good.json').read_text())
    for repo in known_good['repos']:
        if repo['name'] in LOCK['validation']['tag_revisions']:
            repo['commit'] = LOCK['validation']['tag_revisions'][repo['name']]
        if 'tests' not in repo.get('optional', []) and len(repo.get('commit', '')) != 40:
            raise RuntimeError(f'Unpinned validation dependency: {repo["name"]}')
    pinned = work / 'validation-dependency-lock'; pinned.mkdir(exist_ok=True)
    (pinned / 'known_good.json').write_text(json.dumps(known_good, indent=2) + '\n')
    deps = work / 'validation-dependencies'
    run(sys.executable, work / 'source/validation/scripts/update_deps.py', '--dir', deps,
        '--known_good_dir', pinned, '--config', 'relwithdebinfo', '--optional=tests',
        '--generator', 'Visual Studio 17 2022' if windows else 'Ninja',
        '--cmake_var', 'CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '--skip-existing-install')
    build('validation', ['-DUPDATE_DEPS=OFF', '-C', str(deps / 'helper.cmake'),
                         '-DBUILD_TESTS=OFF', '-DBUILD_WERROR=OFF',
                         f'-DCMAKE_INSTALL_PREFIX={work / "validation"}'], install=True)
    if args.driver == 'swiftshader':
        checkout(work / 'source/swiftshader', LOCK['swiftshader'])
        directory = build('swiftshader', ['-DSWIFTSHADER_BUILD_TESTS=OFF',
                          '-DBUILD_VULKAN_WRAPPER=OFF', '-DREACTOR_BACKEND=LLVM',
                          '-DREACTOR_EMIT_DEBUG_INFO=OFF', '-DSWIFTSHADER_BUILD_CPPDAP=OFF',
                          '-DSWIFTSHADER_BUILD_BENCHMARKS=OFF', '-DSWIFTSHADER_WARNINGS_AS_ERRORS=OFF',
                          '-DSWIFTSHADER_BUILD_WSI_WAYLAND=OFF'], 'vk_swiftshader')
        manifests = list(directory.rglob('vk_swiftshader_icd.json'))
    else:
        archive = work / 'mesa.tar.xz'
        if not archive.exists():
            urllib.request.urlretrieve(LOCK['lavapipe']['url'], archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != LOCK['lavapipe']['sha256']:
            raise RuntimeError('Mesa archive checksum mismatch')
        source = work / 'source/mesa-25.2.4'
        if not source.exists():
            with tarfile.open(archive) as package:
                package.extractall(work / 'source', filter='data')
        directory = work / 'build/lavapipe'
        if not (directory / 'build.ninja').exists():
            run('meson', 'setup', directory, source, '--buildtype=debugoptimized',
                f'--prefix={work / "lavapipe"}', '-Dvulkan-drivers=swrast',
                '-Dgallium-drivers=', '-Dplatforms=x11', '-Dglx=disabled', '-Degl=disabled',
                '-Dgles1=disabled', '-Dgles2=disabled', '-Dopengl=false', '-Dllvm=enabled',
                '-Dshared-llvm=enabled', '-Dvideo-codecs=', '-Dbuild-tests=false')
        run('meson', 'compile', '-C', directory, '-j', args.jobs)
        run('meson', 'install', '-C', directory)
        manifests = list((work / 'lavapipe').rglob('lvp_icd*.json'))
    layers = list((work / 'validation').rglob('VkLayer_khronos_validation.json'))
    if len(manifests) != 1 or len(layers) != 1:
        raise RuntimeError(f'Expected one ICD and one layer manifest: {manifests}, {layers}')
    icd = stage_manifest(manifests[0], work / 'staged/icd', 'ICD')
    layer = stage_manifest(layers[0], work / 'staged/layers', 'layer', work / 'validation')
    licenses = work / 'staged/licenses'; licenses.mkdir(parents=True, exist_ok=True)
    for name, source in [('validation', work / 'source/validation'),
                         ('validation-dependencies', deps),
                         (args.driver, work / ('source/swiftshader' if args.driver == 'swiftshader' else 'source/mesa-25.2.4'))]:
        for path in source.rglob('*'):
            if path.is_file() and path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')) and '.git' not in path.parts:
                target = licenses / name / path.relative_to(source); target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    result = {'driver': args.driver, 'icd': str(icd), 'layer_path': str(layer.parent),
              'sources': LOCK, 'platform': platform.platform(),
              'staged_sha256': {str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (work / 'staged').rglob('*') if p.is_file()},
              'validation_known_good_sha256': hashlib.sha256(
                  (pinned / 'known_good.json').read_bytes()).hexdigest()}
    (work / 'runtime.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
