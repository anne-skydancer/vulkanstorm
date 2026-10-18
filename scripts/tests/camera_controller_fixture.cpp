// Frozen-reference differential for shared CPU camera animation and focus extents.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>
using F32=float;using F64=double;using U32=unsigned;
constexpr unsigned VX=0,VY=1,VZ=2;
constexpr float F32_MAX=std::numeric_limits<float>::max(),F_SQRT3=1.7320508075688772f;
constexpr float AVATAR_ZOOM_MIN_X_FACTOR=.55f,AVATAR_ZOOM_MIN_Y_FACTOR=.7f,AVATAR_ZOOM_MIN_Z_FACTOR=1.15f,OBJECT_EXTENTS_PADDING=.5f;
constexpr int CAMERA_MODE_THIRD_PERSON=0,CAMERA_MODE_MOUSELOOK=1;
constexpr float HEAD_BUFFER_SIZE=.3f,MAX_CAMERA_SMOOTH_DISTANCE=50.f,FOV_ZOOM_HALF_LIFE=.07f;
template<class T>T llmin(T a,T b){return std::min(a,b);}template<class T>T llmax(T a,T b){return std::max(a,b);}
template<class T>T llclamp(T x,T a,T b){return std::clamp(x,a,b);}
float llsmoothstep(float a,float b,float v){const auto t=llclamp((v-a)/(b-a),0.f,1.f);return t*t*(3.f-2.f*t);}
struct LLQuaternion{float z=0;};LLQuaternion operator~(LLQuaternion q){q.z=-q.z;return q;}
template<class T>struct Vec
{
 union{T mV[3];T mdV[3];};
 Vec():mV{0,0,0}{}Vec(T x,T y,T z):mV{x,y,z}{}
 template<class U>explicit Vec(const Vec<U>& v):mV{T(v.mV[0]),T(v.mV[1]),T(v.mV[2])}{}
 Vec operator+(const Vec& x)const{return{mV[0]+x.mV[0],mV[1]+x.mV[1],mV[2]+x.mV[2]};}
 Vec operator-(const Vec& x)const{return{mV[0]-x.mV[0],mV[1]-x.mV[1],mV[2]-x.mV[2]};}
 Vec operator*(T a)const{return{mV[0]*a,mV[1]*a,mV[2]*a};}
 Vec operator/(T a)const{return *this*(T(1)/a);}
 T operator*(const Vec& x)const{return mV[0]*x.mV[0]+mV[1]*x.mV[1]+mV[2]*x.mV[2];}
 bool operator==(const Vec& x)const{return mV[0]==x.mV[0]&&mV[1]==x.mV[1]&&mV[2]==x.mV[2];}
 T magVec()const{return std::sqrt(*this**this);}
 T normalize(){const auto l=magVec();if(l>0)*this=*this/l;return l;}
 void abs(){for(auto& v:mV)v=std::abs(v);}void clamp(T a,T b){for(auto& v:mV)v=llclamp(v,a,b);}
 void rotVec(LLQuaternion q){const auto x=mV[0],y=mV[1];mV[0]=x*std::cos(q.z)-y*std::sin(q.z);mV[1]=x*std::sin(q.z)+y*std::cos(q.z);}
 inline static const Vec x_axis{1,0,0},y_axis{0,1,0},z_axis{0,0,1};
};
template<class T>Vec<T> operator*(T x,const Vec<T>& v){return v*x;}
template<class T,class F>T lerp(T a,T b,F f){return a+(b-a)*f;}
using LLVector3=Vec<float>;using LLVector3d=Vec<double>;
struct LLViewerCamera{float near=.1f;static LLViewerCamera*getInstance(){static LLViewerCamera v;return &v;}float getNear(){return near;}};
struct Settings{float smoothing=0;float getF32(const char*)const{return smoothing;}}gSavedSettings;
struct LLSmoothInterpolation{static float getInterpolant(float half,bool=false){return half==0?1.f:1.f-std::pow(2.f,-1.f/(60.f*half));}};
std::vector<int> effects;
struct Agent{LLVector3d p;LLVector3 getPosAgentFromGlobal(LLVector3d v){return LLVector3(v-p);}LLVector3d getPositionGlobal(){return p;}void setShowAvatar(bool b){effects.push_back(b?1:0);}void endAnimationUpdateUI(){effects.push_back(2);}}gAgent;
struct Avatar{void updateAttachmentVisibility(int){effects.push_back(3);}}avatar,*gAgentAvatarp=&avatar;
bool validAvatar=false;bool isAgentAvatarValid(){return validAvatar;}
struct LLToolMgr{bool build=false;static LLToolMgr*getInstance(){static LLToolMgr m;return &m;}bool inBuildMode(){return build;}};
struct Drawable{bool null=false;bool isNull(){return null;}};
struct Object
{
 bool is_dead=false,is_mesh=false,is_avatar=false;Drawable mDrawable;LLQuaternion rotation;LLVector3 extents,position;
 bool isDead(){return is_dead;}bool isMesh(){return is_mesh;}bool isAvatar(){return is_avatar;}
 LLQuaternion getRenderRotation(){return rotation;}LLVector3 getScale(){return extents;}LLVector3 getPositionAgent(){return position;}
 void dump(){}
};
struct Log{template<class T>Log&operator<<(const T&){return*this;}};
#define LL_WARNS() Log{}
#define LL_ERRS() Log{}
#define LL_ENDL 0
struct Timer{float elapsed=0;float getElapsedTimeF32(){return elapsed;}};
struct Camera
{
 int mCameraMode=0,mLastCameraMode=0;bool mCameraAnimating=false,mCameraSmoothingStop=false,mFocusOnAvatar=false,disabled=false;
 float mAnimationDuration=.5f,mCameraFOVZoomFactor=0,mCameraCurrentFOVZoomFactor=0,mFocusObjectDist=0;
 LLVector3d mAnimationCameraStartGlobal,mAnimationFocusStartGlobal,mFocusGlobal,mFocusTargetGlobal,mCameraSmoothingLastPositionGlobal,mCameraSmoothingLastPositionAgent;
 LLVector3 mCameraVirtualPositionAgent,mFocusObjectOffset,cameraPosition;
 Timer mAnimationTimer;Object* mFocusObject=nullptr;
 bool cameraThirdPerson(){return mCameraMode==CAMERA_MODE_THIRD_PERSON;}bool isDisableCameraConstraints(){return disabled;}
 LLVector3 getCameraPositionAgent(){return cameraPosition;}
 float calcCameraFOVZoomFactor(){return mCameraFOVZoomFactor;}
 LLVector3d referenceTargets(LLVector3d,const LLVector3d&);
 LLVector3d applyCameraTargets(LLVector3d,const LLVector3d&);
 bool referenceMin(F32&);
 template<class Focus>bool sharedCameraMinDistance(Focus&,F32&);
};
REFERENCE_TARGETS
CURRENT_TARGETS
REFERENCE_MIN
CURRENT_MIN
int main()
{
 std::mt19937 random(8181);std::uniform_real_distribution<float> value(-30.f,30.f),unit(0.f,1.f);
 auto vec=[&]{return LLVector3d(value(random),value(random),value(random));};
 for(int n=0;n<30000;++n)
 {
  Camera initial;initial.mCameraMode=n%3;initial.mLastCameraMode=(n/3)%3;initial.mCameraAnimating=n&1;initial.mCameraSmoothingStop=n&2;initial.mFocusOnAvatar=n&4;
  initial.mAnimationDuration=.02f+unit(random);initial.mAnimationTimer.elapsed=unit(random)*2;
  initial.mAnimationCameraStartGlobal=vec();initial.mAnimationFocusStartGlobal=vec();
  initial.mCameraSmoothingLastPositionGlobal=vec();initial.mCameraSmoothingLastPositionAgent=vec();
  initial.mCameraFOVZoomFactor=unit(random)*4;initial.mCameraCurrentFOVZoomFactor=unit(random)*2;
  initial.mFocusObject=(n&8)?reinterpret_cast<Object*>(1):nullptr;
  auto a=initial,b=initial;auto target=vec(),focus=vec();if(n%17==0)target=initial.mAnimationCameraStartGlobal;
  gAgent.p=vec();gSavedSettings.smoothing=unit(random)*10;LLToolMgr::getInstance()->build=n&16;validAvatar=n&32;
  effects.clear();const auto eyeA=a.referenceTargets(target,focus);const auto trace=effects;
  effects.clear();const auto eyeB=b.applyCameraTargets(target,focus);
  assert(eyeA==eyeB&&trace==effects&&a.mFocusGlobal==b.mFocusGlobal&&a.mCameraVirtualPositionAgent==b.mCameraVirtualPositionAgent);
  assert(a.mCameraSmoothingLastPositionGlobal==b.mCameraSmoothingLastPositionGlobal&&a.mCameraSmoothingLastPositionAgent==b.mCameraSmoothingLastPositionAgent);
  assert(a.mCameraAnimating==b.mCameraAnimating&&a.mCameraSmoothingStop==b.mCameraSmoothingStop&&a.mCameraCurrentFOVZoomFactor==b.mCameraCurrentFOVZoomFactor);
  Object object;object.is_dead=n%19==0;object.is_mesh=n%23==0;object.is_avatar=n&1;object.mDrawable.null=n%29==0;
  object.rotation.z=value(random);object.extents=LLVector3(std::abs(value(random))+.001f,std::abs(value(random))+.001f,std::abs(value(random))+.001f);object.position=LLVector3(vec());
  Camera c;c.mFocusObject=n%31==0?nullptr:&object;c.disabled=n%13==0;c.mFocusObjectOffset=LLVector3(vec());c.mFocusTargetGlobal=vec();c.cameraPosition=LLVector3(vec());
  struct Peer{Object* p;bool exists(){return p;}bool dead(){return p->isDead();}bool mesh(){return p->isMesh();}bool missingDrawable(){return p->mDrawable.isNull();}void dump(){p->dump();}LLQuaternion rotation(){return p->getRenderRotation();}LLVector3 scale(){return p->getScale();}bool avatar(){return p->isAvatar();}LLVector3 agentPosition(){return p->getPositionAgent();}}peer{c.mFocusObject};
  float minA=-1,minB=-1;const bool softA=c.referenceMin(minA),softB=c.sharedCameraMinDistance(peer,minB);assert(softA==softB&&minA==minB);
 }
 std::cout<<"60000 original/shared camera target and focus extent cases passed\n";
}
