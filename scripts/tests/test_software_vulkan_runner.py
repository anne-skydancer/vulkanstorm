"""Ensure negative probes cannot pass on crashes, missing layers or missing evidence."""
from pathlib import Path
from contextlib import nullcontext
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_software_vulkan_tests import assess, device_evidence, main, windows_manifest_registration
import json
import os


class RunnerFailureTests(unittest.TestCase):
    def test_generic_identity_accepts_available_software_without_vendor_requirement(self):
        identity = device_evidence('ICD_DEVICE=SwiftShader API=4198400 DRIVER=1 VENDOR=0 DEVICE=0 TYPE=4')
        self.assertEqual(identity['device_type'], 4)
        with self.assertRaisesRegex(RuntimeError, 'requested hardware vendor'):
            device_evidence('ICD_DEVICE=SwiftShader API=4198400 DRIVER=1 VENDOR=0 DEVICE=0 TYPE=4', 'amd')
        with self.assertRaisesRegex(RuntimeError, 'Missing Vulkan device identity'):
            device_evidence('driver enumeration failed')

    def test_headless_system_discovery_does_not_force_an_icd_or_create_a_display(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            executable = root / 'test'; executable.write_bytes(b'executable')
            libraries = ('libvulkan.so.1', 'libGraphicsEngineVk.so')
            for name in libraries: (root / name).write_bytes(b'library')
            runtime = root / 'runtime.json'
            lock = Path(__file__).resolve().parents[1] / 'vulkan_ci_dependencies.json'
            runtime.write_text(json.dumps(dict(driver='installed', vendor=None, icd=None,
                installed_driver=None, device_name='', layer_path=str(root),
                sources=json.loads(lock.read_text()), staged_sha256={})))
            seen = []

            def execute(command, cwd, env, **kwargs):
                self.assertEqual(command[1], 'auto')
                for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'VK_DRIVER_FILES', 'VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES'):
                    self.assertNotIn(name, env)
                mode = command[2]; seen.append(mode)
                identity = 'ICD_DEVICE=SwiftShader API=4198400 DRIVER=1 VENDOR=0 DEVICE=0 TYPE=4\n'
                if mode == 'offscreen':
                    for name in ('first.ppm', 'replacement.ppm'): (cwd / name).write_bytes(b'readback')
                    log = identity + 'DILIGENT_DEVICE=SwiftShader\n' + ''.join('LOADED='+str(root/name)+'\n' for name in libraries) + 'PASS offscreen'
                    return SimpleNamespace(returncode=0, stdout=log)
                diagnostic = {'invalid': 'VUID-VkBufferCreateInfo-size-00912 Validation reported an error',
                              'invalid-sync': 'SYNC-HAZARD-WRITE-AFTER-WRITE Validation reported an error',
                              'bad-pixels': 'Pixel oracle mismatch', 'bad-orientation': 'Pixel oracle mismatch'}[mode]
                return SimpleNamespace(returncode=1, stdout=identity+diagnostic)

            with patch('run_software_vulkan_tests.platform.system', return_value='Linux'), \
                 patch('run_software_vulkan_tests.subprocess.run', side_effect=execute), \
                 patch.dict(os.environ, {'DISPLAY': ':99', 'WAYLAND_DISPLAY': 'wayland-0', 'VK_DRIVER_FILES': 'decoy'}), \
                 patch.object(sys, 'argv', ['runner', '--executable', str(executable), '--runtime', str(runtime), '--evidence', str(root/'evidence'), '--headless']):
                with self.assertRaises(SystemExit) as result: main()
                self.assertEqual(result.exception.code, 0)
            result = json.loads((root/'evidence/results.json').read_text())
            self.assertEqual(len(seen), 5)
            self.assertNotIn('present', seen)
            self.assertTrue(result['headless'] and result['passed'])
            self.assertFalse(result['presentation_qualified'])

    def test_success_requires_device_and_loaded_library_evidence(self):
        self.assertFalse(assess('offscreen', 0, 'PASS offscreen'))
        self.assertTrue(assess('offscreen', 0, 'DILIGENT_DEVICE=SwiftShader\nLOADED=loader\nPASS offscreen'))

    def test_invalid_probe_does_not_accept_crash_or_missing_layer(self):
        self.assertFalse(assess('invalid', 1, 'Required validation layer missing'))
        self.assertFalse(assess('invalid', -11, 'VUID-VkBufferCreateInfo-size-00912'))
        self.assertFalse(assess('invalid', 0, 'VUID-VkBufferCreateInfo-size-00912 Validation reported an error'))
        self.assertTrue(assess('invalid', 1, 'VUID-VkBufferCreateInfo-size-00912 Validation reported an error'))

    def test_bad_pixels_must_reach_oracle(self):
        self.assertFalse(assess('bad-pixels', 1, 'Shader compilation failed'))
        self.assertTrue(assess('bad-pixels', 1, 'Pixel oracle mismatch'))
        self.assertFalse(assess('bad-orientation', 1, 'Shader compilation failed'))
        self.assertTrue(assess('bad-orientation', 1, 'Pixel oracle mismatch'))

    def test_sync_probe_requires_actual_hazard(self):
        self.assertFalse(assess('invalid-sync', 1, 'Validation reported an error'))
        self.assertTrue(assess('invalid-sync', 1, 'SYNC-HAZARD-WRITE-AFTER-WRITE Validation reported an error'))

    def test_expected_failure_does_not_hide_unrelated_validation_error(self):
        log = ('VALIDATION VUID-VkBufferCreateInfo-size-00912: expected\n'
               'VALIDATION VUID-vkDestroyDevice-device-00378: unrelated\n'
               'Validation reported an error')
        self.assertFalse(assess('invalid', 1, log))
        self.assertFalse(assess('bad-pixels', 1, 'VALIDATION UNASSIGNED-Test: unexpected\nPixel oracle mismatch'))
        self.assertFalse(assess('bad-pixels', 1, 'DILIGENT 2: unexpected error\nPixel oracle mismatch'))


class WindowsRegistrationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.icd = self.root / 'icd.json'; self.icd.write_text('{}')
        self.layer = self.root / 'VkLayer_khronos_validation.json'; self.layer.write_text('{}')
        self.runtime = {'icd': str(self.icd), 'layer_path': str(self.root)}
        self.values = {}

        def query(key, name):
            try:
                return self.values[key, name]
            except KeyError:
                raise FileNotFoundError(name)

        def delete(key, name):
            try:
                del self.values[key, name]
            except KeyError:
                raise FileNotFoundError(name)

        registry = SimpleNamespace(KEY_READ=1, KEY_WRITE=2, KEY_WOW64_64KEY=4,
            HKEY_LOCAL_MACHINE=0, REG_DWORD=4,
            CreateKeyEx=lambda hive, key, reserved, access: nullcontext(key),
            QueryValueEx=query, DeleteValue=delete,
            SetValueEx=lambda key, name, reserved, kind, value: self.values.update({(key, name): (value, kind)}))
        for replacement in [patch.dict(sys.modules, {'winreg': registry}),
                            patch('run_software_vulkan_tests.platform.system', return_value='Windows'),
                            patch.dict('os.environ', {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'github-hosted'})]:
            replacement.start(); self.addCleanup(replacement.stop)

    def test_restores_previous_value_and_removes_new_registration(self):
        original_key = ('SOFTWARE\\Khronos\\Vulkan\\Drivers', str(self.icd.resolve()))
        self.values[original_key] = (1, 4)
        with windows_manifest_registration(self.runtime, True):
            self.assertEqual(self.values[original_key], (0, 4))
            self.assertEqual(len(self.values), 2)
        self.assertEqual(self.values, {original_key: (1, 4)})

    def test_test_failure_cleans_up_registration(self):
        with self.assertRaisesRegex(ValueError, 'test failure'):
            with windows_manifest_registration(self.runtime, True):
                raise ValueError('test failure')
        self.assertEqual(self.values, {})

    def test_partial_setup_failure_cleans_up_registration(self):
        self.layer.unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing registered manifest'):
            with windows_manifest_registration(self.runtime, True):
                self.fail('Must not run tests')
        self.assertEqual(self.values, {})

    def test_local_or_self_hosted_registration_is_rejected(self):
        with patch.dict('os.environ', {'RUNNER_ENVIRONMENT': 'self-hosted'}):
            with self.assertRaisesRegex(RuntimeError, 'restricted to hosted'):
                with windows_manifest_registration(self.runtime, True):
                    self.fail('Must not register')
        self.assertEqual(self.values, {})


if __name__ == '__main__':
    unittest.main()
