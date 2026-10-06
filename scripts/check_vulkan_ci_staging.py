"""Verify a complete development stage, without claiming the viewer runs Vulkan."""
import hashlib
import json
from pathlib import Path
import platform
import sys


def check(directory):
    windows = platform.system() == 'Windows'
    root = directory / 'newview' / ('RelWithDebInfo' if windows else 'packaged')
    cache = (directory / 'CMakeCache.txt').read_text()
    for flag in ('USE_DILIGENTCORE:BOOL=ON', 'PACKAGE:BOOL=OFF'):
        if flag not in cache:
            raise RuntimeError(f'Missing required build setting: {flag}')
    if 'CMAKE_BUILD_TYPE:STRING=RelWithDebInfo' not in cache:
        raise RuntimeError('Expected RelWithDebInfo development build')
    executable = list(root.glob('*-bin.exe')) if windows else list((root / 'bin').glob('*-bin'))
    if len(executable) != 1:
        raise RuntimeError(f'Expected one staged viewer executable: {executable}')
    libraries = root if windows else root / 'lib'
    names = ['GraphicsEngineVk_64r.dll', 'vulkan-1.dll'] if windows else ['libGraphicsEngineVk.so', 'libvulkan.so.1']
    cef_runtime = ['llplugin/vulkan-1.dll', 'llplugin/libcef.dll', 'llplugin/dullahan_host.exe'] if windows else ['lib/libcef.so', 'bin/dullahan_host']
    required = [libraries / n for n in names] + [executable[0], root / 'app_settings/message_template.msg'] + [root / n for n in cef_runtime]
    for path in required:
        if not path.is_file():
            raise RuntimeError(f'Missing staged file: {path}')
    for name in names:
        result = directory.parents[0] / '.ci/evidence/results.json'
        comparison = json.loads(result.read_text())['staged_library_sha256']
        if hashlib.sha256((libraries / name).read_bytes()).hexdigest() != comparison[name]:
            raise RuntimeError(f'Viewer/test harness GHI bytes differ: {name}')
    for folder, pattern in [('skins/default/xui/en', '*.xml'), ('fonts', '*.ttf'),
                            ('app_settings/shaders', '*.glsl'), ('licenses/vulkan-ghi', '*')]:
        if not any(path.is_file() for path in (root / folder).rglob(pattern)):
            raise RuntimeError(f'Missing staged asset group: {folder}')
    plugin = root / ('SLPlugin.exe' if windows else 'bin/SLPlugin')
    if not plugin.is_file():
        raise RuntimeError(f'Missing staged plugin host: {plugin}')
    plugins = root / ('llplugin' if windows else 'bin/llplugin')
    if not any(plugins.glob('media_plugin_cef.*')):
        raise RuntimeError('Missing staged media plugin')
    # Both install directories must contain the very same pinned GHI archive bytes.
    packages = directory / 'packages'
    metadata = {name: json.loads((packages / 'metadata' / f'{name}.json').read_text()) for name in ('vulkan', 'diligentcore')}
    record = {'stage': str(root.resolve()), 'metadata': metadata,
              'files': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in required},
              'native_viewer_runtime_qualified': False}
    (directory / 'newview/vulkan-ci-staging.json').write_text(json.dumps(record, indent=2) + '\n')
    print('PASS: complete development stage; no native viewer runtime claim')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
