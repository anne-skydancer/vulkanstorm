"""Actual wearable cache, COF completion and avatar-preview ownership regressions."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_WEARABLE_TEST_ROOT',Path(__file__).resolve().parents[2]))
OVERLAY=Path(os.environ['VS_WEARABLE_PROPOSED']) if 'VS_WEARABLE_PROPOSED' in os.environ else None
def source(p):
 q=OVERLAY/p if OVERLAY else ROOT/p
 return (q if q.exists() else ROOT/p).read_text()
class NativeWearableEditorTests(unittest.TestCase):
 def test_actual_saved_copy_cache_and_replacement_reject_retired_avatar(self):
  cache=source('indra/newview/llwearablelist.cpp');service=source('indra/newview/vsnativeappearance.cpp')
  run(self,r"""
#include <cassert>
#include <memory>
#include <map>
#include <vector>
using U32=unsigned;using LLUUID=int;
struct LLViewerWearable {int type=0,item=0,asset=0;bool current=true;unsigned getType()const{return type;}int getItemID()const{return item;}int getAssetID()const{return asset;}bool currentNativeOwner()const{return current;}};
struct LLWearableList {
 std::map<int,LLViewerWearable*>mList;std::map<int,std::shared_ptr<LLViewerWearable>>mNativeOwners;
 static LLWearableList&instance(){static LLWearableList x;return x;}
 std::shared_ptr<LLViewerWearable>retainNativeWearable(const LLViewerWearable*wearable)const CACHE
 void retireNativeAccount()RETIRE
};
std::vector<int> worn={11};std::vector<int>vs_avatar_wearable_items(unsigned){return worn;}
struct Entry{std::shared_ptr<LLViewerWearable>wearable;int asset=0;bool requested=false;};
struct Impl{bool active=true,dirty=false;std::map<int,Entry>entries;bool current()const{return active;}};
struct VSNativeAppearance {std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool replaceWearable(unsigned type,U32 index,LLViewerWearable*wearable)REPLACE std::shared_ptr<LLViewerWearable>retainWearable(const LLViewerWearable*wearable)const RETAIN};
int main(){
 auto& cache=LLWearableList::instance();VSNativeAppearance owner;auto old=std::make_shared<LLViewerWearable>(LLViewerWearable{3,11,1});owner.mImpl->entries[11]={old,1,false};
 assert(owner.retainWearable(old.get())==old);auto copy=std::make_shared<LLViewerWearable>(LLViewerWearable{3,11,2});cache.mNativeOwners[2]=copy;cache.mList[2]=copy.get();
 assert(!owner.replaceWearable(2,0,copy.get())&&!owner.replaceWearable(3,1,copy.get()));copy->item=12;assert(!owner.replaceWearable(3,0,copy.get()));copy->item=11;
 copy->current=false;assert(!owner.replaceWearable(3,0,copy.get())&&!owner.retainWearable(copy.get())&&!cache.retainNativeWearable(copy.get()));copy->current=true;
 auto impostor=*copy;assert(!owner.replaceWearable(3,0,&impostor));owner.mImpl->active=false;assert(!owner.replaceWearable(3,0,copy.get()));owner.mImpl->active=true;
 assert(owner.replaceWearable(3,0,copy.get()));const auto&e=owner.mImpl->entries.at(11);assert(e.wearable==copy&&e.asset==2&&e.requested&&owner.mImpl->dirty);
 auto other=std::make_shared<LLViewerWearable>(LLViewerWearable{3,11,2});cache.mList[2]=other.get();cache.mList[9]=other.get();cache.retireNativeAccount();assert(cache.mNativeOwners.empty()&&cache.mList[2]==other.get()&&cache.mList[9]==other.get()&&owner.retainWearable(copy.get())==copy);
 copy->current=false;assert(!owner.retainWearable(copy.get()));
}
""".replace('CACHE',body(cache,'LLWearableList::retainNativeWearable(')).replace('RETIRE',body(cache,'void LLWearableList::retireNativeAccount(')).replace('REPLACE',body(service,'bool VSNativeAppearance::replaceWearable(')).replace('RETAIN',body(service,'VSNativeAppearance::retainWearable(')))
 def test_actual_ready_queue_waits_and_handles_reentrant_account_and_avatar_retirement(self):
  cpp=source('indra/newview/vsnativeappearance.cpp');ready=body(body(cpp,'void VSNativeAppearance::tick()'),'if (startupReady())')
  when=body(cpp,'void VSNativeAppearance::whenReady(')
  run(self,r"""
