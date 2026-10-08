"""Viewer WSI acceptance must reject incomplete lifecycle and unrelated failures."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_vulkan_viewer_diagnostic import assess, STAGES


def record():
    return dict(schema=1, mode='viewer-native-diagnostic',
                application_lifecycle='LLAppViewer::init/frame/cleanup',
                window_factory='LLWindowManager::createWindow', window_api='Win32',
                stages=STAGES.copy(), presented_frames=9, zero_extent_skips=3,
                minimized_skips=2, native_minimize_observed=True, resize_events=2,
                shutdown_complete=True, validation_errors=0, clear_readback_verified=True,
                passed=True, failure='')


class AcceptanceTests(unittest.TestCase):
    def test_positive_requires_actual_viewer_contract(self):
        good = record()
        log = 'PASS viewer-native-diagnostic'
        self.assertTrue(assess(good, 0, log, 'positive', 'Windows'))
        for key, value in [('application_lifecycle', 'standalone'), ('stages', STAGES[:-1]),
                           ('shutdown_complete', False), ('presented_frames', 8),
                           ('native_minimize_observed', False), ('validation_errors', 1),
                           ('resize_events', 0), ('clear_readback_verified', False), ('passed', False)]:
            with self.subTest(key=key):
                bad = copy.deepcopy(good); bad[key] = value
                self.assertFalse(assess(bad, 0, log, 'positive', 'Windows'))
        self.assertFalse(assess(good, -1, log, 'positive', 'Windows'))
        self.assertFalse(assess(good, 0, log + '\nDILIGENT 2: error', 'positive', 'Windows'))

    def test_negative_requires_expected_error_and_completed_cleanup(self):
        bad = record(); bad.update(passed=False, failure='Injected failure: after-device')
        log = 'FAIL viewer-native-diagnostic'
        self.assertTrue(assess(bad, 1, log, 'after-device', 'Windows'))
        self.assertFalse(assess(bad, -1073741819, log, 'after-device', 'Windows'))
        self.assertFalse(assess(bad, 1, log, 'after-window', 'Windows'))
        bad['shutdown_complete'] = False
        self.assertFalse(assess(bad, 1, log, 'after-device', 'Windows'))

    def test_linux_records_window_manager_limit(self):
        good = record(); good.update(window_api='SDL2/X11', native_minimize_observed=False)
        self.assertTrue(assess(good, 0, 'PASS viewer-native-diagnostic', 'positive', 'Linux'))
        good['native_minimize_observed'] = None
        self.assertFalse(assess(good, 0, 'PASS viewer-native-diagnostic', 'positive', 'Linux'))

    def test_ui_evidence_cannot_be_replaced_by_clear_success(self):
        good = record()
        log = 'PASS viewer-native-diagnostic'
        self.assertFalse(assess(good, 0, log, 'ui-positive', 'Windows'))
        good.update(ui_fixture_enabled=True, ui_readback_verified=True, ui_readbacks=2)
        self.assertTrue(assess(good, 0, log, 'ui-positive', 'Windows'))
        for key, value in [('ui_readbacks', 1), ('ui_readback_verified', False), ('ui_fixture_enabled', False)]:
            with self.subTest(key=key):
                bad = copy.deepcopy(good); bad[key] = value
                self.assertFalse(assess(bad, 0, log, 'ui-positive', 'Windows'))
        good.update(passed=False, failure='Viewer UI pixel oracle mismatch', ui_fixture_enabled=True)
        for case in ('bad-ui', 'ui-orientation'):
            self.assertTrue(assess(good, 1, 'FAIL viewer-native-diagnostic', case, 'Windows'))
            self.assertFalse(assess(good, 0, log, case, 'Windows'))


if __name__ == '__main__':
    unittest.main()
