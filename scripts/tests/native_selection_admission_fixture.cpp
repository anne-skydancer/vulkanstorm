#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <vector>
using U32=unsigned;using U64=unsigned long long;using U8=unsigned char;
constexpr U8 LL_PCODE_LEGACY_AVATAR=5;
struct VSWorldScene{
 struct ObjectHandle{int host=1,id=1;U32 local=1;U64 region=1,generation=1;int account=1;U64 accountGeneration=1;int kind=0;};
 struct ObjectAdmission{U32 updateFlags=0,rootUpdateFlags=0;U8 pcode=0;bool orphaned=false,attachment=false,hud=false;U8 clickAction=0;};
 struct Poison{Poison()=default;Poison(const Poison&){assert(false);}Poison&operator=(const Poison&){assert(false);return*this;}};
 struct Avatar{bool hudAttachment(unsigned point)const{return point==31;}};
 struct Impl{struct Object{int id=1;U32 parent=0,updateFlags=0;U64 generation=1,parentGeneration=0;bool parentRetired=false;U8 pcode=0,attachment=0,clickAction=0;Poison rawStrings;};int host=1;std::map<U32,Object>objects;std::map<int,std::shared_ptr<Avatar>>avatars;};
 std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;
 std::optional<ObjectHandle>object(int host,U32 local)const{if(host!=mImpl->host){auto n=mNeighbours.find(host);return n==mNeighbours.end()?std::nullopt:n->second->object(host,local);}auto i=mImpl->objects.find(local);if(i==mImpl->objects.end())return {};return ObjectHandle{host,i->second.id,local,1,i->second.generation,1,1,0};}
 bool current(const ObjectHandle&)const;

 std::optional<ObjectAdmission>objectAdmission(const ObjectHandle&)const;
};
CURRENT
METHOD
VSWorldScene*activeScene=nullptr;VSWorldScene*scene(){return activeScene;}
struct VSObjectSelection{
 enum class SelectionType{World,Attachment,HUD};struct SelectionTypeState{bool selected=false;std::optional<SelectionType>type;};
 struct Impl{struct Node{VSWorldScene::ObjectHandle object;};std::vector<Node>nodes;bool active=true;bool current(const VSWorldScene::ObjectHandle&token)const{return active&&activeScene&&activeScene->current(token);}};
 std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();SelectionTypeState selectionType()const;
};
SELECT_METHOD
void testSelectionType(){
 VSWorldScene world;activeScene=&world;world.mImpl->objects[1];VSObjectSelection owner;assert(!owner.selectionType().selected);
 owner.mImpl->nodes.push_back({{}});assert(owner.selectionType().selected&&owner.selectionType().type==VSObjectSelection::SelectionType::World);
 auto&child=world.mImpl->objects[1];child.parent=2;child.attachment=0xf1;auto&parent=world.mImpl->objects[2];parent.id=2;parent.pcode=LL_PCODE_LEGACY_AVATAR;
 assert(owner.selectionType().selected&&!owner.selectionType().type);world.mImpl->avatars[2]=std::make_shared<VSWorldScene::Avatar>();assert(owner.selectionType().type==VSObjectSelection::SelectionType::HUD);
 auto old=owner.mImpl->nodes.front();++old.object.generation;owner.mImpl->nodes.insert(owner.mImpl->nodes.begin(),old);assert(owner.selectionType().type==VSObjectSelection::SelectionType::HUD);
 owner.mImpl->active=false;assert(!owner.selectionType().selected);owner.mImpl->active=true;world.mImpl->objects.erase(1);assert(!owner.selectionType().selected);activeScene=nullptr;
}
int main(){testSelectionType();
 VSWorldScene world;auto&child=world.mImpl->objects[1];child.updateFlags=3;child.clickAction=4;VSWorldScene::ObjectHandle token;auto row=world.objectAdmission(token);assert(row&&row->updateFlags==3&&!row->orphaned&&row->clickAction==4);
 for(auto which:{0,1,2,3,4}){auto old=token;switch(which){case 0:++old.id;break;case 1:++old.generation;break;case 2:++old.region;break;case 3:++old.account;break;case 4:++old.accountGeneration;break;}assert(!world.objectAdmission(old));}
 child.parent=2;row=world.objectAdmission(token);assert(row&&row->orphaned&&row->rootUpdateFlags==3);auto&parent=world.mImpl->objects[2];parent.id=2;parent.updateFlags=12;row=world.objectAdmission(token);assert(row&&!row->orphaned&&row->rootUpdateFlags==12);child.parentGeneration=2;assert(world.objectAdmission(token)->orphaned);child.parentGeneration=1;child.parentRetired=true;assert(world.objectAdmission(token)->orphaned);child.parentRetired=false;parent.parent=1;assert(world.objectAdmission(token)->orphaned);parent.parent=0;
 parent.pcode=LL_PCODE_LEGACY_AVATAR;child.attachment=0xf1;assert(!world.objectAdmission(token));world.mImpl->avatars[2]=std::make_shared<VSWorldScene::Avatar>();row=world.objectAdmission(token);assert(row&&!row->orphaned&&row->hud&&row->attachment&&row->rootUpdateFlags==3);
 parent.pcode=0;child.attachment=0;child.parent=0;auto saved=token;world.mImpl->objects.erase(1);auto&reuse=world.mImpl->objects[1];reuse.id=99;reuse.generation=2;assert(!world.objectAdmission(saved));
 auto neighbour=std::make_unique<VSWorldScene>();neighbour->mImpl->host=2;neighbour->mImpl->objects[1].updateFlags=9;world.mNeighbours[2]=std::move(neighbour);token.host=2;row=world.objectAdmission(token);assert(row&&row->updateFlags==9);world.mNeighbours.clear();assert(!world.objectAdmission(token));
}
