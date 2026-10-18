#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <functional>
#include <utility>
using U32=unsigned;using F32=float;constexpr int LL_PCODE_LEGACY_AVATAR=47,LL_PCODE_VOLUME=9,LL_SCULPT_TYPE_MASK=7,LL_SCULPT_TYPE_MESH=5;
struct LLVector3{float x=0,y=0,z=0;LLVector3()=default;LLVector3(float a,float b,float c):x(a),y(b),z(c){};static LLVector3 zero;bool operator==(LLVector3 b)const{return x==b.x&&y==b.y&&z==b.z;}LLVector3 operator+(LLVector3 b)const{return {x+b.x,y+b.y,z+b.z};}LLVector3 operator-(LLVector3 b)const{return {x-b.x,y-b.y,z-b.z};}LLVector3 operator*(float s)const{return {x*s,y*s,z*s};}};LLVector3 LLVector3::zero;
using LLVector3d=LLVector3;
struct LLQuaternion{float angle=0;bool operator==(LLQuaternion b)const{return angle==b.angle;}};
struct LLMatrix4{LLVector3 scale,pos;LLQuaternion rot;void initAll(LLVector3 s,LLQuaternion r,LLVector3 p){scale=s;rot=r;pos=p;}void initScale(LLVector3 s){scale=s;}void rotate(LLQuaternion r){rot=r;}void translate(LLVector3 p){pos=p;}bool operator==(const LLMatrix4& b)const{return scale==b.scale&&pos==b.pos&&rot==b.rot;}};
struct Xform{LLMatrix4 matrix;void setPosition(LLVector3 p){matrix.pos=p;}void setRotation(LLQuaternion q){matrix.rot=q;}void setScale(LLVector3 s){matrix.scale=s;}void updateMatrix(){};const LLMatrix4&getWorldMatrix(){return matrix;}};
struct Drawable{Xform mXform;const LLMatrix4&getWorldMatrix(){return mXform.getWorldMatrix();}void updateXform(LLVector3 target_pos,LLQuaternion target_rot){ORIGINAL_FRAME}};
struct LLViewerObject{Drawable* mDrawable;const LLMatrix4 getRenderMatrix()const;};
ORIGINAL_RENDER
struct LLBBox{LLBBox()=default;LLBBox(LLVector3,LLQuaternion,LLVector3,LLVector3){}};
struct VSCameraFocusObject{LLMatrix4 renderMatrix,assetToAgentMatrix;LLVector3 renderPosition,rawScale,characterAgentPosition;LLQuaternion renderRotation;LLBBox officialBounds;bool isMesh=false,isAvatar=false,isRoot=false,isAnimated=false,hasControlAvatar=false;std::function<bool()>valid;};
struct LLExtendedMeshParams{static constexpr unsigned ANIMATED_MESH_ENABLED_FLAG=1;};
struct Params{int getSculptType()const{return LL_SCULPT_TYPE_MESH;}};
struct VSWorldScene{struct ObjectHandle{int host=1;U32 local=1;};struct Impl{struct Object{LLVector3 scale;Params params;int pcode=LL_PCODE_VOLUME;U32 parent=0,extendedMeshFlags=0;bool controlAvatar=false;int id=1;};int host=1,handle=0;std::map<U32,Object>objects;bool linksCurrent(const Object&)const{return true;}bool hudObject(const Object&)const{return false;}bool transform(U32,LLVector3& p,LLQuaternion& q,std::set<U32>&,bool)const{p={4,5,6};q={.7f};return true;}struct Char{LLVector3 getCharacterPosition(){return {};}};struct Av{Char&character(){return c;}Char c;};std::map<int,std::shared_ptr<Av>>avatars;};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool live=true;bool current(ObjectHandle)const{return live;}std::function<bool()> guard(ObjectHandle)const{return [this]{return live;};}std::optional<VSCameraFocusObject>focusObject(const ObjectHandle&,const LLVector3d&)const;};
LLVector3 from_region_handle(int h){return {float(h),0,0};}
METHOD
int main(){VSWorldScene scene;for(int i=1;i<8192;++i){auto& o=scene.mImpl->objects[1];o.scale={float(i%101)*.25f,float(i%39)*.75f,float(i%17)*.1f};const auto actual=scene.focusObject({},{});assert(actual&&actual->rawScale==o.scale);assert(actual->assetToAgentMatrix.scale==o.scale&&actual->assetToAgentMatrix.pos==actual->renderPosition&&actual->assetToAgentMatrix.rot==actual->renderRotation);Drawable drawable;drawable.updateXform(actual->renderPosition,actual->renderRotation);LLViewerObject reference{&drawable};assert(actual->renderMatrix==reference.getRenderMatrix());assert(actual->valid());}scene.live=false;assert(!scene.focusObject({},{}));}
