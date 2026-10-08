"""Device-free selector coverage included by the viewer diagnostic CI suite."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    start = source.index('void LLPanelPreferenceGraphics::' + name + '(')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class SelectorTests(unittest.TestCase):
    def test_peer_options_and_availability_message(self):
        xui = ROOT / 'indra/newview/skins/default/xui/en'
        panel = ET.parse(xui / 'panel_preferences_graphics1.xml').getroot()
        combo = panel.find('.//combo_box[@name="render_backend"]')
        self.assertEqual([item.get('value') for item in combo], ['OpenGL', 'Zink', 'Vulkan'])
        self.assertNotEqual(combo.find('./combo_box.item[@value="Vulkan"]').get('enabled'), 'false')
        notices = ET.parse(xui / 'notifications.xml').getroot()
        self.assertIsNotNone(notices.find('./notification[@name="VulkanRendererUnavailable"]'))

    def test_actual_callbacks_preserve_identity_and_reject_unavailable_sessions(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        source = (ROOT / 'indra/newview/llfloaterpreference.cpp').read_text()
        methods = '\n'.join(function(source, name) for name in (
            'refreshRenderBackendSelector', 'onRenderBackendCommit', 'callbackRenderBackendRestart'))
        fixture = r'''
#include "vsrenderbackend.h"
#include <cassert>
#include <map>
#include <string>
struct LLSD {
    std::string value;
    LLSD()=default; LLSD(const std::string& s):value(s) {}
    LLSD& operator[](const char*) { return *this; }
    LLSD& operator=(const std::string& s) { value=s; return *this; }
};
struct Settings {
    std::map<std::string,std::string> values;
    std::string getString(const char* key) { return values[key]; }
    void setString(const char* key,const std::string& s) { values[key]=s; }
} gSavedSettings;
struct LLComboBox { std::string value; void setValue(LLSD s) { value=s.value; } void resetDirty() {} };
std::string notice;
int response_option=0;
namespace LLNotificationsUtil {
    template<class... T> void add(const char* name,T...) { notice=name; }
    int getSelectedOption(const LLSD&,const LLSD&) { return response_option; }
}
using S32=int;
constexpr int _1=1,_2=2;
namespace boost { template<class... T> int bind(T...) { return 0; } }
struct LLAppViewer {
    bool quit=false;
    static LLAppViewer* instance() { static LLAppViewer a; return &a; }
    void requestQuit() { quit=true; }
};
struct Log { template<class T> Log& operator<<(T) { return *this; } };
#define LL_INFOS() Log{}
#define LL_ENDL 0
struct LLPanelPreferenceGraphics {
    LLComboBox combo;
    template<class T> T* getChild(const char*) { return &combo; }
    void refreshRenderBackendSelector(); void onRenderBackendCommit();
    void callbackRenderBackendRestart(const LLSD&,const LLSD&);
};
''' + methods + r'''
int main() {
    LLPanelPreferenceGraphics panel;
    for (const auto& active : {"OpenGL","Zink","Vulkan"}) {
        gSavedSettings.setString("RenderBackend",active);
        panel.refreshRenderBackendSelector(); assert(panel.combo.value==active);
    }
    gSavedSettings.setString("RenderBackend","OpenGL");
    gSavedSettings.setString("RenderBackendPending","Vulkan");
    panel.onRenderBackendCommit(); assert(notice=="VulkanRendererUnavailable");
    assert(panel.combo.value=="OpenGL"); assert(gSavedSettings.getString("RenderBackend")=="OpenGL");
    notice.clear(); panel.callbackRenderBackendRestart({},{});
    assert(notice=="VulkanRendererUnavailable"); assert(!LLAppViewer::instance()->quit);
    for (const auto& next : {"OpenGL","Zink"}) {
        gSavedSettings.setString("RenderBackendPending",next);
        panel.callbackRenderBackendRestart({},{});
        assert(gSavedSettings.getString("RenderBackend")==next); assert(LLAppViewer::instance()->quit);
        LLAppViewer::instance()->quit=false;
    }
    gSavedSettings.setString("RenderBackend","Zink");
    gSavedSettings.setString("RenderBackendPending","OpenGL"); response_option=1;
    panel.callbackRenderBackendRestart({},{});
    assert(gSavedSettings.getString("RenderBackend")=="Zink"); assert(panel.combo.value=="Zink");
    assert(!LLAppViewer::instance()->quit);
    assert(!VSRenderBackend::canStartSession("Vulkan"));
    assert(VSRenderBackend::canStartSession("OpenGL") && VSRenderBackend::canStartSession("Zink"));
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'selector.cpp'; path.write_text(fixture)
            exe = Path(temp) / 'selector.exe'
            built = subprocess.run([compiler, '-std=c++17', '-I' + str(ROOT / 'indra/newview'),
                                    str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            subprocess.run([str(exe)], check=True)

    def test_startup_rejection_precedes_gl_provider_selection(self):
        source = (ROOT / 'indra/newview/llappviewer.cpp').read_text()
        init = source[source.index('bool LLAppViewer::init()'):source.index('bool LLAppViewer::frame()')]
        guard = init.index('if (!VSRenderBackend::canStartSession(')
        stop = init.index('return false;', guard)
        self.assertLess(stop, init.index('selectGLBackend();'))
        self.assertLess(stop, init.index('initWindow()'))
        window = source[source.index('bool LLAppViewer::initWindow()'):]
        rejection = window[window.index('if (render_backend == "Vulkan")'):window.index('else if (render_backend == "Zink")')]
        self.assertIn('return false;', rejection)
        self.assertNotIn('render_backend = "OpenGL"', rejection)


if __name__ == '__main__':
    unittest.main()
