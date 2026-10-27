#include "vsparcelgeometry.h"
#include <optional>
#include <map>
#include <memory>
#include <functional>
#include <limits>
#include <cstdint>
#include <cassert>
#include <utility>
#include <array>
#include <set>
using U64=std::uint64_t;using U32=std::uint32_t;using S32=int;
struct LLHost{int value=0;bool operator!=(LLHost other)const{return value!=other.value;}bool operator<(LLHost other)const{return value<other.value;}};
struct ResetOwner {int calls=0;void reset(){++calls;}};
struct Account {bool empty=false;void setNull(){empty=true;}};
struct SceneBudget {Account account;U64 accountGeneration=4,particleRevision=1;};
struct LLHUDIcon {bool dead=false;void markDead(){dead=true;}static void cleanupDeadIcons(){}};
struct LLHUDObject {static void cleanupUnreferenced(){}};
struct SceneObject {LLHUDIcon* icon=nullptr;};
struct Resources {unsigned erased=0;void erase(const char*){++erased;}};
struct VSWorldScene {
 struct TerrainField {U64 revision=0,incarnation=0;U32 width=0,height=0;std::function<bool()>current;std::function<std::optional<float>(float,float)>sample;std::function<std::optional<float>(float,float)>referenceHeight;};
 struct Impl {LLHost host{1};bool retired=false;unsigned patchSize=16,width=256,regionHeight=512;U64 incarnation=7,terrainRevision=1;
 std::shared_ptr<ResetOwner> assets=std::make_shared<ResetOwner>();
 std::map<int,int> avatars,pendingMaterialOverrides,pending,pendingAnimations,visiblePackets;
 std::map<int,SceneObject> objects;std::set<int> particleObjects;std::map<int,int> objectIndex;
 unsigned accepted=3,rejected=2,unsupported=1,nextRequest=5;
 bool waterKnown=true;
 std::shared_ptr<int> water,materialAssets,terrainAssets;
 std::array<int,4> terrainTextures{};
 Resources resources;
 std::shared_ptr<SceneBudget> budgets=std::make_shared<SceneBudget>();
 unsigned neighbourChanges=0;
 void dirtyTerrainNeighbours(){++neighbourChanges;}
 struct Ground{std::array<float,1024>heights{};};std::map<U64,Ground>ground;
    HEIGHT_LOCAL

 };
 bool mRoot=true;
 void reset(bool preserveAssets=false);
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<LLHost,std::unique_ptr<VSWorldScene>>mNeighbours;
 std::optional<U64>terrainRevision(const LLHost&)const;
 std::optional<TerrainField>terrainField(const LLHost&)const;
};
TERRAIN_FIELD
RESET_TERRAIN

void fill(VSWorldScene::Impl& owner){auto& patch=owner.ground[0];for(int y=0;y<16;++y)for(int x=0;x<16;++x)patch.heights[y*16+x]=float(2*x+3*y);}
int main(){
 VSWorldScene scene;fill(*scene.mImpl);assert(scene.terrainRevision({1})==1 && !scene.terrainRevision({2}));
 auto field=scene.terrainField({1});assert(field && field->current() && field->width==256 && field->height==512 && field->incarnation==7);
 assert(field->sample(.25f,.75f)==2.75f && field->sample(1,1)==5.f);
 assert(field->referenceHeight(.25f,.75f)==2.75f && field->referenceHeight(100,100)==0.f);
 // Across the received 16m patch edge, retain the left vertices while the
 // neighboring patch still has LLSurface's initial zero values.
 assert(!field->sample(15.5f,.5f) && field->referenceHeight(15.5f,.5f)==15.f);
 assert(!field->referenceHeight(-1,0) && !field->referenceHeight(256,0));
 assert(!field->sample(100,100) && !field->sample(-1,0) && !field->sample(256,0) && !field->sample(0,512) && !field->sample(std::numeric_limits<float>::quiet_NaN(),0));
 ++scene.mImpl->terrainRevision;assert(!field->current() && !field->sample(1,1) && !field->referenceHeight(1,1));
 field=scene.terrainField({1});assert(field && field->revision==2 && field->sample(1,1)==5.f);
 scene.mImpl->ground[0].heights[17]=std::numeric_limits<float>::quiet_NaN();assert(!field->sample(.5f,.5f));fill(*scene.mImpl);
 auto neighbor=std::make_unique<VSWorldScene>();neighbor->mImpl->host={2};neighbor->mImpl->width=16;neighbor->mImpl->regionHeight=16;neighbor->mImpl->incarnation=9;fill(*neighbor->mImpl);scene.mNeighbours[{2}]=std::move(neighbor);
 auto adjacent=scene.terrainField({2});assert(adjacent && adjacent->incarnation==9 && adjacent->sample(2,3)==13.f);
 scene.mNeighbours.erase({2});assert(!adjacent->current() && !adjacent->sample(2,3) && !adjacent->referenceHeight(2,3));
 scene.mImpl->retired=true;assert(!field->current() && !field->sample(1,1) && !scene.terrainRevision({1}) && !scene.terrainField({1}));
 scene.mImpl=std::make_shared<VSWorldScene::Impl>();fill(*scene.mImpl);
 field=scene.terrainField({1});assert(field && field->current());
 const auto revision=field->revision;
 scene.reset(true);
 assert(!field->current() && !field->sample(1,1) && scene.terrainRevision({1})==revision+1);
 assert(scene.mImpl->assets->calls==0 && scene.mImpl->budgets->accountGeneration==4);
 auto initial=scene.terrainField({1});assert(initial && !initial->sample(0,0) && initial->referenceHeight(0,0)==0.f);
 scene.reset();
 assert(!initial->current() && scene.mImpl->assets->calls==1 && scene.mImpl->budgets->account.empty && scene.mImpl->budgets->accountGeneration==0);
 scene.mImpl=std::make_shared<VSWorldScene::Impl>();scene.mImpl->patchSize=0;assert(scene.terrainField({1}) && !scene.terrainField({1})->sample(0,0));
}
