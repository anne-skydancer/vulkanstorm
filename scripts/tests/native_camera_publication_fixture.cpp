#include <cassert>
#include <cmath>
#include <algorithm>
using F32=float;constexpr F32 F_PI=3.14159265358979323846f;
template<class T>T llclamp(T x,T a,T b){return std::clamp(x,a,b);}
struct Rotation{bool valid=true;bool isFinite()const{return valid;}};
struct LLVector3{float x=0,y=0,z=0;static const LLVector3 x_axis;bool isFinite()const{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}bool isNull()const{return x==0&&y==0&&z==0;}LLVector3 operator*(Rotation)const{return *this;}};
const LLVector3 LLVector3::x_axis{1,0,0};
 using LLVector3d=LLVector3;using U64=unsigned long long;
 LLVector3d operator+(LLVector3d a,LLVector3d b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
 LLVector3d from_region_handle(U64 handle){return {float(handle*256),float(handle*512),0};}
 struct LLAgentCamera{LLVector3d mFocusGlobal;void publishNativeFocus(const LLVector3d&);};LLAgentCamera gAgentCamera;
 FOCUS_BODY

struct Rect{int w=1024,h=768;int getWidth()const{return w;}int getHeight()const{return h;}};
struct Window{Rect rect;Rect getWindowRectRaw(){return rect;}};Window window;Window* gViewerWindow=&window;
float farDistance=128;float vs_camera_far_clip(){return farDistance;}
struct Agent{LLVector3 forward;int resets=0;void resetAxes(LLVector3 v){forward=v;++resets;}}gAgent;
struct LLViewerCamera{
 inline static LLViewerCamera* value=nullptr;LLVector3 eye,at,left,up;float view=.73f,nearClip=0,farClip=0,aspect=0;int height=0;
 static bool instanceExists(){return value;}static void createInstance(){value=new LLViewerCamera;}static auto getInstance(){return value;}
 void setOrigin(LLVector3 v){eye=v;}void setAxes(LLVector3 a,LLVector3 l,LLVector3 u){at=a;left=l;up=u;}
 void setViewNoBroadcast(float v){view=v;}void setNear(float v){nearClip=v;}void setFar(float v){farClip=v;}void setAspect(float v){aspect=v;}void setViewHeightInPixels(int v){height=v;}
};
struct VSNativeSession{enum class Phase{Login,Connected};bool mShutdown=false,mChangingRegion=false;Phase mPhase=Phase::Connected;U64 mHandle=2;
 struct State{LLVector3 focus{20,25,35},center{10,20,30},at{1,0,0},left{0,1,0},up{0,0,1};Rotation bodyRotation;}state;
 void synchronizeCameraMode(bool=false){}
 State cameraState(){return state;}bool publishCamera();};
METHOD
int main(){
 VSNativeSession owner;assert(owner.publishCamera());auto camera=LLViewerCamera::value;assert(camera);
 assert(camera->eye.x==10 && camera->at.x==1 && camera->left.y==1 && camera->up.z==1 && gAgent.resets==1);
 assert(gAgentCamera.mFocusGlobal.x==532 && gAgentCamera.mFocusGlobal.y==1049 && gAgentCamera.mFocusGlobal.z==35);
 assert(camera->view==.73f && camera->nearClip==.1f && camera->farClip==128 && camera->height==768);
 farDistance=2048;assert(owner.publishCamera());assert(camera->farClip==2048);
 window.rect={800,400};owner.state.center.x=11;assert(owner.publishCamera());assert(camera->aspect==2 && camera->eye.x==11);
 owner.mChangingRegion=true;owner.state.center.x=12;assert(!owner.publishCamera() && camera->eye.x==11);owner.mChangingRegion=false;
 owner.mPhase=VSNativeSession::Phase::Login;assert(!owner.publishCamera());owner.mPhase=VSNativeSession::Phase::Connected;
 owner.state.focus.x=std::nanf("");assert(!owner.publishCamera() && gAgentCamera.mFocusGlobal.x==532);owner.state.focus.x=20;
 owner.state.at.x=std::nanf("");assert(!owner.publishCamera());owner.state.at.x=1;
 owner.state.bodyRotation.valid=false;assert(!owner.publishCamera());owner.state.bodyRotation.valid=true;
 window.rect.h=0;assert(!owner.publishCamera());window.rect.h=400;gViewerWindow=nullptr;assert(!owner.publishCamera());gViewerWindow=&window;
 owner.mShutdown=true;assert(!owner.publishCamera());delete camera;LLViewerCamera::value=nullptr;
}
