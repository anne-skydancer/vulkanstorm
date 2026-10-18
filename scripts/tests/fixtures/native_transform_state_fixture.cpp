#include <memory>
#include <vector>
#include <functional>
#include <cassert>
using U8=unsigned char;
struct VSResourceBudget{struct Reservation{int*count;explicit Reservation(int*c):count(c){++*count;}~Reservation(){--*count;}};};
struct VSWorldScene{struct ObjectHandle{int id=1,generation=2;};bool allow=true;int count=0;std::size_t requested=0;std::shared_ptr<const VSResourceBudget::Reservation>reserveSpatialBytes(std::size_t bytes){requested=bytes;return allow?std::make_shared<VSResourceBudget::Reservation>(&count):nullptr;}}world;VSWorldScene*scene(){return &world;}bool sameObject(VSWorldScene::ObjectHandle a,VSWorldScene::ObjectHandle b){return a.id==b.id&&a.generation==b.generation;}
struct VSObjectSelection{struct SavedTransform{int field=0;std::shared_ptr<const VSResourceBudget::Reservation>residency;};struct State{struct Node{VSWorldScene::ObjectHandle object;std::shared_ptr<SavedTransform>transform;};std::vector<Node>nodes;bool alive=true;bool current(VSWorldScene::ObjectHandle t){return alive&&t.generation==2;}};std::shared_ptr<State>mImpl=std::make_shared<State>();std::shared_ptr<SavedTransform>savedTransform(const VSWorldScene::ObjectHandle&)const;};
METHOD
int main(){VSObjectSelection selection;selection.mImpl->nodes.push_back({{},nullptr});VSWorldScene::ObjectHandle token;world.allow=false;assert(!selection.savedTransform(token)&&world.count==0);world.allow=true;auto record=selection.savedTransform(token);assert(record&&world.count==1&&world.requested==sizeof(VSObjectSelection::SavedTransform)+64);record->field=73;world.allow=false;assert(selection.savedTransform(token)==record&&record->field==73);++token.generation;assert(!selection.savedTransform(token));token={};selection.mImpl->alive=false;assert(!selection.savedTransform(token));selection.mImpl->alive=true;selection.mImpl->nodes.clear();assert(!selection.savedTransform(token)&&record->field==73&&world.count==1);record.reset();assert(world.count==0);assert(!selection.savedTransform({2,2}));}
