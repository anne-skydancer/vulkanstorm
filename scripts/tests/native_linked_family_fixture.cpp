#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using LLHost=int;using LLUUID=int;constexpr unsigned LL_PCODE_LEGACY_AVATAR=47;
struct LLVector3{int value=10;static const LLVector3 zero;};const LLVector3 LLVector3::zero{0};
struct Motion{bool selected=false;unsigned initialized=0,deselected=0;void freeze(bool b){selected=b;}void setSelectionState(bool b){selected=b;}void initializeSelection(){++initialized;}void initializeDeselection(){++deselected;}};
INDEX
struct VSWorldScene{
 struct ObjectHandle{int host=1,id=0;U32 local=0;U64 region=1,generation=1;int account=9;U64 accountGeneration=1;};enum class FamilyTraversal{NonJoint,AllChildren};
 struct Impl{struct Object{int id=0;U32 parent=0;unsigned pcode=9;U64 generation=1,parentGeneration=0;bool parentRetired=false;Motion motion;LLVector3 velocity,acceleration,angularVelocity;};std::map<U32,Object>objects;VSObjectIndex<int>objectIndex;int host=1;bool retired=false;U64 incarnation=1;int account=9;U64 accountGeneration=1;};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::shared_ptr<VSWorldScene>>mNeighbours;
 std::optional<ObjectHandle>object(int host,U32 local)const{if(host!=mImpl->host){auto it=mNeighbours.find(host);return it==mNeighbours.end()?std::nullopt:it->second->object(host,local);}auto it=mImpl->objects.find(local);if(it==mImpl->objects.end())return {};return ObjectHandle{host,it->second.id,local,mImpl->incarnation,it->second.generation,mImpl->account,mImpl->accountGeneration};}
 bool current(const ObjectHandle&t)const{auto o=object(t.host,t.local);return o&&o->id==t.id&&o->generation==t.generation&&o->region==t.region&&o->account==t.account&&o->accountGeneration==t.accountGeneration;}
 bool setLegacyMaterialPreview(const ObjectHandle&,bool){return true;}
 bool freezeObject(const ObjectHandle&,bool);bool setSelectionState(const ObjectHandle&,bool);bool initializeSelectionMotion(const ObjectHandle&);bool initializeDeselectionMotion(const ObjectHandle&);
 std::optional<std::vector<ObjectHandle>>linkedFamily(const ObjectHandle&,bool=true,bool=true,FamilyTraversal=FamilyTraversal::NonJoint)const;
};
METHOD
EXTRA_FUNCTIONS
// Firestorm 7.2.5: addChild() notifies the script-editor websocket server about
// relinked published objects. No editor is connected here, so getServer() is null.
struct LLScriptEditorWSServer{static LLScriptEditorWSServer* getServer(){return nullptr;}bool isObjectPublished(unsigned)const{return false;}void unpublishObject(unsigned,const char*){}template<class T>void onLinksetChildAdded(unsigned,T*){}};
struct Reference{using child_list_t=std::vector<Reference*>;unsigned local=0;bool avatar=false,mbCanSelect=true;unsigned mSeatCount=0;child_list_t mChildList;Reference*parent=nullptr;struct Extrap{void changedlink(Reference&) {}}mExtrap;bool isAvatar()const{return avatar;}bool setParent(Reference*p){parent=p;return true;}void afterReparent(){}bool isAttachment()const{return false;}unsigned getID()const{return local;}Reference* getRootEdit(){Reference* r=this;while(r->parent&&!r->parent->isAvatar())r=r->parent;return r;}void addChild(Reference*);void addThisAndAllChildren(std::vector<Reference*>&);void addThisAndNonJointChildren(std::vector<Reference*>&);};
GOLD
int main(){
 // Parent local-ID reuse retires its old child edges; actual re-admission
 // appends to the new parent in fresh accepted order, not the retired order.
 {VSObjectIndex<int> index;VSWorldScene::Impl::Object a,b;a.id=10;a.parent=4;a.parentGeneration=4;b.id=20;b.parent=4;b.parentGeneration=4;index.replace(10,(const VSWorldScene::Impl::Object*)nullptr,a);index.replace(20,(const VSWorldScene::Impl::Object*)nullptr,b);auto old=a;old.parentRetired=true;a.parentGeneration=5;index.replace(10,&old,a);auto [begin,end]=index.children(4);assert(begin->second==20&&std::next(begin)->second==10);old=b;old.parentRetired=true;b.parentGeneration=5;index.replace(20,&old,b);auto range=index.children(4);assert(range.first->second==10&&std::next(range.first)->second==20);}
 VSWorldScene scene;std::map<U32,Reference> ref;U64 generation=1;
 auto install=[&](U32 id,U32 parent,bool avatar=false){auto previous=scene.mImpl->objects.find(id);VSWorldScene::Impl::Object next;next.id=id;next.parent=parent;next.pcode=avatar?LL_PCODE_LEGACY_AVATAR:9;next.generation=previous==scene.mImpl->objects.end()?generation++:previous->second.generation;if(parent&&scene.mImpl->objects.count(parent))next.parentGeneration=scene.mImpl->objects.at(parent).generation;
  scene.mImpl->objectIndex.replace(id,previous==scene.mImpl->objects.end()?nullptr:&previous->second,next);scene.mImpl->objects[id]=next;
  auto& r=ref[id];r.local=id;r.avatar=avatar;if(r.parent&&r.parent->local!=parent){auto& children=r.parent->mChildList;children.erase(std::remove(children.begin(),children.end(),&r),children.end());r.parent=nullptr;}if(parent&&ref.count(parent))ref[parent].addChild(&r);
  // Earlier orphan children attach in accepted order when parent appears.
  auto [begin,end]=scene.mImpl->objectIndex.children(id);for(auto it=begin;it!=end;++it)if(ref.count(it->second)&&!ref[it->second].parent)r.addChild(&ref[it->second]);
 };
 auto compare=[&](U32 local){auto token=scene.object(1,local);assert(token);for(bool root:{false,true})for(bool children:{false,true})for(auto traversal:{VSWorldScene::FamilyTraversal::NonJoint,VSWorldScene::FamilyTraversal::AllChildren}){Reference*r=&ref.at(local);if(root)while(!r->isAvatar()&&r->parent&&!r->parent->isAvatar())r=r->parent;std::vector<Reference*>expected;if(!children)expected.push_back(r);else if(traversal==VSWorldScene::FamilyTraversal::AllChildren)r->addThisAndAllChildren(expected);else r->addThisAndNonJointChildren(expected);auto result=scene.linkedFamily(*token,root,children,traversal);assert(result&&result->size()==expected.size());for(unsigned i=0;i<expected.size();++i)assert((*result)[i].local==expected[i]->local);}};
 install(1,0);install(9,1);install(3,1);install(7,9);install(2,1,true);install(8,2);install(6,8);for(U32 id:{1,2,3,6,7,8,9})compare(id);
 // Metadata/motion refresh does not reorder; A->B->A appends exactly once.
 install(9,1);compare(1);install(9,3);compare(1);install(9,1);compare(1);
 install(50,60);install(40,60);assert(!scene.linkedFamily(*scene.object(1,50),true,true));install(60,1);compare(60);compare(1);
 std::mt19937 random(1234);for(U32 i=100;i<1000;++i)install(i,i==100?1:100+(random()%(i-100)),i%53==0);for(unsigned i=0;i<1000;++i)compare(100+random()%900);
 auto token=*scene.object(1,9);assert(scene.freezeObject(token,true));assert(scene.mImpl->objects.at(9).motion.selected&&!scene.mImpl->objects.at(7).motion.selected&&!scene.mImpl->objects.at(1).motion.selected);assert(scene.initializeSelectionMotion(token));assert(scene.mImpl->objects.at(9).velocity.value==0&&scene.mImpl->objects.at(9).acceleration.value==0&&scene.mImpl->objects.at(9).angularVelocity.value==10&&scene.mImpl->objects.at(9).motion.initialized==1);scene.mImpl->objects.at(9).acceleration.value=77;assert(scene.initializeDeselectionMotion(token));assert(scene.mImpl->objects.at(9).angularVelocity.value==0&&scene.mImpl->objects.at(9).acceleration.value==77&&scene.mImpl->objects.at(9).motion.deselected==1);assert(scene.freezeObject(token,false)&&!scene.mImpl->objects.at(9).motion.selected);auto forged=token;forged.account++;assert(!scene.linkedFamily(forged)&&!scene.freezeObject(forged,true)&&!scene.initializeSelectionMotion(forged)&&!scene.initializeDeselectionMotion(forged));forged=token;forged.accountGeneration++;assert(!scene.linkedFamily(forged)&&!scene.freezeObject(forged,true)&&!scene.initializeSelectionMotion(forged)&&!scene.initializeDeselectionMotion(forged));forged=token;forged.region++;assert(!scene.linkedFamily(forged)&&!scene.freezeObject(forged,true)&&!scene.initializeSelectionMotion(forged)&&!scene.initializeDeselectionMotion(forged));forged=token;forged.generation++;assert(!scene.linkedFamily(forged)&&!scene.freezeObject(forged,true)&&!scene.initializeSelectionMotion(forged)&&!scene.initializeDeselectionMotion(forged));
 scene.mImpl->retired=true;assert(!scene.linkedFamily(token));scene.mImpl->retired=false;scene.mImpl->objects.at(9).id=999;assert(!scene.linkedFamily(token));scene.mImpl->objects.at(9).id=9;
 scene.mImpl->objects.at(9).parentRetired=true;assert(!scene.linkedFamily(token,true,true));assert(scene.linkedFamily(token,false,false));scene.mImpl->objects.at(9).parentRetired=false;scene.mImpl->objects.at(9).parentGeneration++;assert(!scene.linkedFamily(token,true,true));scene.mImpl->objects.at(9).parentGeneration--;
 // Corrupt ancestor cycle fails atomically, no partial family accepted.
 auto old=scene.mImpl->objects.at(1);auto next=old;next.parent=7;next.parentGeneration=scene.mImpl->objects.at(7).generation;scene.mImpl->objectIndex.replace(1,&old,next);scene.mImpl->objects[1]=next;assert(!scene.linkedFamily(token,true,true));assert(!scene.linkedFamily(*scene.object(1,1),false,true));
 // Neighbor circuits resolve only their own same-local identities.
 auto other=std::make_shared<VSWorldScene>();other->mImpl->host=2;other->mImpl->objects[1]={1001};scene.mNeighbours[2]=other;auto remote=*other->object(2,1);auto family=scene.linkedFamily(remote);assert(family&&family->size()==1&&family->front().host==2);other->mImpl->incarnation++;assert(!scene.linkedFamily(remote));
}
