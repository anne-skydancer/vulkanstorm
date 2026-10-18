// Execute frozen original LLFace and production planar functions on real arithmetic.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <optional>
#include <random>
#include <iostream>
using F32=float;using S32=int;constexpr int VX=0,VY=1;
#define LL_WARNS(...) std::cerr
#define LL_WARNS_ONCE(...) std::cerr
#define LL_ENDL '\n'
struct LLQuaternion;
struct LLVector3 {float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};explicit LLVector3(const float*p):mV{p[0],p[1],p[2]}{};static const LLVector3 z_axis;bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}void scaleVec(LLVector3 b){for(int k=0;k<3;k++)mV[k]*=b.mV[k];}LLVector3&operator*=(float s){for(auto&v:mV)v*=s;return *this;}LLVector3&operator/=(float s){return *this*=1/s;}void rotVec(float,LLVector3);};
const LLVector3 LLVector3::z_axis{0,0,1};LLVector3 operator-(LLVector3 a,LLVector3 b){return {a.mV[0]-b.mV[0],a.mV[1]-b.mV[1],a.mV[2]-b.mV[2]};}LLVector3 operator%(LLVector3 a,LLVector3 b){return {a.mV[1]*b.mV[2]-a.mV[2]*b.mV[1],a.mV[2]*b.mV[0]-a.mV[0]*b.mV[2],a.mV[0]*b.mV[1]-a.mV[1]*b.mV[0]};}
struct LLVector2 {float mV[2]{};LLVector2()=default;LLVector2(float x,float y):mV{x,y}{};explicit LLVector2(LLVector3 a):mV{a.mV[0],a.mV[1]}{};LLVector2&operator-=(LLVector2 a){mV[0]-=a.mV[0];mV[1]-=a.mV[1];return *this;}float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]);}void normalize(){auto l=length();if(l)for(float&v:mV)v/=l;}};LLVector2 operator+(LLVector2 a,LLVector2 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1]};}
struct LLQuaternion {float x=0,y=0,z=0,w=1;LLQuaternion()=default;LLQuaternion(float a,LLVector3 v):x(v.mV[0]*std::sin(a/2)),y(v.mV[1]*std::sin(a/2)),z(v.mV[2]*std::sin(a/2)),w(std::cos(a/2)){};LLQuaternion(float a,float b,float c,float d):x(a),y(b),z(c),w(d){};LLQuaternion(LLVector3 a,LLVector3 b,LLVector3 c){ // basis conversion, shared primitive arithmetic only
float m[3][3]{{a.mV[0],a.mV[1],a.mV[2]},{b.mV[0],b.mV[1],b.mV[2]},{c.mV[0],c.mV[1],c.mV[2]}};float q[4]{};float tr=m[0][0]+m[1][1]+m[2][2];if(tr>0){auto s=std::sqrt(tr+1)*2;q[3]=s/4;q[0]=(m[1][2]-m[2][1])/s;q[1]=(m[2][0]-m[0][2])/s;q[2]=(m[0][1]-m[1][0])/s;}else{int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;auto s=std::sqrt(1+m[i][i]-m[j][j]-m[k][k])*2;q[i]=s/4;q[j]=(m[i][j]+m[j][i])/s;q[k]=(m[i][k]+m[k][i])/s;q[3]=(m[j][k]-m[k][j])/s;}x=q[0];y=q[1];z=q[2];w=q[3];}
bool isFinite()const{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::isfinite(w);}void getEulerAngles(float*a,float*b,float*c)const{*a=std::atan2(2*(w*x+y*z),1-2*(x*x+y*y));*b=std::asin(std::clamp(2*(w*y-z*x),-1.f,1.f));*c=std::atan2(2*(w*z+x*y),1-2*(y*y+z*z));}};
LLQuaternion operator~(LLQuaternion q){return {-q.x,-q.y,-q.z,q.w};}LLQuaternion operator*(LLQuaternion a,LLQuaternion b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}LLVector3 operator*(LLVector3 a,LLQuaternion q){auto r=q*LLQuaternion(a.mV[0],a.mV[1],a.mV[2],0)*~q;return {r.x,r.y,r.z};}void LLVector3::rotVec(float a,LLVector3 v){*this=*this*LLQuaternion(a,v);}
struct LLVector4a {float v[4]{};const float*getF32ptr()const{return v;}void setCross3(const LLVector4a&a,const LLVector4a&b){auto n=LLVector3(a.v)%LLVector3(b.v);for(int k=0;k<3;k++)v[k]=n.mV[k];}void mul(float s){for(float&n:v)n*=s;}};
struct LLMatrix4{LLQuaternion r;LLVector3 p;LLQuaternion quaternion()const{return r;}LLVector3 getTranslation()const{return p;}};
namespace LLRender{enum eTexIndex{DIFFUSE_MAP,NORMAL_MAP,SPECULAR_MAP};}
struct LLMaterial{int n=1,s=1;float nr=0,nx=1,ny=1,no=0,np=0,sr=0,sx=1,sy=1,so=0,sp=0;struct ID{int v;bool isNull()const{return !v;}};ID getNormalID()const{return {n};}ID getSpecularID()const{return {s};}float getNormalRotation()const{return nr;}float getNormalRepeatX()const{return nx;}float getNormalRepeatY()const{return ny;}float getNormalOffsetX()const{return no;}float getNormalOffsetY()const{return np;}float getSpecularRotation()const{return sr;}float getSpecularRepeatX()const{return sx;}float getSpecularRepeatY()const{return sy;}float getSpecularOffsetX()const{return so;}float getSpecularOffsetY()const{return sp;}};
struct LLTextureEntry{enum{TEX_GEN_PLANAR=2};int gen=2;float rot=0,mScaleS=1,mScaleT=1,mOffsetS=0,mOffsetT=0;LLMaterial*mat=nullptr;int getTexGen()const{return gen;}float getRotation()const{return rot;}LLMaterial*getMaterialParams()const{return mat;}};
struct LLVolumeFace{int mNumVertices=3;LLVector4a*mNormals,*mTangents,*mCenter;};
struct Volume{LLVolumeFace f;int getNumVolumeFaces()const{return 1;}const LLVolumeFace&getVolumeFace(int)const{return f;}};
struct Object{Volume*vol;Volume*getVolume()const{return vol;}};
void planarProjection(LLVector2&result,const LLVector4a&n,const LLVector4a&,const LLVector4a&p){ // independent primitive coordinate basis
result={.5f+p.v[0]*(1+std::abs(n.v[0]))+p.v[2]*.3f,.5f+p.v[1]*(1+std::abs(n.v[1]))+p.v[2]*.7f};}
namespace VSSurfaceCoordinates{using ::planarProjection;}
struct LLFace{Object*obj;LLMatrix4 matrix;LLTextureEntry*te;int mTEOffset=0;Object*getViewerObject()const{return obj;}const LLMatrix4&getWorldMatrix()const{return matrix;}const LLTextureEntry*getTextureEntry()const{return te;}void getPlanarProjectedParams(LLQuaternion*,LLVector3*,float*)const;bool calcAlignedPlanarTE(const LLFace*,LLVector2*,LLVector2*,float*,LLRender::eTexIndex)const;};
struct VSPlanarFaceFrame{LLQuaternion rotation;LLVector3 position;float scale=0;};
REFERENCE_FRAME
REFERENCE_ALIGN
CURRENT_FRAME
CURRENT_ALIGN
int main(){std::mt19937 rng(914);std::uniform_real_distribution<float>n(-5,5);for(int i=0;i<12000;i++){
 LLVector3 norm{n(rng),n(rng),n(rng)};auto normalize=[](LLVector3&v){float l=std::sqrt(v.mV[0]*v.mV[0]+v.mV[1]*v.mV[1]+v.mV[2]*v.mV[2]);v/=l;};normalize(norm);auto tang=norm%LLVector3{.731f,.413f,.291f};normalize(tang);
 LLVector4a ns{{norm.mV[0],norm.mV[1],norm.mV[2],0}},ts{{tang.mV[0],tang.mV[1],tang.mV[2],i%2?1.f:-1.f}},center{{n(rng),n(rng),n(rng),0}};
 Volume volume{{3,&ns,&ts,&center}};Object obj{&volume};LLMaterial material;material.n=i%5;material.s=i%7;material.nr=n(rng);material.nx=n(rng);material.ny=n(rng);material.no=n(rng);material.np=n(rng);material.sr=n(rng);material.sx=n(rng);material.sy=n(rng);material.so=n(rng);material.sp=n(rng);
 LLTextureEntry a,b;a.rot=n(rng);a.mScaleS=n(rng);a.mScaleT=n(rng);a.mOffsetS=n(rng);a.mOffsetT=n(rng);a.mat=&material;b=a;b.rot=n(rng);if(i%11==0)a.gen=0;if(i%13==0)b.gen=0;
 LLFace fa{&obj,{LLQuaternion(n(rng),norm),{n(rng),n(rng),n(rng)}},&a},fb{&obj,{LLQuaternion(n(rng),norm),{n(rng),n(rng),n(rng)}},&b};
 auto af=vs_planar_face_frame(volume.f,fa.matrix),bf=vs_planar_face_frame(volume.f,fb.matrix);assert(af&&bf);LLQuaternion rot;LLVector3 pos;float scale;fa.getPlanarProjectedParams(&rot,&pos,&scale);assert(rot.x==af->rotation.x&&rot.y==af->rotation.y&&rot.z==af->rotation.z&&rot.w==af->rotation.w&&scale==af->scale);for(int k=0;k<3;k++)assert(pos.mV[k]==af->position.mV[k]);
 LLVector2 ao,as,bo,bs;float ar=0,br=0;auto map=LLRender::eTexIndex(i%3);bool x=fa.calcAlignedPlanarTE(&fb,&ao,&as,&ar,map),y=vs_align_planar_texture(a,*af,b,*bf,&bo,&bs,&br,map);assert(x==y);if(x){for(int k=0;k<2;k++){assert(ao.mV[k]==bo.mV[k]);assert(as.mV[k]==bs.mV[k]);}assert(ar==br);}
 }
 LLVector4a normal4{{0,0,1,0}},t{{1,0,0,1}},c{};LLVolumeFace vf{3,&normal4,&t,&c};LLMatrix4 m;assert(vs_planar_face_frame(vf,m));vf.mNormals=nullptr;assert(!vs_planar_face_frame(vf,m));vf.mNormals=&normal4;vf.mNumVertices=0;assert(!vs_planar_face_frame(vf,m));vf.mNumVertices=3;vf.mTangents=nullptr;assert(!vs_planar_face_frame(vf,m));vf.mTangents=&t;vf.mCenter=nullptr;assert(!vs_planar_face_frame(vf,m));vf.mCenter=&c;t={};assert(!vs_planar_face_frame(vf,m));
}
