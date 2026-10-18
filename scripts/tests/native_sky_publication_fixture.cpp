#include <cassert>
#include <array>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>
using U32=unsigned;using U16=unsigned short;using S32=int;using F32=float;
constexpr float F_PI=3.14159265358979323846f,F_TWO_PI=2*F_PI;
#define llassert(x) assert(x)
struct LLVector3{float mV[3];LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}};
struct LLVector2{float mV[2];LLVector2(float x=0,float y=0):mV{x,y}{}};
template<class T>T llclamp(T x,T lo,T hi){return std::clamp(x,lo,hi);}
struct Settings{unsigned detail=8;unsigned getU32(const char*){return detail;}}gSavedSettings;
struct Sky{float radius=15000;float getDomeRadius(){return radius;}};
struct LLEnvironment{std::shared_ptr<Sky>sky;static LLEnvironment owner;static bool instanceExists(){return true;}static auto&instance(){return owner;}auto getCurrentSky(){return sky;}};
LLEnvironment LLEnvironment::owner;
SHARED_KERNEL
struct VSWorldRenderer{
 struct Vertex{float x,y,z,nx,ny,nz,u,v;};using Mesh=std::shared_ptr<int>;bool refuse=false;int uploads=0;
 bool publishMesh(Mesh&mesh,const std::vector<Vertex>&,const std::vector<unsigned>&){++uploads;if(refuse)return false;mesh=std::make_shared<int>(uploads);return true;}
 Mesh upload(const std::vector<Vertex>&,const std::vector<unsigned>&){return std::make_shared<int>(-1);}
};
struct Budget{bool refuse=false;size_t requested=0;std::shared_ptr<int>reserve(size_t bytes){requested=bytes;return refuse?nullptr:std::make_shared<int>(1);}};
struct Budgets{Budget terrainBytes;};
struct VSWorldScene{struct Impl{bool retired=false;VSWorldRenderer renderer;std::shared_ptr<Budgets>budgets=std::make_shared<Budgets>();VSWorldRenderer::Mesh sky;unsigned skyDetail=0;float skyRadius=0;
 void build(){BUILD}
};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool skyCurrent()const;};
CURRENT
int main(){VSWorldScene scene;auto&p=*scene.mImpl;p.build();assert(p.sky && *p.sky==-1 && !scene.skyCurrent());
LLEnvironment::owner.sky=std::make_shared<Sky>();p.budgets->terrainBytes.refuse=true;p.build();assert(!p.sky && !p.skyDetail && !scene.skyCurrent() && !p.renderer.uploads);
p.budgets->terrainBytes.refuse=false;p.renderer.refuse=true;p.build();assert(!p.sky && !scene.skyCurrent());
p.renderer.refuse=false;p.build();assert(p.sky && *p.sky>0 && p.skyDetail==8 && scene.skyCurrent());auto old=p.sky;
LLEnvironment::owner.sky->radius=17000;p.renderer.refuse=true;p.build();assert(p.sky==old && p.skyRadius==15000 && !scene.skyCurrent());
p.renderer.refuse=false;p.build();assert(p.sky!=old && p.skyRadius==17000 && scene.skyCurrent());old=p.sky;
gSavedSettings.detail=16;p.budgets->terrainBytes.refuse=true;p.build();assert(p.sky==old && p.skyDetail==8 && !scene.skyCurrent());
p.budgets->terrainBytes.refuse=false;p.build();assert(p.sky!=old && p.skyDetail==16 && scene.skyCurrent());
p.retired=true;assert(!scene.skyCurrent());p.retired=false;
LLEnvironment::owner.sky.reset();p.build();assert(p.sky && *p.sky==-1 && !p.skyDetail && !scene.skyCurrent());}
