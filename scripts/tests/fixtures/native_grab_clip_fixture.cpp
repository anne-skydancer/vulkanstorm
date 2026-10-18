#include <cassert>
#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <vector>
using F32=float;using F64=double;constexpr int VX=0,VY=1,VZ=2;constexpr float F_ALMOST_ZERO=0.0001f;template<class T>T llclamp(T v,T a,T b){return std::clamp(v,a,b);}struct LLVector3d;
struct LLVector3{float mV[3]{};LLVector3()=default;LLVector3(const LLVector3d&);};
struct LLVector3d{double mdV[3]{};LLVector3d()=default;LLVector3d(double x,double y,double z):mdV{x,y,z}{}LLVector3d(LLVector3 v):mdV{v.mV[0],v.mV[1],v.mV[2]}{}void setVec(const LLVector3d&v){*this=v;}void abs(){for(auto&v:mdV)v=std::abs(v);}LLVector3d operator-(LLVector3d b)const{return{mdV[0]-b.mdV[0],mdV[1]-b.mdV[1],mdV[2]-b.mdV[2]};}LLVector3d operator+(LLVector3d b)const{return{mdV[0]+b.mdV[0],mdV[1]+b.mdV[1],mdV[2]+b.mdV[2]};}LLVector3d operator*(double f)const{return{mdV[0]*f,mdV[1]*f,mdV[2]*f};}};LLVector3::LLVector3(const LLVector3d&v){for(int i=0;i<3;++i)mV[i]=float(v.mdV[i]);}
struct LLViewerRegion{LLVector3d origin;float width;float getWidth(){return width;}LLVector3 getPosRegionFromGlobal(LLVector3d p){return LLVector3(p-origin);}LLVector3d getPosGlobalFromRegion(LLVector3 p){return origin+LLVector3d(p);}bool contains(LLVector3d p){auto v=p-origin;return v.mdV[0]>=0&&v.mdV[1]>=0&&v.mdV[0]<width&&v.mdV[1]<width;}};
struct LLWorld{std::vector<LLViewerRegion> regions;static LLWorld*instance;static LLWorld*getInstance(){return instance;}bool positionRegionValidGlobal(LLVector3d p){return getRegionFromPosGlobal(p)!=nullptr;}LLViewerRegion*getRegionFromPosGlobal(LLVector3d p){for(auto&r:regions)if(r.contains(p))return&r;return nullptr;}float getRegionMaxHeight(){return 4096;}LLVector3d clipToVisibleRegions(const LLVector3d&,const LLVector3d&);};LLWorld*LLWorld::instance=nullptr;
GOLD
KERNEL
struct NativeRegions{LLWorld&world;bool contains(LLVector3d p)const{return world.positionRegionValidGlobal(p);}std::optional<LLViewerRegion>region(LLVector3d p)const{auto*r=world.getRegionFromPosGlobal(p);if(r)return*r;return{};}float maxHeight()const{return world.getRegionMaxHeight();}};
int main(){LLWorld world;LLWorld::instance=&world;world.regions={{{1000,2000,0},256},{{1256,2000,0},512},{{1000,2512,0},256}};std::mt19937 rng(814);auto f=[&]{return double(int(rng()%4000)-1000);};for(int i=0;i<20000;++i){LLVector3d a(1000+f(),2000+f(),f()),b(1000+f(),2000+f(),f());auto x=world.clipToVisibleRegions(a,b),y=vs_clip_regions(a,b,NativeRegions{world});for(int c=0;c<3;++c)assert(x.mdV[c]==y.mdV[c]||(std::isnan(x.mdV[c])&&std::isnan(y.mdV[c])));}}