#include <cassert>
#include <vector>
#include <map>
#include <memory>
#include <functional>
using uuid_vec_t=std::vector<int>;
struct Impl{bool active=true;std::vector<std::function<void()>>afterReady;std::map<int,int>applied;bool current(){return active;}};
struct Agent{std::vector<int>edited;std::function<void()>action=[]{};void editWearableIfRequested(int id){edited.push_back(id);action();}}gAgentWearables;
struct VSNativeAppearance{std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool ready=false;int generation=1;bool startupReady(){return ready;}void whenReady(std::function<void()>callback)WHEN void tick(){auto&state=*mImpl;int original=generation;auto frameCurrent=[&]{return state.current()&&generation==original;};if(startupReady())READY}};
int main(){VSNativeAppearance a;int called=0;a.whenReady([&]{++called;});a.tick();assert(!called&&a.mImpl->afterReady.size()==1);a.ready=true;a.tick();assert(called==1&&a.mImpl->afterReady.empty());a.tick();assert(called==1);
 a.whenReady([&]{++called;a.whenReady([&]{called+=10;});});a.tick();assert(called==2&&a.mImpl->afterReady.size()==1);a.tick();assert(called==12);
 a.whenReady([&]{++called;a.mImpl->active=false;});a.whenReady([&]{called+=100;});a.tick();assert(called==13);a.whenReady([&]{called+=1000;});assert(a.mImpl->afterReady.empty());
 a.mImpl->active=true;a.whenReady([&]{++called;++a.generation;});a.whenReady([&]{called+=100;});a.tick();assert(called==14);a.mImpl->applied={{1,9},{2,10}};gAgentWearables.action=[&]{a.mImpl->applied.clear();};a.tick();assert((gAgentWearables.edited==std::vector<int>{1,2}));
 gAgentWearables.edited.clear();a.mImpl->applied={{1,9},{2,10}};gAgentWearables.action=[&]{++a.generation;};a.tick();assert((gAgentWearables.edited==std::vector<int>{1}));}
""".replace('WHEN',when).replace('READY',ready))
 def test_actual_weak_view_callback_preserves_void_and_return_semantics(self):
  cpp=source('indra/newview/vsappearanceui.h');template=cpp[cpp.index('template<class View, class Callback>'):]
  run(self,r"""
#include <cassert>
#include <memory>
#include <type_traits>
#include <functional>
#include <utility>
int generation=1;auto vs_native_im_guard(){return[g=generation]{return g==generation;};}
struct Handle{std::weak_ptr<int>p;int*get()const{auto x=p.lock();return x.get();}};
struct View{std::shared_ptr<int>p=std::make_shared<int>();Handle getHandle(){return{p};}};
TEMPLATE
int main(){View v;int calls=0;auto f=vs_appearance_ui_callback(&v,[&](int n){calls+=n;return 7;});auto g=vs_appearance_ui_callback(&v,[&]{++calls;});assert(f(2)==7&&calls==2);g();assert(calls==3);++generation;assert(f(8)==0);g();assert(calls==3);auto h=vs_appearance_ui_callback(&v,[&]{++calls;return true;});v.p.reset();assert(!h()&&calls==3);}
""".replace('TEMPLATE',template))
 def test_actual_bom_mesh_visibility_matches_original_root_and_child_scan(self):
  scene=source('indra/newview/vsworldscene.cpp');begin=scene.index('    std::map<U32, LLUUID> bakedAttachmentRoots;');end=scene.index('    for (auto& [id,avatar]:mImpl->avatars)',begin);collector=scene[begin:end]
  avatar=source('indra/newview/vsworldavatar.cpp');hidden=body(avatar,'const auto hiddenByBake = [&]')
  reference=body(source('indra/newview/llvoavatar.cpp'),'void LLVOAvatar::updateMeshVisibility()')
  run(self,r"""
