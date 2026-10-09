"""Viewer WSI acceptance must reject incomplete lifecycle and unrelated failures."""
import copy
from pathlib import Path
import sys
import shutil
import subprocess
import tempfile
import unittest
from render_backend_selector_fixture import SelectorTests
from test_vulkan_ui_oracle import OracleTest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_vulkan_viewer_diagnostic import assess, assess_startup, STAGES, skin_cases


def record():
    return dict(schema=1, mode='viewer-native-diagnostic',
                application_lifecycle='LLAppViewer::init/frame/cleanup',
                window_factory='LLWindowManager::createWindow', window_api='Win32',
                stages=STAGES.copy(), presented_frames=9, zero_extent_skips=3,
                minimized_skips=2, native_minimize_observed=True, resize_events=2,
                shutdown_complete=True, validation_errors=0, clear_readback_verified=True,
                passed=True, failure='')


class AcceptanceTests(unittest.TestCase):
    def test_startup_acceptance_requires_viewer_ownership_readbacks_and_decisive_failure(self):
        item=dict(schema=1,mode='viewer-native-startup',shutdown_complete=True,
                  validation_errors=0,login_controls_verified=True,progress_owner_verified=True,
                  stages=['native-viewer-window-created','native-startup-progress-created',
                          'native-login-controller-created','native-startup-ui-released',
                          'native-startup-graphics-released','native-startup-window-released'],
                  modal_alert_verified=True,critical_dialog_verified=True,
                  plain_chat_controls_verified=True,required_dialog_actions_verified=True,
                  mfa_actions_verified=True,login_menus_verified=True,unsupported_ui_status_verified=True,
                  native_dpi_event_verified=True,ime_event_route_verified=True,
                  passed=True,presented_frames=17,readbacks=18,failure='')
        self.assertTrue(assess_startup(item,0,'','startup-positive'))
        for key,value in [('modal_alert_verified',False),('critical_dialog_verified',False),('readbacks',17),('plain_chat_controls_verified',False),('required_dialog_actions_verified',False),('mfa_actions_verified',False),('login_menus_verified',False),('unsupported_ui_status_verified',False),('validation_errors',1),('shutdown_complete',False),
                          ('login_controls_verified',False),('progress_owner_verified',False),('stages',[]),
                          ('stages',list(reversed(item['stages'])))]:
            bad=copy.deepcopy(item);bad[key]=value
            self.assertFalse(assess_startup(bad,0,'','startup-positive'))
        self.assertFalse(assess_startup(item,-11,'','startup-positive'))
        bad=copy.deepcopy(item);bad.update(passed=False,failure='Injected failure: startup-ui',presented_frames=0,readbacks=0)
        self.assertTrue(assess_startup(bad,1,'Injected failure: startup-ui','startup-ui'))
        self.assertFalse(assess_startup(bad,-1073741819,'Injected failure: startup-ui','startup-ui'))
        bad['failure']='Unrelated initialization failure'
        self.assertFalse(assess_startup(bad,1,'Injected failure: startup-ui','startup-ui'))

    def test_skin_matrix_requires_catalog_assets_and_preserves_all_theme_selections(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            translated = stage / 'skins/default/xui/de'
            translated.mkdir(parents=True)
            for filename in ('strings.xml', 'panel_progress_mini.xml'):
                (translated / filename).write_text('<xml/>')
            (stage / 'skins/modern/themes/blue').mkdir(parents=True)
            catalog = stage / 'skins/skins.xml'
            catalog.write_text('''<llsd><array><map><key>folder</key><string>modern</string>
<key>themes</key><array><map><key>folder</key><string/></map>
<map><key>folder</key><string>blue</string></map></array></map></array></llsd>''')
            cases = skin_cases(stage)
            self.assertEqual(cases['skin-modern-base'], ('modern', '', 'en'))
            self.assertEqual(cases['skin-modern-blue'], ('modern', 'blue', 'en'))
            self.assertEqual(cases['skin-default-de'], ('default', '', 'de'))
            (translated / 'strings.xml').unlink()
            with self.assertRaisesRegex(RuntimeError, 'German XUI overlay is missing'):
                skin_cases(stage)
            (translated / 'strings.xml').write_text('<xml/>')
            (stage / 'skins/modern/themes/blue').rmdir()
            with self.assertRaisesRegex(RuntimeError, 'directory is missing'):
                skin_cases(stage)
            catalog.write_text('<llsd><array/></llsd>')
            with self.assertRaisesRegex(RuntimeError, 'catalog is empty'):
                skin_cases(stage)
            catalog.write_text('''<llsd><array><map><key>folder</key><string>../escape</string>
<key>themes</key><array><map><key>folder</key><string/></map></array></map></array></llsd>''')
            with self.assertRaisesRegex(RuntimeError, 'Invalid packaged skin folder'):
                skin_cases(stage)
    def test_win32_button_down_uses_message_coordinates_instead_of_polled_cursor(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llwindow/llwindowwin32.cpp').read_text(encoding='utf-8')
        start = source.index('case WM_LBUTTONDOWN:')
        start = source.index('window_imp->postMouseButtonEvent([=]()', start)
        end = source.index('});', start) + 3
        fixture = r"""
#include <cassert>
using MASK=unsigned;
bool sHandleLeftMouseUp=false;
struct Coord { int x=0,y=0;Coord convert() const { return *this; } };
struct LLWinImm { static bool isAvailable() { return false; } };
struct Keyboard { MASK currentMask(bool) const { return 0; } } keyboard;
auto* gKeyboard=&keyboard;
struct Callbacks {
    Coord hover,down;
    void handleMouseMove(void*,Coord p,MASK) { hover=p; }
    void handleMouseDown(void*,Coord p,MASK) { down=p; }
};
struct Window {
    Coord mCursorPosition{900,800};void* mPreeditor=nullptr;
    Callbacks callbacks;Callbacks* mCallbacks=&callbacks;
    void interruptLanguageTextInput() {}
    template<class F>void postMouseButtonEvent(F f) { f(); }
};
int main() {
    Window window;Window* window_imp=&window;const Coord window_coord{25,65};
""" + source[start:end] + r"""
    assert(sHandleLeftMouseUp);
    assert(window.callbacks.hover.x==25 && window.callbacks.hover.y==65);
    assert(window.callbacks.down.x==25 && window.callbacks.down.y==65);
    assert(window.mCursorPosition.x==25 && window.mCursorPosition.y==65);
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'button_coordinates.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = path.with_suffix('.exe')
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_text_bypasses_gl_display_list_collection_and_replay(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llrender/llfontvertexbuffer.cpp').read_text(encoding='utf-8')
        # Compile the production cache implementation, with only GPU/font endpoints stubbed.
        start = source.index('bool LLFontVertexBuffer::sEnableBufferCollection')
        end = source.index('void LLFontVertexBuffer::renderBuffers()', start)
        font = r"""
#pragma once
#include <list>
#include <string>
#include <climits>
#include <cassert>
using S32=int;using F32=float;using U8=unsigned char;using LLWString=std::wstring;
constexpr int S32_MAX=INT_MAX;
struct LLCoordGL { int value=0;bool operator!=(const LLCoordGL& o) const { return value!=o.value; } };
struct LLColor4 { int value=0;bool operator!=(const LLColor4& o) const { return value!=o.value; } };
struct LLRect { int mLeft=0,mTop=0,mRight=0,mBottom=0; };
struct LLRectf {
    float mLeft,mTop,mRight,mBottom;
    LLRectf(float l,float t,float r,float b):mLeft(l),mTop(t),mRight(r),mBottom(b) {}
    float getCenterY() const { return (mTop+mBottom)/2; }
    float getWidth() const { return mRight-mLeft; }
};
struct LLVertexBufferData {};
struct LLFontGL {
    enum HAlign { LEFT };enum VAlign { TOP,VCENTER,BASELINE,BOTTOM };
    enum ShadowType { NO_SHADOW };enum { NORMAL };
    inline static bool sDisplayFont=true,native=false;
    inline static float sScaleX=1,sScaleY=1,sVertDPI=96,sHorizDPI=96;
    inline static int sResolutionGeneration=0;
    inline static LLCoordGL sCurOrigin;
    mutable int draws=0;
    static bool hasNativeDraw() { return native; }
    int getCacheGeneration() const { return 1; }
    int render(const LLWString&,int,float,float,const LLColor4&,HAlign,VAlign,U8,ShadowType,
               int,int,float* right,bool,bool) const { ++draws;if(right)*right=42;return 7; }
};
"""
        fixture = r"""
#include "llfontvertexbuffer.h"
struct Render {
    int collections=0;
    void beginList(std::list<LLVertexBufferData>* list) { ++collections;list->emplace_back(); }
    void endList() {}
} gGL;
int replays=0;
""" + source[start:end] + r"""
void LLFontVertexBuffer::renderBuffers() { ++replays; }
int main() {
    LLFontGL font;LLFontVertexBuffer buffer;LLColor4 color;float right=0;
    auto draw=[&] { return buffer.render(&font,L"native",0,1.f,2.f,color,LLFontGL::LEFT,
        LLFontGL::BASELINE,0,LLFontGL::NO_SHADOW,100,100,&right,false,true); };
    // A populated legacy cache must not be replayed when a native owner takes over.
    assert(draw()==7 && font.draws==1 && gGL.collections==1 && right==42);
    right=0;assert(draw()==7 && font.draws==1 && replays==1 && right==42);
    LLFontGL::native=true;
    for(int i=0;i<2;++i) { right=0;assert(draw()==7 && right==42); }
    assert(font.draws==3 && gGL.collections==1 && replays==1);
    buffer.reset();draw();assert(font.draws==4 && gGL.collections==1 && replays==1);
    LLFontGL::native=false;draw();assert(font.draws==5 && gGL.collections==2);
    LLFontVertexBuffer::enableBufferCollection(false);draw();
    assert(font.draws==6 && gGL.collections==2 && replays==1);
    LLFontGL::sDisplayFont=false;assert(draw()==6 && font.draws==6);
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / 'llfontgl.h').write_text(font, encoding='utf-8')
            (path / 'llfontvertexbuffer.h').write_bytes((root / 'indra/llrender/llfontvertexbuffer.h').read_bytes())
            (path / 'font_cache.cpp').write_text(fixture, encoding='utf-8')
            exe = path / 'font_cache.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path / 'font_cache.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_gl_geometry_trap_preserves_legacy_begin_and_rejects_before_mutation(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llrender/llrender.cpp').read_text(encoding='utf-8')
        start = source.index('void LLRender::begin(const GLuint& mode)')
        end = source.index('\nvoid LLRender::end()', start)
        fixture = r'''
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#define LL_ERRS() std::cerr
#define LL_ENDL std::endl
using GLuint=unsigned;
struct LLRender2D { inline static bool native=false;static bool isNativeUI() { return native; } };
struct LLRender {
    enum { LINES=1,TRIANGLES=2,POINTS=3 };
    unsigned mMode=LINES,mCount=0,flushes=0;
    void flush() { ++flushes;mCount=0; }
    void begin(const GLuint& mode);
};
''' + source[start:end] + r'''
int main() {
    LLRender render;render.begin(LLRender::TRIANGLES);
    assert(render.mMode==LLRender::TRIANGLES && render.flushes==1);
    render.begin(LLRender::TRIANGLES);assert(render.flushes==1);
    LLRender2D::native=true;render.mCount=6;
    bool trapped=false;try { render.begin(LLRender::LINES); }
    catch(const std::logic_error& e) { trapped=std::string(e.what())=="GL geometry in native UI owner"; }
    assert(trapped && render.mCount==6 && render.mMode==LLRender::TRIANGLES && render.flushes==1);
    LLRender2D::native=false;render.begin(LLRender::LINES);
    assert(render.mMode==LLRender::LINES && render.mCount==0 && render.flushes==2);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'native_gl_trap.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'native_gl_trap.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

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

    def test_named_panels_do_not_bypass_specialized_factory_admission(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        root = Path(__file__).resolve().parents[2]
        text = (root / 'indra/llui/llpanel.cpp').read_text(encoding='utf-8')
        start = text.index('LLPanel* LLPanel::createFactoryPanel(')
        body = text[start:]
        fixture = r"""
#include <cassert>
#include <deque>
#include <map>
#include <string>
#include "vsuiadmission.h"
struct LLCallbackMap {
    void* (*mCallback)(void*);
    void* mData;
    using map_t = std::map<std::string, LLCallbackMap>;
    using map_const_iter_t = map_t::const_iterator;
};
struct LLPanel {
    struct Params {};
    static std::deque<const LLCallbackMap::map_t*> sFactoryStack;
    static LLPanel* createFactoryPanel(const std::string&);
};
std::deque<const LLCallbackMap::map_t*> LLPanel::sFactoryStack;
struct LLUICtrlFactory {
    template<class T> static T* create(const typename T::Params&) {
        return VSUIAdmission::widget(typeid(T)) ? new T : nullptr;
    }
};
""" + body + r"""
int main() {
    unsigned calls = 0;
    LLCallbackMap::map_t factories;
    factories.emplace("optional", LLCallbackMap{[](void* p)->void* {
        ++*static_cast<unsigned*>(p); return new LLPanel;
    }, &calls});
    LLPanel::sFactoryStack.push_back(&factories);
    VSUIAdmission admission([](const std::type_info& t){ return t == typeid(LLPanel); },
        [](std::string_view){ return false; }, [](std::string_view){ return false; });
    auto* ordinary = LLPanel::createFactoryPanel("login_content");
    assert(ordinary != nullptr);
    delete ordinary;
    assert(LLPanel::createFactoryPanel("optional") == nullptr && calls == 0);
}
"""
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'panels.cpp'; source.write_text(fixture)
            exe = Path(temp) / 'panels.exe'
            built = subprocess.run([compiler, '-std=c++17', '-I' + str(root / 'indra/llui'),
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
                    ui_facade_verified=True, ui_atlas_verified=True, ui_font_producer_verified=True, ui_delayed_completion_verified=True,
                    ui_admission_verified=True,ui_xui_verified=True,ui_input_verified=True,ui_focus_verified=True,
                    ui_mouse_verified=True,ui_scroll_verified=True)
        self.assertTrue(assess(good, 0, log, 'ui-positive', 'Windows'))
        self.assertTrue(assess(good, 0, log, 'skin-modern-blue', 'Windows'))
        for key, value in [('ui_readbacks', 1), ('ui_readback_verified', False), ('ui_fixture_enabled', False),
                           ('ui_facade_verified', False), ('ui_atlas_verified', False),
                           ('ui_font_producer_verified', False), ('ui_delayed_completion_verified', False), ('ui_admission_verified', False),
                           ('ui_xui_verified', False), ('ui_input_verified', False), ('ui_focus_verified', False),
                           ('ui_mouse_verified', False), ('ui_scroll_verified', False)]:
            with self.subTest(key=key):
                bad = copy.deepcopy(good); bad[key] = value
                self.assertFalse(assess(bad, 0, log, 'ui-positive', 'Windows'))
                self.assertFalse(assess(bad, 0, log, 'skin-modern-blue', 'Windows'))
        for case, failure in [('bad-xui','Viewer XUI pixel oracle mismatch'),
                              ('ui-construction','Injected failure: ui-construction'),
                              ('ui-gl-trap','GL geometry in native UI owner')]:
            bad=copy.deepcopy(good);bad.update(passed=False,failure=failure)
            self.assertTrue(assess(bad,1,'FAIL viewer-native-diagnostic',case,'Windows'))
            self.assertFalse(assess(bad,0,log,case,'Windows'))
        good.update(passed=False, failure='Viewer UI pixel oracle mismatch', ui_fixture_enabled=True)
        for case in ('bad-ui', 'ui-orientation'):
            self.assertTrue(assess(good, 1, 'FAIL viewer-native-diagnostic', case, 'Windows'))
            self.assertFalse(assess(good, 0, log, case, 'Windows'))


if __name__ == '__main__':
    unittest.main()
