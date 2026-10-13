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

    def test_actual_object_replacement_preserves_only_compatible_owner_meshes(self):
        scene = SOURCE.read_text(encoding='utf-8')
        bodies = '\n'.join(method(scene, signature) for signature in (
            'static VSWorldContracts::OwnerKind kind(', 'void eraseObject(',
            'bool replace(U32 id,Object&& object)'))
        self.compile(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <optional>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using S32=int;using U16=uint16_t;
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
struct Params {LLUUID asset;LLUUID getSculptID()const{return asset;}};
struct Motion {int state=12;void accept(int,int,int,int,int,double){++state;}};
struct LLFrameTimer {static double getTotalSeconds(){return 100;}};
namespace VSWorldContracts {enum class OwnerKind {Avatar,ControlAvatar,Attachment,Object};}
struct LLExtendedMeshParams {static constexpr U32 ANIMATED_MESH_ENABLED_FLAG=1;};
struct Mesh {int generation;};
struct Object {
 LLUUID id;U32 parent=0;int pcode=4,attachment=0;
 U64 generation=0,revision=0;U32 extendedMeshFlags=0;bool parentRetired=false,renderable=true,dirty=true;
 std::shared_ptr<const VSResourceBudget::Reservation> slot,primitiveResidency;
 std::shared_ptr<LLVolume> volume;Params params;
 struct Face {std::shared_ptr<Mesh> mesh;};std::vector<Face> faces;
 Motion motion;int position=0,rotation=0,velocity=0,acceleration=0,angularVelocity=0;
 U16 taskSerial=0;bool taskKnown=false;std::string taskFile;
 std::optional<int> taskRequest;double taskDeadline=0;std::map<LLUUID,S32> controlAnimations;
};
struct Budgets {VSResourceBudget objects{32},primitiveBytes{1024};};
struct Scene {
 std::map<U32,Object> objects;std::map<LLUUID,int> avatars;std::map<U32,int> pending;
 std::shared_ptr<Budgets> budgets=std::make_shared<Budgets>();
 U64 nextObjectGeneration=1,nextRevision=1;unsigned requests=0;
 void request(U32){++requests;}void dirtyChildren(U32){}
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
 original.taskKnown=true;original.taskSerial=17;original.taskRequest=4;original.controlAnimations[{21}]=6;
 const auto generation=original.generation,revision=original.revision;
 auto lease=original.slot;
 assert(scene.replace(50,update()));
 const auto& preserved=scene.objects.at(50);
 assert(preserved.faces[0].mesh==first&&preserved.faces[1].mesh==second);
 assert(preserved.generation==generation&&preserved.revision>revision&&preserved.dirty);
 assert(preserved.slot==lease&&preserved.taskKnown&&preserved.taskSerial==17);
 assert(preserved.taskRequest==4&&preserved.controlAnimations.at({21})==6);
 assert(scene.budgets->objects.used()==1);
 // A face-count change preserves matching indices, not nonexistent faces.
 auto fewer=update();fewer.faces.resize(1);assert(scene.replace(50,std::move(fewer)));
 assert(scene.objects.at(50).faces.size()==1&&scene.objects.at(50).faces[0].mesh==first);
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
struct Budgets {VSResourceBudget terrainBytes{sizeof(Ground)*2};};
struct Scene {
 U32 width=256,regionHeight=256;unsigned patchSize=0;std::map<U64,Ground> ground;
 std::shared_ptr<Budgets> budgets=std::make_shared<Budgets>();
TERRAIN
};
int main(){
 Scene scene;VSTerrain::Patch patch;patch.offset=2;VSTerrain::supplied={patch};
 assert(scene.terrain({},0));assert(scene.ground.size()==1);
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
