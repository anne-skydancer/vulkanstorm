"""Run actual terrain admission/commit methods with a refusing GPU adapter."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]

class TerrainPublication(unittest.TestCase):
    def test_pending_detail_texture_keeps_received_ground_visible(self):
        body=method((ROOT/'indra/newview/vsworldscene.cpp').read_text(), 'void surfaces(')
        fixture=r'''
#include "vsmaterial.h"
#include "vsresourcebudget.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
using U32=unsigned;using U64=unsigned long long;using S32=int;using F32=float;
template<class T> T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
struct Origin{double mdV[3]{};};Origin from_region_handle(U64){return {};}
struct RegionLimits{bool water=true;bool getAllowRenderWater(){return water;}}limits;RegionLimits& vs_region_limits(){return limits;}
struct Settings{unsigned skyDetail=16;float getF32(const char*){return 16;}bool getBOOL(const char*){return false;}
 unsigned getU32(const char*){return skyDetail;}}gSavedSettings;
struct LLUUID {int id=0;bool notNull()const{return id!=0;}bool isNull()const{return id==0;}std::string asString()const{return std::to_string(id);}};
LLUUID vs_native_image_id(LLUUID id){return id;}
std::vector<int> requestedImages;
void vs_request_world_image(LLUUID id,unsigned){requestedImages.push_back(id.id);}
struct VSUIRenderer{using Image=std::shared_ptr<int>;};
struct LLVector3 {float mV[3]{};LLVector3()=default;LLVector3(float a,float b,float c):mV{a,b,c}{}
 LLVector3 operator-(const LLVector3& v)const{return {mV[0]-v.mV[0],mV[1]-v.mV[1],mV[2]-v.mV[2]};}
 float lengthSquared()const{return mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2];}float length()const{return std::sqrt(lengthSquared());}};
struct LLVector2 {float mV[2]{};};
// Reference dome topology is covered elsewhere; this adapter supplies one strip per stack.
template<class Vertices,class TexCoords,class Indices>
void vs_sky_strips(U32 begin,U32 end,Vertices& vertices,TexCoords& coordinates,Indices& indices,F32 radius,const U32& slices,const U32&){
 for(U32 i=begin;i<=end;++i)for(U32 j=0;j<slices;++j){*vertices++=LLVector3(radius,F32(i),F32(j));*coordinates++=LLVector2{};}
 *indices++=0;for(U32 i=1;i<=end-begin;++i){*indices++=i*slices;for(U32 j=0;j<slices;++j){*indices++=(i-1)*slices+j;*indices++=i*slices+j;}*indices++=i*slices;}
}
struct Sky {float radius=15000;LLUUID rainbow{11},halo{12};
 float getDomeRadius()const{return radius;}LLUUID getRainbowTextureId()const{return rainbow;}LLUUID getHaloTextureId()const{return halo;}};
struct LLEnvironment {static inline bool exists=false;std::shared_ptr<Sky> sky=std::make_shared<Sky>();
 static bool instanceExists(){return exists;}static LLEnvironment& instance(){static LLEnvironment value;return value;}
 std::shared_ptr<Sky> getCurrentSky()const{return sky;}};
struct LLPipeline {enum {RENDER_TYPE_SKY,RENDER_TYPE_WL_SKY,RENDER_TYPE_TERRAIN,RENDER_TYPE_WATER};
 std::set<int> hidden;bool hasRenderType(int type)const{return !hidden.count(type);}} gPipeline;
struct VSWorldRenderer {
 using Mesh=std::shared_ptr<int>;
 struct Vertex{float x,y,z,nx,ny,nz,u,v;};
 struct Packet{Mesh mesh;std::shared_ptr<int> image;bool background=false,fullbright=false,terrain=false,alpha=false;
 std::array<std::shared_ptr<int>,4> channels{};std::shared_ptr<const VSMaterial::State> surface;
 std::shared_ptr<const int> terrainMaterial;
 U64 terrainKey=0,objectGeneration=0;std::array<float,4> color{1,1,1,1};float distanceSquared=0;};
 Mesh upload(const std::vector<Vertex>&,const std::vector<unsigned>&){return std::make_shared<int>(80);}
 unsigned domes=0;bool refuse=false;
 bool publishMesh(Mesh& current,const std::vector<Vertex>& vertices,const std::vector<unsigned>& indices){
  assert(!vertices.empty()&&!indices.empty()&&indices.size()%3==0);if(refuse)return false;
  current=std::make_shared<int>(int(++domes));return true;}
};
struct Budgets {VSResourceBudget terrainBytes{64*1024*1024};};
struct Resources {
 std::map<std::string,std::shared_ptr<int>> images;std::shared_ptr<int> fallback=std::make_shared<int>(99);
 std::shared_ptr<int> image(const std::string& k){return images[k];}
 std::shared_ptr<int> defaultWorldImage(){return fallback;}
};
struct Scene {
 VSWorldRenderer renderer;Resources resources;VSWorldRenderer::Mesh sky,water;
 std::shared_ptr<Budgets> budgets=std::make_shared<Budgets>();unsigned skyDetail=0;float skyRadius=0;
 struct Ground {VSWorldRenderer::Mesh mesh;unsigned size=16;};std::map<U64,Ground> ground;
 std::array<float,4> terrainStarts{},terrainRanges{1,1,1,1};std::array<LLUUID,4> terrainTextures{{{1},{2},{3},{4}}};
 std::shared_ptr<int> terrainRamp(){return std::make_shared<int>(88);}
 std::shared_ptr<const int> terrainMaterial(unsigned){return {};}
 U64 handle=0,incarnation=7;bool waterKnown=false;float waterHeight=0;unsigned width=256,regionHeight=256;
 float height(int,int,float){return 0;}
BODY
};
int main(){Scene s;auto mesh=std::make_shared<int>(1);s.ground[0]={mesh,16};
 std::vector<VSWorldRenderer::Packet> packets;s.surfaces(packets,{8,8,16});
 assert(packets.size()==2 && packets[1].terrain && packets[1].mesh==mesh && packets[1].image==s.resources.fallback);
 auto material=std::make_shared<int>(123);s.resources.images["remote:1"]=material;
 packets.clear();s.surfaces(packets,{8,8,16});assert(packets.size()==2 && packets[1].image==material && packets[1].mesh==mesh);
 auto retained=packets[1];s.resources.images.erase("remote:1");packets.clear();s.surfaces(packets,{8,8,16});
 assert(packets.size()==2 && packets[1].image==s.resources.fallback && retained.image==material);
 s.resources.fallback.reset();packets.clear();s.surfaces(packets,{8,8,16});assert(packets.size()==2 && packets[1].mesh==mesh);
 for(unsigned c=0;c<3;++c)assert(std::abs(packets[1].color[c]-127.f/255)<1e-6f);
 s.waterKnown=true;packets.clear();s.surfaces(packets,{8,8,16});assert(packets.size()==3);
 limits.water=false;packets.clear();s.surfaces(packets,{8,8,16});assert(packets.size()==2);
 s.waterKnown=false;
 gPipeline.hidden.insert(LLPipeline::RENDER_TYPE_TERRAIN);packets.clear();s.surfaces(packets,{8,8,16});
 assert(packets.size()==1&&packets[0].background);gPipeline.hidden.clear();
 // The current environment sky replaces the startup quad with an admitted dome.
 LLEnvironment::exists=true;auto quad=s.sky;packets.clear();s.surfaces(packets,{8,8,16});
 assert(packets.size()==2&&packets[0].background&&packets[0].mesh!=quad&&s.renderer.domes==1);
 assert(s.skyDetail==16&&s.skyRadius==15000&&s.budgets->terrainBytes.used()==0);
 s.resources.images["remote:11"]=std::make_shared<int>(211);packets.clear();s.surfaces(packets,{8,8,16});
 assert(s.renderer.domes==1&&*packets[0].image==211&&!packets[0].channels[0]);
 gSavedSettings.skyDetail=32;s.renderer.refuse=true;auto dome=s.sky;packets.clear();s.surfaces(packets,{8,8,16});
 assert(s.sky==dome&&s.skyDetail==16&&packets[0].mesh==dome);s.renderer.refuse=false;
 LLEnvironment::exists=false;packets.clear();s.surfaces(packets,{8,8,16});
 assert(s.skyDetail==0&&packets[0].mesh&&packets[0].mesh!=dome);
 s.ground[0].mesh.reset();packets.clear();s.surfaces(packets,{8,8,16});assert(packets.size()==1); // Never invent missing heights/geometry.
}
'''.replace('BODY',body)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'terrain_surface.cpp';binary=path.with_suffix('.exe');path.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I'+str(ROOT/'indra/newview'),str(path),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_terrain_evidence_rejects_missing_and_wrong_pixels(self):
        import sys
        sys.path.insert(0,str(ROOT/'scripts'))
        from run_vulkan_session_replay import terrain_contracts_passed
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'terrain').mkdir()
            record={'terrain_publication':{'complete':True}}
            for phase,color in (('pending',(127,127,127,255)),('published',(32,160,64,255)),('retired',(127,127,127,255))):
                name='terrain/'+phase+'.rgba';(root/name).write_bytes(bytes(color)*64*64)
                record['terrain_publication'][phase]={'file':name,'draw_packets':1,'pixels_verified':True}
            (root/'terrain/detail.j2c').write_bytes(b'fixture')
            self.assertTrue(terrain_contracts_passed(record,root))
            path=root/'terrain/published.rgba';data=path.read_bytes();path.write_bytes(bytes([255])+data[1:])
            self.assertFalse(terrain_contracts_passed(record,root));path.write_bytes(data);path.unlink()
            self.assertFalse(terrain_contracts_passed(record,root))

    def test_entire_region_frame_survives_partial_gpu_refusal(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
        prepare=method(source,'bool prepareGround(')
        commit=method(source,'void VSWorldScene::publishGroundFrame(').replace('VSWorldScene::','Scene::')
        fixture=r"""
