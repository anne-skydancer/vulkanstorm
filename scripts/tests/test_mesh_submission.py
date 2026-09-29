"""Exercise production color/depth batch submission with GL calls replaced by observers."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'indra/newview/lldrawpool.cpp').read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = r'''
#include <cassert>
#include <vector>
#include <memory>
using U32 = unsigned;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL
constexpr int GL_CULL_FACE = 1;
struct LLGLDisable { explicit LLGLDisable(int) {} };
struct Material { bool mDoubleSided=false; int binds=0; void bind(int){++binds;} };
struct Mat : std::shared_ptr<Material> { using std::shared_ptr<Material>::shared_ptr; bool notNull()const{return bool(*this);} };
struct LLDrawInfo { int id=0; int group=0; bool resident=true; bool direct=false; bool mComputeBatch=false; Mat mGLTFMaterial; int mTexture=0; };
std::vector<int> rendered;
std::vector<size_t> attempts;
bool batch_success=true;
int setups=0,teardowns=0,models=0;
void setup_texture_matrix(LLDrawInfo&){++setups;}
void teardown_texture_matrix(LLDrawInfo&){++teardowns;}
struct LLCullResult {using drawinfo_iterator=LLDrawInfo**;static void increment_iterator(drawinfo_iterator& i,drawinfo_iterator){++i;}};
struct Pipeline {
 std::vector<LLDrawInfo*> list;
 LLDrawInfo** beginRenderMap(U32){return list.data();}
 LLDrawInfo** endRenderMap(U32){return list.data()+list.size();}
} gPipeline;
namespace LLComputeMesh {
 struct Batch {};
 void submitRegistered(LLDrawInfo&,bool){assert(false);}
 bool compatibleBatch(const LLDrawInfo& a,const LLDrawInfo& b){return a.resident&&b.resident&&a.group==b.group;}
 bool drawBatch(const std::vector<LLDrawInfo*>& batch,Batch*){
  if(!batch.front()->resident)return false;
  attempts.push_back(batch.size());
  if(!batch_success)return false;
  for(auto* p:batch)rendered.push_back(p->id);
  return true;
 }
}
namespace LLMeshGeometry {
 bool compatibleBatch(const LLDrawInfo& a,const LLDrawInfo& b){return a.direct&&b.direct&&a.group==b.group;}
 bool drawBatch(const std::vector<LLDrawInfo*>& batch){
  if(!batch.front()->direct)return false;
  attempts.push_back(batch.size());
  if(!batch_success)return false;
  for(auto* p:batch)rendered.push_back(p->id);
  return true;
 }
}
struct LLRenderPass {
 static void pushGLTFBatches(U32,bool);
 static void pushGLTFBatches(U32);
 static void pushUntexturedGLTFBatches(U32);
 static void pushGLTFBatch(LLDrawInfo&,const std::vector<LLDrawInfo*>* =nullptr,LLComputeMesh::Batch* =nullptr);
 static void pushUntexturedGLTFBatch(LLDrawInfo&,const std::vector<LLDrawInfo*>* =nullptr,LLComputeMesh::Batch* =nullptr);
 static void applyModelMatrix(LLDrawInfo&){++models;}
 static void drawGeometry(LLDrawInfo& p){rendered.push_back(p.id);}
};
'''
production = '\n'.join(method(signature) for signature in [
    'void LLRenderPass::pushGLTFBatches(U32 type, bool textured)',
    'void LLRenderPass::pushGLTFBatches(U32 type)',
    'void LLRenderPass::pushUntexturedGLTFBatches(U32 type)',
    'void LLRenderPass::pushGLTFBatch(LLDrawInfo& params,',
    'void LLRenderPass::pushUntexturedGLTFBatch(LLDrawInfo& params,',
])
tests = r'''
int main(){
 Mat material(new Material);
 std::vector<LLDrawInfo> records(520);
 for(size_t i=0;i<records.size();++i){
  records[i].id=int(i);records[i].mGLTFMaterial=material;
  gPipeline.list.push_back(&records[i]);
 }
 // A nonresident record and a material boundary must preserve submission order.
 records[258].resident=false;
 for(size_t i=260;i<records.size();++i)records[i].group=1;
 for(bool direct:{false,true})for(bool textured:{false,true})for(bool success:{false,true}){
  for(size_t i=0;i<records.size();++i){records[i].resident=!direct&&i!=258;records[i].direct=direct&&i!=258;}
  batch_success=success;rendered.clear();attempts.clear();
  setups=teardowns=models=material->binds=0;
  if(textured)LLRenderPass::pushGLTFBatches(0);
  else LLRenderPass::pushUntexturedGLTFBatches(0);
  assert(rendered.size()==records.size());
  for(size_t i=0;i<rendered.size();++i)assert(rendered[i]==int(i));
  assert((attempts==std::vector<size_t>{256,2,256,4}));
  assert(models==6);
  assert(setups==(textured?6:0)&&teardowns==setups&&material->binds==setups);
 }
 gPipeline.list.clear();rendered.clear();attempts.clear();
 LLRenderPass::pushGLTFBatches(0);LLRenderPass::pushUntexturedGLTFBatches(0);
 assert(rendered.empty()&&attempts.empty());
}
'''
with tempfile.TemporaryDirectory(prefix='mesh-submission-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture + production + tests)
    (path / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(mesh_submission LANGUAGES CXX)\n'
        'add_executable(mesh_submission test.cpp)\ntarget_compile_features(mesh_submission PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(path / 'build'), '--config', 'Debug'], check=True)
    executable = path / 'build' / ('Debug/mesh_submission.exe' if sys.platform == 'win32' else 'mesh_submission')
    subprocess.run([str(executable)], check=True)
