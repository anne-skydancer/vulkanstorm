"""Compile the production mesh producer; test cache, queue and reload ownership.
GPU attribute values are independently checked by perf/test_mesh_geometry.py.
"""
from pathlib import Path
import ast
import re
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
particle=ast.parse((ROOT/'scripts/tests/test_particle_compute.py').read_text())
fixture=next(ast.literal_eval(n.value) for n in particle.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='fixture' for t in n.targets))
fixture=fixture[:fixture.index('struct LLVector3')]
fixture=fixture.replace('void glBufferData(U32,size_t size,const void* data,U32){assert(flushed);','void glBufferData(U32,size_t size,const void* data,U32){')
fixture=fixture.replace('assert(x==(count+63)/64 && y==1 && z==1);','assert(x>0 && y==1 && z==1);')
fixture+=r'''
#include <map>
#include <memory>
#include <tuple>
using U16=uint16_t;
#define llassert(x) assert(x)
constexpr U32 GL_STATIC_DRAW=12,GL_FALSE=0,GL_ELEMENT_ARRAY_BARRIER_BIT=8;
void glBufferSubData(U32,size_t,size_t,const void*){}
void glUniformMatrix4fv(int,int,U8,const float*){}
void glUniform4uiv(int,int,const U32*){}
void glUniform4fv(int,int,const float*){}
struct LLVector4a{float v[4]={};};
struct LLMatrix4a {
 float v[16]={};
 void setIdentity(){for(int i=0;i<16;++i)v[i]=i%5==0?1.f:0.f;}
 const float* getF32ptr()const{return v;}
};
template<class T>struct LLPointer {
 T* p=nullptr;LLPointer()=default;LLPointer(T* value):p(value){}
 T* get(){return p;}bool operator==(const LLPointer& o)const{return p==o.p;}
 T* operator->(){return p;}const T* operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}
};
struct LLVolumeFace {
 S32 mNumVertices=65,mNumIndices=6;bool mWeightsScrubbed=false;
 LLVector4a storage[65];U16 index[6]={0,1,2,2,3,0};
 LLVector4a* mPositions=storage;LLVector4a* mNormals=storage;LLVector4a* mTangents=storage;
 LLVector4a* mWeights=storage;void* mTexCoords=storage;U16* mIndices=index;
};
struct LLVolume {
 LLVolumeFace face;U64 revision=1;
 U64 getGeometryRevision()const{return revision;}S32 getNumVolumeFaces()const{return 1;}
 const LLVolumeFace& getVolumeFace(S32)const{return face;}
};
struct LLVertexBuffer {
 enum AttributeType{TYPE_VERTEX,TYPE_NORMAL,TYPE_TEXCOORD0,TYPE_TEXCOORD1,TYPE_TEXCOORD2,
  TYPE_TEXCOORD3,TYPE_COLOR,TYPE_EMISSIVE,TYPE_TANGENT,TYPE_WEIGHT,TYPE_WEIGHT4,TYPE_CLOTHWEIGHT,TYPE_JOINT,TYPE_TEXTURE_INDEX};
 static inline U32 sTypeSize[TYPE_TEXTURE_INDEX]={16,16,8,8,8,8,4,4,16,4,16,16,8};
 U32 verts=512,indices=1024,handle=100,index_handle=101;
 void(*pending)()=nullptr;
 U32 getNumVerts()const{return verts;}U32 getNumIndices()const{return indices;}
 U32 getGLBuffer()const{return handle;}U32 getGLIndices()const{return index_handle;}
 U32 getOffset(AttributeType t)const{return U32(t)*8192;}U32 getTypeMask()const{return 0x1fff;}
 bool hasDataType(AttributeType t)const{return (getTypeMask()&(1u<<t))!=0;}
 void setBeforeBind(void(*p)()){pending=p;}
 void unmapBuffer(){flushed=true;}
 void setBuffer(){if(pending)pending();}
 void drawIndirect(U32,U32,U32 n){assert(n>=2);}
};
struct LLRender {enum{TRIANGLES};};
constexpr U32 GL_DRAW_INDIRECT_BUFFER_BINDING=701,GL_DRAW_INDIRECT_BUFFER=702,GL_COMMAND_BARRIER_BIT=16;

struct Resident{bool valid=true;};
struct LLDrawInfo {
 bool mMeshGeometry=true;LLPointer<LLVertexBuffer> mVertexBuffer;
 Resident* mComputeLOD=nullptr;void* mComputeBatch=nullptr;
 void *mModelMatrix=nullptr,*mTextureMatrix=nullptr,*mNormalMapMatrix=nullptr,*mSpecularMapMatrix=nullptr;
 void *mTexture=nullptr,*mNormalMap=nullptr,*mSpecularMap=nullptr,*mGLTFMaterial=nullptr,*mMaterial=nullptr;
 void *mAvatar=nullptr,*mSkinInfo=nullptr;
 std::vector<int> mTextureList;
 U32 mMaterialID=0,mShaderMask=0,mSpecColor=0,mEnvIntensity=0,mAlphaMaskCutoff=0,mDiffuseAlphaMode=0;
 U32 mBlendFuncSrc=0,mBlendFuncDst=0,mBump=0,mShiny=0,mFullbright=0,mHasGlow=0,mCount=3,mOffset=0;
};
struct GLManager {float mGLVersion=4.3f;bool mIsDisabled=false;} gGLManager;
struct Render {void flush(){flushed=true;}}gGL;
constexpr int LL_PATH_APP_SETTINGS=0;
struct Dir {std::string getExpandedFilename(int,const char*,const char*){return SHADER_PATH;}}dir;
Dir* gDirUtilp=&dir;
'''
header=re.sub(r'^#(?:include|pragma).*$', '', (ROOT/'indra/newview/llmeshgeometry.h').read_text(),flags=re.M)
source=re.sub(r'^#include "[^\n]+\n', '', (ROOT/'indra/newview/llmeshgeometry.cpp').read_text(),flags=re.M)
tests=r'''
int main(){
 LLVolume volume;LLVertexBuffer buffer;LLMatrix4a matrix;matrix.setIdentity();
 assert(LLMeshGeometry::initGL());
 const auto start_buffers=next_buffer;
 assert(LLMeshGeometry::generate(buffer,volume,0,4,65,68,7,matrix,matrix,true,true));
 assert(next_buffer==start_buffers+1&&buffer.pending&&dispatches==0);
 assert(bound_shader==99&&generic_binding==98&&indexed_binding[0]==12&&range_start[0]==32&&range_size[0]==64);
 // A new transform reuses immutable source storage, including queued users.
 matrix.v[12]=12;
 assert(LLMeshGeometry::generate(buffer,volume,0,4,65,68,7,matrix,matrix,true,true));
 assert(next_buffer==start_buffers+1);
 buffer.pending();assert(dispatches==2&&!buffer.pending);
 assert(bound_shader==99&&generic_binding==98&&indexed_binding[0]==12&&range_start[0]==32&&range_size[0]==64);
 assert((barriers&(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT|GL_ELEMENT_ARRAY_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT))==11);
 assert(!LLMeshGeometry::generate(buffer,volume,0,500,65,68,0,matrix,matrix,true,true));
 assert(!LLMeshGeometry::generate(buffer,volume,0,0,66,68,0,matrix,matrix,true,true));
 assert(!LLMeshGeometry::generate(buffer,volume,0,0,65,64,0,matrix,matrix,true,true));
 assert(!LLMeshGeometry::generate(buffer,volume,1,0,1,1,0,matrix,matrix,true,true));
 assert(!buffer.pending&&dispatches==2);
 ++volume.revision;
 assert(LLMeshGeometry::indices(buffer,volume,0,1,6,3));
 assert(next_buffer==start_buffers+2);
 volume.face.mWeightsScrubbed=true;
 assert(LLMeshGeometry::weights(buffer,volume,0,4,65));
 assert(next_buffer==start_buffers+3);
 LLMeshGeometry::Texcoords uv;
 assert(LLMeshGeometry::texcoords(buffer,volume,0,LLVertexBuffer::TYPE_TEXCOORD0,4,65,uv));
 LLMeshGeometry::fill(buffer,LLVertexBuffer::TYPE_COLOR,4,68,0x12345678);
 // Reload must publish queued data before releasing the callbacks and sources.
 LLMeshGeometry::destroyGL();assert(dispatches==6&&!buffer.pending);
 assert(LLMeshGeometry::initGL());
 LLVertexBuffer target;target.handle=102;target.index_handle=103;
 assert(LLMeshGeometry::generate(buffer,volume,0,4,65,68,7,matrix,matrix,true,true));
 const auto before=dispatches;
 LLMeshGeometry::copyResidentRange(target,buffer,4,1,65,6,12,3);
 assert(dispatches==before+1&&!buffer.pending&&target.pending&&copies==13);
 assert(copy_read_binding==96&&copy_write_binding==97);
 target.pending();assert(dispatches==before+2&&!target.pending);
 ++volume.revision;fail_allocation=1;
 assert(!LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
 assert(!buffer.pending);fail_allocation=0;
 assert(LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
 // Lost contexts discard queued jobs, unlike ordinary shader reload.
 LLDrawInfo a,b;a.mVertexBuffer=&buffer;b.mVertexBuffer=&buffer;b.mOffset=3;
 assert(LLMeshGeometry::compatibleBatch(a,b));
 b.mBlendFuncDst=1;assert(!LLMeshGeometry::compatibleBatch(a,b));b.mBlendFuncDst=0;
 b.mSkinInfo=&volume;assert(!LLMeshGeometry::compatibleBatch(a,b));b.mSkinInfo=nullptr;
 b.mMeshGeometry=false;assert(!LLMeshGeometry::compatibleBatch(a,b));b.mMeshGeometry=true;
 Resident resident;b.mComputeLOD=&resident;assert(!LLMeshGeometry::compatibleBatch(a,b));b.mComputeLOD=nullptr;
 const auto command_start=dispatches;
 assert(LLMeshGeometry::drawBatch({&a,&b}));
 assert(dispatches==command_start+2); // command generation and pending vertex publication
 assert(LLMeshGeometry::drawBatch({&a,&b})&&dispatches==command_start+2); // cached commands
 b.mOffset=6;assert(LLMeshGeometry::drawBatch({&a,&b})&&dispatches==command_start+3);
 assert(!LLMeshGeometry::drawBatch({&a}));
 // Pressure must publish queued destinations before evicting their sources.
 LLMeshGeometry::destroyGL();assert(LLMeshGeometry::initGL());
 volume.face.mNumVertices=65535; // GL observer does not dereference upload pointers.
 const U64 source_bytes=65535ull*72+12;
 const auto pressure_start=dispatches;
 const auto pressure_deletes=deletes;
 for(int i=0;i<80;++i){
   ++volume.revision;
   assert(LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
   assert(cached_bytes<=CACHE_BYTES);
   assert(cached_bytes==sources.size()*source_bytes);
   for(const auto& job:jobs)assert(job.source&&std::any_of(sources.begin(),sources.end(),[&](const auto& entry){return entry.second==job.source;}));
 }
 assert(deletes>pressure_deletes&&dispatches>pressure_start);
 LLMeshGeometry::flushRequired();assert(dispatches==pressure_start+80&&!buffer.pending);
 // Hit refreshes LRU age; the next insertion must evict the oldest other key.
 const auto newest_revision=volume.revision;
 const Key recent=sources.begin()->first;
 volume.revision=std::get<1>(recent);
 assert(LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
 const auto kept=sources.at(recent)->buffer;
 volume.revision=newest_revision+1;
 assert(LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
 assert(sources.at(recent)->buffer==kept);
 LLMeshGeometry::destroyGL();assert(LLMeshGeometry::initGL());
 volume.face.mNumVertices=65;
 // Both producer entry points must bound the queue and retain the new callback.
 const auto queue_start=dispatches;
 for(size_t i=0;i<MAX_JOBS;++i)
   assert(LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
 assert(dispatches==queue_start&&jobs.size()==MAX_JOBS);
 LLMeshGeometry::fill(target,LLVertexBuffer::TYPE_COLOR,0,1,0xff);
 assert(dispatches==queue_start+MAX_JOBS&&!buffer.pending&&target.pending&&jobs.size()==1);
 for(size_t i=1;i<MAX_JOBS;++i)LLMeshGeometry::fill(target,LLVertexBuffer::TYPE_COLOR,0,1,0xff);
 assert(LLMeshGeometry::generate(buffer,volume,0,0,1,1,0,matrix,matrix,true,true));
 assert(dispatches==queue_start+2*MAX_JOBS&&!target.pending&&buffer.pending&&jobs.size()==1);
 LLMeshGeometry::flushRequired();assert(dispatches==queue_start+2*MAX_JOBS+1);
 // Repeated packet eviction stays bounded, and an evicted packet regenerates.
 const auto packets_start=dispatches;
 for(U32 i=0;i<300;++i){b.mOffset=3+i*3;assert(LLMeshGeometry::drawBatch({&a,&b}));assert(commands.size()<=256);}
 assert(commands.size()==256&&dispatches==packets_start+300);
 b.mOffset=3;assert(LLMeshGeometry::drawBatch({&a,&b})&&dispatches==packets_start+301);
 assert(LLMeshGeometry::drawBatch({&a,&b})&&dispatches==packets_start+301);
 LLMeshGeometry::fill(buffer,LLVertexBuffer::TYPE_COLOR,0,1,0xff);
 LLMeshGeometry::fill(target,LLVertexBuffer::TYPE_COLOR,0,1,0xff);
 assert(buffer.pending&&target.pending&&!jobs.empty());
 const auto prior=dispatches;gGLManager.mIsDisabled=true;
 LLMeshGeometry::destroyGL();assert(dispatches==prior&&!buffer.pending);
 assert(!target.pending&&jobs.empty()&&sources.empty()&&commands.empty()&&cached_bytes==0);
 std::cout<<"Mesh producer: source reuse/revision, queue ownership/overflow, cache pressure, packet eviction, bounds, copies, bindings, allocation failure and reload passed.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='mesh-geometry-queue-') as directory:
    path=Path(directory)
    shader=(ROOT/'indra/newview/app_settings/shaders/class1/objects/meshGeometryC.glsl').as_posix()
    (path/'test.cpp').write_text('#define SHADER_PATH "'+shader+'"\n'+fixture+header+source+tests)
    (path/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(mesh_geometry_queue LANGUAGES CXX)\nadd_executable(test test.cpp)\ntarget_compile_features(test PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake','-S',str(path),'-B',str(path/'build')],check=True)
    subprocess.run(['cmake','--build',str(path/'build'),'--config','Debug'],check=True)
    subprocess.run([str(path/'build'/('Debug/test.exe' if sys.platform=='win32' else 'test'))],check=True)
