"""Run actual terrain admission/commit methods with a refusing GPU adapter."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]

class TerrainPublication(unittest.TestCase):
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
 struct Vertex{float x,y,z,nx,ny,nz,u,v;};using Mesh=std::shared_ptr<unsigned>;
 unsigned attempts=0,next=10,refuse=0;
 bool publishMesh(Mesh& current,const std::vector<Vertex>&,const std::vector<U32>&){
  if(++attempts==refuse)return false;current=std::make_shared<unsigned>(next++);return true;}
};
struct VSTerrainGeometry {
 struct Data{std::vector<LLVector3>vertices,normals;std::vector<LLVector2>uv;std::vector<U32>indices;};Data data;
 template<class F> VSTerrainGeometry(U32,U32,U32,U32,F sample){
  LLVector3 p,n;LLVector2 uv,d;sample(0,0,0,&p,&n,&uv,&d);
  data.vertices={p};data.normals={n};data.uv={uv};data.indices={0,0,0};}
 Data mesh()const{return data;}
};
struct Ground {
 unsigned size=16;VSWorldRenderer::Mesh mesh;bool dirty=true;std::array<float,1024>heights{};
 std::shared_ptr<const VSResourceBudget::Reservation>residency;
 unsigned stride=1,north=1,east=1;
};
struct Budgets{VSResourceBudget terrainBytes{2*1024*1024};};
struct Impl {
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
