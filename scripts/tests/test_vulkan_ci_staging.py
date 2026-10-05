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
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name)
        self.build = self.workspace / 'build'
        self.stage = self.build / 'newview/RelWithDebInfo'
        files = ['vulkanstorm-bin.exe', 'vulkan-1.dll', 'GraphicsEngineVk_64r.dll',
                 'app_settings/message_template.msg', 'skins/default/xui/en/test.xml',
                 'fonts/test.ttf', 'app_settings/shaders/test.glsl',
                 'licenses/vulkan-ghi/test.txt', 'SLPlugin.exe', 'llplugin/media_plugin_cef.dll']
        for name in files:
            path = self.stage / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'test')
        (self.build / 'CMakeCache.txt').write_text('USE_DILIGENTCORE:BOOL=ON\nPACKAGE:BOOL=OFF\nCMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n')
        metadata = self.build / 'packages/metadata'; metadata.mkdir(parents=True)
        for name in ('vulkan', 'diligentcore'):
            (metadata / f'{name}.json').write_text('{}')
        evidence = self.workspace / '.ci/evidence'; evidence.mkdir(parents=True)
        hashes = {name: hashlib.sha256(b'test').hexdigest() for name in ('vulkan-1.dll', 'GraphicsEngineVk_64r.dll')}
        (evidence / 'results.json').write_text(json.dumps({'staged_library_sha256': hashes}))
        system = patch('check_vulkan_ci_staging.platform.system', return_value='Windows'); system.start(); self.addCleanup(system.stop)

    def test_complete_stage(self):
        check(self.build)

    def test_wrong_library_bytes(self):
        (self.stage / 'vulkan-1.dll').write_bytes(b'wrong')
        with self.assertRaisesRegex(RuntimeError, 'GHI bytes differ'):
            check(self.build)

    def test_missing_shader_assets(self):
        (self.stage / 'app_settings/shaders/test.glsl').unlink()
        with self.assertRaisesRegex(RuntimeError, 'asset group'):
            check(self.build)

    def test_missing_plugin(self):
        (self.stage / 'SLPlugin.exe').unlink()
        with self.assertRaisesRegex(RuntimeError, 'plugin host'):
            check(self.build)

    def test_installer_configuration_rejected(self):
        (self.build / 'CMakeCache.txt').write_text('USE_DILIGENTCORE:BOOL=ON\nPACKAGE:BOOL=ON\n')
        with self.assertRaisesRegex(RuntimeError, 'build setting'):
            check(self.build)


if __name__ == '__main__':
    unittest.main()
