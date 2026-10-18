#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
using U32=unsigned;using S32=int;constexpr int GRID_MODE_WORLD=0,GRID_MODE_LOCAL=1,GRID_MODE_REF_OBJECT=2,SELECT_TYPE_WORLD=0,SELECT_TYPE_ATTACHMENT=1,SELECT_TYPE_HUD=2;constexpr float F32_MAX=1e30f;
struct LLQuaternion{int id=0;void loadIdentity(){id=0;}bool isFinite(){return true;}bool operator==(LLQuaternion b)const{return id==b.id;}};
struct LLVector3{float mV[3]{};static LLVector3 zero;LLVector3()=default;explicit LLVector3(const float*p){std::copy_n(p,3,mV);}LLVector3(float x,float y,float z){mV[0]=x;mV[1]=y;mV[2]=z;}LLVector3 operator+(LLVector3 b)const{return{mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 b)const{return{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}LLVector3 operator*(float f)const{return{mV[0]*f,mV[1]*f,mV[2]*f};}void clearVec(){*this={};}void set(const float*p){std::copy_n(p,3,mV);}bool isFinite()const{return std::all_of(mV,mV+3,[](float f){return std::isfinite(f);});}bool operator==(LLVector3 b)const{return std::equal(mV,mV+3,b.mV);}};LLVector3 LLVector3::zero;using LLVector3d=LLVector3;
template<class T>T llmin(T a,T b){return std::min(a,b);}template<class T>T llmax(T a,T b){return std::max(a,b);}void llassert(bool b){assert(b);}
struct LLVector4a{LLVector3 v;explicit LLVector4a(float f=0):v(f,f,f){}void setAdd(LLVector4a a,LLVector4a b){v=a.v+b.v;}void setSub(LLVector4a a,LLVector4a b){v=a.v-b.v;}void mul(float f){v=v*f;}const float*getF32ptr(){return v.mV;}};
void update_min_max(LLVector4a&min,LLVector4a&max,LLVector4a lo){for(int i=0;i<3;++i){min.v.mV[i]=llmin(min.v.mV[i],lo.v.mV[i]);max.v.mV[i]=llmax(max.v.mV[i],lo.v.mV[i]);}}
struct LLBBox{LLVector3 center{4,5,6},extent{2,4,8};LLQuaternion rotation{7};LLVector3 getCenterAgent()const{return center;}LLVector3 getExtentLocal()const{return extent;}LLQuaternion getRotation()const{return rotation;}};
struct Settings{float resolution=3;float getF32(const char*)const{return resolution;}}gSavedSettings;
struct LLXform{LLVector3 position{7,8,9};LLQuaternion rotation{13};LLVector3 getWorldPosition(){return position;}LLQuaternion getWorldRotation(){return rotation;}};
struct Drawable {bool valid=true;LLVector4a ext[2];LLXform joint;struct Xform{LLXform*parent=nullptr;LLXform*getParent(){return parent;}}mXform;Drawable*operator->(){return this;}operator Drawable*(){return this;}bool notNull(){return valid;}const LLVector4a*getSpatialExtents(){return ext;}bool isActive(){return false;}};using LLDrawable=Drawable;
struct Matrix{};LLVector3 operator*(LLVector3 v,Matrix){return v;}
struct Object {int type=0;Drawable mDrawable;LLQuaternion rotation{11};Object*getRootEdit(){return this;}Matrix getRenderMatrix(){return {};}LLQuaternion getRenderRotation(){return rotation;}};using LLViewerObject=Object;
struct Node{Object*object=nullptr;LLQuaternion mSavedRotation{17};Object*getObject(){return object;}};using LLSelectNode=Node;
struct Selection{std::vector<Node*>nodes;int mSelectType=0;void cleanupNodes(){};Object*getFirstObject(){return nodes.empty()?nullptr:nodes[0]->object;}Object*getFirstRootObject(bool){return getFirstObject();}int getObjectCount(){return int(nodes.size());}Node*findNode(Object*o){for(auto*n:nodes)if(n->object==o)return n;return nullptr;}auto begin(){return nodes.begin();}auto end(){return nodes.end();}};struct LLObjectSelection{using iterator=std::vector<Node*>::iterator;};
struct ReferenceManager{Selection mGridObjects,selected;Selection*mSelectedObjects=&selected;LLBBox mSavedSelectionBBox;int mGridMode=0;LLVector3 mGridOrigin;LLQuaternion mGridRotation;LLVector3 mGridScale;int getSelectTypeForObject(Object*o){return o?o->type:0;}void getGrid(LLVector3&,LLQuaternion&,LLVector3&,bool=false);};
REFERENCE
struct Agent{LLVector3d getPosGlobalFromAgent(LLVector3){return {};}}gAgent;
struct Avatar{bool attachmentTransform(unsigned,LLVector3&p,LLQuaternion&q){p={7,8,9};q={13};return true;}};
struct World {struct Token{int id=0,host=1;};struct Admission{bool hud=false,attachment=false;};struct Focus{LLQuaternion renderRotation{11};bool current()const{return true;}};struct Attachment{unsigned point=31;};std::map<int,std::array<LLVector3,2>>bounds;int type=0;bool live=true;Avatar avatar;std::optional<Focus>focusObject(Token,LLVector3d){return Focus{};}std::optional<std::array<LLVector3,2>>publishedObjectBounds(Token t,LLVector3d){if(bounds.count(t.id))return bounds[t.id];return {};}std::optional<Admission>objectAdmission(Token){return Admission{type==2,type==1};}std::optional<Attachment>objectAttachment(Token){return Attachment{};}Avatar*avatarForObject(int){return &avatar;}}world;World*scene(){return &world;}
struct Region{LLVector3d getOriginGlobal()const{return {};}};std::optional<Region>vs_region_context(int){return Region{};}
struct Session{bool live=true;std::function<bool()> accountGuard(){return [this]{return live;};}};
struct VSObjectSelection {enum class SelectionType{World,Attachment,HUD};struct Saved{LLQuaternion mSavedRotation{17};};struct Impl{struct GridReference{World::Token object;int residency=0;};std::vector<GridReference>gridObjects;std::weak_ptr<Session>session;bool retired=false;bool current(World::Token){return world.live;}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool selected=true;Saved saved;LLBBox box;int objectCount(){return selected?1:0;}std::optional<LLBBox>savedSelectionBBox(){return box;}std::optional<int>node(World::Token){return selected?std::optional<int>(1):std::nullopt;}Saved*savedTransform(World::Token){return &saved;}std::optional<World::Token>firstRootObject(bool){return selected?std::optional<World::Token>({1,1}):std::nullopt;}bool getGrid(S32,LLVector3&,LLQuaternion&,LLVector3&,bool);};
NATIVE
int main(){for(int mode=0;mode<3;++mode)for(int type=0;type<3;++type)for(int selected=0;selected<2;++selected)for(int snap=0;snap<2;++snap)for(int size=0;size<9;++size){
 ReferenceManager old;VSObjectSelection native;auto owner=std::make_shared<Session>();native.mImpl->session=owner;Object object;object.type=type;object.mDrawable.mXform.parent=&object.mDrawable.joint;object.mDrawable.ext[0].v={-float(size),-2,-3};object.mDrawable.ext[1].v={float(size),4,5};Node node{&object};old.mGridMode=mode;world.type=type;world.live=true;native.selected=selected;if(selected)old.selected.nodes.push_back(&node);if(size){old.mGridObjects.nodes.push_back(&node);native.mImpl->gridObjects.push_back({{1,1},0});}world.bounds[1]={object.mDrawable.ext[0].v,object.mDrawable.ext[1].v};
 LLVector3 a,b,c,d;LLQuaternion qa,qb;old.getGrid(a,qa,b,snap);assert(native.getGrid(mode,c,qb,d,snap));assert(a==c&&qa==qb&&b==d);
 }
 VSObjectSelection native;auto owner=std::make_shared<Session>();native.mImpl->session=owner;owner->live=false;LLVector3 origin{4,5,6},scale{7,8,9};LLQuaternion rotation{33};assert(!native.getGrid(0,origin,rotation,scale,false));assert(origin==LLVector3(4,5,6)&&scale==LLVector3(7,8,9)&&rotation.id==33);owner->live=true;native.mImpl->retired=true;assert(!native.getGrid(0,origin,rotation,scale,false));
}
