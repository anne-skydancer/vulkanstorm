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
struct LLVector3{float mV[3]{};template<class T>LLVector3(const T&p):mV{p.v[0],p.v[1],p.v[2]}{};LLVector3(const float*p):mV{p[0],p[1],p[2]}{};LLVector3(float*p):LLVector3((const float*)p){};void setVec(float a,float b,float c){mV[0]=a;mV[1]=b;mV[2]=c;}float lengthSquared()const{return mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2];}void scaleVec(LLVector3 b){for(int n=0;n<3;n++)mV[n]*=b.mV[n];}LLVector3 scaledVec(LLVector3 b)const{b.scaleVec(*this);return b;}LLVector3()=default;LLVector3(float a,float b,float c):mV{a,b,c}{};void setVec(LLVector3 v){*this=v;}void clamp(float a,float b){for(float&v:mV)v=std::clamp(v,a,b);}void abs(){for(float&v:mV)v=std::abs(v);}float magVecSquared()const{return lengthSquared();}float magVec()const{return std::sqrt(lengthSquared());}float normVec(){return normalize();}LLVector3&operator*=(float s){for(float&v:mV)v*=s;return *this;}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n>0)for(float&v:mV)v/=n;return n;}void rotVec(LLQuaternion q){float a=mV[0],b=mV[1];mV[0]=a*std::cos(q.a)-b*std::sin(q.a);mV[1]=a*std::sin(q.a)+b*std::cos(q.a);}LLVector3&operator-=(LLVector3 b){for(int n=0;n<3;n++)mV[n]-=b.mV[n];return *this;}};
LLVector3 operator+(LLVector3 a,LLVector3 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 a,LLVector3 b){a-=b;return a;}LLVector3 operator*(float s,LLVector3 a){return {s*a.mV[0],s*a.mV[1],s*a.mV[2]};}float operator*(LLVector3 a,LLVector3 b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}LLVector3 operator/(LLVector3 a,float s){return (1/s)*a;}LLVector3 operator*(LLVector3 a,float s){return s*a;}using LLVector3d=LLVector3;
float dist_vec(LLVector3 a,LLVector3 b){auto v=a-b;return std::sqrt(v*v);}float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}LLVector3 lerp(LLVector3 a,LLVector3 b,float t){return (1-t)*a+t*b;}
struct LLMatrix4{LLVector3 axes[3];LLVector3 getFwdRow4()const{return axes[0];}LLVector3 getLeftRow4()const{return axes[1];}LLVector3 getUpRow4()const{return axes[2];}};
struct LLBBox{LLVector3 center,size;bool containsPointAgent(LLVector3 p)const{p-=center;for(int n=0;n<3;n++)if(std::abs(p.mV[n])>size.mV[n]/2)return false;return true;}};

