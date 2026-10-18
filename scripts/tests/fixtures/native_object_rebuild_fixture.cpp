#include <cassert>
#include <map>
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
using U32=unsigned;constexpr int LL_PCODE_VOLUME=9;
struct VSWorldScene{struct ObjectHandle{int host=1;U32 local=1,generation=1;};struct Impl{struct Object{int pcode=9;U32 parent=0,parentGeneration=1,generation=1;bool parentRetired=false,dirty=false;U32 revision=1;};struct Index{std::multimap<U32,U32>edges;auto children(U32 p){return edges.equal_range(p);}}objectIndex;int host=1;U32 nextRevision=2;std::map<U32,Object>objects;};std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool live=true;bool current(ObjectHandle t){return live&&mImpl->objects.count(t.local)&&mImpl->objects.at(t.local).generation==t.generation;}bool rebuildObject(const ObjectHandle&);};
struct LLSpatialGroup{void dirtyGeom(){}};
struct LLDrawable{enum{REBUILD_VOLUME=1,MOVE_UNDAMPED=2};bool volume=true;unsigned moved=0;void*getVOVolume(){return volume?this:nullptr;}void setState(int){}void updateMove(){++moved;}LLSpatialGroup*getSpatialGroup(){return nullptr;}};
struct LLViewerObject{using child_list_t=std::vector<LLViewerObject*>;using const_child_list_t=const child_list_t;LLDrawable*mDrawable;child_list_t children;const child_list_t&getChildren(){return children;}};
struct Pipeline{void markRebuild(LLDrawable*,int){}void markRebuild(LLSpatialGroup*){}}gPipeline;
struct LLManip{static void rebuild(LLViewerObject*);};
ORIGINAL
METHOD
int main(){for(unsigned bits=0;bits<4096;++bits){VSWorldScene world;std::array<LLDrawable,12>drawables;std::array<LLViewerObject,12>objects;for(unsigned i=0;i<objects.size();++i){auto&n=world.mImpl->objects[i+1];n.pcode=(bits&(1u<<i))?9:47;n.parent=i?((i-1)/2)+1:0;drawables[i].volume=n.pcode==9;objects[i].mDrawable=&drawables[i];if(i){world.mImpl->objectIndex.edges.emplace(n.parent,i+1);objects[n.parent-1].children.push_back(&objects[i]);}}LLManip::rebuild(&objects[0]);assert(world.rebuildObject({}));for(unsigned i=0;i<objects.size();++i)assert(world.mImpl->objects[i+1].dirty==bool(drawables[i].moved));world.live=false;assert(!world.rebuildObject({}));}
VSWorldScene w;w.mImpl->objects[1]={};w.mImpl->objects[2]={};w.mImpl->objects[2].parent=1;w.mImpl->objects[1].parent=2;w.mImpl->objectIndex.edges={{1,2},{2,1}};assert(!w.rebuildObject({}));assert(!w.mImpl->objects[1].dirty&&!w.mImpl->objects[2].dirty);w.mImpl->objects[2].parentRetired=true;assert(w.rebuildObject({})&&w.mImpl->objects[1].dirty&&!w.mImpl->objects[2].dirty);}
