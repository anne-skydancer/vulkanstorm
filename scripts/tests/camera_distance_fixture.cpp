#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <map>
#include <string>
using F32=float;
template<class T>T llmax(T a,T b){return std::max(a,b);}
template<class T>T llmin(T a,T b){return std::min(a,b);}
struct Settings {std::map<std::string,double> values;}gSavedSettings;
template<class T>struct LLCachedControl {
 std::string name;LLCachedControl(Settings&,const char* n,T):name(n){}
 operator T()const{return T(gSavedSettings.values.at(name));}
 T operator()()const{return T(*this);}
};
constexpr int CAMERA_MODE_CUSTOMIZE_AVATAR=1,STATE_MISC=10;
struct AgentCamera {F32 mDrawDistance=128;int mode=0;int getCameraMode()const{return mode;}}gAgentCamera;
struct LLViewerTexture {
 inline static F32 sDesiredDiscardBias=0,budgetFactor=1;inline static bool critical=false;
 static bool isSystemMemoryCritical(){return critical;}static F32 getSystemMemoryBudgetFactor(){return budgetFactor;}
};
struct LLViewerCamera {
 F32 farClip=128;static LLViewerCamera* getInstance(){static LLViewerCamera c;return &c;}
 F32 getFar()const{return farClip;}
};
struct LLWorld {
 bool locked=false;static LLWorld* getInstance(){static LLWorld w;return &w;}
 bool getLockedDrawDistance()const{return locked;}
};
LLWorld& vs_region_limits(){return *LLWorld::getInstance();}
double frameTime=0;
struct LLFrameTimer {
 double started=frameTime;float getElapsedTimeF32()const{return float(frameTime-started);}
 void reset(){started=frameTime;}
};
struct LLAgent {enum {TELEPORT_ARRIVING=1};};int tp_state=0;
struct LLStartUp {inline static int state=20;static int getStartupState(){return state;}};
bool gCubeSnapshot=false;
REFERENCE
CURRENT
int main(){
 for(float requested:{1.f,16.f,128.f,1024.f,4096.f})
  for(float bias:{0.f,2.f,2.1f,3.f,8.f})
   for(bool optimize:{false,true})for(bool customize:{false,true})
    for(bool snapshot:{false,true})for(bool locked:{false,true}){
     gAgentCamera.mDrawDistance=requested;gAgentCamera.mode=customize?CAMERA_MODE_CUSTOMIZE_AVATAR:0;
     gSavedSettings.values={{"FSDrawDistanceVRAMOptimization",optimize},{"RenderReflectionProbeDrawDistance",64}};
     LLViewerTexture::sDesiredDiscardBias=bias;LLWorld::getInstance()->locked=locked;
     LLViewerCamera::getInstance()->farClip=777;gCubeSnapshot=snapshot;
     assert(vs_camera_far_clip(snapshot)==reference_camera_far_clip());
     if(locked)assert(vs_camera_far_clip(snapshot)==777);
    }
 for(float requested:{16.f,64.f,128.f,512.f,4096.f})
  for(bool critical:{false,true})for(float factor:{1.f,1.5f,2.f,4.f}){
   gAgentCamera.mDrawDistance=requested;LLViewerTexture::critical=critical;LLViewerTexture::budgetFactor=factor;
   tp_state=LLAgent::TELEPORT_ARRIVING;LLStartUp::state=20;
   assert(vs_agent_draw_distance(true)==reference_agent_draw_distance());
   tp_state=0;
   for(double delta:{0.,.5,.5,.01,1.,1.01,2.,.2,1.1,20.}){
    frameTime+=delta;
    assert(vs_agent_draw_distance(false)==reference_agent_draw_distance());
   }
   // A reduced request or memory budget is applied immediately; an increase
   // follows the reference ten-percent/one-second interest-list ramp.
   gAgentCamera.mDrawDistance*=.25f;
   assert(vs_agent_draw_distance(false)==reference_agent_draw_distance());
   gAgentCamera.mDrawDistance*=8;
   for(int step=0;step<12;++step){frameTime+=1.01;assert(vs_agent_draw_distance(false)==reference_agent_draw_distance());}
   LLStartUp::state=STATE_MISC-1;
   assert(vs_agent_draw_distance(true)==reference_agent_draw_distance());
  }
}
