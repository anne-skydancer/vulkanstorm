"""Compile production scene replacement methods with CPU-only owner/decoder adapters."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'indra/newview/vsworldscene.cpp'


class WorldSceneReplacement(unittest.TestCase):
    def compile(self, fixture):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        budget = (ROOT / 'indra/newview/vsresourcebudget.h').as_posix()
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'replacement.cpp'
            binary = source.with_suffix('.exe')
            source.write_text(f'#include "{budget}"\n' + fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_texture_cost_before_upload_after_publication_and_retirement(self):
        body = method(SOURCE.read_text(encoding='utf-8'), 'F32 textureCost(')
        self.compile(r'''
#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <string>
using F32=float;using S32=int;
struct LLUUID {std::string value;std::string asString()const{return value;}};
LLUUID vs_native_image_id(const LLUUID& id){return id;}
struct Image {unsigned width,height;};
struct VSUIRenderer {
 static inline unsigned extentReads=0;
 static std::array<unsigned,2> imageExtent(const std::shared_ptr<Image>& image){
  assert(image && "Pending or retired images have no GPU dimensions");
  ++extentReads;return {image->width,image->height};
 }
};
struct Resources {
 std::map<std::string,std::shared_ptr<Image>> images;
 std::shared_ptr<Image> image(const std::string& key)const{
  auto found=images.find(key);return found==images.end()?nullptr:found->second;
 }
};
struct Scene {Resources resources;
''' + body + r'''
};
int main(){
 Scene scene;LLUUID id{"pending"};
 assert(scene.textureCost(id)==256);assert(VSUIRenderer::extentReads==0);
 scene.resources.images["remote:pending"]=nullptr;
 assert(scene.textureCost(id)==256);assert(VSUIRenderer::extentReads==0);
 scene.resources.images["remote:pending"]=std::make_shared<Image>(Image{512,256});
 assert(scene.textureCost(id)==352);assert(VSUIRenderer::extentReads==1);
 scene.resources.images["remote:pending"]=std::make_shared<Image>(Image{1024,1024});
 assert(scene.textureCost(id)==512);assert(VSUIRenderer::extentReads==2);
 scene.resources.images.clear();
 assert(scene.textureCost(id)==256);assert(VSUIRenderer::extentReads==2);
}
''')

    def test_embedded_image_limit_check_accepts_pending_publication(self):
        body = method((ROOT / 'indra/newview/vsworldgltf.cpp').read_text(),
                      'VSUIRenderer::Image image(unsigned index,unsigned demand)')
        check = body[body.index('        const auto extent='):body.index('        if(previous &&')]
        self.compile(r'''
#include <array>
#include <cassert>
#include <memory>
struct Image {unsigned width,height;};
struct VSUIRenderer {
 static std::array<unsigned,2> imageExtent(const std::shared_ptr<Image>& image){
  assert(image);return {image->width,image->height};
 }
};
bool overLimit(const std::shared_ptr<Image>& previous,bool fullRes,unsigned limit){
''' + check + r'''
 return overLimit;
}
int main(){
 assert(!overLimit(nullptr,false,512));
 auto image=std::make_shared<Image>(Image{1024,512});
 assert(overLimit(image,false,512));assert(!overLimit(image,true,512));
 assert(!overLimit(image,false,1024));
}
''')

    def test_actual_object_replacement_preserves_only_compatible_owner_meshes(self):
        scene = SOURCE.read_text(encoding='utf-8')
        bodies = '\n'.join(method(scene, signature) for signature in (
            'static VSWorldContracts::OwnerKind kind(', 'void eraseObject(',
            'bool replace(U32 id,Object&& object)', 'void eraseAnimated()'))
        self.compile(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <optional>
#include <vector>
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;using S32=int;using U16=uint16_t;
constexpr int LL_PCODE_LEGACY_AVATAR=9;
struct LLUUID {
 int value=0;
 bool isNull()const{return value==0;}
 bool operator==(const LLUUID&)const=default;
 bool operator<(const LLUUID& other)const{return value<other.value;}
};
struct LLVector4a{float value[4];};struct LLVector2{float value[2];};
struct LLVolume {
 struct Face{int mNumVertices=0,mNumIndices=0;};
 int getNumVolumeFaces()const{return 0;}
 Face getVolumeFace(int)const{return {};}
};
struct Params {LLUUID asset;LLUUID getSculptID()const{return asset;}int getSculptType()const{return 0;}bool operator==(const Params&)const=default;};
struct Flexible {bool operator==(const Flexible&)const=default;};
struct FlexState {unsigned pathSignatureSize=9;};
struct Motion {int state=12;void accept(int,int,int,int,int,double){++state;}};
struct LLFrameTimer {static double getTotalSeconds(){return 100;}};
namespace VSWorldContracts {enum class OwnerKind {Avatar,ControlAvatar,Attachment,Object};}
struct LLExtendedMeshParams {static constexpr U32 ANIMATED_MESH_ENABLED_FLAG=1;};
struct Mesh {int generation;};
struct Material {std::shared_ptr<int> value;
 void setGLTFMaterialOverride(const std::shared_ptr<int>& v){value=v;}
 std::shared_ptr<int> getGLTFMaterialOverride()const{return value;}};
struct RenderMaterials {std::map<U8,int> values;int getMaterial(U8 face)const{
 auto found=values.find(face);return found==values.end()?0:found->second;}};
struct TextureAnimation {int token=0;bool equals(const TextureAnimation& other)const{return token==other.token;}};
struct Object {
 LLUUID id;U32 parent=0;int pcode=4,attachment=0;
 U64 generation=0,revision=0;U32 extendedMeshFlags=0;bool parentRetired=false,renderable=true,dirty=true;
 std::shared_ptr<const VSResourceBudget::Reservation> slot,primitiveResidency;
 bool nameValuesPresent=false;std::string nameValues;LLUUID inventoryItem;
 std::shared_ptr<const VSResourceBudget::Reservation> metadataResidency;
 std::shared_ptr<LLVolume> volume;Params params;
 struct Face {Material material;std::shared_ptr<int> surface;std::array<std::shared_ptr<int>,4> publishedChannels;std::shared_ptr<Mesh> mesh;int publishedMaterial=0;std::shared_ptr<int> publishedImage;bool published=false;U64 poseGeneration=0;};std::vector<Face> faces,admittedFaces;
 std::map<unsigned,std::shared_ptr<int>> media;RenderMaterials renderMaterials;
 std::shared_ptr<TextureAnimation> animation;U8 textureAnimMode=0;
 Motion motion;int scale=0,position=0,rotation=0,velocity=0,acceleration=0,angularVelocity=0;
 U16 taskSerial=0;bool taskKnown=false;std::string taskFile;
 std::optional<int> taskRequest;double taskDeadline=0;std::map<LLUUID,S32> controlAnimations;
 std::shared_ptr<int> gltf,pendingGLTF,controlAvatar;
 std::shared_ptr<const VSResourceBudget::Reservation> controlAnimationResidency;
 std::optional<Flexible> flexible;std::shared_ptr<FlexState> flex;
};
struct Budgets {VSResourceBudget objects{32},primitiveBytes{1024};};
struct Scene {
 std::map<U32,Object> objects;std::map<LLUUID,int> avatars;std::map<U32,int> pending;
 struct AnimationState {std::map<LLUUID,S32> animations;double deadline=0;std::shared_ptr<const VSResourceBudget::Reservation> residency;};
 std::map<LLUUID,AnimationState> pendingAnimations;
 std::shared_ptr<Budgets> budgets=std::make_shared<Budgets>();
 U64 nextObjectGeneration=1,nextRevision=1;unsigned requests=0;
 std::map<U32,int> pendingMaterialOverrides;unsigned overrides=0;
 bool applyMaterialOverrides(Object& object,int value){++overrides;object.faces[0].material.value=std::make_shared<int>(value);return true;}

 void request(U32){++requests;}void dirtyChildren(U32){}
 bool linksCurrent(const Object& object)const{return !object.parentRetired;}
BODIES
};
Object update(int identity=7,int asset=42){
 Object value;value.id.value=identity;value.params.asset.value=asset;value.faces.resize(2);return value;
}
int main(){
 // Same identity retains GPU generations while material/interaction revision advances.
 Scene scene;assert(scene.replace(50,update()));
 auto first=std::make_shared<Mesh>(Mesh{81});auto second=std::make_shared<Mesh>(Mesh{82});
 auto& original=scene.objects.at(50);original.faces[0].mesh=first;original.faces[1].mesh=second;
 original.faces[0].published=true;original.faces[0].publishedMaterial=34;original.faces[0].poseGeneration=9;
 original.faces[0].publishedImage=std::make_shared<int>(112);const auto image=original.faces[0].publishedImage;
 original.faces[0].material.value=std::make_shared<int>(113);
 original.faces[0].surface=std::make_shared<int>(114);original.faces[0].publishedChannels[0]=std::make_shared<int>(115);
 auto materialOverride=original.faces[0].material.value,materialSurface=original.faces[0].surface,normal=original.faces[0].publishedChannels[0];
 original.media[0]=std::make_shared<int>(116);auto media=original.media[0];
 original.animation=std::make_shared<TextureAnimation>(TextureAnimation{6});auto animation=original.animation;original.textureAnimMode=3;
 original.taskKnown=true;original.taskSerial=17;original.taskRequest=4;original.controlAnimations[{21}]=6;
 original.controlAvatar=std::make_shared<int>(123);auto controller=original.controlAvatar;
 original.flexible=Flexible{};original.flex=std::make_shared<FlexState>();auto simulation=original.flex;
 const auto generation=original.generation,revision=original.revision;
 auto lease=original.slot;
 auto compatible=update();compatible.animation=std::make_shared<TextureAnimation>(TextureAnimation{6});compatible.flexible=Flexible{};assert(scene.replace(50,std::move(compatible)));
 const auto& preserved=scene.objects.at(50);
 assert(preserved.faces[0].mesh==first&&preserved.faces[1].mesh==second);
 assert(preserved.faces[0].published&&preserved.faces[0].publishedMaterial==34&&
        preserved.faces[0].publishedImage==image&&preserved.faces[0].poseGeneration==9);
 assert(preserved.faces[0].material.value==materialOverride&&preserved.faces[0].surface==materialSurface&&preserved.faces[0].publishedChannels[0]==normal);
 assert(preserved.media.at(0)==media&&preserved.animation==animation&&preserved.textureAnimMode==3);
 assert(preserved.generation==generation&&preserved.revision>revision&&preserved.dirty);
 assert(preserved.slot==lease&&preserved.taskKnown&&preserved.taskSerial==17);
 assert(preserved.taskRequest==4&&preserved.controlAnimations.at({21})==6);
 assert(preserved.controlAvatar==controller && preserved.flex==simulation && simulation->pathSignatureSize==0);
 assert(scene.budgets->objects.used()==1);
 // A face-count change preserves matching indices, not nonexistent faces.
 auto fewer=update();fewer.faces.resize(1);assert(scene.replace(50,std::move(fewer)));
 assert(scene.objects.at(50).faces.size()==1&&scene.objects.at(50).faces[0].mesh==first);
 assert(scene.objects.at(50).admittedFaces.size()==2);
 assert(scene.objects.at(50).admittedFaces[0].mesh==first&&scene.objects.at(50).admittedFaces[1].mesh==second);
 auto extra=update();extra.faces.resize(3);assert(scene.replace(50,std::move(extra)));
 assert(scene.objects.at(50).faces[0].mesh==first&&!scene.objects.at(50).faces[1].mesh&&!scene.objects.at(50).faces[2].mesh);
 // Owner, producer, resource identity or unsupported state changes prohibit reuse.
 for(int changed=0;changed<7;++changed){
  Scene fresh;assert(fresh.replace(50,update()));
  fresh.objects.at(50).faces[0].mesh=first;
  auto next=update();
  if(changed==0)next.id.value=8;
  if(changed==1)next.pcode=LL_PCODE_LEGACY_AVATAR;
  if(changed==2)next.attachment=1;
  if(changed==3)next.extendedMeshFlags=LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG;
  if(changed==4)next.renderable=false;
  if(changed==5)fresh.objects.at(50).renderable=false;
  if(changed==6)next.params.asset.value=43;
  assert(fresh.replace(50,std::move(next)));
  assert(!fresh.objects.at(50).faces[0].mesh);
 }
 // A changed asset ID retains its incoming override, not the previous asset's.
 Scene materials;auto prior=update();prior.renderMaterials.values[0]=4;
 prior.faces[0].material.value=std::make_shared<int>(11);assert(materials.replace(50,std::move(prior)));
 auto changed=update();changed.renderMaterials.values[0]=5;auto incoming=std::make_shared<int>(22);
 changed.faces[0].material.value=incoming;assert(materials.replace(50,std::move(changed)));
 assert(materials.objects.at(50).faces[0].material.value==incoming);
 Scene queued;queued.pendingMaterialOverrides[50]=99;assert(queued.replace(50,update()));
 assert(queued.pendingMaterialOverrides.empty()&&queued.overrides==1&&*queued.objects.at(50).faces[0].material.value==99);
 // The ordinary animesh kill switch retires the complete linked family,
 // while retaining unrelated owners and their resource leases.
 Scene animated;auto control=update();control.extendedMeshFlags=1;
 assert(animated.replace(50,std::move(control)));
 auto member=update(9);member.parent=50;assert(animated.replace(51,std::move(member)));
 assert(animated.replace(52,update(10)));
 auto held=animated.objects.at(50).slot;animated.eraseAnimated();
 assert(!animated.objects.count(50)&&!animated.objects.count(51)&&animated.objects.count(52));
 assert(animated.budgets->objects.used()==2);held.reset();assert(animated.budgets->objects.used()==1);
 // UUID reassignment expires the old host/local binding and its linked children.
 Scene moved;assert(moved.replace(50,update()));moved.objects.at(50).faces[0].mesh=first;
 auto child=update(9);child.parent=50;assert(moved.replace(51,std::move(child)));
 assert(moved.replace(52,update()));assert(!moved.objects.count(50));
 assert(moved.objects.at(51).parentRetired&&!moved.objects.at(52).faces[0].mesh);
}
'''.replace('BODIES', bodies))

    def test_actual_terrain_replacement_preserves_gpu_mesh_and_lease(self):
        terrain = method(SOURCE.read_text(encoding='utf-8'),
                         'bool terrain(const std::vector<U8>& data,U8 type)')
        self.compile(r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <limits>
using U8=uint8_t;using U16=uint16_t;using U32=uint32_t;using U64=uint64_t;
constexpr int LL_PCODE_LEGACY_GRASS=95;
namespace VSTerrain {
 struct Patch {unsigned x=0,y=0,size=16;float offset=0;unsigned range=1,quant=1;std::array<int,1024> coefficients{};};
 std::vector<Patch> supplied;bool valid=true;
 bool decode(const std::vector<U8>&,U8,std::vector<Patch>& result,U32,U32){result=supplied;return valid;}
}
struct LLGroupHeader {unsigned stride=0;U8 patch_size=0,layer_type=0;};
struct LLPatchHeader {float dc_offset=0;U16 range=0;U8 quant_wbits=0;unsigned patchids=0;};
unsigned patchExtent=0;
void init_patch_decompressor(unsigned n){patchExtent=n;}
void set_group_of_patch_header(LLGroupHeader*){}
void decompress_patch(float* result,int*,LLPatchHeader* header){
 for(unsigned i=0;i<patchExtent*patchExtent;++i)result[i]=header->dc_offset;
}
struct VSWorldRenderer {using Mesh=std::shared_ptr<int>;};
struct Ground {
 unsigned size=0;VSWorldRenderer::Mesh mesh;bool dirty=true;std::array<float,1024> heights{};
 std::shared_ptr<const VSResourceBudget::Reservation> residency;
};
struct Scene;
struct Budgets {VSResourceBudget terrainBytes{sizeof(Ground)*2};std::map<int,std::weak_ptr<Scene>> regions;};
struct Scene {
 bool retired=false;
 U32 width=256,regionHeight=256;unsigned patchSize=0;std::map<U64,Ground> ground;
 struct Object {int pcode=0;bool dirty=false;};std::map<U32,Object> objects;
 std::shared_ptr<Budgets> budgets=std::make_shared<Budgets>();
TERRAIN
};
int main(){
 Scene scene;VSTerrain::Patch patch;patch.offset=2;VSTerrain::supplied={patch};
 auto neighbor=std::make_shared<Scene>();neighbor->ground[0].dirty=false;
 neighbor->objects[4]={LL_PCODE_LEGACY_GRASS,false};scene.budgets->regions[1]=neighbor;
 auto retired=std::make_shared<Scene>();retired->retired=true;retired->ground[0].dirty=false;
 scene.budgets->regions[2]=retired;
 assert(scene.terrain({},0));assert(scene.ground.size()==1);
 assert(neighbor->ground.at(0).dirty&&neighbor->objects.at(4).dirty&&!retired->ground.at(0).dirty);
 auto& first=scene.ground.at(0);assert(!first.mesh&&first.dirty&&first.heights[0]==2);
 first.mesh=std::make_shared<int>(99);first.dirty=false;
 auto mesh=first.mesh;auto lease=first.residency;
 patch.offset=3;VSTerrain::supplied={patch};
 assert(scene.terrain({},0));
 assert(scene.ground.at(0).mesh==mesh&&scene.ground.at(0).residency==lease);
 assert(scene.ground.at(0).dirty&&scene.ground.at(0).heights[0]==3);
 assert(scene.budgets->terrainBytes.used()==sizeof(Ground));
 patch.x=1;VSTerrain::supplied={patch};assert(scene.terrain({},0));
 assert(!scene.ground.at(U64(16)<<32).mesh&&scene.ground.at(U64(16)<<32).dirty);
 assert(scene.ground.at(0).mesh==mesh&&scene.budgets->terrainBytes.used()==sizeof(Ground)*2);
 // Refused new-patch admission leaves previously admitted content unchanged.
 scene.ground.at(0).dirty=false;patch.x=2;VSTerrain::supplied={patch};
 assert(!scene.terrain({},0)&&scene.ground.size()==2&&!scene.ground.at(0).dirty);
 assert(scene.ground.at(0).mesh==mesh&&scene.ground.at(0).residency==lease);
 patch.x=0;patch.offset=std::numeric_limits<float>::quiet_NaN();VSTerrain::supplied={patch};
 assert(!scene.terrain({},0)&&scene.ground.at(0).heights[0]==3);
 assert(scene.ground.at(0).mesh==mesh&&scene.ground.at(0).residency==lease);
}
'''.replace('TERRAIN', terrain))


if __name__ == '__main__':
    unittest.main()
