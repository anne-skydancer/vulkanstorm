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
    def test_sdl_native_window_initializes_ime_policy_and_routes_composition(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llwindow/llwindowsdl2.cpp').read_text(encoding='utf-8')
        creation = source[source.index('bool LLWindowSDL::createNativeWindow('):
                          source.index('static SDL_Surface *Load_BMP_Resource')]
        focus = source[source.index('void LLWindowSDL::allowLanguageTextInput('):
                       source.index('#endif // LL_SDL', source.index('void LLWindowSDL::allowLanguageTextInput('))]
        editing = source[source.index('            case SDL_TEXTEDITING:'):
                         source.index('            case SDL_TEXTINPUT:')]
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
using S32=int;using LLWString=std::wstring;
template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
LLWString utf8str_to_wstring(const char* text){return LLWString(text,text+std::string(text).size());}
#define LL_X11 1
#define LL_WARNS(x) std::cerr
#define LL_ENDL std::endl
constexpr auto SDL_HINT_IME_INTERNAL_EDITING="ime";
constexpr auto SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR="bypass";
constexpr int SDL_INIT_VIDEO=1,SDL_WINDOW_RESIZABLE=2,SDL_SYSWM_X11=3,SDL_TEXTEDITING=4;
struct Settings{bool enabled=false;bool getBOOL(const char*)const{return enabled;}}gSavedSettings;
std::string ime_hint;int starts=0,stops=0;
void SDL_SetHint(const char* name,const char* value){if(std::string(name)=="ime")ime_hint=value;}
int SDL_InitSubSystem(int){assert(ime_hint==(gSavedSettings.enabled?"1":"0"));return 0;}
using SDL_Window=int;
SDL_Window* SDL_CreateWindow(const char*,int,int,int,int,int){static int window;return &window;}
void SDL_DestroyWindow(SDL_Window*){}
void SDL_StartTextInput(){++starts;}
void SDL_StopTextInput(){++stops;}
struct SDL_SysWMinfo{int version=0,subsystem=0;struct{struct{void* display=nullptr;unsigned long window=0;}x11;}info;};
#define SDL_VERSION(v) (*(v)=1)
bool SDL_GetWindowWMInfo(SDL_Window*,SDL_SysWMinfo* info){info->subsystem=SDL_SYSWM_X11;info->info.x11={reinterpret_cast<void*>(1),42};return true;}
struct LLPreeditor{
    LLWString text;int resets=0,updates=0,caret=0;
    void resetPreedit(){++resets;text.clear();}
    void updatePreedit(const LLWString& value,const std::vector<S32>& lengths,const std::deque<bool>& standouts,S32 position){
        assert(lengths==std::vector<S32>{S32(value.size())} && standouts==std::deque<bool>{true});
        ++updates;text=value;caret=position;
    }
};
struct Event{int type=SDL_TEXTEDITING;struct{char text[32]="abc";int start=2;}edit;};
struct LLWindowSDL{
    bool mUseGL=false,mIMEEnabled=false;LLPreeditor* mPreeditor=nullptr;
    std::string mWindowTitle="native";int mSDLFlags=0;SDL_Window* mWindow=nullptr;
    void* mSDL_Display=nullptr;unsigned long mSDL_XWindowID=0;
    bool createNativeWindow(S32,S32,S32,S32);
    void allowLanguageTextInput(LLPreeditor*,bool);
    void gatherEditing(Event event){switch(event.type){
''' + editing + r'''
    default:break;}}
};
''' + creation + focus + r'''
int main(){
    gSavedSettings.enabled=true;LLWindowSDL window;
    assert(window.createNativeWindow(0,0,640,480));
    LLPreeditor editor;window.allowLanguageTextInput(&editor,true);window.gatherEditing({});
    assert(editor.updates==1 && editor.text==L"abc" && editor.caret==2);
    window.allowLanguageTextInput(&editor,false);window.gatherEditing({});
    assert(editor.updates==1 && editor.text.empty() && stops==1);
    window.allowLanguageTextInput(&editor,true);window.mUseGL=true;window.gatherEditing({});
    assert(editor.updates==1); // Existing GL composition behavior is unchanged.
    gSavedSettings.enabled=false;LLWindowSDL disabled;
    assert(disabled.createNativeWindow(0,0,640,480));
    disabled.allowLanguageTextInput(&editor,true);disabled.gatherEditing({});
    assert(editor.updates==1 && disabled.mPreeditor==nullptr);
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'native_sdl_ime.cpp'
            path.write_text(fixture, encoding='utf-8')
            executable = path.with_suffix('.exe')
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_return_character_commits_after_text_and_only_once(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llviewerwindow.cpp').read_text(encoding='utf-8')
        start = source.index('bool LLViewerWindow::handleUnicodeChar(')
        start = source.index('    if (mNativeVulkan)', start)
        end = source.index('    // HACK:', start)
        body = source[start:end]
        fixture = r"""
#include <cassert>
#include <string>
using llwchar=unsigned;using MASK=unsigned;
constexpr MASK MASK_NONE=0,MASK_CONTROL=1,MASK_ALT=2;
constexpr int KEY_RETURN=13;
struct Focus {
    bool wants=false,consume=true;int submits=0;std::string text;
    bool wantsReturnKey()const{return wants;}
    bool handleUnicodeChar(llwchar c,bool){if(c<32)return false;text+=char(c);return true;}
    bool handleKey(int key,MASK,bool){assert(key==KEY_RETURN);if(consume)++submits;return consume;}
};
struct FocusManager {Focus* focus=nullptr;Focus* getKeyboardFocus(){return focus;}}gFocusMgr;
struct Root {int submits=0;bool handleKey(int key,MASK,bool){assert(key==KEY_RETURN);++submits;return true;}};
struct LLViewerWindow {
    bool mNativeVulkan=true;Root* mRootView=nullptr;
    bool handleUnicodeChar(llwchar uni_char,MASK mask){
""" + body + r"""
        return false;
    }
};
int main(){
    Root root;Focus edit;gFocusMgr.focus=&edit;LLViewerWindow window;window.mRootView=&root;
    assert(window.handleUnicodeChar('a',0));assert(window.handleUnicodeChar('b',0));
    assert(edit.text=="ab" && edit.submits==0);
    assert(window.handleUnicodeChar(13,0) && edit.submits==1 && edit.text=="ab");
    edit.wants=true;assert(window.handleUnicodeChar(13,0) && edit.submits==1);
    edit.wants=false;edit.consume=false;
    assert(window.handleUnicodeChar(3,0) && root.submits==1);
    assert(!window.handleUnicodeChar(13,MASK_CONTROL) && root.submits==1);
    assert(!window.handleUnicodeChar(13,MASK_ALT) && root.submits==1);
    gFocusMgr.focus=nullptr;assert(window.handleUnicodeChar(13,0) && root.submits==2);
}
"""
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'native_return.cpp'
            path.write_text(fixture, encoding='utf-8')
            executable = path.with_suffix('.exe')
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_about_uses_running_backend_and_never_queries_gl_for_vulkan(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llappviewer.cpp').read_text(encoding='utf-8')
        start = source.index('    const bool native_vulkan =', source.index('LLSD LLAppViewer::getViewerInfo()'))
        body = source[start:source.index('// [RLVa:KB] - Checked: 2010-04-18', start)]
        cache_start = source.index('    if (LLDiskCache::instanceExists())')
        cache_body = source[cache_start:source.index('    // </FS:Beq>', cache_start)]
        fixture = r"""
#include <cassert>
#include <map>
#include <sstream>
#include <string>
#define VS_NATIVE_VULKAN 1
#define LL_WINDOWS 0
struct LLSD {
    using Map=std::map<std::string,LLSD>;Map data;std::string value;bool defined=false;
    using Integer=int;
    LLSD()=default;LLSD(const char* v):value(v),defined(true){}
    LLSD(const std::string& v):value(v),defined(true){}
    LLSD(int v):value(std::to_string(v)),defined(true){}
    LLSD& operator[](const std::string& k){return data[k];}
    const LLSD& operator[](const std::string& k)const{return data.at(k);}
    auto beginMap()const{return data.begin();}auto endMap()const{return data.end();}
};
struct Context {LLSD facts;LLSD rendererInfo()const{return facts;}};
struct Window {bool native=false;Context* context=nullptr;
    bool isNativeVulkan()const{return native;}Context* nativeContext(){return context;}};
Window* gViewerWindow=nullptr;bool mNativeVulkanLogin=false;
struct GLManager {bool mInited=false;int mVRAM=42,mVRAMDetected=64;}gGLManager;
int calls=0;constexpr int GL_VENDOR=1,GL_RENDERER=2,GL_VERSION=3;
const char* glGetString(int name){++calls;return name==GL_VERSION?"4.6 Mesa/Zink":name==GL_VENDOR?"Mesa":"Zink";}
std::string ll_safe_string(const char* p){return p?p:"";}
#define LL_PROFILE_ZONE_NAMED(name)
struct LLDiskCache {
    inline static bool initialized=false;inline static int calls=0;
    static bool instanceExists(){return initialized;}
    static LLDiskCache* getInstance(){assert(initialized);static LLDiskCache cache;return &cache;}
    std::string getCacheInfo(){++calls;return "cache ready";}
};
LLSD report(){LLSD info;
""" + body + cache_body + r"""
return info;}
int main(){
    Context context;context.facts["GRAPHICS_CARD"]="Selected Vulkan device";
    context.facts["RENDERING_API_VERSION"]="1.3.0";
    Window window;gViewerWindow=&window;window.native=true;window.context=&context;
    gGLManager.mInited=true;
    auto info=report();assert(calls==0 && info["RENDERING_API"].value=="Vulkan");
    assert(info["GRAPHICS_CARD"].value=="Selected Vulkan device" && !info["OPENGL_VERSION"].defined);
    window.context=nullptr;info=report();assert(calls==0 && !info["RENDERING_API_VERSION"].defined);
    gViewerWindow=nullptr;mNativeVulkanLogin=true;info=report();assert(calls==0 && info["RENDERING_API"].value=="Vulkan");
    gViewerWindow=&window;window.native=false; // Running OpenGL wins over a next-session preference.
    info=report();assert(calls==3 && info["RENDERING_API"].value=="OpenGL");
    assert(info["RENDERING_API_VERSION"].value=="4.6 Mesa/Zink" && info["GRAPHICS_CARD"].value=="Zink");
    gGLManager.mInited=false;info=report();assert(calls==3 && !info["RENDERING_API_VERSION"].defined);
    assert(LLDiskCache::calls==0);
    LLDiskCache::initialized=true;info=report();assert(LLDiskCache::calls==1 && info["DISK_CACHE_INFO"].value=="cache ready");
}
"""
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'about_renderer.cpp'
            path.write_text(fixture, encoding='utf-8')
            executable = path.with_suffix('.exe')
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_startup_acceptance_requires_viewer_ownership_readbacks_and_decisive_failure(self):
        item=dict(schema=1,mode='viewer-native-startup',shutdown_complete=True,
                  validation_errors=0,login_controls_verified=True,progress_owner_verified=True,
                  about_renderer_verified=True,login_os_input_verified=True,login_submit_actions=2,
                  stages=['native-viewer-window-created','native-startup-progress-created',
                          'native-login-controller-created','native-startup-ui-released',
                          'native-startup-graphics-released','native-startup-window-released'],
                  modal_alert_verified=True,critical_dialog_verified=True,
                  plain_chat_controls_verified=True,required_dialog_actions_verified=True,
                  mfa_actions_verified=True,login_menus_verified=True,unsupported_ui_status_verified=True,
                  native_dpi_event_verified=True,ime_event_route_verified=True,
                  passed=True,presented_frames=17,readbacks=24,failure='')
        self.assertTrue(assess_startup(item,0,'','startup-positive'))
        for key,value in [('modal_alert_verified',False),('critical_dialog_verified',False),('readbacks',17),('readbacks',18),('readbacks',23),('plain_chat_controls_verified',False),('required_dialog_actions_verified',False),('mfa_actions_verified',False),('login_menus_verified',False),('unsupported_ui_status_verified',False),('validation_errors',1),('shutdown_complete',False),
                          ('login_controls_verified',False),('progress_owner_verified',False),('stages',[]),
                          ('about_renderer_verified',False),('login_os_input_verified',False),('login_submit_actions',0),('login_submit_actions',1),
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
