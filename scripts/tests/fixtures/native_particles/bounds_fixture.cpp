#include <cassert>
#include <cmath>
#include <random>
using F32=float;struct LLVector3{float v[3]{};LLVector3 operator-(LLVector3 b)const{return{{v[0]-b.v[0],v[1]-b.v[1],v[2]-b.v[2]}};}float length()const{return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}};template<class T>T llmax(T a,T b){return a>b?a:b;}template<class T>T llmax(T a,T b,T c){return llmax(llmax(a,b),c);}struct LLPartData{static constexpr int LL_PART_RIBBON_MASK=4;};template<class T>struct Ptr{T*p=nullptr;T*operator->()const{return p;}bool notNull()const{return p;}};struct Source{LLVector3 mPosAgent;};struct Part{struct Scale{float mV[2];}mScale;int mFlags=0;Part*mParent=nullptr;Ptr<Source>mPartSourcep;LLVector3 mPosAgent;};
GOLD
KERNEL
int main(){std::mt19937 rng(213);auto f=[&]{return float(int(rng()%2000)-1000)*.001f;};for(int i=0;i<10000;++i){Part p,parent;Source source;p.mScale={{std::fabs(f()),std::fabs(f())}};p.mFlags=i%2?4:0;p.mPosAgent={{f(),f(),f()}};parent.mPosAgent={{f(),f(),f()}};source.mPosAgent={{f(),f(),f()}};if(i%3==0)p.mParent=&parent;if(i%5==0)p.mPartSourcep.p=&source;assert(referenceScale(p)==vs_particle_scale(p));}}
