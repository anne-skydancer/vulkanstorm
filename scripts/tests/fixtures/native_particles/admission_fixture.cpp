#include <cassert>
#include <memory>
#include <sstream>
#include <vector>
#define LL_WARNS() std::cerr
#define LL_ENDL std::endl
#include <iostream>
using S32=int;constexpr int MAX_PART_COUNT=10;
bool native=false,admit=true,sourceAdmit=true;int releases=0,created=0,destroyed=0;
#define llassert assert
template<class T>struct LLPointer:std::shared_ptr<T>{using std::shared_ptr<T>::shared_ptr;LLPointer()=default;LLPointer(std::shared_ptr<T>v):std::shared_ptr<T>(v){}bool notNull()const{return bool(*this);}};
struct LLViewerPart;
struct LLViewerPartSource{LLViewerPart*mLastPart=nullptr;bool dead=false;int starts=0;void setDead(){dead=true;}void setStart(){++starts;}};
struct LLViewerPart{LLPointer<LLViewerPartSource>mPartSourcep;LLViewerPart*mParent=nullptr,*mChild=nullptr;LLViewerPart(){++created;}~LLViewerPart();};
bool vs_native_particle_peer(){return native;}bool vs_native_particle_admit(LLViewerPart*){return admit;}bool vs_native_particle_source(LLPointer<LLViewerPartSource>){return sourceAdmit;}void vs_native_particle_release(LLViewerPart*){++releases;}
struct LLViewerPartSim{static int sParticleCount,sParticleCount2;std::vector<LLViewerPart*>parts;std::vector<LLPointer<LLViewerPartSource>>mViewerPartSources;void put(LLViewerPart*p){parts.push_back(p);}void addPart(LLViewerPart*);void addPartSource(LLPointer<LLViewerPartSource>);};int LLViewerPartSim::sParticleCount=0;int LLViewerPartSim::sParticleCount2=0;
METHODS
int main(){LLViewerPartSim sim;native=true;admit=false;sim.addPart(new LLViewerPart);assert(sim.parts.empty()&&releases==1);admit=true;sim.addPart(new LLViewerPart);assert(sim.parts.size()==1&&releases==1);LLViewerPartSim::sParticleCount=MAX_PART_COUNT;sim.addPart(new LLViewerPart);assert(sim.parts.size()==1&&releases==2);native=false;admit=false;LLViewerPartSim::sParticleCount=0;sim.addPart(new LLViewerPart);assert(sim.parts.size()==2);for(auto*p:sim.parts)delete p;assert(releases==4);
 native=true;sourceAdmit=false;auto source=std::make_shared<LLViewerPartSource>();sim.addPartSource(source);assert(source->dead&&source->starts==0&&sim.mViewerPartSources.empty());sourceAdmit=true;source=std::make_shared<LLViewerPartSource>();sim.addPartSource(source);assert(!source->dead&&source->starts==1&&sim.mViewerPartSources.size()==1);native=false;sourceAdmit=false;source=std::make_shared<LLViewerPartSource>();sim.addPartSource(source);assert(!source->dead&&source->starts==1&&sim.mViewerPartSources.size()==2);sim.addPartSource({});assert(sim.mViewerPartSources.size()==2);
}
