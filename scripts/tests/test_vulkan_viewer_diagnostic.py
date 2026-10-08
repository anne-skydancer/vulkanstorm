"""Viewer WSI acceptance must reject incomplete lifecycle and unrelated failures."""
import copy
from pathlib import Path
import sys
import shutil
import subprocess
import tempfile
import unittest
from render_backend_selector_fixture import SelectorTests

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
    def test_restored_floaters_are_denied_before_settings_and_callbacks(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llui/llfloaterreg.cpp').read_text()
        start = source.index('void LLFloaterReg::showInitialVisibleInstances()')
        opening = source.index('{', start); depth = 1; end = opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}'); end += 1
        fixture = r'''
#include "vsuiadmission.h"
#include <cassert>
#include <map>
#include <string>
struct LLSD {};
struct LLUICtrl { enum { TT_INACTIVE }; };
int settings_reads=0,callbacks=0,transparency=0;
struct Settings {
    bool controlExists(const std::string&) { ++settings_reads;return true; }
    bool getBOOL(const std::string&) { ++settings_reads;return true; }
};
struct LLFloater {
    static Settings* getControlGroup() { static Settings s;return &s; }
    void updateTransparency(int) { ++transparency; }
};
struct LLFloaterReg {
    using build_map_t=std::map<std::string,int>;
    inline static build_map_t sBuildMap{{"media",0},{"agreement",0}};
    static std::string getVisibilityControlName(const std::string& n) { return n; }
    static LLFloater* showInstance(const std::string& n,const LLSD&) {
        ++callbacks;static LLFloater f;return n=="media"?&f:nullptr;
    }
    static void showInitialVisibleInstances();
};
''' + source[start:end] + r'''
int main() {
    {
        VSUIAdmission admission([](const std::type_info&) { return false; },
                                [](std::string_view n) { return n=="agreement"; });
        LLFloaterReg::showInitialVisibleInstances();
        assert(settings_reads==2 && callbacks==1 && transparency==0);
    }
    settings_reads=callbacks=transparency=0;
    LLFloaterReg::showInitialVisibleInstances();
    assert(settings_reads==4 && callbacks==2 && transparency==1);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'restored_floaters.cpp'; path.write_text(fixture)
            exe = Path(temp) / 'restored_floaters.exe'
            built = subprocess.run([compiler, '-std=c++17', '-I' + str(root / 'indra/llui'),
                                    str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_admission_scope_is_exclusive_and_restores_legacy_policy(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        fixture = r'''
#include "vsuiadmission.h"
#include <cassert>
struct Required {};
struct Optional {};
int main() {
    assert(VSUIAdmission::widget(typeid(Optional)));
    assert(VSUIAdmission::floater("media"));
    assert(VSUIAdmission::panelFactory("media_panel"));
    try {
        VSUIAdmission scope([](const std::type_info& t) { return t==typeid(Required); },
                            [](std::string_view n) { return n=="agreement"; });
        assert(VSUIAdmission::widget(typeid(Required)));
        assert(!VSUIAdmission::widget(typeid(Optional)));
        assert(VSUIAdmission::floater("agreement"));
        assert(!VSUIAdmission::floater("media"));
        assert(!VSUIAdmission::panelFactory("media_panel"));
        bool rejected=false;
        try {
            VSUIAdmission nested([](const std::type_info&) { return true; },
                                 [](std::string_view) { return true; });
        } catch (const std::logic_error&) { rejected=true; }
        assert(rejected && !VSUIAdmission::widget(typeid(Optional)));
        throw 7;
    } catch (int) {}
    assert(VSUIAdmission::widget(typeid(Optional)));
    assert(VSUIAdmission::floater("media"));
    assert(VSUIAdmission::panelFactory("media_panel"));
    bool rejected=false;
    try { VSUIAdmission incomplete({},[](std::string_view) { return true; }); }
    catch (const std::logic_error&) { rejected=true; }
    assert(rejected && VSUIAdmission::widget(typeid(Optional)));
}
'''
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'admission.cpp'; source.write_text(fixture)
            exe = Path(temp) / 'admission.exe'
            built = subprocess.run([compiler, '-std=c++17', '-I' + str(Path(__file__).resolve().parents[2] / 'indra/llui'),
                                    str(source), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

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
        good.update(ui_fixture_enabled=True, ui_readback_verified=True, ui_readbacks=2,
                    ui_facade_verified=True, ui_atlas_verified=True, ui_font_producer_verified=True,
                    ui_admission_verified=True)
        self.assertTrue(assess(good, 0, log, 'ui-positive', 'Windows'))
        for key, value in [('ui_readbacks', 1), ('ui_readback_verified', False), ('ui_fixture_enabled', False),
                           ('ui_facade_verified', False), ('ui_atlas_verified', False),
                           ('ui_font_producer_verified', False), ('ui_admission_verified', False)]:
            with self.subTest(key=key):
                bad = copy.deepcopy(good); bad[key] = value
                self.assertFalse(assess(bad, 0, log, 'ui-positive', 'Windows'))
        good.update(passed=False, failure='Viewer UI pixel oracle mismatch', ui_fixture_enabled=True)
        for case in ('bad-ui', 'ui-orientation'):
            self.assertTrue(assess(good, 1, 'FAIL viewer-native-diagnostic', case, 'Windows'))
            self.assertFalse(assess(good, 0, log, case, 'Windows'))


if __name__ == '__main__':
    unittest.main()
