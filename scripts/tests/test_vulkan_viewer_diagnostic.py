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
    def test_shared_edit_delete_needs_no_scene_menu_owner(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llviewermenu.cpp').read_text()
        body = source.split('class LLEditDelete : public view_listener_t', 1)[1].split('void handle_spellcheck_replace', 1)[0]
        fixture = r'''#include <cassert>
struct LLSD {};
struct view_listener_t { virtual bool handleEvent(const LLSD&)=0; };
struct Menu { int hidden=0;void hide(){++hidden;}void hideMenus(){++hidden;} };
Menu* gMenuHolder=nullptr;Menu* gMenuObject=nullptr;
struct LLEditMenuHandler { static LLEditMenuHandler* gEditMenuHandler;int deleted=0;bool allowed=true;
 bool canDoDelete(){return allowed;}void doDelete(){++deleted;} };
LLEditMenuHandler* LLEditMenuHandler::gEditMenuHandler=nullptr;
class LLEditDelete : public view_listener_t BODY
int main(){
 LLEditDelete action;LLEditMenuHandler editor;LLEditMenuHandler::gEditMenuHandler=&editor;
 view_listener_t& dispatch=action;assert(dispatch.handleEvent(LLSD{}));assert(editor.deleted==1);
 Menu holder;gMenuHolder=&holder;dispatch.handleEvent(LLSD{});assert(editor.deleted==2 && holder.hidden==1);
 Menu object;gMenuObject=&object;dispatch.handleEvent(LLSD{});assert(editor.deleted==3 && object.hidden==1);
 editor.allowed=false;dispatch.handleEvent(LLSD{});assert(editor.deleted==3 && object.hidden==2);
 LLEditMenuHandler::gEditMenuHandler=nullptr;dispatch.handleEvent(LLSD{});assert(object.hidden==3);
}
'''.replace('BODY', body)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'edit_delete.cpp';path.write_text(fixture)
            exe = Path(directory) / 'edit_delete.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_native_root_and_following_chrome_share_scaled_extent(self):
        root = Path(__file__).resolve().parents[2]
        context = (root / 'indra/newview/vsuicontext.cpp').read_text()
        viewer = (root / 'indra/newview/vsviewerwindow.cpp').read_text()
        expression = context.split('panel.rect       = LLRect(0, ', 1)[1].split(', 0);', 1)[0]
        initial = viewer.split('mWindowRectScaled    = LLRect(0, ', 1)[1].split(', 0);', 1)[0]
        resize = viewer.split('mWindowRectScaled    = LLRect(0, ', 2)[2].split(', 0);', 1)[0]
        fixture = r'''#include <cassert>
#include <cmath>
int ll_round(float value){return int(std::floor(value+0.5f));}
struct Rect { int height,width; };
struct Size { int mX,mY; } size;
Rect root(float width,float height,float dpi){return {ROOT};}
Rect initial(float dpi){return {INITIAL};}
Rect resized(float width,float height,float dpi){return {RESIZE};}
int main(){
 for(float dpi:{1.25f,1.5f,1.75f}){
  size={1024,739}; auto r=root(size.mX,size.mY,dpi), child=initial(dpi);
  assert(r.width==child.width && r.height==child.height);
  // Shared LLView followers preserve each initial difference on reshape.
  auto next=resized(1025,740,dpi);
  child.width+=next.width-r.width;child.height+=next.height-r.height;
  assert(child.width==next.width && child.height==next.height);
 }
}
'''.replace('#include <cmath>', '#include <cmath>\n#include <initializer_list>')
        fixture = fixture.replace('ROOT', expression).replace('INITIAL', initial).replace('RESIZE', resize)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'scaled_chrome.cpp'; path.write_text(fixture)
            exe = Path(directory) / 'scaled_chrome.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_status_and_empty_stats_do_not_read_scene_recordings(self):
        root = Path(__file__).resolve().parents[2]
        def condition(source, marker):
            start = source.index(marker) + source[source.index(marker):].index('(')
            depth = 0
            for end in range(start, len(source)):
                depth += (source[end] == '(') - (source[end] == ')')
                if depth == 0:
                    return source[start + 1:end]
            self.fail('Unclosed production condition')
        fps = condition((root / 'indra/newview/llstatusbar.cpp').read_text(),
                        'if (fsStatusBarShowFPS && !native_status_bar()')
        stat = condition((root / 'indra/llui/llstatgraph.cpp').read_text(),
                         'if (mNewStatFloatp && LLTrace::')
        fixture = r'''#include <cassert>
struct Timer { float elapsed=2; float getElapsedTimeF32(){return elapsed;} } mFPSUpdateTimer;
bool native=false, fsStatusBarShowFPS=true, mNewStatFloatp=true;
bool native_status_bar(){return native;}
namespace LLTrace { struct Recorder { int periods=0,queries=0; int getNumRecordedPeriods(){++queries;return periods;} } recording;
Recorder& get_frame_recording(){return recording;} }
int fpsReads=0,statReads=0;
void fps(){ if (FPS_CONDITION) ++fpsReads; }
void stat(){ if (STAT_CONDITION) ++statReads; }
int main(){
 native=true;fps();assert(fpsReads==0 && LLTrace::recording.queries==0);
 native=false;fps();stat();assert(fpsReads==0 && statReads==0);
 LLTrace::recording.periods=4;fps();stat();assert(fpsReads==1 && statReads==1);
 native=true;fps();assert(fpsReads==1);
 mNewStatFloatp=false;stat();assert(statReads==1);
}
'''.replace('FPS_CONDITION', fps).replace('STAT_CONDITION', stat)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'empty_recording.cpp';path.write_text(fixture)
            exe=Path(directory)/'empty_recording.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_people_scene_omission_is_exact_and_precedes_widget_construction(self):
        root = Path(__file__).resolve().parents[2]
        factory = (root / 'indra/llui/lluictrlfactory.cpp').read_text(encoding='utf-8')
        start = factory.index('void LLUICtrlFactory::createChildren(')
        end = factory.index('bool LLUICtrlFactory::getLayeredXMLNode(', start)
        children = factory[start:end]
        self.assertLess(children.index('VSUIAdmission::child('), children.index('instance().createFromXML('))
        source = (root / 'indra/newview/vsstartupui.cpp').read_text(encoding='utf-8')
        start = source.index('[](std::string_view filename, std::string_view child)')
        end = source.index('});', start)
        policy = source[start:end+1]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        fixture = '#include <cassert>\n#include <string_view>\nint main(){auto policy = ' + policy + r""";
 assert(!policy("skins/ansastorm_modern/xui/en/panel_people.xml","nearby_panel"));
 assert(!policy("panel_people.xml","nearby_panel"));
 assert(policy("panel_people.xml","friends_panel"));
 assert(policy("panel_people.xml","groups_panel"));
 assert(policy("panel_people.xml","contact_sets_panel"));
 assert(policy("panel_people.xml","blocked_panel"));
 assert(policy("panel_profile.xml","nearby_panel"));
 assert(policy("","nearby_panel"));
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'people_scene_gate.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'people_scene_gate.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_menu_callbacks_deny_scene_before_registration_and_keep_labels(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (root / 'indra/llui/llmenugl.cpp').read_text(encoding='utf-8')
        start = source.index('void LLMenuItemCallGL::initFromParams(')
        end = source.index('void LLMenuItemCallGL::onCommit', start)
        fixture = r"""
#include <cassert>
#include <iostream>
#include <string>
struct LLSD{};
#include "vsuiadmission.h"
#define LL_WARNS() std::cerr
#define LL_ENDL std::endl
struct Name{std::string value;std::string operator()()const{return value;}bool isProvided()const{return false;}};
struct Callback{bool supplied=false;Name function_name,control_name;LLSD parameter()const{return{};}bool isProvided()const{return supplied;}};
struct LLControlVariable{};
struct LLUICtrl{template<class P>void initFromParams(const P&){};};
struct Signal{int installed=0;void connect(int){++installed;}};
struct LLMenuItemCallGL:LLUICtrl {
 struct Params{Callback on_visible,on_enable,on_click;};
 bool mNativeCallbacksAllowed=true,enabled=true,visible=true;Signal mVisibleSignal;
 int enables=0,commits=0;
 void initFromParams(const Params&);
 int initEnableCallback(const Callback&){return 1;}
 int initCommitCallback(const Callback&){return 1;}
 void setEnableCallback(int){++enables;}void setCommitCallback(int){++commits;}
 void setEnabled(bool v){enabled=v;}void setEnabledControlVariable(LLControlVariable*){}
 LLControlVariable* findControl(const std::string&){return nullptr;}
 std::string getName(){return "retained scene menu label";}
};
""" + source[start:end] + r"""
int main(){LLMenuItemCallGL::Params p;
 p.on_click={true,{"Edit.Copy"},{}};p.on_visible={true,{"Scene.Visible"},{}};
 {VSUIAdmission scope([](const std::type_info&){return true;},[](std::string_view){return true;}, {}, {}, {},
  [](std::string_view name,const LLSD&){return name=="Edit.Copy";});
  LLMenuItemCallGL item;item.initFromParams(p);
  assert(item.visible&&!item.enabled&&!item.mNativeCallbacksAllowed);
  assert(item.mVisibleSignal.installed==0&&item.commits==1);
  p.on_click.function_name.value="Scene.Execute";p.on_visible.supplied=false;
  LLMenuItemCallGL blocked;blocked.initFromParams(p);
  assert(blocked.visible&&!blocked.enabled&&blocked.commits==0);
  p.on_click.function_name.value="Edit.Copy";
  LLMenuItemCallGL allowed;allowed.initFromParams(p);assert(allowed.enabled&&allowed.commits==1);
 }
 p.on_visible.supplied=true;p.on_click.function_name.value="Scene.Execute";
 LLMenuItemCallGL legacy;legacy.initFromParams(p);
 assert(legacy.enabled&&legacy.mVisibleSignal.installed==1&&legacy.commits==1);
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'native_menu.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'native_menu.exe'
            built = subprocess.run([compiler, '-std=c++17', '-I'+str(root / 'indra/llui'), str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_hover_restores_visibility_and_releases_stale_cursor(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (root / 'indra/newview/vsviewerwindow.cpp').read_text(encoding='utf-8')
        start = source.index('void LLViewerWindow::nativeHover(')
        end = source.index('void LLViewerWindow::nativeScroll(', start)
        fixture = r"""
#include <cassert>
#include <cmath>
using S32=int;using MASK=int;
enum {VX,VY,UI_CURSOR_ARROW=10,UI_CURSOR_IBEAM=11};
int ll_round(float v){return int(std::lround(v));}
struct LLCoordGL{int mX,mY;};
struct Window{bool hidden=true;int cursor=UI_CURSOR_IBEAM;int calls=0;
 void showCursorFromMouseMove(){hidden=false;++calls;}
 void setCursor(int c){cursor=c;++calls;}};
struct View{Window* window;bool editor=false;int x=-1,y=-1,mask=-1;
 void screenPointToLocal(int sx,int sy,int* x,int* y){*x=sx-3;*y=sy-4;}
 void handleHover(int sx,int sy,int m){x=sx;y=sy;mask=m;if(editor)window->setCursor(UI_CURSOR_IBEAM);}};
struct Focus{View* capture=nullptr;View* getMouseCapture(){return capture;}}gFocusMgr;
struct LLViewerWindow{Window* mWindow;View* mRootView;
 struct{float mV[2]={2,2};}mDisplayScale;
 LLCoordGL mCurrentMousePoint{0,0};bool mMouseInWindow=false;
 void nativeHover(LLCoordGL,MASK);};
""" + source[start:end] + r"""
int main(){Window window;View root{&window},editor{&window,true};
 LLViewerWindow viewer{&window,&root};viewer.nativeHover({20,30},7);
 assert(!window.hidden&&window.cursor==UI_CURSOR_ARROW);
 assert(root.x==7&&root.y==11&&root.mask==7&&viewer.mMouseInWindow);
 gFocusMgr.capture=&editor;viewer.nativeHover({24,34},1);
 assert(editor.x==9&&editor.y==13&&window.cursor==UI_CURSOR_IBEAM);
 gFocusMgr.capture=nullptr;viewer.nativeHover({20,30},0);
 assert(window.cursor==UI_CURSOR_ARROW);
 viewer.mRootView=nullptr;int calls=window.calls;viewer.nativeHover({1,1},0);
 assert(window.calls==calls);
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'native_cursor.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'native_cursor.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_nearby_chat_xui_controls_are_inside_owner_and_use_chat_palette(self):
        from xml.etree import ElementTree as ET
        root = Path(__file__).resolve().parents[2]
        panel = ET.parse(root / 'indra/newview/skins/default/xui/en/panel_vs_nearby_chat.xml').getroot()
        self.assertEqual(panel.get('layout'), 'topleft')
        width, height = int(panel.get('width')), int(panel.get('height'))
        rectangles = {}
        for child in panel:
            self.assertEqual(child.get('layout'), 'topleft')
            left, top = int(child.get('left')), int(child.get('top'))
            w, h = int(child.get('width')), int(child.get('height'))
            # Actual applyXUILayout top-left conversion: parentHeight-top.
            rect = (left, height-top-h, left+w, height-top)
            self.assertGreaterEqual(rect[0], 0)
            self.assertGreaterEqual(rect[1], 0)
            self.assertLessEqual(rect[2], width)
            self.assertLessEqual(rect[3], height)
            rectangles[child.get('name')] = rect
        self.assertEqual(rectangles['native_region'], (4,232,396,256))
        self.assertEqual(rectangles['native_logout'], (400,232,496,256))
        self.assertEqual(rectangles['native_plain_transcript'], (4,36,496,228))
        self.assertEqual(rectangles['native_chat_input'], (4,4,496,30))
        transcript = panel.find("simple_text_editor[@name='native_plain_transcript']")
        self.assertEqual(transcript.get('bg_readonly_color'), 'ChatHistoryBgColor')
        self.assertEqual(transcript.get('text_readonly_color'), 'ChatHistoryTextColor')
        self.assertEqual(transcript.get('word_wrap'), 'true')
        self.assertEqual(transcript.get('track_bottom'), 'true')

    def test_native_notification_stamp_remains_expired_after_owner_shutdown(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (root / 'indra/newview/vsnativeim.cpp').read_text(encoding='utf-8')
        start = source.index('bool vs_native_im_notification_current(')
        opening = source.index('{', start)
        end, depth = opening + 1, 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        fixture = r"""
#include <cassert>
#include <map>
#include <memory>
#include <string>
using U64=unsigned long long;
struct LLSD {
 struct Value {std::string text;std::string asUUID()const{return text;}std::string asString()const{return text;}};
 std::map<std::string,Value> values;
 bool has(const char* key)const{return values.count(key)!=0;}
 Value operator[](const char* key)const{auto it=values.find(key);return it==values.end()?Value{}:it->second;}
};
const std::string sNotificationEpoch="application-epoch";
struct VSNativeSession {
 enum class Phase{Login,Connected};
 Phase state=Phase::Connected;U64 epoch=4;
 static inline std::shared_ptr<VSNativeSession> owner;
 static std::shared_ptr<VSNativeSession> active(){return owner;}
 Phase phase()const{return state;}U64 generation()const{return epoch;}
};
""" + source[start:end] + r"""
int main(){
 LLSD legacy,stamp;
 stamp.values["vs_notification_epoch"].text=sNotificationEpoch;
 stamp.values["vs_notification_generation"].text="4";
 assert(vs_native_im_notification_current(legacy));
 assert(!vs_native_im_notification_current(stamp));
 auto retained=std::make_shared<VSNativeSession>();VSNativeSession::owner=retained;
 assert(vs_native_im_notification_current(legacy));
 assert(vs_native_im_notification_current(stamp));
 ++retained->epoch;assert(!vs_native_im_notification_current(stamp));
 stamp.values["vs_notification_generation"].text="5";
 assert(vs_native_im_notification_current(stamp));
 stamp.values["vs_notification_epoch"].text="previous-application";
 assert(!vs_native_im_notification_current(stamp));
 stamp.values["vs_notification_epoch"].text=sNotificationEpoch;
 retained->state=VSNativeSession::Phase::Login;
 assert(!vs_native_im_notification_current(stamp));
 assert(!vs_native_im_notification_current(legacy));
 // Coroutine retention of the retired session must not restore authorization
 // when final shutdown clears the public owner.
 VSNativeSession::owner.reset();
 assert(!vs_native_im_notification_current(stamp));
 assert(vs_native_im_notification_current(legacy));
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'notification_epoch.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'notification_epoch.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_sdl_native_window_initializes_ime_policy_and_routes_composition(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llwindow/llwindowsdl2.cpp').read_text(encoding='utf-8')
        creation = source[source.index('bool LLWindowSDL::createNativeWindow('):
                          source.index('static SDL_Surface *Load_BMP_Resource')]
        focus = source[source.index('void LLWindowSDL::allowLanguageTextInput('):
                       source.index('#endif // LL_SDL', source.index('void LLWindowSDL::allowLanguageTextInput('))]
        editing = source[source.index('            case SDL_TEXTEDITING:'):
                         source.index('            case SDL_TEXTINPUT:')]
        committing = source[source.index('            case SDL_TEXTINPUT:'):
                            source.index('                auto string = utf8str_to_utf16str( event.text.text );')]
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
constexpr int SDL_INIT_VIDEO=1,SDL_WINDOW_RESIZABLE=2,SDL_SYSWM_X11=3,SDL_TEXTEDITING=4,SDL_TEXTINPUT=5;
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
    LLWString text;int resets=0,updates=0,caret=0;S32 compositionLength=0;bool selected=false;
    void getPreeditRange(S32* position,S32* length)const{*position=caret;*length=compositionLength;}
    void resetPreedit(){++resets;if(selected || compositionLength)text.clear();compositionLength=0;selected=false;}
    void updatePreedit(const LLWString& value,const std::vector<S32>& lengths,const std::deque<bool>& standouts,S32 position){
        assert(lengths==std::vector<S32>{S32(value.size())} && standouts==std::deque<bool>{true});
        ++updates;text=value;caret=position;compositionLength=S32(value.size());
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
    void gatherCommit(){switch(SDL_TEXTINPUT){
''' + committing + r'''
        break;}
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
    // Ordinary select-all is committed text, not pending IME composition.
    LLPreeditor username,password;username.text=L"Offline Test";username.selected=true;
    window.allowLanguageTextInput(&username,true);
    window.allowLanguageTextInput(&password,true);
    assert(username.text==L"Offline Test" && username.selected && username.resets==0);
    window.allowLanguageTextInput(&username,true);
    window.allowLanguageTextInput(&username,false);
    assert(username.text==L"Offline Test" && username.selected && username.resets==0);
    window.allowLanguageTextInput(&username,true);
    Event cancel;cancel.edit.text[0]=0;window.gatherEditing(cancel);
    assert(username.text==L"Offline Test" && username.selected && username.resets==0);
    // Starting composition replaces selection; cancellation retires only that composition.
    window.gatherEditing({});assert(username.text==L"abc" && username.compositionLength==3);
    window.gatherEditing(cancel);assert(username.text.empty() && username.compositionLength==0);
    window.gatherEditing({});window.allowLanguageTextInput(&password,true);
    assert(username.text.empty() && username.compositionLength==0);
    window.allowLanguageTextInput(&username,true);window.gatherEditing({});window.gatherCommit();
    assert(username.text.empty() && username.compositionLength==0);
    username.text=L"replace me";username.selected=true;window.gatherCommit();
    assert(username.text.empty() && !username.selected); // Actual commit still replaces normal selection.
    window.allowLanguageTextInput(&username,false);
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
                                [](std::string_view n) { return n=="agreement"; }, {}, {},
                            [](std::string_view n) { return n=="chat"; });
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
    assert(VSUIAdmission::command("build"));
    try {
        VSUIAdmission scope([](const std::type_info& t) { return t==typeid(Required); },
                            [](std::string_view n) { return n=="agreement"; }, {}, {},
                            [](std::string_view n) { return n=="chat"; });
        assert(VSUIAdmission::widget(typeid(Required)));
        assert(!VSUIAdmission::widget(typeid(Optional)));
        assert(VSUIAdmission::floater("agreement"));
        assert(!VSUIAdmission::floater("media"));
        assert(!VSUIAdmission::panelFactory("media_panel"));
        assert(VSUIAdmission::command("chat"));
        assert(!VSUIAdmission::command("build"));
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
    assert(VSUIAdmission::command("build"));
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