#include <cassert>
#include <map>
#include <memory>
#include <vector>
#include <cstring>
using S32=int;using U32=unsigned;using LLUUID=int;
enum{BAKED_HEAD,BAKED_UPPER,BAKED_LOWER,BAKED_EYES,BAKED_SKIRT,BAKED_HAIR,BAKED_LEFT_ARM,BAKED_LEFT_LEG,BAKED_AUX1,BAKED_AUX2,BAKED_AUX3,BAKED_NUM_INDICES};
enum{MESH_ID_HAIR,MESH_ID_HEAD,MESH_ID_SKIRT,MESH_ID_UPPER_BODY,MESH_ID_LOWER_BODY,MESH_ID_EYEBALL_LEFT,MESH_ID_EYEBALL_RIGHT,MESH_ID_EYELASH,OTHER};
constexpr int IMG_USE_BAKED_HEAD=100,IMG_USE_BAKED_UPPER=101,IMG_USE_BAKED_LOWER=102,IMG_USE_BAKED_EYES=103,IMG_USE_BAKED_SKIRT=104,IMG_USE_BAKED_HAIR=105,IMG_USE_BAKED_LEFTARM=106,IMG_USE_BAKED_LEFTLEG=107,IMG_USE_BAKED_AUX1=108,IMG_USE_BAKED_AUX2=109,IMG_USE_BAKED_AUX3=110,LL_PCODE_LEGACY_AVATAR=1;
namespace LLAvatarAppearanceDefines{constexpr int BAKED_NUM_INDICES=::BAKED_NUM_INDICES;struct LLAvatarAppearanceDictionary{static int assetIdToBakedTextureIndex(int id){return id>=100&&id<111?id-100:BAKED_NUM_INDICES;}};}
struct LLTextureEntry{int id=0;int getID()const{return id;}};
struct RefObject;struct P{RefObject*p=nullptr;bool isNull()const{return!p;}RefObject*operator->()const{return p;}};
struct RefObject{std::vector<LLTextureEntry>te;std::vector<P>children;int getNumTEs(){return int(te.size());}LLTextureEntry*getTE(int i){return&te.at(i);}const auto&getChildren(){return children;}};
struct Point{std::vector<P>mAttachedObjects;};struct LLAvatarJoint{bool visible=true;void setVisible(bool value,bool){visible=value;}};
struct Reference{enum{AOA_NORMAL=0};int appearance=0;std::map<int,std::shared_ptr<Point>>mAttachmentPoints;std::vector<LLAvatarJoint*>mMeshLOD;int getOverallAppearance(){return appearance;}void updateMeshVisibility()REFERENCE};
struct Face{LLTextureEntry material;};struct Object{int id=0,parent=0,pcode=0;bool attachment=false,current=true;std::vector<Face>faces;};struct Impl{std::map<U32,Object>objects;bool linksCurrent(const Object&o){return o.current;}};
enum class Visibility{Normal,Jellydoll,Invisible};struct Avatar{Visibility visibility=Visibility::Normal;U32 attachedBakedMask=0;bool hidden(int index){auto&a=*this;auto hiddenByBake=[&](int index)HIDDEN;return hiddenByBake(index);}};
std::map<LLUUID,U32> collect(Impl* mImpl){COLLECT return bakedAttachmentMasks;}
int main(){
 for(int image=99;image<=111;++image)for(int mode=0;mode<3;++mode)for(bool child:{false,true})for(bool active:{false,true}){
  Impl scene;scene.objects[1]={42,0,LL_PCODE_LEGACY_AVATAR};scene.objects[2]={9,1,0,true,active,{{{child?0:image}}}};scene.objects[3]={10,2,0,false,active,{{{child?image:0}}}};
  // A grandchild and another avatar's attachment must not hide this avatar.
  scene.objects[4]={11,3,0,false,true,{{{100}}}};scene.objects[5]={43,0,LL_PCODE_LEGACY_AVATAR};scene.objects[6]={12,5,0,true,true,{{{100}}}};
  auto masks=collect(&scene);Avatar native;native.visibility=Visibility(mode);native.attachedBakedMask=masks[42];
  Reference ref;ref.appearance=mode;RefObject root{{{child?0:image}},{}},leaf{{{child?image:0}},{}},grand{{{100}},{}};root.children={{&leaf}};leaf.children={{&grand}};auto point=std::make_shared<Point>();if(active)point->mAttachedObjects={{&root}};ref.mAttachmentPoints[1]=point;
  std::vector<LLAvatarJoint>joints(OTHER+1);for(auto&j:joints)ref.mMeshLOD.push_back(&j);ref.updateMeshVisibility();for(int i=0;i<=OTHER;++i)assert(native.hidden(i)==!joints[i].visible);
 }
 // Removing the only BOM attachment clears the mask on the next source pass.
 Impl scene;scene.objects[1]={42,0,LL_PCODE_LEGACY_AVATAR};scene.objects[2]={9,1,0,true,true,{{{100}}}};assert(collect(&scene)[42]==1);scene.objects.erase(2);assert(collect(&scene)[42]==0);
}
""".replace('REFERENCE',reference).replace('HIDDEN',hidden).replace('COLLECT',collector))
 def test_original_customization_hint_algorithm_and_rigged_scope_are_retained(self):
  original=source('indra/newview/lltoolmorph.cpp');native=body(original,'bool LLVisualParamHint::renderNative()');shared=body(original,'bool LLVisualParamHint::render()')
  for operation in ('mVisualParam->getCameraElevation()', 'mVisualParam->getCameraDistance()', 'mVisualParam->getCameraAngle()'):
   self.assertIn(operation,native);self.assertIn(operation,shared)
  self.assertIn('AppearanceCameraMovement', native);self.assertIn('AppearanceCameraMovement', shared)
  for required in ('~Restore()', 'ownerGeneration() != generation', 'wearable->setVisualParamWeight(param, weight, false)', 'owner.setVisualParamWeight("Blink_Left", left, false)', 'wearable->setVolatile(volatileValue)', 'mBackgroundp->getName()', 'appendAppearanceAttachments'):
   self.assertIn(required,native)
  scene=body(source('indra/newview/vsworldscene.cpp'),'void VSWorldScene::appendAppearanceAttachments(')
  for required in ('object.asset->skin.isMap()', 'linksCurrent(object)', 'mImpl->hudObject(object)', 'geometry.mWeights', 'appearanceRiggedVertices'):
   self.assertIn(required,scene)
if __name__=='__main__':unittest.main()
