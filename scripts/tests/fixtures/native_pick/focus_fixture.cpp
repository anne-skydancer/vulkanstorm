#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <array>
using F32=float;using U32=unsigned;using S32=int;
constexpr int VX=0,VY=1,VZ=2;constexpr float F32_MAX=std::numeric_limits<float>::max();
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b){return std::min(a,b);}float llabs(float v){return std::abs(v);}
struct LLQuaternion{float a=0;};LLQuaternion operator~(LLQuaternion q){return {-q.a};}
struct LLVector3{float mV[3]{};void scaleVec(LLVector3 b){for(int n=0;n<3;n++)mV[n]*=b.mV[n];}LLVector3 scaledVec(LLVector3 b)const{b.scaleVec(*this);return b;}LLVector3()=default;LLVector3(float a,float b,float c):mV{a,b,c}{};void setVec(LLVector3 v){*this=v;}void clamp(float a,float b){for(float&v:mV)v=std::clamp(v,a,b);}void abs(){for(float&v:mV)v=std::abs(v);}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n>0)for(float&v:mV)v/=n;return n;}void rotVec(LLQuaternion q){float a=mV[0],b=mV[1];mV[0]=a*std::cos(q.a)-b*std::sin(q.a);mV[1]=a*std::sin(q.a)+b*std::cos(q.a);}LLVector3&operator-=(LLVector3 b){for(int n=0;n<3;n++)mV[n]-=b.mV[n];return *this;}};
LLVector3 operator+(LLVector3 a,LLVector3 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 a,LLVector3 b){a-=b;return a;}LLVector3 operator*(float s,LLVector3 a){return {s*a.mV[0],s*a.mV[1],s*a.mV[2]};}float operator*(LLVector3 a,LLVector3 b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}LLVector3 operator/(LLVector3 a,float s){return (1/s)*a;}LLVector3 operator*(LLVector3 a,float s){return s*a;}using LLVector3d=LLVector3;
float dist_vec(LLVector3 a,LLVector3 b){auto v=a-b;return std::sqrt(v*v);}float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}LLVector3 lerp(LLVector3 a,LLVector3 b,float t){return (1-t)*a+t*b;}
struct LLMatrix4{LLVector3 axes[3];LLVector3 getFwdRow4()const{return axes[0];}LLVector3 getLeftRow4()const{return axes[1];}LLVector3 getUpRow4()const{return axes[2];}};
struct LLBBox{LLVector3 center,size;bool containsPointAgent(LLVector3 p)const{p-=center;for(int n=0;n<3;n++)if(std::abs(p.mV[n])>size.mV[n]/2)return false;return true;}};
std::vector<std::string> trace;
struct LLVOAvatar{LLVector3 p;LLVector3 getCharacterPosition(){trace.push_back("character");return p;}};
struct LLViewerObject{LLMatrix4 m;LLQuaternion q;LLVector3 p,s;LLBBox b;LLVOAvatar av;bool a=false,r=false,anim=false,ctrl=false;
LLMatrix4 getRenderMatrix(){trace.push_back("matrix");return m;}LLQuaternion getRenderRotation(){trace.push_back("rotation");return q;}LLVector3 getRenderPosition(){trace.push_back("position");return p;}bool isAvatar(){trace.push_back("avatar");return a;}bool isRoot(){trace.push_back("root");return r;}bool isAnimatedObject(){trace.push_back("animated");return anim;}void*getControlAvatar(){trace.push_back("control");return ctrl?this:nullptr;}LLVOAvatar*asAvatar(){trace.push_back("asAvatar");return &av;}LLVector3 getScale(){trace.push_back("scale");return s;}LLBBox getBoundingBoxAgent(){trace.push_back("bounds");return b;}};
struct LLViewerCamera{LLVector3 origin,at;static LLViewerCamera*getInstance(){static LLViewerCamera c;return &c;}LLVector3 getOrigin(){return origin;}LLVector3 getAtAxis(){return at;}};
struct Agent{LLVector3 offset;LLVector3 getPosGlobalFromAgent(LLVector3 a){return a+offset;}LLVector3 getPosAgentFromGlobal(LLVector3 a){return a-offset;}}gAgent;
struct Window{void mousePointOnPlaneGlobal(LLVector3d&out,int x,int y,LLVector3 p,LLVector3 n){trace.push_back("plane");LLVector3 ray{float(x)/200, float(y)/200,-1};ray.normalize();auto from=gAgent.getPosGlobalFromAgent(LLViewerCamera::getInstance()->getOrigin());float d=ray*n;out=std::abs(d)>1e-6?from+(((p-from)*n)/d)*ray:p;}}window;Window*gViewerWindow=&window;
struct LLAgentCamera{LLVector3 mFocusTargetGlobal;float mCameraFOVZoomFactor=0;LLVector3 camera;LLVector3 getCameraPositionGlobal(){trace.push_back("virtualCamera");return camera;}LLVector3 calcFocusOffset(LLViewerObject*,LLVector3,int,int);};
REFERENCE
CURRENT
REFERENCE_BOUNDS
CURRENT_BOUNDS
int main(){std::mt19937 rng(914);std::uniform_real_distribution<float>n(-100,100),u(-1,1);LLAgentCamera camera;auto*vc=LLViewerCamera::getInstance();for(int i=0;i<30000;i++){
LLViewerObject o;o.q={u(rng)*3};o.p={n(rng),n(rng),n(rng)};o.s={std::abs(n(rng)),std::abs(n(rng)),std::abs(n(rng))};if(i%19==0)o.s.mV[i%3]=0;for(int k=0;k<3;k++){o.m.axes[k]={0,0,0};o.m.axes[k].mV[k]=o.s.mV[k];o.m.axes[k].rotVec(o.q);}o.b={o.p+LLVector3{u(rng),u(rng),u(rng)},o.s};o.av.p=o.p+LLVector3{2,3,4};o.a=i%4==0;o.r=i%3==0;o.anim=i%7==0;o.ctrl=i%5==0;vc->origin={n(rng),n(rng),n(rng)};vc->at={u(rng),u(rng),u(rng)};vc->at.normalize();gAgent.offset={n(rng),n(rng),n(rng)};camera.mFocusTargetGlobal={n(rng),n(rng),n(rng)};camera.camera=gAgent.getPosGlobalFromAgent(vc->origin);camera.mCameraFOVZoomFactor=std::abs(u(rng));LLVector3 hit{n(rng),n(rng),n(rng)};int x=i%1000,y=(i*7)%1000;
trace.clear();auto a=camera.calcFocusOffset(&o,hit,x,y);auto expected=trace;trace.clear();auto b=sharedFocusOffset(ReferenceFocusObject{&o},hit,x,y,[&]{return gAgent.getPosAgentFromGlobal(camera.mFocusTargetGlobal+(camera.getCameraPositionGlobal()-camera.mFocusTargetGlobal)/(1+camera.mCameraFOVZoomFactor));});assert(expected==trace);for(int k=0;k<3;k++)assert(a.mV[k]==b.mV[k]||(std::isnan(a.mV[k])&&std::isnan(b.mV[k])));
const auto map=[&](LLVector3 point){if(i%2)point.scaleVec(o.s);point.rotVec(o.q);return point+o.p;};
auto original=referenceBounds(LLVector3{-1,-2,-3},LLVector3{2,3,4},map),current=VSSurfaceCoordinates::volumeBounds(LLVector3{-1,-2,-3},LLVector3{2,3,4},map);
for(int v=0;v<8;v++)for(int k=0;k<3;k++)assert(original[v].mV[k]==current[v].mV[k]);
}}
