"""Compiled native CPU statistics and chart geometry ownership regressions."""
from pathlib import Path
import shutil
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
def function(source,signature):
    start=source.index(signature);opening=source.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

class NativeStatisticsTests(unittest.TestCase):
    def compile(self,fixture):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'native_statistics.cpp';path.write_text(fixture)
            exe=Path(directory)/'native_statistics.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_statistics_container_admission_and_native_draw(self):
        source=(ROOT/'indra/newview/vsstartupui.cpp').read_text(encoding='utf-8')
        start=source.index('const bool admitted =')
        predicate=source[start:source.index(';',start)+1]
        names=set(re.findall(r'typeid\((\w+(?:::\w+)*)\)',predicate))
        helpers=re.findall(r'(\w+)::(\w+)\(t\)',predicate)
        definitions=[]
        for name in sorted(names | {owner for owner,_ in helpers} | {'LLRadioGroup'}):
            methods=''.join('static bool '+method+'(const std::type_info&){return false;}'
                            for owner,method in helpers if owner==name)
            if name=='LLRadioGroup':
                methods+='static const std::type_info& itemType(){return typeid(Rejected);}'
            if '::' in name:
                namespace,child=name.split('::')
                definitions.append('namespace '+namespace+' { struct '+child+' {}; }')
            else:
                definitions.append('struct '+name+' {'+methods+'};')
        self.compile('#include <typeinfo>\n#include <cassert>\nstruct Rejected {};\n'+
            '\n'.join(definitions)+'\nbool admitted(const std::type_info& t){'+predicate+
            'return admitted;}\nint main(){assert(admitted(typeid(LLContainerView)));'
            'assert(admitted(typeid(LLStatView)));assert(admitted(typeid(LLStatBar)));'
            'struct Unreviewed:LLContainerView{};assert(!admitted(typeid(Unreviewed)));}')
        draw=function((ROOT/'indra/llui/llcontainerview.cpp').read_text(), 'void LLContainerView::draw()')
        self.compile(r"""
#include <cassert>
#include <string>
using F32=float;
struct LLColor4 { LLColor4(float=0,float=0,float=0,float=1){} };
struct LLRect { int getHeight()const{return 100;} int getWidth()const{return 200;} };
struct LLRender2D { static inline bool native=true;static bool isNativeUI(){return native;} };
int rectangles=0,texts=0,children=0,gl_calls=0;
struct LLTexUnit { enum{TT_TEXTURE};void unbind(int){assert(!LLRender2D::native);++gl_calls;} } unit;
struct GL { LLTexUnit* getTexUnit(int){return &unit;} } gGL;
void gl_rect_2d(int,int,int,int,LLColor4){++rectangles;}
struct LLFontGL { enum{LEFT,TOP};static LLFontGL* getFontMonospace(){static LLFontGL f;return &f;}
 template<class... Args>void render(Args...){++texts;} };
struct LLView { void draw(){++children;} };
struct LLContainerView:LLView {
 bool mBackgroundVisible=true,mShowLabel=true;LLColor4 mBackgroundColor;std::wstring mLabel=L"Statistics";
 LLRect getRect(){return {};}void draw();
};
DRAW
int main(){
 LLContainerView view;view.draw();assert(rectangles==1&&texts==1&&children==1&&gl_calls==0);
 view.mBackgroundVisible=false;view.mShowLabel=false;view.draw();
 assert(rectangles==1&&texts==1&&children==2&&gl_calls==0);
 LLRender2D::native=false;view.mBackgroundVisible=view.mShowLabel=true;view.draw();
 assert(rectangles==2&&texts==2&&children==3&&gl_calls==1);
}
""".replace('DRAW',draw))

    def test_actual_sampler_uses_frames_and_current_native_circuit(self):
        source=(ROOT/'indra/newview/llviewerstats.cpp').read_text()
        body=function(source,'void update_native_statistics()')
        fixture=r'''#include <cassert>
#include <map>
#include <memory>
using U32=unsigned;using U64=unsigned long long;using F32=float;
struct Unit{double n;Unit(double v=0):n(v){}double value()const{return n;}operator double()const{return n;}};
using F64Seconds=Unit;using F64Milliseconds=Unit;using F64Bits=Unit;
struct LLTimer{static double delta;double getElapsedTimeAndResetF64(){return delta;}};double LLTimer::delta=.02;
struct LLHost{int id=0;bool isOk()const{return id!=0;}bool operator!=(const LLHost&o)const{return id!=o.id;}};
struct VSNativeSession{
 enum class Phase{Login,Connected};U64 gen=1;LLHost endpoint{1};Phase state=Phase::Connected;
 U64 generation()const{return gen;}LLHost host()const{return endpoint;}Phase phase()const{return state;}
 static std::shared_ptr<VSNativeSession> owner;static auto active(){return owner;}
};std::shared_ptr<VSNativeSession> VSNativeSession::owner;
struct Frame{int periods=0,resets=0;void reset(){periods=0;++resets;}void nextPeriod(){++periods;}}frames;
namespace LLTrace{Frame& get_frame_recording(){return frames;}}
std::map<int,double> values;
namespace LLStatViewer{enum{FPS,FRAME_STACKTIME,FPS_SAMPLE,SIM_PING,PACKETS_IN,PACKETS_LOST,PACKETS_LOST_PERCENT,MESSAGE_SYSTEM_DATA_IN,MESSAGE_SYSTEM_DATA_OUT,ACTIVE_MESSAGE_DATA_RECEIVED};}
template<class T>void sample(int key,T v){values[key]=double(v);}template<class T>void record(int key,T v){values[key]=double(v);}template<class T>void add(int key,T v){values[key]+=double(v);}
namespace LLUnits{struct Ratio{static double fromValue(double v){return v;}};}
struct LLViewerStats{int resets=0,updates=0;static LLViewerStats& instance(){static LLViewerStats s;return s;}
 void resetNativeFrameStats(){++resets;values.clear();}void updateFrameStats(Unit v){assert(v.n>0);++updates;}};
struct Circuit{U32 packets=10,lost=1;Unit getPingDelay(){return 75;}U32 getPacketsIn(){return packets;}U32 getPacketsLost(){return lost;}}circuit;
struct CircuitInfo{Circuit* findCircuit(LLHost h){return h.isOk()?&circuit:nullptr;}};
struct Ring{int in=800,out=400;int getAndResetActualInBits(){int v=in;in=0;return v;}int getAndResetActualOutBits(){int v=out;out=0;return v;}};
struct Message{CircuitInfo mCircuitInfo;Ring mPacketRing;}message;Message* gMessageSystem=&message;
BODY
int main(){
 VSNativeSession::owner=std::make_shared<VSNativeSession>();update_native_statistics();
 assert(values[LLStatViewer::FPS]==1 && values[LLStatViewer::FPS_SAMPLE]==50);
 assert(values[LLStatViewer::FRAME_STACKTIME]==.02 && values[LLStatViewer::SIM_PING]==75);
 assert(values[LLStatViewer::PACKETS_IN]==10 && values[LLStatViewer::PACKETS_LOST]==1);
 assert(values[LLStatViewer::PACKETS_LOST_PERCENT]>.099 && values[LLStatViewer::PACKETS_LOST_PERCENT]<.101);
 assert(values[LLStatViewer::ACTIVE_MESSAGE_DATA_RECEIVED]==800);
 circuit.packets=14;circuit.lost=1;update_native_statistics();
 assert(values[LLStatViewer::PACKETS_IN]==14 && values[LLStatViewer::PACKETS_LOST_PERCENT]==0);
 VSNativeSession::owner->gen=2;VSNativeSession::owner->endpoint={2};circuit.packets=3;circuit.lost=0;
 update_native_statistics();assert(values[LLStatViewer::PACKETS_IN]==3 && values[LLStatViewer::FPS]==1);
 assert(LLViewerStats::instance().resets==2);
 VSNativeSession::owner->state=VSNativeSession::Phase::Login;update_native_statistics();
 assert(values.count(LLStatViewer::SIM_PING)==0 && values.count(LLStatViewer::PACKETS_IN)==0);
 assert(values[LLStatViewer::FPS]==1);gMessageSystem=nullptr;update_native_statistics();
}
'''.replace('BODY',body)
        self.compile(fixture)
        session=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
        self.assertIn('"SimStats"',session.split('const std::set<std::string> session =',1)[1].split('};',1)[0])
        self.assertIn('msg.setHandlerFunc("SimStats", process_sim_stats)',session)
        self.assertNotIn('LLWorld',body)
        status=(ROOT/'indra/newview/llstatusbar.cpp').read_text()
        self.assertNotIn('Native presentation does not populate',status)
        self.assertIn('update_native_statistics();',(ROOT/'indra/newview/llappviewer.cpp').read_text())

    def test_actual_history_vertices_publish_native_triangles_without_gl(self):
        source=(ROOT/'indra/llui/llstatbar.cpp').read_text()
        body=function(source,'auto history_vertex =')
        fixture=r'''#include <array>
#include <cassert>
using F32=float;
struct LLFontGL{static constexpr float sScaleX=1.5f,sScaleY=1.5f;};
struct LLRender2D{using native_triangle_t=std::array<std::array<float,6>,3>;
 static inline bool native=true;static inline native_triangle_t captured;static inline int emitted=0;
 static bool isNativeUI(){return native;}static std::array<float,2> nativeOrigin(){return {4,8};}
 static void nativeTriangle(const native_triangle_t&t){captured=t;++emitted;}};
struct GL{int vertices=0;void vertex2f(float,float){++vertices;}}gGL;
int main(){LLRender2D::native_triangle_t triangle;unsigned vertex_count=0;
BODY;
 history_vertex(2,3);history_vertex(4,5);history_vertex(6,7);
 assert(LLRender2D::emitted==1 && gGL.vertices==0);
 assert(LLRender2D::captured[0][0]==9 && LLRender2D::captured[0][1]==16.5f);
 assert(LLRender2D::captured[2][0]==15 && LLRender2D::captured[2][1]==22.5f);
 assert(LLRender2D::captured[0][2]==1 && LLRender2D::captured[0][5]==1);
 LLRender2D::native=false;history_vertex(1,1);assert(gGL.vertices==1 && LLRender2D::emitted==1);
}
'''.replace('BODY',body)
        self.compile(fixture)
        self.assertIn('if (!sample_available) display_value = std::numeric_limits<F32>::quiet_NaN()',source)
        lag=(ROOT/'indra/newview/llfloaterlagmeter.cpp').read_text()
        self.assertIn('getSampleCount(LLStatViewer::SIM_FRAME_TIME)',lag)
        self.assertIn('getSampleCount(LLStatViewer::SIM_PING)',lag)
        self.assertLess(lag.index('getString("native_client_cause_msg")'),lag.index('getTextureFetch()->getNumRequests()'))

    def test_actual_cpu_routes_and_debug_write_boundary(self):
        source=(ROOT/'indra/newview/llfloatersettingsdebug.cpp').read_text()
        helper=function(source,'bool native_debug_setting_editable(')
        fixture=r'''#include <cassert>
#include <string>
struct LLSD{std::string name;LLSD(const std::string& n):name(n){}};
struct LLControlVariable{std::string name;const std::string& getName()const{return name;}};
struct Window{bool native=false;bool isNativeVulkan()const{return native;}} window;
Window* gViewerWindow=&window;
struct VSUIAdmission{static bool callback(const char*,const LLSD& setting){return setting.name=="PlainTextChatHistory";}};
HELPER
int main(){LLControlVariable safe{"PlainTextChatHistory"},scene{"RenderDeferred"},filter{"DebugSettingsHideDefault"};
 assert(!native_debug_setting_editable(nullptr));assert(native_debug_setting_editable(&scene));
 window.native=true;assert(native_debug_setting_editable(&safe));assert(native_debug_setting_editable(&filter));
 assert(!native_debug_setting_editable(&scene));gViewerWindow=nullptr;assert(native_debug_setting_editable(&scene));}
'''.replace('HELPER',helper)
        self.compile(fixture)
        for signature in ['void LLFloaterSettingsDebug::onCommitSettings()',
                          'void LLFloaterSettingsDebug::onClickDefault()',
                          'void LLFloaterSettingsDebug::onClickSanityWarning()']:
            self.assertIn('native_debug_setting_editable',function(source,signature))
        self.assertIn('mSelectedControlConnection =',source)
        self.assertIn('mFilterConnection =',source)
        policy=(ROOT/'indra/newview/vsstartupui.cpp').read_text()
        for name in ['settings_debug','webbrowser','defaultchatbar','howto','guidebook','help_browser',
                     'Help.ToggleHowTo','ToggleWebBrowser','ToggleSettingsDebug','Advanced.ShowDebugSettings']:
            self.assertIn('"'+name+'"',policy)
        help_source=(ROOT/'indra/newview/llviewerhelp.cpp').read_text()
        self.assertNotIn('isNativeVulkan()',function(help_source,'void LLViewerHelp::showTopic('))
        reset=(ROOT/'indra/newview/vsnativeim.cpp').read_text()
        for name in ['settings_debug','guidebook','help_browser']:
            self.assertIn('"'+name+'"',reset)

    def test_actual_connected_menu_final_height_updates_hover_and_highlight(self):
        source=(ROOT/'indra/newview/llviewermenu.cpp').read_text()
        helper=function(source,'void fit_native_menu_bar(')
        arrange=function((ROOT/'indra/llui/llmenugl.cpp').read_text(),'void LLMenuBarGL::arrange( void )')
        fixture=r'''#include <cassert>
#include <vector>
using U32=unsigned;using S32=int;
struct LLRect{int mLeft,mTop,mRight,mBottom;LLRect(int l=0,int t=0,int r=0,int b=0):mLeft(l),mTop(t),mRight(r),mBottom(b){}
 int getWidth()const{return mRight-mLeft;}int getHeight()const{return mTop-mBottom;}
 bool contains(int x,int y)const{return x>=mLeft&&x<mRight&&y>=mBottom&&y<mTop;}};
struct LLView{LLRect rect;const LLRect& getRect()const{return rect;}};
struct LLMenuItemGL:LLView{bool visible=true;int width=40;int labels=0;
 bool getVisible()const{return visible;}int getNominalWidth()const{return width;}
 void setRect(LLRect r){rect=r;}void buildDrawLabel(){++labels;}};
struct LLMenuBarGL:LLView{using item_list_t=std::vector<LLMenuItemGL*>;item_list_t mItems;bool needs=true;
 void setRect(LLRect r){rect=r;}void needsArrange(){needs=true;}
 void arrangeAndClear(){if(needs){arrange();needs=false;}}
 void reshape(int w,int h){rect.mRight=rect.mLeft+w;rect.mBottom=rect.mTop-h;}
 void arrange();};
ARRANGE
HELPER
int main(){LLMenuItemGL avatar,help,hidden;hidden.visible=false;
 LLMenuBarGL menu;menu.rect=LLRect(0,9,80,0);menu.mItems={&avatar,&help,&hidden};menu.arrangeAndClear();
 menu.setRect(LLRect(0,18,80,0));assert(!avatar.getRect().contains(20,14)); // reproduces stale upper half
 for(int height:{18,19,27}){LLView holder;holder.rect=LLRect(0,500,1000,500-height);fit_native_menu_bar(menu,holder);
  assert(menu.getRect().getHeight()==height);assert(menu.getRect().getWidth()==80);
  for(auto* item:{&avatar,&help}){assert(item->getRect().getHeight()==height);
   assert(item->getRect().contains(item->getRect().mLeft+2,1));
   assert(item->getRect().contains(item->getRect().mLeft+2,height-1));}
  assert(hidden.labels==0);assert(!menu.needs);}
}
'''.replace('HELPER',helper).replace('ARRANGE',arrange)
        self.compile(fixture)
        native=function(source,'void init_native_connected_menus()')
        self.assertIn('fit_native_menu_bar(*gMenuBarView, *holder)',native)
        menu=(ROOT/'indra/llui/llmenugl.cpp').read_text()
        self.assertIn('viewp->pointInView(local_x, local_y)',function(menu,'bool LLMenuBarGL::handleHover('))
        self.assertIn('getRect().getHeight()',function(menu,'void LLMenuItemBranchDownGL::draw('))
    def test_history_confirmation_rejects_retired_or_reindexed_entries(self):
        source=(ROOT/'indra/newview/llpanelteleporthistory.cpp').read_text()
        body=function(source,'bool LLTeleportHistoryPanel::onTeleportConfirmation(')
        valid=function(source,'bool history_index_valid(')
        fixture=r'''#include <cassert>
#include <map>
#include <string>
#include <vector>
using S32=int;
struct LLSD{std::string value;std::map<std::string,LLSD> fields;LLSD()=default;LLSD(const char*s):value(s){}
 const LLSD& operator[](const char*k)const{return fields.at(k);}LLSD& operator[](const char*k){return fields[k];}};
bool llsd_equals(const LLSD&a,const LLSD&b){return a.value==b.value;}
bool current=true;bool vs_native_im_notification_current(const LLSD&){return current;}
namespace LLNotificationsUtil{int getSelectedOption(const LLSD&,const LLSD&r){return std::stoi(r.value);}}
struct Item{LLSD value;LLSD toLLSD()const{return value;}};
struct LLTeleportHistoryStorage{std::vector<Item> items{{LLSD("original")}};int moves=0;
 static LLTeleportHistoryStorage* getInstance(){static LLTeleportHistoryStorage s;return &s;}
 const auto&getItems(){return items;}void goToItem(int){++moves;}};
struct LLTeleportHistoryPanel{static bool onTeleportConfirmation(const LLSD&,const LLSD&,S32);};
VALID
BODY
int main(){LLSD n;n["payload"]["history_entry"]="original";
 auto*s=LLTeleportHistoryStorage::getInstance();LLSD yes("0"),no("1");
 LLTeleportHistoryPanel::onTeleportConfirmation(n,yes,0);assert(s->moves==1);
 current=false;LLTeleportHistoryPanel::onTeleportConfirmation(n,yes,0);assert(s->moves==1);current=true;
 LLTeleportHistoryPanel::onTeleportConfirmation(n,no,0);assert(s->moves==1);
 LLTeleportHistoryPanel::onTeleportConfirmation(n,yes,-1);assert(s->moves==1);
 LLTeleportHistoryPanel::onTeleportConfirmation(n,yes,1);assert(s->moves==1);
 s->items[0].value="new entry";LLTeleportHistoryPanel::onTeleportConfirmation(n,yes,0);assert(s->moves==1);
 s->items.clear();LLTeleportHistoryPanel::onTeleportConfirmation(n,yes,0);assert(s->moves==1);
}
'''.replace('VALID',valid).replace('BODY',body)
        self.compile(fixture)
        clear=function(source,'void LLTeleportHistoryPanel::onRemoveSelected(')
        self.assertIn('vs_native_im_ui_callback(this',clear)
        self.assertIn('vs_native_im_stamp_notification(payload)',clear)
        copy=function(source,'void LLTeleportHistoryPanel::onGearMenuAction(')
        self.assertIn('current() && weak.get()',copy)
        self.assertIn('entry.mSLURL.getSLURLString()',copy)

    def test_persistent_history_reset_saves_old_account_before_model_signal(self):
        source=(ROOT/'indra/newview/llteleporthistory.cpp').read_text()
        reset=function(source,'void LLTeleportHistory::resetNativeSession(')
        fixture=r'''#include <cassert>
#include <vector>
std::vector<int> order;
struct LLTeleportHistoryStorage{static inline bool alive=true;static bool instanceExists(){return alive;}
 static LLTeleportHistoryStorage&instance(){static LLTeleportHistoryStorage s;return s;}
 void save(){order.push_back(1);}static void deleteSingleton(){order.push_back(2);alive=false;}};
struct LLTeleportHistory{std::vector<int>mItems{1};int mCurrentItem=0,mRequestedItem=0;
 bool mGotInitialUpdate=true;void*mTeleportHistoryStorage=(void*)1;
 void onHistoryChanged(){assert(!LLTeleportHistoryStorage::alive);order.push_back(3);}void resetNativeSession();};
BODY
int main(){LLTeleportHistory h;h.resetNativeSession();assert((order==std::vector<int>{1,2,3}));
 assert(h.mItems.empty() && h.mCurrentItem==-1 && h.mRequestedItem==-1 && !h.mGotInitialUpdate && !h.mTeleportHistoryStorage);
 order.clear();h.resetNativeSession();assert((order==std::vector<int>{3}));}
'''.replace('BODY',reset)
        self.compile(fixture)
        storage=(ROOT/'indra/newview/llteleporthistorystorage.cpp').read_text()
        self.assertIn('mSessionHistoryConnection = th->setHistoryChangedCallback',storage)
        header=(ROOT/'indra/newview/llteleporthistorystorage.h').read_text()
        self.assertIn('boost::signals2::scoped_connection mSessionHistoryConnection',header)
        admission=(ROOT/'indra/newview/vsstartupui.cpp').read_text()
        for name in ('marketplace','grid_status','destinations','fs_teleporthistory'):
            self.assertIn('name == "'+name+'"',admission)
            self.assertIn('"'+name+'"',(ROOT/'indra/newview/vsnativeim.cpp').read_text())

    def test_grid_status_uses_existing_sl_default_without_region_feature_owner(self):
        source=(ROOT/'indra/newview/llfloatergridstatus.cpp').read_text()
        body=function(source,'std::string LLFloaterGridStatus::gridStatusURL(')
        fixture=r'''#include <cassert>
#include <string>
#define VS_NATIVE_VULKAN 1
struct VSNativeSession{static inline bool native=true;static bool active(){return native;}};
struct LLGridManager{bool sl=true;static LLGridManager*getInstance(){static LLGridManager g;return &g;}bool isInSecondLife(){return sl;}};
const std::string DEFAULT_GRID_STATUS_URL="existing SL default";
struct LFSimFeatureHandler{std::string url="advertised URL";static LFSimFeatureHandler&instance(){static LFSimFeatureHandler f;return f;}std::string gridStatusURL(){return url;}};
struct LLFloaterGridStatus{static std::string gridStatusURL();};
BODY
int main(){assert(LLFloaterGridStatus::gridStatusURL()==DEFAULT_GRID_STATUS_URL);
 LLGridManager::getInstance()->sl=false;assert(LLFloaterGridStatus::gridStatusURL()=="advertised URL");
 LFSimFeatureHandler::instance().url.clear();assert(LLFloaterGridStatus::gridStatusURL().empty());
 VSNativeSession::native=false;LFSimFeatureHandler::instance().url="legacy";assert(LLFloaterGridStatus::gridStatusURL()=="legacy");}
'''.replace('BODY',body)
        self.compile(fixture)
        destination=(ROOT/'indra/newview/llfloaterdestinations.cpp').read_text()
        opened=function(destination,'void LLFloaterDestinations::onOpen(')
        self.assertLess(opened.index('if (VSNativeSession::active()) return'),opened.index('LFSimFeatureHandler::instance()'))
    def test_simulator_notification_responder_requires_account_and_originating_host(self):
        source=(ROOT/'indra/newview/llviewermessage.cpp').read_text()
        helper=function(source,'bool native_simulator_notification_current(')
        fixture=r'''#include <cassert>
#include <map>
#include <memory>
#include <string>
#define VS_NATIVE_VULKAN 1
struct LLSD{std::string value;std::map<std::string,LLSD> fields;bool has(const char*k)const{return fields.count(k);}
 const LLSD&operator[](const char*k)const{return fields.at(k);}LLSD&operator[](const char*k){return fields[k];}
 std::string asString()const{return value;}};
struct LLHost{std::string value;LLHost(std::string s):value(s){}bool operator==(const LLHost&h)const{return value==h.value;}};
struct VSNativeSession{static inline std::shared_ptr<VSNativeSession> owner;
 static auto active(){return owner;}LLHost host(){return LLHost("currenthost");}};
bool current=true;bool vs_native_im_notification_current(const LLSD&){return current;}
HELPER
int main(){LLSD p;assert(native_simulator_notification_current(p));
 VSNativeSession::owner=std::make_shared<VSNativeSession>();p["vs_simulator_host"].value="currenthost";
 assert(native_simulator_notification_current(p));p["vs_simulator_host"].value="oldhost";
 assert(!native_simulator_notification_current(p));p["vs_simulator_host"].value="currenthost";
 current=false;assert(!native_simulator_notification_current(p));}
'''.replace('HELPER',helper)
        self.compile(fixture)
        session=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
        for name,handler in (('ScriptQuestion','process_script_question'),('ScriptDialog','process_script_dialog'),
                             ('LoadURL','process_load_url'),('AlertMessage','process_alert_message'),
                             ('AgentAlertMessage','process_agent_alert_message')):
            self.assertIn('msg.setHandlerFunc("'+name+'", '+handler+')',session)
        for signature in ('bool callback_load_url(', 'bool callback_script_dialog(', 'bool script_question_cb('):
            self.assertIn('native_simulator_notification_current(notification["payload"])',function(source,signature))
        standard=function(source,'bool attempt_standard_notification(')
        self.assertIn('LLNotificationsUtil::add(notificationID, llsdBlock, payload)',standard)
        self.assertIn('!gViewerWindow->isNativeVulkan()',standard)
        self.assertIn('getBOOL("AlertData", "Modal", modal)',function(source,'void process_agent_alert_message('))

    def test_actual_native_toast_bounds_follow_visible_chiclet_host(self):
        source=(ROOT/'indra/newview/llscreenchannel.cpp').read_text()
        body=function(source,'LLRect LLScreenChannelBase::getChannelRect(')
        fixture=r'''#include <cassert>
#include <algorithm>
#define LL_PROFILE_ZONE_SCOPED
struct LLRect{int mLeft=0,mTop=500,mRight=800,mBottom=0;};
struct View{LLRect rect;bool visible=true;LLRect getLocalRect(){return rect;}
 void localRectToScreen(LLRect r,LLRect*out){*out=r;}bool isInVisibleChain(){return visible;}};
struct Window{bool native=true;View snap,top,bottom;View*chiclet=nullptr;
 View*getFloaterSnapRegion(){return &snap;}View*getChicletContainer(){return chiclet;}bool isNativeVulkan(){return native;}}window;
Window*gViewerWindow=&window;
struct Settings{bool top=true;bool getBOOL(const char*){return top;}}gSavedSettings;
int llmin(int a,int b){return std::min(a,b);}int llmax(int a,int b){return std::max(a,b);}
struct LLScreenChannelBase{View*mFloaterSnapRegion=nullptr,*mChicletRegion=nullptr;LLRect getChannelRect();};
BODY
int main(){window.top.rect={0,500,800,468};window.bottom.rect={0,64,800,32};LLScreenChannelBase c;
 assert(c.getChannelRect().mTop==500 && c.mChicletRegion==nullptr);
 window.chiclet=&window.top;assert(c.getChannelRect().mTop==468 && c.mChicletRegion==&window.top);
 gSavedSettings.top=false;window.chiclet=&window.bottom;
 assert(c.getChannelRect().mBottom==64 && c.mChicletRegion==&window.bottom);
 window.bottom.visible=false;assert(c.getChannelRect().mBottom==0);
 window.bottom.visible=true;window.native=false;assert(c.getChannelRect().mBottom==0);
 window.native=true;window.chiclet=nullptr;assert(c.getChannelRect().mBottom==0);
}
'''.replace('BODY',body)
        self.compile(fixture)
        self.assertLess(body.index('if (gViewerWindow->isNativeVulkan()) mChicletRegion ='), body.index('if (!mChicletRegion) return'))
        owner=(ROOT/'indra/newview/vsviewerwindow.cpp').read_text()
        self.assertIn('mChicletContainer = chiclet_host;',function(owner,'void LLViewerWindow::setNativeConnected('))
        self.assertIn('mChicletContainer = host;',function(owner,'void LLViewerWindow::drawNativeUI('))
        bottom=function(source,'void LLScreenChannel::showToastsBottom(')
        self.assertIn('if (gViewerWindow->isNativeVulkan())\n        bottom =',bottom)

    def test_actual_native_logout_uses_shared_skinned_progress_policy(self):
        source=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
        body=function(source,'void VSNativeSession::requestLogout(')
        fixture=r'''#include <cassert>
#include <string>
#include <map>
#include <functional>
struct LLSD{};
struct Settings{bool disable=false;bool getBOOL(const char*){return disable;}}gSavedSettings;
namespace LLTrans{std::string getString(const char*s){return s;}}
struct Viewer{int shown=0;bool full=false;float percent=0;std::string text;
 void setShowProgress(bool s,bool f){shown+=s;full=f;}void setProgressPercent(float f){percent=f;}
 void setProgressString(std::string s){text=s;}}viewer;Viewer*gViewerWindow=&viewer;
struct LLAppViewer{bool quit=false;static LLAppViewer*instance(){static LLAppViewer a;return &a;}void forceQuit(){quit=true;}};
float gLogoutMaxTime=6;
double now(){return 10;}
struct VSNativeSession{enum class Phase{Connected,Connecting,Logout,Login};Phase mPhase=Phase::Connected;
 bool mQuit=false;double mDeadline=0;int sent=0,released=0;
 struct Poll{void reset(){}}mPoll;
 struct Circuit {std::function<void()> cancel;Poll poll;};std::map<int,Circuit> mCircuits;
 void typing(bool){}void releaseMovement(){++released;}void clearParcelPlayback(){}void unbind(){}
 void send(const char* name,LLSD){assert(released>0);if(sent==0)assert(std::string(name)=="AgentUpdate");++sent;}
 void requestLogout(bool);};
BODY
int main(){VSNativeSession s;s.requestLogout(false);assert(s.mPhase==VSNativeSession::Phase::Logout);
 assert(s.sent==2 && s.released==1 && s.mDeadline==16 && viewer.shown==1 && viewer.full && viewer.percent==100 && viewer.text=="LoggingOut");
 s.requestLogout(true);assert(s.mQuit && s.sent==2 && s.released==1 && viewer.shown==1);
 s.mPhase=VSNativeSession::Phase::Connected;gSavedSettings.disable=true;s.requestLogout(false);assert(!viewer.full && viewer.shown==2);
 s.mPhase=VSNativeSession::Phase::Login;s.requestLogout(true);assert(LLAppViewer::instance()->quit);}
'''.replace('BODY',body)
        self.compile(fixture)
        finish=function(source,'void VSNativeSession::finishLogout(')
        self.assertLess(finish.index('setShowProgress(false, true)'),finish.index('reset()'))
        owner=(ROOT/'indra/newview/vsviewerwindow.cpp').read_text()
        self.assertIn('sendChildToFront(mProgressView)',function(owner,'void LLViewerWindow::drawNativeUI('))
    def test_connecting_progress_uses_reference_stage_text_and_motd(self):
        source=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
        progress=function(source,'void VSNativeSession::publishProgress(')
        block=function(progress,'if (mPhase==Phase::Connecting && gViewerWindow)')
        self.compile(r'''#include <cassert>
#include <string>
using F32=float;
struct Viewer {float percent=30;std::string text,message;
 void setProgressPercent(float p){assert(p>=percent);percent=p;}
 void setProgressString(const std::string&s){text=s;}void setProgressMessage(const std::string&s){message=s;}} viewer;
auto* gViewerWindow=&viewer;
namespace LLTrans {std::string getString(const char* key){return key;}}
struct Agent {std::string mMOTD="Shared MOTD";}gAgent;
struct Session {enum class Phase{Connecting,Connected,Logout};Phase mPhase=Phase::Connecting;
 bool mSeedReady=false,mHandshake=false;void progress(){
''' + block + r'''}};
int main(){Session s;s.progress();assert(viewer.percent==40 && viewer.text=="LoginRequestSeedCapGrant" && viewer.message==gAgent.mMOTD);
 s.mSeedReady=true;s.progress();assert(viewer.percent==59 && viewer.text=="LoginWaitingForRegionHandshake");
 s.mHandshake=true;s.progress();assert(viewer.percent==60 && viewer.text=="LoginConnectingToRegion");
 s.mPhase=Session::Phase::Logout;viewer.text="LoggingOut";s.progress();assert(viewer.text=="LoggingOut");}
''')

    def test_quit_keeps_progress_while_return_to_login_hides_it(self):
        source=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
        body=function(source,'void VSNativeSession::finishLogout(')
        self.compile(r'''#include <cassert>
struct Viewer {bool visible=true;void setShowProgress(bool show,bool){visible=show;}} viewer;
auto* gViewerWindow=&viewer;
struct LLAppViewer {int quits=0;static LLAppViewer* instance(){static LLAppViewer app;return &app;}void forceQuit(){++quits;}};
int logins=0;void reset_login(){++logins;}
struct VSNativeSession {bool mQuit=false;int resets=0;void reset(){++resets;mQuit=false;}void finishLogout();};
''' + body + r'''
int main(){VSNativeSession s;s.mQuit=true;s.finishLogout();
 assert(viewer.visible && s.resets==1 && LLAppViewer::instance()->quits==1 && logins==0);
 s.finishLogout();assert(!viewer.visible && s.resets==2 && logins==1);
}
''')

    def test_native_toolbar_origin_matches_shared_world_panel_not_navigation_bottom(self):
        source=(ROOT/'indra/newview/vsviewerwindow.cpp').read_text()
        body=function(source,'LLRect nativeToolbarRect(')
        fixture=r'''#include <cassert>
struct LLRect{int mLeft,mTop,mRight,mBottom;LLRect(int l,int t,int r,int b):mLeft(l),mTop(t),mRight(r),mBottom(b){}
 int getWidth()const{return mRight-mLeft;}};
BODY
int main(){for(int width:{683,1024,1280})for(int height:{492,768,1024})for(int status_height:{18,19,24}){
 LLRect window(0,height,width,0),status(0,height,width,height-status_height);
 auto toolbar=nativeToolbarRect(status,window);assert(toolbar.mTop==height-status_height);
 assert(toolbar.mLeft==0 && toolbar.mRight==width && toolbar.mBottom==0);
 // panel_toolbar_view's top chiclet offset is applied exactly once.
 int native_chiclet_top=toolbar.mTop-40;int shared_chiclet_top=height-status_height-40;
 assert(native_chiclet_top==shared_chiclet_top);
 // The prior native parent origin subtracted navigation height a second time.
 assert(native_chiclet_top!=(height-status_height-39)-40);}}
'''.replace('BODY',body).replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
        self.compile(fixture)
        native=function(source,'void LLViewerWindow::setNativeConnected(')
        self.assertIn('gToolBarView->setShape(nativeToolbarRect(mStatusBarContainer->getRect(), mWindowRectScaled))',native)
        self.assertIn('chiclets->setFollowsAll()',native)
        import xml.etree.ElementTree as ET
        witnesses=0
        for path in (ROOT/'indra/newview/skins').glob('*/xui/en/main_view.xml'):
            xml=ET.parse(path).getroot();parents={child:parent for parent in xml.iter() for child in parent}
            toolbar=xml.find(".//*[@name='toolbar_view_holder']")
            if toolbar is None:continue
            self.assertEqual(parents[toolbar].get('name'),'world_panel',str(path))
            self.assertEqual(toolbar.get('top'),'0',str(path));witnesses+=1
        self.assertGreater(witnesses,0)
