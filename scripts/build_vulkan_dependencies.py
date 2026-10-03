"""Build pinned Windows/Linux x64 Autobuild packages for the Vulkan GHI.

Creates a separate Autobuild install configuration with real local archives and
hashes. It does not add unpublished URLs to the viewer's default installables.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / '3p/vulkan-dependencies.json').read_text())


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def checkout(directory, pin):
    if not directory.exists():
        run('git', 'clone', pin['url'], directory)
        run('git', '-C', directory, 'checkout', '--detach', pin['revision'])
    actual = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != pin['revision']:
        raise RuntimeError(f'{directory}: expected {pin["revision"]}, got {actual}')
    if subprocess.check_output(['git', '-C', str(directory), 'status', '--porcelain'], text=True).strip():
        raise RuntimeError(f'{directory}: source checkout is dirty')
    if pin.get('recursive'):
        run('git', '-C', directory, 'submodule', 'update', '--init', '--recursive')


def copy_headers(source, destination):
    for file in source.rglob('*'):
        if file.is_file() and file.suffix in ('.h', '.hpp', '.inl'):
            target = destination / file.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)


def encode(value):
    if isinstance(value, list):
        node = ET.Element('array')
        for item in value:
            node.append(encode(item))
        return node
    if isinstance(value, bool):
        node = ET.Element('boolean')
        node.text = 'true' if value else 'false'
        return node
    if isinstance(value, dict):
        node = ET.Element('map')
        for key, item in value.items():
            ET.SubElement(node, 'key').text = key
            node.append(encode(item))
        return node
    node = ET.Element('string')
    node.text = str(value)
    return node


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, default=ROOT / '.tmp/vulkan-dependencies')
    parser.add_argument('--sources', type=Path)
    parser.add_argument('--install-dir', type=Path, help='Install packages using Autobuild after packaging')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 2))
    parser.add_argument('--autobuild', default=os.environ.get('AUTOBUILD', 'autobuild'))
    parser.add_argument('--install-only', action='store_true', help='Install previously built packages without rebuilding')
    args = parser.parse_args()
    if platform.machine().lower() not in ('amd64', 'x86_64') or platform.system() not in ('Windows', 'Linux'):
        parser.error('Only Windows/Linux x64 are supported')
    windows = platform.system() == 'Windows'
    host = 'windows64' if windows else 'linux64'
    work = args.work_dir.resolve()
    config_path = work / 'autobuild.xml'
    if args.install_only:
        if not args.install_dir or not config_path.is_file():
            parser.error('--install-only requires --install-dir and an existing package configuration')
        run(args.autobuild, 'install', '--config-file', config_path, '-A', '64', '--install-dir', args.install_dir.resolve(), 'vulkan', 'diligentcore')
        return
    sources = (args.sources or work / 'sources').resolve()
    sources.mkdir(parents=True, exist_ok=True)
    for package in LOCK.values():
        for name, pin in package['sources'].items():
            checkout(sources / name, pin)
    common = ['-G', 'Visual Studio 17 2022', '-A', 'x64'] if windows else ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo']
    def build(source, name, options, target=None):
        directory = work / 'build' / name
        run('cmake', '-S', source, '-B', directory, *common, *options)
        command = ['cmake', '--build', directory, '--config', 'RelWithDebInfo', '--parallel', args.jobs]
        if target:
            command += ['--target', target]
        run(*command)
        return directory
    header_install = work / 'headers'
    build(sources / 'vulkan-headers', 'headers', [f'-DCMAKE_INSTALL_PREFIX={header_install}'])
    run('cmake', '--install', work / 'build/headers', '--config', 'RelWithDebInfo')
    loader = build(sources / 'vulkan-loader', 'loader', [f'-DCMAKE_PREFIX_PATH={header_install}', '-DBUILD_TESTS=OFF', '-DBUILD_WERROR=OFF'])
    diligent = build(sources / 'diligentcore', 'diligentcore', [
        f'-DCMAKE_PROJECT_DiligentCore_INCLUDE={ROOT / "3p/3p-diligentcore/vulkan-headers.cmake"}',
        f'-DVULKAN_HEADERS_SOURCE_DIR={sources / "vulkan-headers"}',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DDILIGENT_BUILD_TESTS=OFF',
        '-DDILIGENT_NO_DIRECT3D11=ON', '-DDILIGENT_NO_DIRECT3D12=ON', '-DDILIGENT_NO_OPENGL=ON',
        '-DDILIGENT_NO_METAL=ON', '-DDILIGENT_NO_WEBGPU=ON', '-DDILIGENT_NO_ARCHIVER=ON',
        '-DDILIGENT_NO_SUPER_RESOLUTION=ON', '-DDILIGENT_NO_HLSL=ON', '-DDILIGENT_NO_VULKAN=OFF',
        '-DDILIGENT_INSTALL_CORE=OFF', '-DDILIGENT_DEVELOPMENT=ON'
    ], 'Diligent-GraphicsEngineVk-shared')
    installables = {}
    for name, metadata in LOCK.items():
        package = work / 'packages' / name
        if package.exists():
            raise RuntimeError(f'{package} already exists; use a fresh work directory for packaging')
        (package / 'LICENSES').mkdir(parents=True)
        if name == 'vulkan':
            shutil.copytree(header_install / 'include', package / 'include')
            for license in (sources / 'vulkan-headers/LICENSES').glob('*.txt'):
                shutil.copy2(license, package / 'LICENSES' / ('Vulkan-Headers-' + license.name))
            for license in (sources / 'vulkan-loader').glob('LICENSE*'):
                if license.is_file():
                    shutil.copy2(license, package / 'LICENSES' / ('Vulkan-Loader-' + license.name))
                elif license.is_dir():
                    shutil.copytree(license, package / 'LICENSES' / ('Vulkan-Loader-' + license.name))
            products = [loader / 'loader' / 'RelWithDebInfo' / 'vulkan-1.dll', loader / 'loader' / 'RelWithDebInfo' / 'vulkan-1.lib'] if windows else [loader / 'loader/libvulkan.so.1', loader / 'loader/libvulkan.so']
            for product in products:
                destination = package / ('bin/release' if product.suffix == '.dll' else 'lib/release') / product.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(product, destination)
        else:
            for section in ('Common', 'Graphics', 'Platforms', 'Primitives'):
                copy_headers(sources / 'diligentcore' / section, package / 'include/DiligentCore' / section)
            for source_license in (sources / 'diligentcore').rglob('*'):
                if source_license.is_file() and source_license.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')) and '.git' not in source_license.parts:
                    target = package / 'LICENSES/DiligentCore' / source_license.relative_to(sources / 'diligentcore')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source_license, target)
            names = ['GraphicsEngineVk_64r.dll', 'GraphicsEngineVk_64r.lib'] if windows else ['libGraphicsEngineVk.so']
            for product_name in names:
                matches = [p for p in diligent.rglob(product_name) if p.is_file()]
                if len(matches) != 1:
                    raise RuntimeError(f'Expected one {product_name}, got {matches}')
                destination = package / ('bin/release' if product_name.endswith('.dll') else 'lib/release') / product_name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(matches[0], destination)
        (package / 'metadata').mkdir()
        (package / 'metadata' / f'{name}.json').write_text(json.dumps(metadata, indent=2)+'\n')
        package_description = {'name':name, 'version':metadata['version'],
            'copyright':'Khronos Group and contributors' if name == 'vulkan' else '2015-2019 Egor Yusov; 2019-2026 Diligent Graphics LLC and third-party contributors',
            'license':'Apache-2.0/MIT' if name == 'vulkan' else 'Apache-2.0 and third-party licenses',
            'license_file':'LICENSES/Vulkan-Headers-Apache-2.0.txt' if name == 'vulkan' else 'LICENSES/DiligentCore/License.txt'}
        build_id = next(iter(metadata['sources'].values()))['revision'][:12]
        package_metadata = ET.Element('llsd')
        package_metadata.append(encode({'version':'1', 'type':'metadata',
            'package_description':package_description, 'platform':host, 'build_id':build_id,
            'configuration':'RelWithDebInfo', 'dependencies':{},
            'manifest':sorted(p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file())}))
        ET.indent(package_metadata)
        ET.ElementTree(package_metadata).write(package / 'autobuild-package.xml', encoding='utf-8', xml_declaration=True)
        archive = work / f'{name}-{metadata["version"]}-{host}-{build_id}.tar.bz2'
        with tarfile.open(archive, 'w:bz2') as output:
            for child in sorted(package.iterdir()):
                output.add(child, arcname=child.name)
        installables[name] = dict(package_description, platforms={host:{'name':host,'archive':{'url':archive.as_uri(),'hash':hashlib.sha256(archive.read_bytes()).hexdigest(),'hash_algorithm':'sha256'}}})
    config = ET.Element('llsd')
    config.append(encode({'version':'1.3','type':'autobuild',
                         'package_description': {'name':'vulkanstorm-ghi', 'platforms':{host:{'name':host,
                             'configurations':{'RelWithDebInfo':{'name':'RelWithDebInfo', 'default':True}}}}},
                         'installables':installables}))
    ET.indent(config)
    config_path = work / 'autobuild.xml'
    ET.ElementTree(config).write(config_path, encoding='utf-8', xml_declaration=True)
    if args.install_dir:
        run(args.autobuild, 'install', '--config-file', config_path, '-A', '64', '--install-dir', args.install_dir.resolve(), 'vulkan', 'diligentcore')
    print(f'Autobuild configuration: {config_path}')


if __name__ == '__main__':
    main()
