#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <array>
#include <glm/gtc/type_ptr.hpp>
#include "vsviewprojection.h"
using S32=int;using F32=float;using U32=unsigned;
int llceil(float a){return int(std::ceil(a));}
ORIGINAL
struct LLVector3:glm::vec3{using glm::vec3::vec3;LLVector3()=default;LLVector3(glm::vec3 v):glm::vec3(v){}void setVec(const float*p){x=p[0];y=p[1];z=p[2];}void normVec(){auto v=glm::normalize(*this);x=v.x;y=v.y;z=v.z;}};
struct LLCamera{
 float near=.1f,far=512;LLVector3 origin{3,7,2},at{1,0,0};std::array<LLVector3,8> corners;
 const LLVector3&getOrigin()const{return origin;}const LLVector3&getAtAxis()const{return at;}float getNear()const{return near;}float getFar()const{return far;}
 void setNear(float n){near=std::clamp(n,.1f,1023.9f);}void setFar(float f){far=std::clamp(f,.2f,100000.f);}void calcAgentFrustumPlanes(const LLVector3*p){std::copy(p,p+8,corners.begin());}
};
struct Rect{int mLeft=17,mBottom=31,w=1280,h=720;int getWidth()const{return w;}int getHeight()const{return h;}};
struct Window{Rect rect;Rect getWorldViewRectRaw(){return rect;}}window;Window*gViewerWindow=&window;
struct LLViewerCamera:LLCamera{float mZoomFactor=1,view=1.047f,aspect=1280.f/720.f;int mZoomSubregion=0;glm::mat4 model=glm::lookAt(glm::vec3(origin),glm::vec3(origin+at),glm::vec3(0,0,1));float getView()const{return view;}float getAspect()const{return aspect;}glm::mat4 getUIModelview()const{return model;}
 std::optional<LLCamera>selectionFrustum(S32,S32,S32,S32,F32,F32,F32)const;
};
FRUSTUM
METHOD
int main(){LLViewerCamera camera;for(int i=0;i<4096;++i){float zoom=1.f+(i%5)*.75f;int sub=i%25;glm::ivec4 viewport(17,31,1280,720);int x=i%700,y=i%400,w=1+i%400,h=1+i%300;float n=.1f+(i%8)*.11f,f=16.f+(i%127)*4.f;
 auto crop=glm::pickMatrix(glm::vec2(x+w/2.f,y+h/2.f),glm::vec2(float(w),float(h)),viewport);assert(vs_pick_projection(viewport,x,y,w,h)==crop);
 auto original=originalProjection(crop,zoom,sub,camera.view,camera.aspect,n,f);auto now=vs_view_projection(crop,zoom,sub,camera.view,camera.aspect,n,f);assert(original==now);
 camera.mZoomFactor=zoom;camera.mZoomSubregion=sub;auto result=camera.selectionFrustum(x,y,w,h,n,700.f,f);assert(result);LLCamera reference=camera;reference.setNear(n);reference.setFar(700.f);updateCameraFrustum(reference,viewport,camera.model,original,false,false,false);
 for(unsigned j=0;j<8;++j)assert(glm::distance(glm::vec3(reference.corners[j]),glm::vec3(result->corners[j]))<1e-4f);assert(camera.getNear()==.1f&&camera.getFar()==512.f);
 }
 assert(!camera.selectionFrustum(0,0,0,10,.1f,100,100));window.rect.w=0;assert(!camera.selectionFrustum(0,0,10,10,.1f,100,100));window.rect.w=1280;gViewerWindow=nullptr;assert(!camera.selectionFrustum(0,0,10,10,.1f,100,100));
}
