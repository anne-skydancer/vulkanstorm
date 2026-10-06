"""Ensure negative probes cannot pass on crashes, missing layers or missing evidence."""
from pathlib import Path
from contextlib import nullcontext
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_software_vulkan_tests import assess, windows_manifest_registration


class RunnerFailureTests(unittest.TestCase):
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
