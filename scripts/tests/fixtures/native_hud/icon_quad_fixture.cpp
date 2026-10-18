#include <algorithm>
#include <cassert>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <vector>
#include <cstdint>
using F32=float;using U8=unsigned char;using U64=unsigned long long;
constexpr int VX=0,VY=1,VZ=2;constexpr float F_PI=3.14159265358979323846f,F_PI_BY_TWO=F_PI*.5f;
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b,T c){return std::min({a,b,c});}
struct LLUUID{int id=0;static LLUUID null;bool operator==(const LLUUID&)const=default;};LLUUID LLUUID::null{};
struct LLQuaternion{float a=0;};LLQuaternion operator~(LLQuaternion q){return {-q.a};}
struct LLVector3{float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};void abs(){for(float&v:mV)v=std::abs(v);}float normVec(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n)for(float&v:mV)v/=n;return n;}bool operator==(const LLVector3&)const=default;};
LLVector3 operator+(LLVector3 a,LLVector3 b){return{a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 a,LLVector3 b){return{a.mV[0]-b.mV[0],a.mV[1]-b.mV[1],a.mV[2]-b.mV[2]};}LLVector3 operator*(LLVector3 a,float b){return{a.mV[0]*b,a.mV[1]*b,a.mV[2]*b};}LLVector3 operator*(float b,LLVector3 a){return a*b;}LLVector3&operator+=(LLVector3&a,LLVector3 b){return a=a+b;}LLVector3 operator*(LLVector3 p,LLQuaternion q){return{p.mV[0]*std::cos(q.a)-p.mV[1]*std::sin(q.a),p.mV[0]*std::sin(q.a)+p.mV[1]*std::cos(q.a),p.mV[2]};}using LLVector3d=LLVector3;
float dist_vec(LLVector3 a,LLVector3 b){return(a-b).normVec();}float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}
struct Timer{float time=0;float getElapsedTimeF32()const{return time;}};
namespace VSUIRenderer{struct Texture{unsigned w=32,h=64;};using Image=std::shared_ptr<const Texture>;std::array<unsigned,2>imageExtent(const Image&i){assert(i);return{i->w,i->h};}}
struct LegacyImage{unsigned w=32,h=64;int getFullWidth(){return w;}int getFullHeight(){return h;}};
struct Drawable{float radius=1;float getRadius(){return radius;}};
struct Object{LLUUID id;LLVector3 p,s;LLQuaternion q;Drawable drawable;Drawable*mDrawable=&drawable;LLVector3 getRenderPosition(){return p;}LLQuaternion getRenderRotation(){return q;}LLVector3 getScale(){return s;}LLUUID getID(){return id;}};
struct Ptr{Object*p=nullptr;bool isNull()const{return !p;}operator bool()const{return p;}Object*operator->()const{return p;}};
struct ImagePtr{LegacyImage*p=nullptr;bool isNull()const{return !p;}LegacyImage*operator->()const{return p;}};
struct LLViewerCamera{LLVector3 origin,up{.2f,.3f,.93f};static LLViewerCamera*getInstance(){static LLViewerCamera c;return &c;}LLVector3 getOrigin()const{return origin;}LLVector3 getUpAxis()const{return up;}void getPixelVectors(LLVector3 p,LLVector3&y,LLVector3&x){float d=dist_vec(p,origin);x={0,d*.001f,0};y={0,0,d*.001f};}};
struct Window{int height=1080;int getWindowHeightScaled(){return height;}}window;Window*gViewerWindow=&window;
constexpr F32 ANIM_TIME=.4f,DIST_START_FADE=15.f,DIST_END_FADE=30.f,MAX_VISIBLE_TIME=15.f,FADE_OUT_TIME=1.f;
BOUNCE
class LLHUDIcon{public:struct NativeSource{LLUUID id;LLVector3 position,scale;LLVector3d globalPosition;LLQuaternion rotation;F32 radius=0;};using NativeSourceQuery=std::function<std::optional<NativeSource>()>;NativeSourceQuery mNativeSource;std::function<VSUIRenderer::Image()>mNativeImageQuery;VSUIRenderer::Image mNativeImage;bool mDead=false,mHidden=false;float mDistance=0,mScale=.03f;Timer mAnimTimer,mLifeTimer;Ptr mSourceObject;ImagePtr mImagep;LLVector3 global;void markDead(){mDead=true;}void setPositionGlobal(LLVector3 p){global=p;}bool nativeQuad(std::array<LLVector3,4>&,F32&);bool referenceQuad(std::array<LLVector3,4>&,F32&);LLUUID getSourceID()const;};
REFERENCE
CURRENT
IDENTITY
int main(){std::mt19937 rng(172);std::uniform_real_distribution<float>n(-50,50),positive(.01f,8.f),time(0,17);for(unsigned i=0;i<20000;++i){Object o;o.id={9};o.p={n(rng),n(rng),n(rng)};o.s={positive(rng),positive(rng),positive(rng)};o.q.a=n(rng);o.drawable.radius=positive(rng);LegacyImage image;image.w=(i%5+1)*13;image.h=(i%7+1)*11;LLHUDIcon original,native;original.mSourceObject.p=&o;original.mImagep.p=&image;original.mLifeTimer.time=native.mLifeTimer.time=time(rng);original.mAnimTimer.time=native.mAnimTimer.time=time(rng)*.1f;original.mScale=native.mScale=positive(rng)*.01f;original.mHidden=native.mHidden=i%79==0;auto pixels=std::make_shared<VSUIRenderer::Texture>();pixels->w=image.w;pixels->h=image.h;native.mNativeImage=pixels;bool current=true;native.mNativeSource=[&]()->std::optional<LLHUDIcon::NativeSource>{if(!current)return{};return LLHUDIcon::NativeSource{o.id,o.p,o.s,o.p,o.q,o.drawable.radius};};std::array<LLVector3,4>a,b;float aa=-1,ba=-1;auto expected=original.referenceQuad(a,aa);auto actual=native.nativeQuad(b,ba);assert(expected==actual);if(expected){for(unsigned v=0;v<4;++v)for(unsigned k=0;k<3;++k)assert(a[v].mV[k]==b[v].mV[k]);assert(aa==ba&&original.mDistance==native.mDistance);assert(native.getSourceID()==o.id);}current=false;assert(native.getSourceID()==LLUUID::null);assert(!native.nativeQuad(b,ba));}
 LLHUDIcon pending;auto pixels=std::make_shared<VSUIRenderer::Texture>();pending.mNativeSource=[](){return LLHUDIcon::NativeSource{{9},{5,0,0},{1,1,1},{5,0,0},{},1};};bool admitted=false;int calls=0;pending.mNativeImageQuery=[&]()->VSUIRenderer::Image{++calls;return admitted?pixels:nullptr;};std::array<LLVector3,4>quad;float alpha;assert(!pending.nativeQuad(quad,alpha)&&!pending.mDead&&calls==1);admitted=true;assert(pending.nativeQuad(quad,alpha)&&calls==2);pending.mNativeImage.reset();pending.mLifeTimer.time=16;assert(!pending.nativeQuad(quad,alpha)&&pending.mDead&&calls==2);
}
