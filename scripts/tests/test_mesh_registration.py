"""Test production mesh packet caching, invalidation and cross-view ownership."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'indra/newview/llcomputelod.cpp').read_text()
def method(signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
fixture=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using GLuint=unsigned;
template<class T>T llmin(T a,T b){return std::min(a,b);}
template<class T>struct LLPointer:std::shared_ptr<T>{using std::shared_ptr<T>::shared_ptr;LLPointer(T* p):std::shared_ptr<T>(p){};LLPointer()=default;operator T*()const{return this->get();}};
class LLDrawInfo;
namespace LLComputeMesh {struct Submission;
'''
fixture+=method('struct Batch')+';\n'+method('struct Submission')+';\n'
fixture+=r'''
struct Resident {bool valid=true;U32 slot=0,page=1;std::vector<std::weak_ptr<Submission>> submissions;};
}
class LLDrawInfo{
public:
 U32 mStart=0,mEnd=2,mCount=3,mOffset=0;int mTexture=0,mVertexBuffer=0,mGLTFMaterial=0;bool mFullbright=false;unsigned char mBump=0;
 void* mModelMatrix=nullptr;void* mTextureMatrix=nullptr;
 int id=0;std::shared_ptr<LLComputeMesh::Resident> mComputeLOD;
 std::shared_ptr<LLComputeMesh::Batch> mComputeBatch;
 LLDrawInfo(U32 start,U32 end,U32 count,U32 offset,int texture,int buffer,bool fullbright,unsigned char bump):mStart(start),mEnd(end),mCount(count),mOffset(offset),mTexture(texture),mVertexBuffer(buffer),mFullbright(fullbright),mBump(bump){}
};
struct LLSpatialGroup{std::map<U32,std::vector<LLPointer<LLDrawInfo>>> mDrawMap;std::shared_ptr<LLComputeMesh::Submission> mComputeSubmission;};
struct LLCullResult{
 std::vector<std::shared_ptr<LLComputeMesh::Submission>> retained;std::vector<LLDrawInfo*> draws;
 void retainMeshSubmission(const std::shared_ptr<LLComputeMesh::Submission>& p){retained.push_back(p);}
 void pushDrawInfo(U32,LLDrawInfo* p){draws.push_back(p);}
 void clear(){draws.clear();retained.clear();}
};
struct LLPipeline{static inline bool sShadowRender=false,sReflectionRender=false;};
bool gCubeSnapshot=false,available=true;int checks=0,fast=0;U64 counted=0;
struct Pipeline{void addTrianglesDrawn(int count){counted+=count;}}gPipeline;
bool enabled(){return available;}constexpr U32 BATCH_CAPACITY=256;
U32 generation=1;std::set<GLuint> registered_buffers;int deleted=0;void glDeleteBuffers(int n,const GLuint*){deleted+=n;}
constexpr int GL_SHADER_STORAGE_BUFFER=1,GL_STATIC_DRAW=2,GL_NO_ERROR=0;
unsigned next_buffer=10;int allocations=0,gl_error=0;std::vector<U32> uploaded_slots;
void glGenBuffers(int,unsigned* value){*value=next_buffer++;}
void glBindBuffer(int,unsigned){}
void glBufferData(int,size_t bytes,const void* data,int){++allocations;const auto* slots=static_cast<const U32*>(data);uploaded_slots.assign(slots,slots+bytes/4);}
int glGetError(){int error=gl_error;gl_error=0;return error;}
std::vector<int> rendered;
struct LLRenderPass{
 enum{PASS_GLTF_PBR=1};
 static void pushGLTFBatch(LLDrawInfo& p,const std::vector<LLDrawInfo*>* batch=nullptr,LLComputeMesh::Batch* registration=nullptr){
  if(batch){assert(registration);++fast;for(auto* member:*batch)rendered.push_back(member->id);}else rendered.push_back(p.id);
 }
 static void pushUntexturedGLTFBatch(LLDrawInfo& p,const std::vector<LLDrawInfo*>* batch=nullptr,LLComputeMesh::Batch* registration=nullptr){pushGLTFBatch(p,batch,registration);}
};
namespace LLComputeMesh{
 bool compatibleBatch(const LLDrawInfo& a,const LLDrawInfo& b){++checks;return a.mComputeLOD&&b.mComputeLOD&&a.mComputeLOD->valid&&b.mComputeLOD->valid&&a.mComputeLOD->page==b.mComputeLOD->page&&a.mGLTFMaterial==b.mGLTFMaterial;}
 bool appendSubmission(LLSpatialGroup&,LLCullResult&,bool=true);void submitRegistered(LLDrawInfo&,bool);
}
'''
production='\n'.join(method(s) for s in ['void invalidateSubmissions(', 'LLComputeMesh::Batch::~Batch()', 'GLuint registeredCandidates(', 'bool LLComputeMesh::appendSubmission(', 'void LLComputeMesh::submitRegistered('])
tests=r'''
int main(){
 LLSpatialGroup group;auto& source=group.mDrawMap[LLRenderPass::PASS_GLTF_PBR];
 for(int i=0;i<520;++i){LLPointer<LLDrawInfo> p=new LLDrawInfo(0,2,3,0,0,0,false,0);p->id=i;p->mGLTFMaterial=i<260?0:1;p->mComputeLOD=std::make_shared<LLComputeMesh::Resident>();p->mComputeLOD->slot=i;source.push_back(p);}
 source[258]->mComputeLOD->valid=false;
 LLCullResult world,shadow,next;
 assert(LLComputeMesh::appendSubmission(group,world));assert(world.draws.size()==6&&counted==1560);
 const int first_checks=checks;auto old=group.mComputeSubmission;std::weak_ptr<LLComputeMesh::Submission> previous=old;
 LLPipeline::sShadowRender=true;assert(LLComputeMesh::appendSubmission(group,shadow,false));LLPipeline::sShadowRender=false;
 assert(checks==first_checks&&counted==1560&&world.draws==shadow.draws); // no per-face reclassification
 for(bool textured:{false,true}){
  rendered.clear();fast=0;
  for(auto* p:world.draws){if(p->mComputeBatch)LLComputeMesh::submitRegistered(*p,textured);else LLRenderPass::pushGLTFBatch(*p);}
  assert(rendered.size()==520&&fast==4);for(int i=0;i<520;++i)assert(rendered[i]==i);
 }
 auto changed=source[10]->mComputeLOD;invalidateSubmissions(*changed);changed->page=2;assert(old->dirty);
 rendered.clear();fast=0;LLComputeMesh::submitRegistered(*world.draws.front(),true);assert(fast==0&&rendered.size()==256);
 assert(LLComputeMesh::appendSubmission(group,next));assert(checks>first_checks&&group.mComputeSubmission!=old&&next.draws.size()>6);
 old.reset();assert(!previous.expired());world.clear();assert(!previous.expired());shadow.clear();assert(previous.expired());
 // A view owns its synthetic packets after group replacement/destruction.
 auto* retained=next.draws.front();group.mComputeSubmission.reset();group.mDrawMap.clear();assert(retained==next.draws.front());
 rendered.clear();if(retained->mComputeBatch)LLComputeMesh::submitRegistered(*retained,false);else LLRenderPass::pushGLTFBatch(*retained);assert(!rendered.empty());
 next.clear();
 // Groups without a multi-record packet retain the original render-map path,
 // but a residency publication still wakes their cached classification.
 auto& small=group.mDrawMap[LLRenderPass::PASS_GLTF_PBR];
 for(int i=0;i<2;++i){LLPointer<LLDrawInfo> p=new LLDrawInfo(0,2,3,0,0,0,false,0);p->mComputeLOD=std::make_shared<LLComputeMesh::Resident>();p->mComputeLOD->page=i+1;small.push_back(p);}
 assert(!LLComputeMesh::appendSubmission(group,world));const int no_packet_checks=checks;
 assert(!LLComputeMesh::appendSubmission(group,world)&&checks==no_packet_checks);
 invalidateSubmissions(*small[1]->mComputeLOD);small[1]->mComputeLOD->page=1;
 assert(LLComputeMesh::appendSubmission(group,world)&&world.draws.size()==1);
 world.clear();group.mComputeSubmission.reset();group.mDrawMap.clear();
 available=false;assert(!LLComputeMesh::appendSubmission(group,world));
 {LLComputeMesh::Batch batch;batch.candidates=7;batch.generation=1;registered_buffers.insert(7);}assert(deleted==1&&registered_buffers.empty());
 {LLComputeMesh::Batch batch;batch.candidates=8;batch.generation=1;++generation;}assert(deleted==1); // context reset already retired names
 {LLComputeMesh::Batch batch;batch.slots={3,9,5};
  auto buffer=registeredCandidates(batch);assert(buffer&&allocations==1&&uploaded_slots==batch.slots);
  for(int view=0;view<12;++view)assert(registeredCandidates(batch)==buffer);
  assert(allocations==1); // no per-view candidate upload
  registered_buffers.clear();++generation; // GL teardown owns retirement of old names
  assert(registeredCandidates(batch)!=buffer&&allocations==2);
 }
 assert(deleted==2&&registered_buffers.empty());
 {LLComputeMesh::Batch batch;batch.slots={1,2};gl_error=1;
  assert(!registeredCandidates(batch)&&batch.failed&&registered_buffers.empty());
  assert(!registeredCandidates(batch)&&allocations==3); // failure does not allocate every frame
 }
 assert(deleted==3);
}
'''
with tempfile.TemporaryDirectory(prefix='mesh-registration-') as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(fixture+production+tests)
    (path/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(mesh_registration LANGUAGES CXX)\nadd_executable(mesh_registration test.cpp)\ntarget_compile_features(mesh_registration PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake','-S',str(path),'-B',str(path/'build')],check=True)
    subprocess.run(['cmake','--build',str(path/'build'),'--config','Debug'],check=True)
    executable=path/'build'/('Debug/mesh_registration.exe' if sys.platform=='win32' else 'mesh_registration')
    subprocess.run([str(executable)],check=True)
