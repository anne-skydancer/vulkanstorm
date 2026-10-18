#include <cassert>
#include <memory>
#include <vector>
using F32=float;using S32=int;using U32=unsigned;
struct LLDrawable{static U32 frame;static U32 getCurrentFrame(){return frame;}bool isDead(){return false;}struct LLSpatialGroup*getSpatialGroup();static constexpr int REBUILD_ALL=1;};U32 LLDrawable::frame=0;
struct LLSpatialGroup{bool visible=true;bool isVisible(){return visible;}}spatial;LLSpatialGroup*LLDrawable::getSpatialGroup(){return&spatial;}
struct LLViewerObject{LLDrawable*mDrawable=nullptr;bool isDead(){return false;}};
struct VSParticleGroupState{bool current=true;U32 visibleFrame=0;};bool vs_native_particle_group_current(const std::shared_ptr<VSParticleGroupState>&v){return v&&v->current;}
VISIBLE
struct Group{bool mNative=false;std::shared_ptr<VSParticleGroupState>mNativeGroup;LLViewerObject*mVOPartGroupp=nullptr;U32 mID=0;float mSkippedTime=0;std::vector<float>steps;void updateParticles(float f){steps.push_back(f+mSkippedTime);}int getCount(){return 1;}};
struct Pipeline{void markRebuild(LLDrawable*,int){}}gPipeline;
struct Sim{std::vector<Group*>mViewerPartGroups;void loop(float dt){S32 count,i;
LOOP
}};
int main(){for(unsigned id=0;id<40;++id){Group a,b;LLDrawable drawable;LLViewerObject object{&drawable};a.mVOPartGroupp=&object;b.mNative=true;b.mNativeGroup=std::make_shared<VSParticleGroupState>();a.mID=b.mID=id;Sim gl{{&a}},native{{&b}};for(unsigned frame=1;frame<1000;++frame){LLDrawable::frame=frame;spatial.visible=frame%19<7;if(spatial.visible)b.mNativeGroup->visibleFrame=frame;gl.loop(.016f);native.loop(.016f);assert(a.mSkippedTime==b.mSkippedTime&&a.steps==b.steps);}b.mNativeGroup->current=false;assert(!vs_native_particle_group_visible(b.mNativeGroup));} }
