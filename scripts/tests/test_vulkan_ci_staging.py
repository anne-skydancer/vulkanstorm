"""Reject incomplete stages and libraries different from the tested GHI."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_vulkan_ci_staging import check


class StagingFailureTests(unittest.TestCase):
    system = 'Windows'

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name)
        self.build = self.workspace / 'build'
        windows = self.system == 'Windows'
        self.stage = self.build / ('newview/RelWithDebInfo' if windows else 'newview/packaged')
        self.libraries = ('vulkan-1.dll', 'GraphicsEngineVk_64r.dll') if windows else ('libvulkan.so.1', 'libGraphicsEngineVk.so')
        self.library_dir = self.stage if windows else self.stage / 'lib'
        self.host = self.stage / ('SLPlugin.exe' if windows else 'bin/SLPlugin')
        self.media = self.stage / ('llplugin/media_plugin_cef.dll' if windows else 'bin/llplugin/media_plugin_cef.so')
        self.cef_runtime = ['llplugin/vulkan-1.dll', 'llplugin/libcef.dll', 'llplugin/dullahan_host.exe'] if windows else ['lib/libcef.so', 'bin/dullahan_host']
        platform_files = ['vulkanstorm-bin.exe', *self.libraries] if windows else ['bin/vulkanstorm-bin', *(f'lib/{name}' for name in self.libraries)]
        files = [*platform_files, *self.cef_runtime,
                 'app_settings/message_template.msg', 'skins/default/xui/en/test.xml',
                 'fonts/test.ttf', 'app_settings/shaders/test.glsl',
                 'licenses/vulkan-ghi/test.txt', str(self.host.relative_to(self.stage)), str(self.media.relative_to(self.stage))]
        for name in files:
            path = self.stage / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'test')
        (self.build / 'CMakeCache.txt').write_text('USE_DILIGENTCORE:BOOL=ON\nPACKAGE:BOOL=OFF\nCMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n')
        metadata = self.build / 'packages/metadata'; metadata.mkdir(parents=True)
        for name in ('vulkan', 'diligentcore'):
            (metadata / f'{name}.json').write_text('{}')
        evidence = self.workspace / '.ci/evidence'; evidence.mkdir(parents=True)
        hashes = {name: hashlib.sha256(b'test').hexdigest() for name in self.libraries}
        (evidence / 'results.json').write_text(json.dumps({'staged_library_sha256': hashes}))
        system = patch('check_vulkan_ci_staging.platform.system', return_value=self.system); system.start(); self.addCleanup(system.stop)

    def test_complete_stage(self):
        check(self.build)

    def test_wrong_library_bytes(self):
        (self.library_dir / self.libraries[0]).write_bytes(b'wrong')
        with self.assertRaisesRegex(RuntimeError, 'GHI bytes differ'):
            check(self.build)

    def test_missing_shader_assets(self):
        (self.stage / 'app_settings/shaders/test.glsl').unlink()
        with self.assertRaisesRegex(RuntimeError, 'asset group'):
            check(self.build)

    def test_missing_plugin(self):
        self.host.unlink()
        with self.assertRaisesRegex(RuntimeError, 'plugin host'):
            check(self.build)

    def test_missing_media_plugin(self):
        self.media.unlink()
        with self.assertRaisesRegex(RuntimeError, 'media plugin'):
            check(self.build)

    def test_missing_cef_runtime(self):
        (self.stage / self.cef_runtime[0]).unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing staged file'):
            check(self.build)

    def test_installer_configuration_rejected(self):
        (self.build / 'CMakeCache.txt').write_text('USE_DILIGENTCORE:BOOL=ON\nPACKAGE:BOOL=ON\n')
        with self.assertRaisesRegex(RuntimeError, 'build setting'):
            check(self.build)


class LinuxStagingFailureTests(StagingFailureTests):
    system = 'Linux'


if __name__ == '__main__':
    unittest.main()
