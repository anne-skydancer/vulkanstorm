#include <cassert>
#include <cstddef>
using U32=unsigned;
constexpr U32 AGENT_CONTROL_MOUSELOOK=16;
enum ECameraMode {CAMERA_MODE_THIRD_PERSON,CAMERA_MODE_MOUSELOOK,CAMERA_MODE_FOLLOW,CAMERA_MODE_CUSTOMIZE_AVATAR};
enum EKeyboardMode {MODE_FIRST_PERSON,MODE_EDIT_AVATAR,MODE_SITTING,MODE_THIRD_PERSON};
struct Settings {bool enabled=true;bool getBOOL(const char*)const{return enabled;}}gSavedSettings;
struct RlvActions {inline static bool enabled=false,allowed=true;static bool isRlvEnabled(){return enabled;}static bool canChangeToMouselook(){return allowed;}};
struct LLViewerJoystick {bool overrideCamera=false;static LLViewerJoystick* getInstance(){static LLViewerJoystick value;return &value;}bool getOverrideCamera(){return overrideCamera;}};
struct Window {int resets=0;void resetBusyCount(){++resets;}};
struct Viewer {Window window;Window* getWindow(){return &window;}}viewer,*gViewerWindow=&viewer;
struct Menu {int hides=0;void hideMenus(){++hides;}}menu;
struct LLMenuGL {inline static Menu* sMenuContainer=&menu;};
struct LLUI {int clears=0;static LLUI* getInstance(){static LLUI value;return &value;}void clearPopups(){++clears;}};
struct Focus {int clears=0;void setKeyboardFocus(std::nullptr_t){++clears;}}gFocusMgr;
struct AOEngine {int changes=0;bool mouselook=false;static AOEngine* getInstance(){static AOEngine value;return &value;}void inMouselook(bool value){mouselook=value;++changes;}};
enum EAnimRequest {ANIM_REQUEST_START,ANIM_REQUEST_STOP};constexpr int ANIM_AGENT_CUSTOMIZE=7;
struct Avatar {int stopped=0;void stopMotion(int id){if(id==ANIM_AGENT_CUSTOMIZE)++stopped;}}avatar;Avatar* vs_avatar_character(){return &avatar;}
struct Agent {U32 flags=0;int unpauses=0,uiTransitions=0,customizeStops=0;bool sitting=false,customAnim=true;void sendAnimationRequest(int id,EAnimRequest request){if(id==ANIM_AGENT_CUSTOMIZE&&request==ANIM_REQUEST_STOP)++customizeStops;}void setCustomAnim(bool value){customAnim=value;}void unpauseAnimation(){++unpauses;}void setControlFlags(U32 value){flags|=value;}void clearControlFlags(U32 value){flags&=~value;}void endAnimationUpdateUI(){++uiTransitions;}bool isSitting(){return sitting;}}gAgent;
struct Morph {bool visible=false;bool getVisible(){return visible;}void setVisible(bool value){visible=value;}}morph,*gMorphView=&morph;
struct LLAgentCamera {
 ECameraMode mCameraMode=CAMERA_MODE_THIRD_PERSON,mLastCameraMode=CAMERA_MODE_THIRD_PERSON;bool mCameraAnimating=false;
 bool completeNativeCameraMode(bool,bool=false);
 void updateLastCamera(){mLastCameraMode=mCameraMode;}
 bool cameraMouselook()const{return mCameraMode==CAMERA_MODE_MOUSELOOK && mLastCameraMode==CAMERA_MODE_MOUSELOOK;}
}gAgentCamera;
struct LLViewerInput {EKeyboardMode getMode()const;}input;
CAMERA_METHOD
INPUT_METHOD
int main(){
 auto& camera=gAgentCamera;auto* ao=AOEngine::getInstance();
 assert(input.getMode()==MODE_THIRD_PERSON);
 assert(camera.completeNativeCameraMode(true));
 assert(camera.cameraMouselook() && input.getMode()==MODE_FIRST_PERSON);
 assert(ao->mouselook && ao->changes==1 && gAgent.uiTransitions==1 && (gAgent.flags&AGENT_CONTROL_MOUSELOOK));
 assert(gFocusMgr.clears==1 && menu.hides==1 && gAgent.unpauses==1);
 for(int i=0;i<100;++i)assert(camera.completeNativeCameraMode(true));
 assert(ao->changes==1 && gAgent.uiTransitions==1);
 assert(camera.completeNativeCameraMode(false));
 assert(input.getMode()==MODE_THIRD_PERSON && !ao->mouselook && ao->changes==2 && gAgent.uiTransitions==2 && !(gAgent.flags&AGENT_CONTROL_MOUSELOOK));
 for(int mask=0;mask<8;++mask){
  gSavedSettings.enabled=!(mask&1);RlvActions::enabled=mask&2;RlvActions::allowed=false;LLViewerJoystick::getInstance()->overrideCamera=mask&4;
  assert(camera.completeNativeCameraMode(true)==(mask==0));
  if(!mask)assert(camera.completeNativeCameraMode(false));
  assert(input.getMode()==MODE_THIRD_PERSON);
 }
 gSavedSettings.enabled=true;RlvActions::enabled=false;LLViewerJoystick::getInstance()->overrideCamera=false;
 camera.mCameraMode=camera.mLastCameraMode=CAMERA_MODE_FOLLOW;
 assert(camera.completeNativeCameraMode(false) && camera.mCameraMode==CAMERA_MODE_FOLLOW);
 assert(camera.completeNativeCameraMode(false,true) && camera.mCameraMode==CAMERA_MODE_THIRD_PERSON && camera.mLastCameraMode==CAMERA_MODE_THIRD_PERSON);
 camera.mCameraMode=camera.mLastCameraMode=CAMERA_MODE_CUSTOMIZE_AVATAR;
 assert(camera.completeNativeCameraMode(false) && camera.mCameraMode==CAMERA_MODE_CUSTOMIZE_AVATAR);
 morph.visible=true;assert(input.getMode()==MODE_EDIT_AVATAR);morph.visible=false;
 gAgent.sitting=true;assert(input.getMode()==MODE_SITTING);
 assert(avatar.stopped==0 && gAgent.customizeStops==0 && gAgent.customAnim);morph.visible=true;
 assert(camera.completeNativeCameraMode(true));assert(input.getMode()==MODE_FIRST_PERSON);
 // Leaving appearance editing retires the customize animation and morph view.
 assert(avatar.stopped==1 && gAgent.customizeStops==1 && !gAgent.customAnim && !morph.visible);
}
