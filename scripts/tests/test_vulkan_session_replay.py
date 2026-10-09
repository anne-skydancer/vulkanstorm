"""Connected-session evidence must fail for missing stages and corrupt diagnostics."""
import sys
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import assess, read_llsd, REQUIRED

class SessionEvidenceTests(unittest.TestCase):
    def record(self):
        return dict(schema=1, mode='viewer-native-session-replay', passed=True,
                    world_owners=0, live_server_qualified=False, sent=8, expired=2,
                    **{name: True for name in REQUIRED})

    def test_complete_replay(self):
        self.assertTrue(assess(self.record(), 0, 'Native Vulkan normal login shut down'))

    def test_every_stage_is_decisive(self):
        for name in REQUIRED:
            record = self.record(); del record[name]
            with self.subTest(name=name):
                self.assertFalse(assess(record, 0, 'Native Vulkan normal login shut down'))

    def test_validation_crash_and_unobserved_shutdown_fail(self):
        for code, log in ((143, ''), (0, ''), (0, 'VUID-test Native Vulkan normal login shut down'),
                          (0, 'Forbidden GL path Native Vulkan normal login shut down')):
            self.assertFalse(assess(self.record(), code, log))

    def test_replay_cannot_claim_live_acceptance(self):
        record = self.record(); record['live_server_qualified'] = True
        self.assertFalse(assess(record, 0, 'Native Vulkan normal login shut down'))

    def test_malformed_map_fails(self):
        with self.assertRaises(ValueError):
            read_llsd(ET.fromstring('<llsd><map><key>passed</key></map></llsd>'))

if __name__ == '__main__':
    unittest.main()
