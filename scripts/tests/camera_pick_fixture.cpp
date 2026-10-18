#include <cassert>
#include <cstddef>
#include <string>
#include <vector>
using S32=int;using MASK=unsigned;
constexpr MASK MASK_ALT=1;constexpr int CAMERA_MODE_CUSTOMIZE_AVATAR=2,SELECT_TYPE_HUD=3;constexpr bool ANIMATE=true;
std::vector<int> trace;
struct LLVector3d{int value=0;bool isExactlyZero()const{return value==0;}};
struct LLViewerObject
{
 bool hud=false,attachment=false,owner=false;LLViewerObject* wearer=nullptr;
 bool isHUDAttachment(){return hud;}bool isAttachment(){return attachment;}bool permYouOwner(){return owner;}
 bool isSelf();
};
LLViewerObject selfObject;LLViewerObject* gAgentAvatarp=&selfObject;bool validAvatar=true;
bool LLViewerObject::isSelf(){return this==gAgentAvatarp;}
bool isAgentAvatarValid(){return validAvatar;}
struct LLVOAvatar{static LLViewerObject*findAvatarFromAttachment(LLViewerObject* p){return p->wearer;}};
struct LLPickInfo{struct Point{int mX=0,mY=0;}mMousePt;LLViewerObject* object=nullptr;LLVector3d mPosGlobal;MASK mKeyMask=0;int mObjectID=0;LLViewerObject*getObject()const{return object;}};
struct Tool{std::string name="Pie";std::string getName(){return name;}};
struct LLToolCamera:Tool
{
 bool mClickPickPending=true,mValidClickPoint=false,mMouseSteering=false;
 int mMouseDownX=0,mMouseDownY=0;
 inline static LLToolCamera* instance=nullptr;
 static LLToolCamera*getInstance(){return instance;}
 static void referenceCallback(const LLPickInfo&);
 template<class Peer>void applyPick(const LLPickInfo&,Peer&);
};
struct Window{bool left=true;void moveCursorToCenter(){trace.push_back(1);}bool getLeftMouseDown(){return left;}}window,*gViewerWindow=&window;
struct Camera
{
 int mode=0;bool third=true;
 int getCameraMode(){return mode;}bool cameraThirdPerson(){return third;}
 void setFocusOnAvatar(bool a,bool b){trace.push_back(10+2*a+b);}
 void setFocusGlobal(const LLPickInfo&){trace.push_back(20);}
 LLVector3d getCameraPositionGlobal(){trace.push_back(30);return{4};}
 void setCameraPosAndFocusGlobal(LLVector3d eye,LLVector3d focus,int id){trace.push_back(40+eye.value+focus.value+id);}
}gAgentCamera;
struct Selection{int count=0,type=0;int getObjectCount(){return count;}int getSelectType(){return type;}};
using LLObjectSelectionHandle=Selection*;
struct LLSelectMgr{Selection selection;static LLSelectMgr*getInstance(){static LLSelectMgr m;return &m;}static LLSelectMgr&instance(){return*getInstance();}Selection*getSelection(){return &selection;}};
struct LLToolMgr{Tool current;bool baseCamera=false;static LLToolMgr*getInstance(){static LLToolMgr m;return &m;}Tool*getCurrentTool(){return &current;}Tool*getBaseTool(){return baseCamera?static_cast<Tool*>(LLToolCamera::getInstance()):&current;}};
struct Morph{void setCameraDrivenByKeys(bool){trace.push_back(50);}}morph,*gMorphView=&morph;
bool gCameraBtnZoom=false,freeCamera=false,freeze=false;
struct LLFloaterCamera{static bool inFreeCameraMode(){return freeCamera;}};
struct Settings{}gSavedSettings;
template<class T>struct LLCachedControl{LLCachedControl(Settings&,const char*){}operator bool()const{return freeze;}};
struct Peer
{
 bool live=true;
 bool current(){return live;}LLViewerObject*object(const LLPickInfo& p){return p.getObject();}
 bool hud(LLViewerObject*p){return p->isHUDAttachment();}
 bool hudSelection(){auto*s=LLSelectMgr::instance().getSelection();return s->getObjectCount()&&s->getSelectType()==SELECT_TYPE_HUD;}
 bool self(LLViewerObject*p){return isAgentAvatarValid()&&p==gAgentAvatarp;}
 bool isSelfPointer(LLViewerObject*p){return p==gAgentAvatarp;}
 bool attachment(LLViewerObject*p){return p->isAttachment();}bool youOwner(LLViewerObject*p){return p->permYouOwner();}
 bool attachmentSelf(LLViewerObject*p){return LLVOAvatar::findAvatarFromAttachment(p)->isSelf();}
};
REFERENCE
CURRENT
int main()
{
 LLViewerObject object;object.wearer=gAgentAvatarp;
 for(unsigned n=0;n<65536;++n)
 {
  LLPickInfo pick;pick.object=n&1?((n&2)?gAgentAvatarp:&object):nullptr;pick.mPosGlobal.value=n&4?5:0;pick.mMousePt={int(n%511),int(n%223)};pick.mKeyMask=n&8?MASK_ALT:0;pick.mObjectID=int(n%37);
  object.hud=n&16;object.attachment=n&32;object.owner=n&64;
  gAgentCamera.mode=n&128?CAMERA_MODE_CUSTOMIZE_AVATAR:0;gAgentCamera.third=n&256;
  LLToolMgr::getInstance()->current.name=n&512?"Camera":"Pie";LLToolMgr::getInstance()->baseCamera=n&1024;
  auto& selection=LLSelectMgr::instance().selection;selection.count=n&2048?1:0;selection.type=n&4096?SELECT_TYPE_HUD:0;
  freeCamera=n&8192;gCameraBtnZoom=n&16384;freeze=n&32768;validAvatar=n%3;window.left=n%5;gMorphView=n%7?&morph:nullptr;
  LLToolCamera a,b;a.mClickPickPending=b.mClickPickPending=n%11;
  trace.clear();LLToolCamera::instance=&a;LLToolCamera::referenceCallback(pick);const auto expected=trace;
  trace.clear();LLToolCamera::instance=&b;Peer peer;b.applyPick(pick,peer);
  assert(trace==expected&&a.mClickPickPending==b.mClickPickPending&&a.mValidClickPoint==b.mValidClickPoint&&a.mMouseSteering==b.mMouseSteering&&a.mMouseDownX==b.mMouseDownX&&a.mMouseDownY==b.mMouseDownY);
  LLToolCamera retired;trace.clear();peer.live=false;retired.applyPick(pick,peer);assert(trace.empty()&&retired.mClickPickPending&&!retired.mValidClickPoint);
 }
}
