#include <cassert>
#include <memory>
using LLUUID=int;constexpr LLUUID gAgentID=1;
struct LLVector3{double x=0;};struct LLVector3d{double x=0;LLVector3d()=default;LLVector3d(LLVector3 v):x(v.x){}explicit LLVector3d(double v):x(v){}LLVector3d operator+(LLVector3d v)const{return LLVector3d(x+v.x);}};
LLVector3d from_region_handle(int){return LLVector3d(1000);}
struct View{LLVector3 center{5},focus{8};};
struct LLAgentCamera;
struct VSNativeSession
{
 inline static std::shared_ptr<VSNativeSession> selected;
 static auto active(){return selected;}int resets=0;
 void cameraReset();int handle(){return 1;}View cameraState(){return{};}
};
struct LLAgentCamera
{
 bool mFocusOnAvatar=false,mCameraAnimating=false,mAllowChangeToFollow=true;float mCameraFOVZoomFactor=9;
 int starts=0,stops=0,binds=0,poses=0;LLUUID lastID=9;double lastEye=0,lastFocus=0;bool lastAnimate=false;
 void startCameraAnimation(){++starts;mCameraAnimating=true;}void stopCameraAnimation(){++stops;mCameraAnimating=false;}
 void bindNativeFocusObject(LLUUID id){++binds;lastID=id;}
 bool setNativeCameraPose(LLVector3d eye,LLVector3d focus,LLUUID id,bool animate){++poses;lastEye=eye.x;lastFocus=focus.x;lastID=id;lastAnimate=animate;mFocusOnAvatar=false;if(animate)startCameraAnimation();else stopCameraAnimation();return true;}
 void setFocusOnAvatar(bool,bool,bool);
}camera;
void VSNativeSession::cameraReset(){++resets;camera.mCameraAnimating=false;}
METHOD
int main()
{
 auto owner=std::make_shared<VSNativeSession>();VSNativeSession::selected=owner;
 camera.setFocusOnAvatar(true,true,true);assert(camera.mFocusOnAvatar&&camera.mCameraAnimating&&camera.starts==1&&owner->resets==1&&camera.lastID==0&&camera.mCameraFOVZoomFactor==0);
 camera.setFocusOnAvatar(true,true,true);assert(camera.starts==1&&owner->resets==1); // Unchanged focus never restarts a timer.
 camera.setFocusOnAvatar(false,false,true);assert(!camera.mFocusOnAvatar&&!camera.mCameraAnimating&&!camera.mAllowChangeToFollow&&camera.poses==1);
 assert(camera.lastEye==1005&&camera.lastFocus==1008&&camera.lastID==gAgentID&&!camera.lastAnimate);
 camera.setFocusOnAvatar(true,false,false);assert(camera.mFocusOnAvatar&&!camera.mCameraAnimating&&owner->resets==1&&camera.stops==2);
 camera.setFocusOnAvatar(false,true,false);assert(!camera.mFocusOnAvatar&&camera.mCameraAnimating&&camera.lastAnimate&&camera.poses==2);
}
