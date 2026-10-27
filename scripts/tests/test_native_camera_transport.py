"""Compile the real native camera and authenticated AgentUpdate producer headlessly."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


STUBS = r'''
#include <cassert>
#include <cmath>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <limits>
#include <optional>
using F32=float;using U32=unsigned;using S32=int;using U8=unsigned char;using U64=unsigned long long;
enum{VX=0,VY=1,VZ=2};
struct LLVector3{
 float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}
 void clear(){*this={};}bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 float magVecSquared()const{return mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2];}
 float length()const{return std::sqrt(magVecSquared());}
 float normalize(){float n=length();if(n>0)*this*=1/n;return n;}
 LLVector3 operator+(LLVector3 v)const{return {mV[0]+v.mV[0],mV[1]+v.mV[1],mV[2]+v.mV[2]};}
 LLVector3 operator-(LLVector3 v)const{return {mV[0]-v.mV[0],mV[1]-v.mV[1],mV[2]-v.mV[2]};}
 LLVector3 operator*(float s)const{return {mV[0]*s,mV[1]*s,mV[2]*s};}
 LLVector3& operator+=(LLVector3 v){*this=*this+v;return *this;}LLVector3& operator*=(float s){*this=*this*s;return *this;}
 static const LLVector3 zero,x_axis,y_axis,z_axis;
};const LLVector3 LLVector3::zero{},LLVector3::x_axis{1,0,0},LLVector3::y_axis{0,1,0},LLVector3::z_axis{0,0,1};
struct LLQuaternion{float angle=0;LLVector3 axis;LLQuaternion()=default;LLQuaternion(float a,LLVector3 v):angle(a),axis(v){}LLQuaternion operator*(LLQuaternion v)const{return {angle+v.angle,axis};}};
struct LLVector3d{double mdV[3]{};LLVector3d()=default;explicit LLVector3d(LLVector3 v):mdV{v.mV[0],v.mV[1],v.mV[2]}{}
 LLVector3d operator-(LLVector3d v)const{LLVector3d r;for(int i=0;i<3;++i)r.mdV[i]=mdV[i]-v.mdV[i];return r;}
 operator LLVector3()const{return {float(mdV[0]),float(mdV[1]),float(mdV[2])};}};
LLVector3d from_region_handle(U64){return {};}
struct LLCoordFrame{LLVector3 at,left,up;void lookAt(LLVector3,LLVector3,LLVector3){}LLVector3 getAtAxis()const{return at;}LLVector3 getLeftAxis()const{return left;}
 LLVector3 getUpAxis()const{return up;}LLQuaternion getQuaternion()const{return {};}};
struct LLUUID{int id=1;static const LLUUID null;};const LLUUID LLUUID::null{0};
struct LLHost{int id=1;bool isOk()const{return id!=0;}bool operator!=(LLHost other)const{return id!=other.id;}};
struct LLSD{const LLSD& operator[](const char*)const{return *this;}std::string asString()const{return {};}int asInteger()const{return 0;}};
struct LLAgent{LLVector3 position;U32 flags=0;bool flying=false;
 void setPositionAgent(LLVector3 p){position=p;}void setFlying(bool value){flying=value;}bool getFlying()const{return flying;}
 U32 getControlFlags(){return flags;}void resetControlFlags();}gAgent;
struct AgentCamera{LLVector3 offset{0,0,-.2f},focus{1,0,.2f};float scale=.75f;
 LLVector3 getCameraOffsetInitial(){return offset;}LLVector3 getFocusOffsetInitial(){return focus;}float getCameraOffsetScale(){return scale;}
 bool focusOnAvatar=true;S32 scrolled=0;bool getFocusOnAvatar()const{return focusOnAvatar;}void handleScrollWheel(S32 clicks){scrolled+=clicks;}
 bool nativeCameraPose(LLVector3d&,LLVector3d&,LLVector3&)const{return false;}}gAgentCamera;
struct ViewerInput{void resetNativeKeys(){}}gViewerInput;
struct Settings{float getF32(const char*){return 128;}}gSavedSettings;
constexpr int STATE_MISC=10;
struct LLStartUp {static int getStartupState(){return STATE_MISC;}};
float vs_agent_draw_distance(bool arriving){return arriving?64.f:128.f;}
template<class T>T llclamp(T v,T a,T b){return std::clamp(v,a,b);}
struct LLMessageSystem{
 std::string message;LLHost host;std::map<std::string,LLVector3> vectors;std::map<std::string,U32> ints;
 std::map<std::string,float> floats;std::map<std::string,LLQuaternion> quaternions;int sends=0,result=1;bool reliable=false;
 void newMessage(const char* s){message=s;}void nextBlock(const char*){}void addUUID(const char*,LLUUID){}
 void addU32(const char* s,U32 value){ints[s]=value;}void addU8(const char* s,U8 value){ints[s]=value;}
 void addS32(const char* s,S32 value){ints[s]=value;}void addString(const char*,const std::string&){}
 void addF32(const char* s,float value){floats[s]=value;}void addQuat(const char* s,LLQuaternion q){quaternions[s]=q;}
 void addVector3(const char* s,LLVector3 v){vectors[s]=v;}int sendReliable(LLHost h){reliable=true;host=h;++sends;return result;}
 int sendMessage(LLHost h){reliable=false;host=h;++sends;return result;}
} messages,*gMessageSystem=&messages;
'''


class NativeCameraTransportTests(unittest.TestCase):
    def test_actual_camera_controls_and_wire_producer(self):
        header = (ROOT / 'indra/newview/vsnativecamera.h').read_text()
        header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M)
        constants = (ROOT / 'indra/llcommon/indra_constants.h').read_text()
        constants = '\n'.join(line for line in constants.splitlines() if line.startswith('constexpr U32 CONTROL_') or line.startswith('constexpr U32 AGENT_CONTROL_'))
        session = (ROOT / 'indra/newview/vsnativesession.cpp').read_text()
        tick = method(session, 'void VSNativeSession::tick()')
        dispatch = method(tick, 'if (cameraTime >= mAgentUpdateTime ||')
        app = (ROOT / 'indra/newview/llappviewer.cpp').read_text()
        frame = method(app, 'bool LLAppViewer::frameNativeVulkanLogin()')
        self.assertIn('gAgent.resetControlFlags();', frame)
        self.assertLess(frame.index('mNativeSession->tick()'), frame.index('gAgent.resetControlFlags();'))
        self.assertNotIn('resetControlFlags', method(session, 'bool VSNativeSession::send('))
        agent = (ROOT / 'indra/newview/llagent.cpp').read_text()
        reset = method(agent, 'void LLAgent::resetControlFlags()').replace('mControlFlags', 'flags')
        functions = '\n'.join(method(session, declaration) for declaration in (
            'VSNativeCamera::State VSNativeSession::cameraState()', 'bool VSNativeSession::movementAction(', 'void VSNativeSession::releaseMovement()',
            'void VSNativeSession::cameraOrbit(', 'void VSNativeSession::cameraZoom(',
            'bool VSNativeSession::updateAgentPosition(', 'U32 VSNativeSession::agentControlFlags()', 'bool VSNativeSession::send('))
        fixture = STUBS + constants + header + r'''
struct VSNativeSession{
 enum class Phase{Login,Connected,Disconnected};Phase mPhase=Phase::Connected;bool mChangingRegion=false,mFrozen=false,mInterestListArriving=true;
 LLHost mHost;LLUUID mAgent,mSession;U32 mCircuit=99,mMouseControls=0;U64 mGeneration=3,mRegionEpoch=7,mSent=0,mHandle=0;
 bool regionReady()const{return mPhase==Phase::Connected&&!mChangingRegion;}
 VSNativeCamera mCamera;LLVector3 mPosition{100,100,20};double mAgentUpdateTime=1;
 std::optional<U32> mLastAgentControls;U32 agentControlFlags()const;
 VSNativeCamera::State cameraState()const;
 bool movementAction(U32,const std::string&,bool,bool=false);void releaseMovement();void cameraOrbit(F32,F32);void cameraZoom(S32);void synchronizeCameraMode(bool=false){};
 bool updateAgentPosition(const LLVector3&,const LLHost&,U64,U64);bool send(const std::string&,const LLSD&);
 void frame(double cameraTime){DISPATCH gAgent.resetControlFlags();}
};
RESET
FUNCTIONS
float dot(LLVector3 a,LLVector3 b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}
bool close(float a,float b){return std::abs(a-b)<.001;}
int main(){
 VSNativeSession owner;auto initial=owner.cameraState();
 assert(close(initial.center.mV[0],100)&&close(initial.center.mV[1],100)&&close(initial.center.mV[2],20.85f));
 assert(close(initial.at.mV[1],0)&&initial.at.mV[0]>0&&initial.at.mV[2]<0);
 // These are the shared reference settings, not a private Vulkan preference.
 gAgentCamera.offset={-3,.4f,.8f};gAgentCamera.scale=.5f;
 auto configured=owner.cameraState();
 assert(close(configured.center.mV[0],98.5f)&&close(configured.center.mV[1],100.2f)&&close(configured.center.mV[2],21.4f));
 gAgentCamera.offset={0,0,-.2f};gAgentCamera.scale=.75f;
 assert(owner.movementAction(1,"push_forward",true));assert(owner.mCamera.controls()&AGENT_CONTROL_AT_POS);
 assert(owner.movementAction(2,"push_forward",true));owner.movementAction(1,"",false);assert(owner.mCamera.controls()&AGENT_CONTROL_AT_POS);
 assert(owner.send("AgentUpdate",{}));assert(!messages.reliable&&messages.message=="AgentUpdate"&&messages.host.id==1&&messages.ints["ControlFlags"]&AGENT_CONTROL_AT_POS);
 assert(close(messages.vectors["CameraCenter"].mV[0],initial.center.mV[0]));
 assert(messages.floats["Far"]==64 && !owner.mInterestListArriving);
 assert(owner.send("AgentUpdate",{}));assert(messages.floats["Far"]==128);
 messages.result=0;owner.mInterestListArriving=true;
 assert(!owner.send("AgentUpdate",{}));assert(owner.mInterestListArriving);
 messages.result=1;assert(owner.send("AgentUpdate",{}));assert(!owner.mInterestListArriving);
 owner.movementAction(2,"",false);assert(!(owner.mCamera.controls()&AGENT_CONTROL_AT_POS));
 owner.movementAction(3,"toggle_fly",true);owner.movementAction(3,"toggle_fly",true,true);assert(owner.mCamera.controls()&AGENT_CONTROL_FLY);
 gAgent.flags=AGENT_CONTROL_STAND_UP;assert(owner.send("AgentUpdate",{}));
 assert(messages.ints["ControlFlags"]&AGENT_CONTROL_STAND_UP);assert(gAgent.flags==AGENT_CONTROL_STAND_UP);
 gAgent.resetControlFlags();assert(gAgent.flags==0);
 gAgent.flags=AGENT_CONTROL_SIT_ON_GROUND;assert(owner.send("AgentUpdate",{}));
 assert(messages.ints["ControlFlags"]&AGENT_CONTROL_SIT_ON_GROUND);assert(gAgent.flags==AGENT_CONTROL_SIT_ON_GROUND);
 gAgent.resetControlFlags();assert(gAgent.flags==0);
 owner.movementAction(3,"",false);owner.movementAction(3,"toggle_fly",true);assert(!(owner.mCamera.controls()&AGENT_CONTROL_FLY));
 owner.movementAction(4,"turn_left",true);owner.mCamera.step(.1f);auto turned=owner.cameraState();assert(turned.at.mV[1]>initial.at.mV[1]);
 owner.releaseMovement();assert(owner.mCamera.controls()==0);
 owner.cameraOrbit(1.f,.3f);auto orbital=owner.cameraState();
 assert(close(orbital.at.magVecSquared(),1)&&close(orbital.left.magVecSquared(),1)&&close(orbital.up.magVecSquared(),1));
 assert(close(dot(orbital.at,orbital.left),0)&&close(dot(orbital.at,orbital.up),0)&&close(dot(orbital.left,orbital.up),0));
 owner.cameraZoom(-10000);assert(owner.cameraState().firstPerson&&owner.mCamera.controls()&AGENT_CONTROL_MOUSELOOK);
 owner.cameraZoom(10000);assert(!owner.cameraState().firstPerson);
 // A detached shared focus routes the wheel to the reference camera, not the native zoom.
 gAgentCamera.focusOnAvatar=false;owner.cameraZoom(-10000);assert(gAgentCamera.scrolled==-10000&&!owner.cameraState().firstPerson);gAgentCamera.focusOnAvatar=true;
 auto before=owner.cameraState();owner.cameraOrbit(std::numeric_limits<float>::infinity(),0);assert(close(owner.cameraState().at.mV[0],before.at.mV[0]));
 owner.mCamera.step(std::numeric_limits<float>::quiet_NaN());assert(owner.cameraState().center.isFinite());
 assert(!owner.updateAgentPosition({1,2,3},LLHost{2},3,7));assert(!owner.updateAgentPosition({1,2,3},LLHost{1},2,7));
 assert(!owner.updateAgentPosition({1,2,3},LLHost{1},3,6));assert(!owner.updateAgentPosition({NAN,2,3},LLHost{1},3,7));
 assert(owner.updateAgentPosition({102,103,20},LLHost{1},3,7));assert(gAgent.position.mV[0]==102);
 owner.mChangingRegion=true;assert(!owner.movementAction(5,"push_forward",true));assert(!owner.updateAgentPosition({9,9,9},LLHost{1},3,7));
 owner.mChangingRegion=false;owner.mFrozen=true;assert(!owner.movementAction(5,"push_forward",true));
 owner.send("AgentUpdate",{});assert(messages.ints["ControlFlags"]==0);
 owner.mCamera.reset();assert(owner.mCamera.controls()==0&&!owner.cameraState().firstPerson);
 // Floater axes use reference rates, zoom polarity and independent pan/roll.
 auto beforeControls=owner.cameraState();
 owner.mCamera.controls(.1f,1,0,1,{0,1,0},1,gAgentCamera.offset,gAgentCamera.focus,gAgentCamera.scale);
 auto afterControls=owner.cameraState();assert(afterControls.at.mV[1]>beforeControls.at.mV[1]);
 // Saving the current native eye/focus must reproduce the view after a reset,
 // including non-default scale, pan, orbit and zoom.
 const auto savedOffset=(afterControls.center-owner.mPosition-LLVector3(0,0,1))*(1.f/gAgentCamera.scale);
 const auto savedFocus=afterControls.focus-owner.mPosition;
 VSNativeCamera restored;const auto reloaded=restored.state(owner.mPosition,savedOffset,savedFocus,gAgentCamera.scale);
 for(int i=0;i<3;++i){assert(close(reloaded.center.mV[i],afterControls.center.mV[i]));assert(close(reloaded.at.mV[i],afterControls.at.mV[i]));}

 assert(owner.mCamera.zoomFraction()> (8.f-1.f)/7.75f);
 owner.mCamera.resetView();assert(close(owner.cameraState().center.mV[0],beforeControls.center.mV[0]));
 owner.mCamera.setZoomFraction(0);assert(close(owner.mCamera.zoomFraction(),0));
 owner.mCamera.setZoomFraction(1);assert(close(owner.mCamera.zoomFraction(),1));
 owner.mCamera.setFirstPerson(true);
 owner.mCamera.controls(.1f,0,0,0,{0,0,0},0,gAgentCamera.offset,gAgentCamera.focus,gAgentCamera.scale);
 assert(owner.cameraState().firstPerson); // Idle controls must not eject mouselook.
 owner.mCamera.setFirstPerson(false);assert(!owner.cameraState().firstPerson);

 // Shared camera position/focus commands must reproduce both requested points.
 LLVector3 eye{110,103,22},target{108,103,21};
 assert(owner.mCamera.lookAt(owner.mPosition,eye,target,gAgentCamera.offset,gAgentCamera.focus,gAgentCamera.scale));
 const auto focused=owner.cameraState();
 for(int i=0;i<3;++i){assert(close(focused.center.mV[i],eye.mV[i]));assert(close(focused.focus.mV[i],target.mV[i]));}
 assert(!owner.mCamera.lookAt(owner.mPosition,target,target,gAgentCamera.offset,gAgentCamera.focus,gAgentCamera.scale));
 owner.mPhase=VSNativeSession::Phase::Login;assert(!owner.movementAction(6,"push_forward",true));
 // Transitions are sent before the heartbeat and ephemeral flags retire every
 // frame. Opposite turns cannot combine into a stale, mutually cancelling pair.
 VSNativeSession movement;messages.sends=0;gAgent.flags=0;
 movement.frame(.2);assert(messages.sends==1&&messages.ints["ControlFlags"]==0);
 gAgent.flags=AGENT_CONTROL_YAW_NEG;movement.frame(.21);
 assert(messages.sends==2&&messages.ints["ControlFlags"]==AGENT_CONTROL_YAW_NEG&&gAgent.flags==0);
 gAgent.flags=AGENT_CONTROL_YAW_POS;movement.frame(.22);
 assert(messages.sends==3&&messages.ints["ControlFlags"]==AGENT_CONTROL_YAW_POS&&gAgent.flags==0);
 movement.frame(.23);assert(messages.sends==4&&messages.ints["ControlFlags"]==0);
 movement.frame(.24);assert(messages.sends==4);
 gAgent.flags=AGENT_CONTROL_FLY|AGENT_CONTROL_AWAY|AGENT_CONTROL_MOUSELOOK|AGENT_CONTROL_STAND_UP;
 movement.frame(.25);assert(messages.ints["ControlFlags"]&AGENT_CONTROL_STAND_UP);
 assert(gAgent.flags==(AGENT_CONTROL_FLY|AGENT_CONTROL_AWAY|AGENT_CONTROL_MOUSELOOK));
 movement.mFrozen=true;movement.frame(.26);assert(messages.ints["ControlFlags"]==0);
 gAgent.flags|=AGENT_CONTROL_YAW_NEG;movement.frame(.27);assert(!(gAgent.flags&AGENT_CONTROL_YAW_NEG));
 movement.mFrozen=false;messages.result=0;auto accepted=movement.mLastAgentControls;
 gAgent.flags|=AGENT_CONTROL_STAND_UP;movement.frame(.28);
 assert(movement.mLastAgentControls==accepted&&!(gAgent.flags&AGENT_CONTROL_STAND_UP));
 messages.result=1;movement.frame(.29);assert(movement.mLastAgentControls==gAgent.flags);
}
'''.replace('FUNCTIONS', functions).replace('RESET', reset).replace('DISPATCH', dispatch)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'camera_transport.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_production_input_prioritizes_ui_and_resets_lifetime(self):
        viewer = (ROOT / 'indra/newview/llviewerwindow.cpp').read_text()
        key = method(viewer, 'bool LLViewerWindow::handleTranslatedKeyDown(')
        self.assertIn('return gViewerInput.handleKey(key, mask, repeated)', key)
        self.assertNotIn('owner->movementAction', key)
        scan = method(viewer, 'void LLViewerWindow::handleScanKey(')
        self.assertIn('gViewerInput.scanKey(key, key_down, key_up, key_level)', scan)
        self.assertNotIn('if (mNativeVulkan) return', scan)
        self.assertIn('releaseMovement()', method(viewer, 'void LLViewerWindow::handleFocusLost('))
        session = (ROOT / 'indra/newview/vsnativesession.cpp').read_text()
        self.assertIn('mCamera.reset()', method(session, 'void VSNativeSession::reset()'))
        self.assertIn('releaseMovement()', method(session, 'bool VSNativeSession::changeRegion('))
        receive = method(session, 'void VSNativeSession::receive(')
        self.assertLess(receive.index('admit(name,msg->getSender())'), receive.index('world.agentPosition(position)'))


if __name__ == '__main__':
    unittest.main()