struct LLVector4a{float mV[4]{};void load3(const float*p){std::copy_n(p,3,mV);}};
namespace glm{
struct vec3{float v[3]{};vec3()=default;vec3(float a,float b,float c):v{a,b,c}{};template<class T>vec3(const T&p):v{p.mV[0],p.mV[1],p.mV[2]}{}};
struct mat4{float m[16]{};mat4()=default;};mat4 make_mat4(float*p){mat4 a;std::copy_n(p,16,a.m);return a;}
struct mat3{mat4 value;mat3(mat4 m):value(m){}};
vec3 operator*(const mat3&m,vec3 p){return {m.value.m[0]*p.v[0]+m.value.m[4]*p.v[1]+m.value.m[8]*p.v[2],m.value.m[1]*p.v[0]+m.value.m[5]*p.v[1]+m.value.m[9]*p.v[2],m.value.m[2]*p.v[0]+m.value.m[6]*p.v[1]+m.value.m[10]*p.v[2]};}
vec3 normalize(vec3 p){float l=std::sqrt(p.v[0]*p.v[0]+p.v[1]*p.v[1]+p.v[2]*p.v[2]);for(float&v:p.v)v/=l;return p;}
mat4 transpose(mat4 a){mat4 b;for(int i=0;i<4;i++)for(int j=0;j<4;j++)b.m[i*4+j]=a.m[j*4+i];return b;}
mat4 inverse(mat4 m){const auto*a=m.m;const float A=a[0],B=a[4],C=a[8],D=a[1],E=a[5],F=a[9],G=a[2],H=a[6],I=a[10];float determinant=A*(E*I-F*H)-B*(D*I-F*G)+C*(D*H-E*G);mat4 r;r.m[0]=(E*I-F*H)/determinant;r.m[4]=(C*H-B*I)/determinant;r.m[8]=(B*F-C*E)/determinant;r.m[1]=(F*G-D*I)/determinant;r.m[5]=(A*I-C*G)/determinant;r.m[9]=(C*D-A*F)/determinant;r.m[2]=(D*H-E*G)/determinant;r.m[6]=(B*G-A*H)/determinant;r.m[10]=(A*E-B*D)/determinant;r.m[15]=1;for(int i=0;i<3;i++)r.m[12+i]=-(r.m[i]*a[12]+r.m[4+i]*a[13]+r.m[8+i]*a[14]);return r;}
const float*value_ptr(const vec3&p){return p.v;}
}
glm::vec3 mul_mat4_vec3(const glm::mat4&m,glm::vec3 p){auto v=glm::mat3(m)*p;for(int i=0;i<3;i++)v.v[i]+=m.m[12+i];return v;}
SPHERE
struct Joint{struct Transform{struct Matrix{float mMatrix[4][4]{};}matrix;const Matrix&getWorldMatrix(){return matrix;}}xform;unsigned updates=0;void updateWorldMatrix(){++updates;}Transform*getXform(){return &xform;}};
struct OriginalAvatar{int mNumCollisionVolumes=3;Joint*mCollisionVolumes;bool intersect(const LLVector4a&,const LLVector4a&,LLVector4a*,LLVector4a*);};
REFERENCE
struct VSWorldRenderer{struct SurfaceHit{std::array<float,3>point{},normal{};float fraction=1;};};
struct VSWorldAvatar{struct Impl{bool self=false,control=false,positionKnown=true;int mNumCollisionVolumes=3;Joint*mCollisionVolumes;};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::optional<VSWorldRenderer::SurfaceHit>collisionPick(const LLVector3&,const LLVector3&,bool)const;};
NATIVE
int main(){std::mt19937 rng(19);std::uniform_real_distribution<float>u(-1,1);for(int test=0;test<10000;test++){
Joint joints[3];for(int n=0;n<3;n++){auto*matrix=(float*)joints[n].xform.matrix.mMatrix;float angle=u(rng)*3,sx=std::abs(u(rng))*2+.1f,sy=std::abs(u(rng))*2+.1f,sz=std::abs(u(rng))*2+.1f;matrix[0]=std::cos(angle)*sx;matrix[1]=std::sin(angle)*sx;matrix[4]=-std::sin(angle)*sy;matrix[5]=std::cos(angle)*sy;matrix[10]=sz;matrix[12]=u(rng);matrix[13]=u(rng);matrix[14]=u(rng);matrix[15]=1;}
OriginalAvatar original{3,joints};VSWorldAvatar native;native.mImpl->mCollisionVolumes=joints;LLVector3 start{u(rng)*3,u(rng)*3,3},end{u(rng)*3,u(rng)*3,-3};LLVector4a a,b,p,n;a.load3(start.mV);b.load3(end.mV);bool expected=original.intersect(a,b,&p,&n);unsigned calls=0;for(auto&j:joints){calls+=j.updates;j.updates=0;}auto actual=native.collisionPick(start,end,true);unsigned nativeCalls=0;for(auto&j:joints)nativeCalls+=j.updates;assert(expected==bool(actual)&&calls==nativeCalls);if(actual)for(int k=0;k<3;k++){assert(p.mV[k]==actual->point[k]);assert(n.mV[k]==actual->normal[k]);}
native.mImpl->self=true;for(auto&j:joints)j.updates=0;assert(!native.collisionPick(start,end,false));for(auto&j:joints)assert(j.updates==0);native.mImpl->control=true;assert(!native.collisionPick(start,end,true));native.mImpl->control=false;native.mImpl->positionKnown=false;assert(!native.collisionPick(start,end,true));
}}
