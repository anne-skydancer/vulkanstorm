#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <vector>
using F32=float;using U32=unsigned;using S32=int;struct LLVector2{};
struct LLVector4a{float v[3]{};void setSub(const LLVector4a&a,const LLVector4a&b){for(int i=0;i<3;++i)v[i]=a.v[i]-b.v[i];}void setAdd(const LLVector4a&a,const LLVector4a&b){for(int i=0;i<3;++i)v[i]=a.v[i]+b.v[i];}void mul(float f){for(auto&x:v)x*=f;}};
float dot(LLVector4a a,LLVector4a b){return a.v[0]*b.v[0]+a.v[1]*b.v[1]+a.v[2]*b.v[2];}LLVector4a cross(LLVector4a a,LLVector4a b){return{{a.v[1]*b.v[2]-a.v[2]*b.v[1],a.v[2]*b.v[0]-a.v[0]*b.v[2],a.v[0]*b.v[1]-a.v[1]*b.v[0]}};}
bool LLTriangleRayIntersect(const LLVector4a&a,const LLVector4a&b,const LLVector4a&c,const LLVector4a&s,const LLVector4a&d,float&u,float&v,float&t){LLVector4a ab,ac,delta;ab.setSub(b,a);ac.setSub(c,a);auto p=cross(d,ac);float det=dot(ab,p);if(std::fabs(det)<1e-6)return false;delta.setSub(s,a);u=dot(delta,p)/det;if(u<0||u>1)return false;auto q=cross(delta,ab);v=dot(d,q)/det;if(v<0||v+u>1)return false;t=dot(ac,q)/det;return true;}
template<class T>struct LLStrider{T*p=nullptr;LLStrider&operator=(T*q){p=q;return*this;}T&operator*(){return*p;}void operator++(int){++p;}};
struct LLViewerPart{LLVector4a corners[4];};struct Group{std::vector<LLViewerPart*>mParticles;};
struct LLVOPartGroup{Group*mViewerPartGroupp;void getGeometry(const LLViewerPart&p,LLStrider<LLVector4a>&v){for(auto c:p.corners){*v=c;v++;}}bool lineSegmentIntersect(const LLVector4a&,const LLVector4a&,S32,bool,bool,bool,S32*,LLVector4a*,LLVector2*,LLVector4a*,LLVector4a*);};
GOLD
KERNEL
int main(){std::mt19937 rng(913);auto f=[&]{return float(int(rng()%2000)-1000)*.01f;};for(int n=0;n<20000;++n){std::vector<LLViewerPart>parts(1+rng()%20);Group group;for(auto&p:parts){float x=f(),y=f(),z=f(),size=std::fabs(f());p.corners[0]={{x,y,z}};p.corners[1]={{x+size,y,z}};p.corners[2]={{x,y+size,z}};p.corners[3]={{x+size,y+size,z}};group.mParticles.push_back(&p);}LLVOPartGroup reference{&group};LLVector4a start{{f(),f(),f()}},end{{f(),f(),f()}},a{},b{};int ia=-1,ib=-1;bool ga=reference.lineSegmentIntersect(start,end,-1,true,false,true,&ia,&a,nullptr,nullptr,nullptr);bool gb=vs_particle_intersect(group.mParticles,start,end,&ib,&b,[](const LLViewerPart&p,LLVector4a*&v){for(auto c:p.corners)*v++=c;});assert(ga==gb&&ia==ib);if(ga)for(int i=0;i<3;++i)assert(a.v[i]==b.v[i]);}}