#include "vsresourcebudget.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using F32=float;using S32=int;
template<class T> T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}int lltrunc(float v){return int(v);}
struct LLVector3 {
 float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}
 explicit LLVector3(const float* p):mV{p[0],p[1],p[2]}{}
 LLVector3 operator-(const LLVector3& b)const{return {mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}
 float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);}
};
struct LLVector2{float mV[2]{};LLVector2()=default;LLVector2(float x,float y):mV{x,y}{};static const LLVector2 zero;};const LLVector2 LLVector2::zero{};
struct Settings{bool dynamic=true;float getF32(const char*)const{return 1;}bool getBOOL(const char*)const{return dynamic;}}gSavedSettings;
struct VSWorldRenderer {
 enum class Topology{Triangles};
 struct Vertex{float x,y,z,nx,ny,nz,u,v,u1,v1;};using Mesh=std::shared_ptr<unsigned>;
 unsigned attempts=0,next=10,refuse=0;
 bool publishMesh(Mesh& current,const std::vector<Vertex>&,const std::vector<U32>&,Topology,unsigned){
  if(++attempts==refuse)return false;current=std::make_shared<unsigned>(next++);return true;}
};
struct VSTerrainGeometry {
 struct Data{std::vector<LLVector3>vertices,normals;std::vector<LLVector2>uv,detail;std::vector<U32>indices;};Data data;
 template<class F> VSTerrainGeometry(U32,U32,U32,U32,F sample){
  LLVector3 p,n;LLVector2 uv,d;sample(0,0,0,&p,&n,&uv,&d);
  data.vertices={p};data.normals={n};data.uv={uv};data.detail={d};data.indices={0,0,0};}
 Data mesh()const{return data;}
};
struct Ground {
 unsigned size=16;VSWorldRenderer::Mesh mesh;bool dirty=true;std::array<float,1024>heights{};
 std::shared_ptr<const VSResourceBudget::Reservation>residency;
 unsigned stride=1,north=1,east=1;
};
struct Budgets{VSResourceBudget terrainBytes{2*1024*1024};};
struct Origin{double mdV[3]{};};Origin from_region_handle(U64){return {};}
namespace VSTerrainMaterial{std::array<float,2> coordinates(double,double,float,float,float,float,std::array<float,4>,std::array<float,4>){return {};}}
namespace VSWorldContracts{enum{Position=1,Normal=2,UV0=4,UV1=8};}
struct Impl {
 std::array<float,4> terrainStarts{},terrainRanges{1,1,1,1};
 bool retired=false;U64 handle=0;U32 width=256,regionHeight=256;
 struct GroundPublication{VSWorldRenderer::Mesh mesh;unsigned stride=1,north=1,east=1;};
 std::map<U64,Ground>ground;std::optional<std::map<U64,GroundPublication>>groundCandidate;
 std::shared_ptr<const VSResourceBudget::Reservation>groundCandidateResidency;
 std::shared_ptr<Budgets>budgets=std::make_shared<Budgets>();VSWorldRenderer&renderer;
 explicit Impl(VSWorldRenderer& r):renderer(r){}
 bool edgePeak=false;
 float height(int x,int y,float value){return edgePeak&&x==32&&y==32?1000.f:value;}
 LLVector3 terrainNormal(int,int,float){return {0,0,1};}
PREPARE
};
namespace VSWorldContracts {std::array<float,3> regionOffset(U64 a,U64 b){return {float(int(b)-int(a)),0,0};}}
struct Scene {
 bool mRoot=true;std::shared_ptr<Impl>mImpl;std::map<int,std::unique_ptr<Scene>>mNeighbours;
 explicit Scene(VSWorldRenderer& r):mImpl(std::make_shared<Impl>(r)){}
 void publishGroundFrame(const LLVector3&);
};
COMMIT
int main(){
 VSWorldRenderer gpu;Scene scene(gpu);scene.mImpl->ground[0]=Ground{};
 scene.mNeighbours[1]=std::make_unique<Scene>(gpu);auto& neighbor=*scene.mNeighbours[1];
 neighbor.mRoot=false;neighbor.mImpl->handle=256;neighbor.mImpl->ground[0]=Ground{};
 LLVector3 eye(10,10,10);scene.publishGroundFrame(eye);
 const auto first=scene.mImpl->ground.at(0).mesh,second=neighbor.mImpl->ground.at(0).mesh;
 assert(first&&second&&!scene.mImpl->ground.at(0).dirty&&!neighbor.mImpl->ground.at(0).dirty);
 const auto attempts=gpu.attempts;scene.publishGroundFrame(eye);assert(gpu.attempts==attempts);
 scene.mImpl->ground.at(0).dirty=true;neighbor.mImpl->ground.at(0).dirty=true;
 gpu.refuse=gpu.attempts+2;scene.publishGroundFrame(eye);
 assert(scene.mImpl->ground.at(0).mesh==first&&neighbor.mImpl->ground.at(0).mesh==second);
 assert(scene.mImpl->ground.at(0).dirty&&neighbor.mImpl->ground.at(0).dirty);
 assert(!scene.mImpl->groundCandidate&&!neighbor.mImpl->groundCandidate);
 assert(scene.mImpl->budgets->terrainBytes.used()==0&&neighbor.mImpl->budgets->terrainBytes.used()==0);
 gpu.refuse=0;scene.publishGroundFrame(eye);
 assert(scene.mImpl->ground.at(0).mesh!=first&&neighbor.mImpl->ground.at(0).mesh!=second);
 assert(!scene.mImpl->ground.at(0).dirty&&!neighbor.mImpl->ground.at(0).dirty);
 // Refused temporary CPU admission also leaves the entire frame untouched.
 auto pressure=scene.mImpl->budgets->terrainBytes.reserve(2*1024*1024);assert(pressure);
 const auto admitted=scene.mImpl->ground.at(0).mesh;scene.mImpl->ground.at(0).dirty=true;
 scene.publishGroundFrame(eye);assert(scene.mImpl->ground.at(0).mesh==admitted);
 pressure.reset();neighbor.mImpl->retired=true;neighbor.mImpl->ground.at(0).dirty=true;
 const auto retired=neighbor.mImpl->ground.at(0).mesh;scene.publishGroundFrame(eye);
 assert(scene.mImpl->ground.at(0).mesh!=admitted&&neighbor.mImpl->ground.at(0).mesh==retired);
 assert(neighbor.mImpl->ground.at(0).dirty);
 // Interior patch LOD uses the north/east shared samples in its radius.
 Scene detail(gpu);const U64 key=(U64(16)<<32)|16;detail.mImpl->ground[key]=Ground{};
 detail.publishGroundFrame({200,24,0});assert(detail.mImpl->ground.at(key).stride==16);
 detail.mImpl->edgePeak=true;detail.mImpl->ground.at(key).dirty=true;
 detail.publishGroundFrame({200,24,0});assert(detail.mImpl->ground.at(key).stride==4);
 // Disabling adaptive LOD requests full detail rather than distance reduction.
 gSavedSettings.dynamic=false;detail.publishGroundFrame({200,24,0});
 assert(detail.mImpl->ground.at(key).stride==1);
}
""".replace('PREPARE',prepare).replace('COMMIT',commit)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'terrain.cpp';binary=path.with_suffix('.exe');path.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(ROOT/'indra/newview'),
                                  str(path),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__':unittest.main()
