#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <array>
using F32=float;using U32=unsigned;using S32=int;
constexpr int VX=0,VY=1,VZ=2;constexpr float F32_MAX=std::numeric_limits<float>::max();
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b){return std::min(a,b);}float llabs(float v){return std::abs(v);}
struct LLQuaternion{float a=0;};LLQuaternion operator~(LLQuaternion q){return {-q.a};}
struct LLVector3{static const LLVector3 zero;float mV[3]{};LLVector3(const float*v):mV{v[0],v[1],v[2]}{};void set(const float*v){for(int n=0;n<3;n++)mV[n]=v[n];}void scaleVec(LLVector3 b){for(int n=0;n<3;n++)mV[n]*=b.mV[n];}LLVector3 scaledVec(LLVector3 b)const{b.scaleVec(*this);return b;}LLVector3()=default;LLVector3(float a,float b,float c):mV{a,b,c}{};void setVec(LLVector3 v){*this=v;}void clamp(float a,float b){for(float&v:mV)v=std::clamp(v,a,b);}void abs(){for(float&v:mV)v=std::abs(v);}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n>0)for(float&v:mV)v/=n;return n;}void rotVec(LLQuaternion q){float a=mV[0],b=mV[1];mV[0]=a*std::cos(q.a)-b*std::sin(q.a);mV[1]=a*std::sin(q.a)+b*std::cos(q.a);}LLVector3&operator-=(LLVector3 b){for(int n=0;n<3;n++)mV[n]-=b.mV[n];return *this;}};
LLVector3 operator+(LLVector3 a,LLVector3 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 a,LLVector3 b){a-=b;return a;}LLVector3 operator*(float s,LLVector3 a){return {s*a.mV[0],s*a.mV[1],s*a.mV[2]};}float operator*(LLVector3 a,LLVector3 b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}LLVector3 operator/(LLVector3 a,float s){return (1/s)*a;}LLVector3 operator*(LLVector3 a,float s){return s*a;}using LLVector3d=LLVector3;
float dist_vec(LLVector3 a,LLVector3 b){auto v=a-b;return std::sqrt(v*v);}float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}LLVector3 lerp(LLVector3 a,LLVector3 b,float t){return (1-t)*a+t*b;}
struct LLMatrix4{LLVector3 axes[3];LLVector3 getFwdRow4()const{return axes[0];}LLVector3 getLeftRow4()const{return axes[1];}LLVector3 getUpRow4()const{return axes[2];}};
struct LLBBox{LLVector3 center,size;bool containsPointAgent(LLVector3 p)const{p-=center;for(int n=0;n<3;n++)if(std::abs(p.mV[n])>size.mV[n]/2)return false;return true;}};

