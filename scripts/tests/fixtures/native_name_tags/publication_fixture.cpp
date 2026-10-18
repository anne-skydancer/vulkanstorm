#include <cassert>
#include <memory>
#include <vector>
#include <functional>
#include <cstddef>
using U64=unsigned long long;using S32=int;
struct LLSD {bool valid=true;bool isMap()const{return valid;}};
struct Volume {int faces=2;int getNumVolumeFaces()const{return faces;}};
namespace VSWorldAssets {struct Geometry {int lod=0;U64 generation=1;LLSD skin;std::shared_ptr<Volume> volume=std::make_shared<Volume>();};}
struct LLMeshSkinInfo {explicit LLMeshSkinInfo(const LLSD&) {}};
struct Charge {static inline unsigned live=0;Charge(){++live;}~Charge(){--live;}};
struct VSAvatarRiggingInfo {
 std::shared_ptr<Charge> charge;
 static inline unsigned builds=0;
 static std::shared_ptr<const VSAvatarRiggingInfo> create(const Volume&,const LLMeshSkinInfo&,const std::function<std::shared_ptr<Charge>(std::size_t)>& reserve){++builds;auto charge=reserve(100);if(!charge)return {};auto result=std::make_shared<VSAvatarRiggingInfo>();result->charge=charge;return result;}
};
struct Surface {std::shared_ptr<const VSWorldAssets::Geometry> asset;bool rigged=true;};
struct Face {bool mesh=true;std::shared_ptr<Surface> pickSurface;};
struct Impl {struct Object {std::vector<Face> admittedFaces;S32 nameRiggingLOD=-1;U64 nameRiggingGeneration=0;std::shared_ptr<const VSAvatarRiggingInfo> nameRigging;};};
struct Scene {
 bool refuse=false;
 std::shared_ptr<Charge> reserveSpatialBytes(std::size_t){return refuse?nullptr:std::make_shared<Charge>();}
 std::shared_ptr<const VSAvatarRiggingInfo> update(Impl::Object& object){CURRENT;return rigging(object);}
};
int main(){
 Scene scene;Impl::Object object;
 auto low=std::make_shared<VSWorldAssets::Geometry>();low->lod=1;
 auto face=[&](const std::shared_ptr<const VSWorldAssets::Geometry>& source){return Face{true,std::make_shared<Surface>(Surface{source,true})};};
 object.admittedFaces={face(low),face(low)};auto first=scene.update(object);assert(first&&VSAvatarRiggingInfo::builds==1&&object.nameRiggingLOD==1);
 auto high=std::make_shared<VSWorldAssets::Geometry>();high->lod=3;high->generation=2;
 // A failed partial GPU publication must never adopt a mixed source envelope.
 object.admittedFaces[0]=face(high);assert(scene.update(object)==first&&VSAvatarRiggingInfo::builds==1);
 object.admittedFaces[1]=face(high);scene.refuse=true;assert(scene.update(object)==first&&object.nameRiggingLOD==1&&object.nameRiggingGeneration==1);
 scene.refuse=false;auto second=scene.update(object);assert(second!=first&&object.nameRiggingLOD==3&&object.nameRiggingGeneration==2);
 // Bounds retain only the compact metadata: old mesh/CPU volume can retire.
 std::weak_ptr<const VSWorldAssets::Geometry> retired=high;
 object.admittedFaces={face(low),face(low)};high.reset();assert(retired.expired());assert(scene.update(object)==second);
 assert(VSAvatarRiggingInfo::builds==3);
 auto replacement=std::make_shared<VSWorldAssets::Geometry>();replacement->lod=3;replacement->generation=4;object.admittedFaces={face(replacement),face(replacement)};auto third=scene.update(object);assert(third!=second&&object.nameRiggingGeneration==4);
 object.admittedFaces[1].mesh=false;assert(scene.update(object)==third&&VSAvatarRiggingInfo::builds==4);
 first.reset();second.reset();third.reset();object={};assert(Charge::live==0);
}
