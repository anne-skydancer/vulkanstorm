"""Ensure negative probes cannot pass on crashes, missing layers or missing evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_software_vulkan_tests import assess


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


if __name__ == '__main__':
    unittest.main()
