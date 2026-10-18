
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
using S32=int;using U8=unsigned char;
struct LLUUID{int id=0;void setNull(){id=0;}bool operator!=(const LLUUID&b)const{return id!=b.id;}};
struct VSWorldScene{struct ObjectHandle{int id=0,generation=1;};struct Admission{U8 pcode=9;};std::vector<int> roots;std::vector<U8> codes;std::optional<ObjectHandle> rootObject(const ObjectHandle&t,bool){if(t.id<0||t.id>=int(roots.size())||roots[t.id]<0)return {};return ObjectHandle{roots[t.id],t.generation};}std::optional<Admission> objectAdmission(const ObjectHandle&t){if(t.id<0||t.id>=int(codes.size()))return {};return Admission{codes[t.id]};}};
bool sameObject(const VSWorldScene::ObjectHandle&a,const VSWorldScene::ObjectHandle&b){return a.id==b.id&&a.generation==b.generation;}
VSWorldScene world;VSWorldScene* scene(){return &world;}
struct Settings{bool parts=false;bool getBOOL(const char*){return parts;}}gSavedSettings;
struct VSObjectProperties{LLUUID group_id;};
struct VSObjectSelection{struct Metadata{bool individual=false;};struct Publication{std::shared_ptr<VSObjectProperties>properties;};struct Row{VSWorldScene::ObjectHandle object;Metadata metadata;bool pending=false;std::optional<Publication> publication;};struct State{std::vector<Row>nodes;int generation=1;bool account=true;bool current(const VSWorldScene::ObjectHandle&t){return account&&t.generation==generation;}};std::shared_ptr<State>mImpl=std::make_shared<State>();std::optional<VSWorldScene::ObjectHandle> firstRootObject(bool=false)const;S32 rootObjectCount()const;S32 objectCount()const;bool selectionAllPCode(U8)const;bool selectGetGroup(LLUUID&)const;};
METHODS
int main(){
 world.roots={0,0,2,3};world.codes={9,9,9,7};VSObjectSelection value;
 auto add=[&](int id,bool individual,int group,bool valid=true){VSObjectSelection::Row row;row.object={id,1};row.metadata.individual=individual;row.pending=!valid;if(valid)row.publication=VSObjectSelection::Publication{std::make_shared<VSObjectProperties>(VSObjectProperties{LLUUID{group}})};value.mImpl->nodes.push_back(row);};
 LLUUID group{999};assert(!value.firstRootObject()&&value.rootObjectCount()==0&&value.objectCount()==0&&value.selectionAllPCode(9));assert(value.selectGetGroup(group)&&group.id==0);
 add(1,false,1);assert(!value.firstRootObject()&&value.firstRootObject(true)->id==1&&value.rootObjectCount()==0&&value.objectCount()==1);
 add(0,true,2);assert(!value.firstRootObject()&&value.firstRootObject(true)->id==1);assert(value.selectGetGroup(group)&&group.id==2); // root-object iterator includes individual roots
 add(2,false,3);assert(value.firstRootObject()->id==2&&value.rootObjectCount()==1);assert(!value.selectGetGroup(group)&&group.id==2);
 gSavedSettings.parts=true;assert(!value.selectGetGroup(group)&&group.id==1);gSavedSettings.parts=false;
 value.mImpl->nodes[1].pending=true;value.mImpl->nodes[1].publication.reset();assert(value.selectGetGroup(group)&&group.id==0); // stop on first invalid root
 gSavedSettings.parts=true;assert(!value.selectGetGroup(group)&&group.id==1); // valid iterator skips invalid root
 value.mImpl->nodes[0].object.generation=2;assert(value.objectCount()==2&&value.firstRootObject(true)->id==2);assert(value.selectGetGroup(group)&&group.id==3);
 add(3,false,3);assert(!value.selectionAllPCode(9)&&value.rootObjectCount()==2);value.mImpl->account=false;assert(value.objectCount()==0&&value.rootObjectCount()==0&&value.selectionAllPCode(9)&&!value.firstRootObject(true));assert(value.selectGetGroup(group)&&group.id==0);
}