const LLVector3 LLVector3::zero{};LLVector3&operator+=(LLVector3&a,LLVector3 b){a=a+b;return a;}
struct LLVector2{float mV[2]{};LLVector2()=default;LLVector2(float x,float y):mV{x,y}{};LLVector2(const float*p):mV{p[0],p[1]}{}};
struct LLVector4{float mV[4]{};LLVector4()=default;LLVector4(float x,float y,float z,float w):mV{x,y,z,w}{};void set(const float*v){for(int n=0;n<4;n++)mV[n]=v[n];}void normalize(){float l=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(l)for(float&v:mV)v/=l;}};
struct LLVector4a{float mV[4]{};void load3(const float*p){for(int n=0;n<3;n++)mV[n]=p[n];}void set(float a,float b,float c,float d){mV[0]=a;mV[1]=b;mV[2]=c;mV[3]=d;}void setCross3(const LLVector4a&a,const LLVector4a&b){set(a.mV[1]*b.mV[2]-a.mV[2]*b.mV[1],a.mV[2]*b.mV[0]-a.mV[0]*b.mV[2],a.mV[0]*b.mV[1]-a.mV[1]*b.mV[0],0);}void mul(float w){for(float&v:mV)v*=w;}const float*getF32ptr()const{return mV;}};
struct Coord{int mX=0,mY=0;Coord()=default;Coord(int x,int y):mX(x),mY(y){}};using LLCoordScreen=Coord;
struct UUID{int value=0;void setNull(){value=0;}};
struct LLPickInfo{enum{PICK_INVALID,PICK_OBJECT,PICK_LAND,PICK_FLORA};int mPickType=PICK_INVALID;UUID mObjectID;int mObjectFace=-1;Coord mMousePt{320,240},mPickPt,mXYCoords;int mGLTFNodeIndex=-1,mGLTFPrimitiveIndex=-1;LLVector3 mIntersection,mNormal,mBinormal,mObjectOffset,mPosGlobal;LLVector4 mTangent;LLVector2 mSTCoords,mUVCoords;bool mPickHUD=false,mPickTransparent=true,mWantSurfaceInfo=true;bool wantsSurfaceInfo()const{return mWantSurfaceInfo;}};
struct Agent{LLVector3 offset{50,100,0};LLVector3 getPosGlobalFromAgent(LLVector3 p){return p+offset;}LLVector3 getPosAgentFromGlobal(LLVector3 p){return p-offset;}}gAgent;
struct Camera{float mHUDCurZoom=1;LLVector3 received;bool accepted=true;std::optional<LLVector3>calcNativeFocusOffset(int,LLVector3 p,int,int){received=p;return accepted?std::optional<LLVector3>{{1,2,3}}:std::nullopt;}}gAgentCamera;
struct LLViewerCamera{static LLViewerCamera*getInstance(){static LLViewerCamera c;return &c;}float getView(){return 1;}float getFar(){return 100;}};
LLVector3 from_region_handle(unsigned){return {100,200,0};}
struct VSNativeSession{static std::shared_ptr<VSNativeSession>active(){static auto s=std::make_shared<VSNativeSession>();return s;}bool ready=true;bool regionReady(){return ready;}unsigned handle(){return 1;}struct CameraState{LLVector3 center{0,0,1},at{1,0,0},left{0,1,0},up{0,0,1};};CameraState cameraState(){return {};}};
constexpr int LL_PCODE_LEGACY_GRASS=5,LL_PCODE_LEGACY_TREE=6,LL_PCODE_TREE_NEW=7;
struct Scene{int pcode=2;struct Admission{int pcode;};struct Pick{struct Face{struct Object{UUID id{7};}object;unsigned face=2;}face;bool land=false,hud=false,surfaceKnown=true;LLVector3 position{2,3,4},normal{0,0,2};std::array<float,2>uv{.2f,.7f},st{.3f,.4f};std::array<float,4>tangent{2,0,0,-1};std::array<int,2>xy{20,30};int gltfNode=-1,gltfPrimitive=-1;};std::optional<Pick>value{Pick{}};bool focusReady=true;std::optional<Pick>pickHUD(float,float,float,float){return value&&value->hud?value:std::nullopt;}std::optional<Pick>pick(LLVector3,LLVector3,bool,float){return value;}std::optional<Admission>objectAdmission(Pick::Face::Object){return Admission{pcode};}std::optional<int>focusObject(Pick::Face::Object,LLVector3){return focusReady?std::optional<int>{1}:std::nullopt;}};
struct Context{Scene scene;Scene&world(){return scene;}};
struct LLViewerWindow{struct Rect{int mLeft=0,mBottom=0;int getWidth()const{return 640;}int getHeight()const{return 480;}};Context*mVulkanContext;Rect getWindowRectScaled(){return {};}bool nativePick(LLPickInfo&);};
BODY
int main(){Context context;LLViewerWindow window{&context};LLPickInfo info;assert(window.nativePick(info));assert(info.mPickType==LLPickInfo::PICK_OBJECT&&info.mObjectID.value==7&&info.mObjectFace==2);assert(info.mPosGlobal.mV[0]==102&&info.mIntersection.mV[0]==52&&gAgentCamera.received.mV[0]==52);assert(info.mObjectOffset.mV[0]==1&&info.mObjectOffset.mV[2]==3);assert(info.mSTCoords.mV[0]==.3f&&info.mUVCoords.mV[0]==.2f&&info.mUVCoords.mV[1]==.7f&&info.mXYCoords.mY==30);assert(info.mNormal.mV[2]==1&&info.mTangent.mV[0]==1&&info.mBinormal.mV[1]==-1);
context.scene.value->hud=true;assert(window.nativePick(info));assert(info.mPosGlobal.mV[0]==52&&info.mIntersection.mV[0]==2&&gAgentCamera.received.mV[0]==2&&info.mPickHUD);
context.scene.focusReady=false;assert(!window.nativePick(info));assert(info.mPickType==LLPickInfo::PICK_INVALID&&info.mObjectID.value==0&&info.mObjectFace==-1&&info.mUVCoords.mV[0]==-1);
context.scene.focusReady=true;context.scene.value->hud=false;context.scene.value->surfaceKnown=false;assert(window.nativePick(info));assert(info.mSTCoords.mV[0]==-1&&info.mUVCoords.mV[0]==-1&&info.mXYCoords.mX==-1&&info.mTangent.mV[0]==0);
info.mWantSurfaceInfo=false;context.scene.value->surfaceKnown=true;assert(window.nativePick(info));assert(info.mNormal.mV[2]==0&&info.mIntersection.mV[0]==0&&info.mSTCoords.mV[0]==-1);info.mWantSurfaceInfo=true;context.scene.pcode=LL_PCODE_LEGACY_GRASS;assert(window.nativePick(info));assert(info.mPickType==LLPickInfo::PICK_FLORA);context.scene.value->land=true;assert(window.nativePick(info));assert(info.mObjectFace==-1&&info.mPickType==LLPickInfo::PICK_LAND);
VSNativeSession::active()->ready=false;assert(!window.nativePick(info));assert(info.mPickType==LLPickInfo::PICK_INVALID&&info.mObjectFace==-1);
}
