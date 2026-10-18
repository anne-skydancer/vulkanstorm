#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cassert>
#include <cmath>
#include <cstring>
#include <random>
using U32=unsigned;using F32=float;using GLint=int;
struct LLVector3{float mV[3]{};void setVec(const float*p){for(int i=0;i<3;++i)mV[i]=p[i];}void normVec(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);for(auto&v:mV)v/=n;}LLVector3 operator-(LLVector3 b)const{return{{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]}};}LLVector3 operator+(LLVector3 b)const{return{{mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]}};}LLVector3 operator*(float f)const{return{{mV[0]*f,mV[1]*f,mV[2]*f}};}};
struct LLCamera{LLVector3 origin{{0,0,0}},at{{1,0,0}},frustum[8];float farClip=128;int updates=0;LLVector3 getOrigin(){return origin;}LLVector3 getAtAxis(){return at;}float getFar(){return farClip;}void calcAgentFrustumPlanes(LLVector3*v){for(int i=0;i<8;++i)frustum[i]=v[i];++updates;}};
struct Rect{int mLeft=11,mBottom=23,width=800,height=600;int getWidth()const{return width;}int getHeight()const{return height;}};struct Window{bool native=true;Rect rect;bool isNativeVulkan(){return native;}Rect getWorldViewRectRaw(){return rect;}}window;auto*gViewerWindow=&window;
glm::mat4 model(1),projection(1);int gGLViewport[4]{11,23,800,600};int glReads=0;glm::mat4 get_current_modelview(){++glReads;return model;}glm::mat4 get_current_projection(){++glReads;return projection;}
struct LLViewerCamera:LLCamera{void updateNativeFrustum();glm::mat4 getUIModelview(){return model;}glm::mat4 getUIProjection(){return projection;}};
GOLD
KERNEL
NATIVE
int main(){std::mt19937 rng(1300);auto random=[&]{return float(int(rng()%2000)-1000)*.01f;};for(int i=0;i<1000;++i){LLViewerCamera a,b;glm::vec3 eye(random(),random(),random()),direction(random(),random(),random());direction=glm::normalize(direction);glm::vec3 up=std::abs(direction.z)<.9?glm::vec3(0,0,1):glm::vec3(0,1,0);model=glm::lookAt(eye,eye+direction,up);projection=glm::perspective(.5f+float(rng()%100)*.01f,4.f/3,.1f,128.f);a.origin.setVec(glm::value_ptr(eye));a.at.setVec(glm::value_ptr(direction));b.origin=a.origin;b.at=a.at;for(int flags=0;flags<8;++flags){referenceFrustum(a,flags&1,flags&2,flags&4);updateCameraFrustum(b,glm::make_vec4(gGLViewport),model,projection,flags&1,flags&2,flags&4);assert(std::memcmp(a.frustum,b.frustum,sizeof a.frustum)==0);}referenceFrustum(a,false,false,false);int reads=glReads;b.updateNativeFrustum();assert(glReads==reads&&std::memcmp(a.frustum,b.frustum,sizeof a.frustum)==0);int updates=b.updates;window.rect.width=0;b.updateNativeFrustum();assert(b.updates==updates);window.rect.width=800;window.native=false;b.updateNativeFrustum();assert(b.updates==updates);window.native=true;}}
