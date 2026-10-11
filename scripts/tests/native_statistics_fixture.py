"""Compiled native CPU statistics and chart geometry ownership regressions."""
from pathlib import Path
import shutil
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
