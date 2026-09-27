"""Compile the production particle dispatcher with a fake GL driver.

Checks admission, compact packing, binding restoration, failure handling and
reload/context cleanup. GPU arithmetic is tested separately on real GL.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
fixture = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <cassert>
using F32=float; using U32=uint32_t; using U64=uint64_t; using S32=int;
using U8=uint8_t; using GLuint=U32; using GLint=int; using GLint64=int64_t;
using GLenum=U32;
#define LL_PROFILE_ZONE_NAMED(x)
#define LL_WARNS(x) std::cerr
#define LL_ERRS(x) std::cerr
#define LL_INFOS(x) std::cerr
#define LL_ENDL std::endl
constexpr U32 GL_CURRENT_PROGRAM=1, GL_SHADER_STORAGE_BUFFER_BINDING=2,
 GL_SHADER_STORAGE_BUFFER_START=3, GL_SHADER_STORAGE_BUFFER_SIZE=4,
 GL_SHADER_STORAGE_BUFFER=5, GL_COMPUTE_SHADER=6, GL_COMPILE_STATUS=7,
 GL_LINK_STATUS=8, GL_STREAM_DRAW=9, GL_NO_ERROR=0,
 GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT=1, GL_BUFFER_UPDATE_BARRIER_BIT=2,
 GL_SHADER_STORAGE_BARRIER_BIT=4, GL_COPY_READ_BUFFER=10, GL_COPY_WRITE_BUFFER=11,
 GL_COPY_READ_BUFFER_BINDING=10,GL_COPY_WRITE_BUFFER_BINDING=11;
GLint bound_shader=99, generic_binding=98, indexed_binding[3]={12,13,14};
GLint64 range_start[3]={32,0,0}, range_size[3]={64,0,0};
unsigned error_code=0, fail_allocation=0, dispatches=0, deletes=0, barriers=0, count=0;
U32 offset_values[4]={};
float camera_values[3]={}, normal_values[3]={};
bool flushed=false, setting=true;
std::vector<U8> upload,group_upload;
unsigned next_buffer=22,copies=0; GLint copy_read_binding=96,copy_write_binding=97;
void glGetIntegerv(U32 key,GLint* v){*v=key==GL_CURRENT_PROGRAM?bound_shader:key==GL_COPY_READ_BUFFER?copy_read_binding:key==GL_COPY_WRITE_BUFFER?copy_write_binding:generic_binding;}
void glGetIntegeri_v(U32,U32 i,GLint* v){*v=indexed_binding[i];}
void glGetInteger64i_v(U32 key,U32 i,GLint64* v){*v=key==GL_SHADER_STORAGE_BUFFER_START?range_start[i]:range_size[i];}
void glUseProgram(U32 v){bound_shader=v;}
void glBindBufferRange(U32,U32 i,U32 v,GLint64 start,GLint64 size){indexed_binding[i]=v;range_start[i]=start;range_size[i]=size;generic_binding=v;}
void glBindBufferBase(U32,U32 i,U32 v){indexed_binding[i]=v;range_start[i]=range_size[i]=0;generic_binding=v;}
void glBindBuffer(U32 key,U32 v){if(key==GL_COPY_READ_BUFFER)copy_read_binding=v;else if(key==GL_COPY_WRITE_BUFFER)copy_write_binding=v;else generic_binding=v;}
void glCopyBufferSubData(U32,U32,size_t,size_t,size_t){++copies;}
U32 glCreateShader(U32){return 20;}
void glShaderSource(U32,int,const char**,void*){}
void glCompileShader(U32){}
void glGetShaderiv(U32,U32,int* v){*v=1;}
void glGetShaderInfoLog(U32,int,void*,char*){}
void glDeleteShader(U32){}
U32 glCreateProgram(){return 21;}
void glAttachShader(U32,U32){}
void glLinkProgram(U32){}
void glGetProgramiv(U32,U32,int* v){*v=1;}
void glGetProgramInfoLog(U32,int,void*,char*){}
void glDeleteProgram(U32){++deletes;}
void glGenBuffers(int,U32* v){*v=next_buffer++;}
void glDeleteBuffers(int,U32*){++deletes;}
int glGetUniformLocation(U32,const char* name){return std::strcmp(name,"cameraOrigin")==0?2:std::strcmp(name,"particleNormal")==0?3:1;}
void glBufferData(U32,size_t size,const void* data,U32){assert(flushed);if(data){auto& bytes=size%80==0?upload:group_upload;bytes.assign((const U8*)data,(const U8*)data+size);}error_code=fail_allocation;}
U32 glGetError(){U32 e=error_code;error_code=0;return e;}
void glUniform1ui(int,U32 v){count=v;}
void glUniform4ui(int,U32 a,U32 b,U32 c,U32 d){offset_values[0]=a;offset_values[1]=b;offset_values[2]=c;offset_values[3]=d;}
void glUniform3fv(int location,int,const float* values){std::copy(values,values+3,location==2?camera_values:normal_values);}
void glDispatchCompute(U32 x,U32 y,U32 z){assert(x==(count+63)/64 && y==1 && z==1);++dispatches;}
void glMemoryBarrier(U32 bits){barriers|=bits;}
struct LLVector3 {
 float mV[3]={};
 LLVector3()=default; LLVector3(std::initializer_list<float> v){std::copy(v.begin(),v.end(),mV);}
 LLVector3(float x,float y,float z):mV{x,y,z}{}
 LLVector3 operator*(int)const{return *this;}
 LLVector3 operator-(const LLVector3& b)const{return {{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]}};}
 LLVector3 operator-()const{return {{-mV[0],-mV[1],-mV[2]}};}
 bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 float lengthSquared()const{return mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2];}
};
struct LLVector2 {float mV[2]={2,3};bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1]);}};
struct LLColor4U {U8 mV[4]={1,2,3,4}; LLColor4U()=default; LLColor4U(U8 a,U8 b,U8 c,U8 d):mV{a,b,c,d}{};};
inline int ll_round(float x){return int(std::round(x));}
template<class T>struct Ptr {T* p=nullptr;Ptr()=default;Ptr(T* value):p(value){}bool notNull()const{return p!=nullptr;}T* operator->()const{return p;}};
template<class T>using LLPointer=Ptr<T>;
struct SourceObject {int getRenderRotation()const{return 0;}};
struct LLViewerPartSource {LLVector3 mPosAgent;Ptr<SourceObject> mSourceObjectp;};
struct LLPartData {enum {LL_PART_RIBBON_MASK=1,LL_PART_FOLLOW_VELOCITY_MASK=2};};
struct LLViewerPart {U32 mFlags=0;LLVector3 mPosAgent{{10,20,30}};LLVector2 mScale,mStartScale;LLColor4U mColor,mGlow,mStartColor;LLVector3 mAxis{{0,0,1}},mVelocity{{1,2,3}};LLViewerPart* mParent=nullptr;Ptr<LLViewerPartSource> mPartSourcep;float mStartGlow=.5f;};
struct LLViewerPartGroup {std::vector<LLViewerPart*> mParticles;};
struct LLViewerRegion {enum {PARTITION_PARTICLE=1,PARTITION_HUD_PARTICLE=2};};
struct LLViewerObject {virtual ~LLViewerObject()=default;};
struct LLVOPartGroup:LLViewerObject {
 enum {VERTEX_DATA_MASK=123};int type=1;LLViewerPartGroup group;
 U32 getPartitionType()const{return type;}
 LLViewerPartGroup* getViewerPartGroup(){return &group;}
};
struct LLFace {LLViewerObject* object;S32 index;LLViewerObject* getViewerObject()const{return object;}S32 getTEOffset()const{return index;}};
class LLVertexBuffer {
public:
 enum {TYPE_VERTEX,TYPE_NORMAL,TYPE_COLOR,TYPE_EMISSIVE,TYPE_TEXCOORD0};
 U32 handle=77,mask=123,verts=256;
 U32 getNumVerts()const{return verts;}U32 getGLBuffer()const{return handle;}
 U32 getTypeMask()const{return mask;}U32 getOffset(int i)const{return U32(i)*1024;}
 U32 bytes=8192;U32 getSize()const{return bytes;}
 void (*pending)()=nullptr;void setBeforeBind(void (*p)()){pending=p;}
 void unmapBuffer(){flushed=true;}
};
struct LLViewerCamera {
 static LLViewerCamera* getInstance(){static LLViewerCamera c;return &c;}
 LLVector3 getOrigin()const{return {};}
 LLVector3 getXAxis()const{return {{1,0,0}};}
};
bool gDebugGL=false;
struct { LLVector3 getCameraPositionAgent()const{return {{2,3,4}};} } gAgentCamera;
struct {float mGLVersion=4.3f;} gGLManager;
struct {bool debug=false;bool hasRenderDebugMask(U64)const{return debug;}} gPipeline;
int gSavedSettings=0;
template<class T>struct LLCachedControl {LLCachedControl(int,const char*,bool){}operator bool()const{return setting;}};
struct {void flush(){}} gGL;
constexpr int LL_PATH_APP_SETTINGS=0;
std::string shader_file;
struct Dir {std::string getExpandedFilename(int,const char*,const char*)const{return shader_file;}} dir;
Dir* gDirUtilp=&dir;
namespace LLParticleCompute {bool flush();bool initGL();bool generate(LLVertexBuffer&,const std::vector<LLFace*>&);void destroyGL();}
'''
checks = r'''
void restored(){
 assert(bound_shader==99 && generic_binding==98);
 assert(copy_read_binding==96 && copy_write_binding==97 && indexed_binding[2]==14);
 assert(indexed_binding[0]==12 && range_start[0]==32 && range_size[0]==64);
 assert(indexed_binding[1]==13 && range_start[1]==0 && range_size[1]==0);
}
int main(int argc,char** argv){
 assert(argc==2);shader_file=argv[1];
 LLVOPartGroup object;
 std::vector<LLViewerPart> particles(64);
 std::vector<LLFace> storage;storage.reserve(64);
 std::vector<LLFace*> faces;
 for(int i=0;i<64;++i){object.group.mParticles.push_back(&particles[i]);storage.push_back({&object,i});faces.push_back(&storage.back());}
 LLVertexBuffer buffer;
 auto rejected=[&](){auto before=dispatches;assert(!LLParticleCompute::generate(buffer,faces));assert(before==dispatches);restored();};
 gGLManager.mGLVersion=4.1f;rejected();gGLManager.mGLVersion=4.3f;
 storage[63].index=64;rejected();storage[63].index=63;
 buffer.handle=0;rejected();buffer.handle=77;
 buffer.mask=0;rejected();buffer.mask=123;
 buffer.verts=255;rejected();buffer.verts=256;
 assert(LLParticleCompute::generate(buffer,faces));assert(dispatches==0 && buffer.pending);
 buffer.pending();restored();assert(dispatches==1 && copies==1 && !buffer.pending);
 assert(upload.size()==64*80 && count==64 && barriers==2);
 float camera[4],normal[4];std::memcpy(camera,group_upload.data()+32,16);std::memcpy(normal,group_upload.data()+48,16);
 assert(camera[0]==2 && camera[1]==3 && camera[2]==4 && normal[0]==-1);
 float position[4];std::memcpy(position,upload.data(),16);
 assert(position[0]==10 && position[1]==20 && position[2]==30 && position[3]==2);
 float scale_y;std::memcpy(&scale_y,upload.data()+28,4);assert(scale_y==3);
 assert(upload[64]==1 && upload[67]==4 && upload[68]==1 && upload[71]==4);
 U32 offsets[4];std::memcpy(offsets,group_upload.data(),16);assert(offsets[0]==0 && offsets[1]==256 && offsets[2]==512 && offsets[3]==768);
 // All particle modes and even one-particle groups use compute.
 auto small=faces;small.resize(1);
 assert(LLParticleCompute::generate(buffer,small));assert(LLParticleCompute::flush());restored();
 object.type=2;particles[0].mFlags=2;
 assert(LLParticleCompute::generate(buffer,small));assert(LLParticleCompute::flush());restored();
 U32 flags;std::memcpy(&flags,upload.data()+60,4);assert(flags==6);
 object.type=1;particles[0].mFlags=1;
 particles[0].mParent=&particles[1];particles[1].mPosAgent={{7,8,9}};
 particles[1].mColor.mV[0]=23;
 assert(LLParticleCompute::generate(buffer,small));assert(LLParticleCompute::flush());restored();
 float parent[4];std::memcpy(parent,upload.data()+32,16);
 assert(parent[0]==7 && parent[1]==8 && parent[2]==9 && upload[72]==23);
 particles[0].mParent=nullptr;
 assert(LLParticleCompute::generate(buffer,small));assert(LLParticleCompute::flush());restored();
 assert(upload[76]==0 && upload[79]==128);
 LLViewerPartSource source;SourceObject source_object;
 source.mSourceObjectp.p=&source_object;source.mPosAgent={{6,5,4}};
 particles[0].mPartSourcep.p=&source;
 assert(LLParticleCompute::generate(buffer,small));assert(LLParticleCompute::flush());restored();
 std::memcpy(parent,upload.data()+32,16);assert(parent[0]==6 && parent[1]==5 && parent[2]==4);
 particles[0].mFlags=0;particles[0].mPosAgent={{2,3,4}};
 gDebugGL=true;gPipeline.debug=true;
 assert(LLParticleCompute::generate(buffer,small));assert(LLParticleCompute::flush());restored();
 // Multiple destinations, mixed camera snapshots, and duplicate buffer writes
 // must batch into a single dispatch and restore state only once.
 LLVertexBuffer other;other.handle=78;
 auto before=dispatches;
 assert(LLParticleCompute::generate(buffer,small));
 assert(LLParticleCompute::generate(other,small));
 assert(LLParticleCompute::generate(buffer,small));
 assert(dispatches==before && buffer.pending && other.pending);
 assert(LLParticleCompute::flush());assert(dispatches==before+1 && count==3);
 assert(!buffer.pending && !other.pending);restored();
 // Reject a malformed later face without dropping an earlier queued group.
 assert(LLParticleCompute::generate(other,small));
 storage[63].index=64;rejected();storage[63].index=63;
 assert(LLParticleCompute::flush());assert(count==1);restored();
 // A full scratch arena forces an ordered GPU flush, never CPU generation.
 buffer.bytes=16*1024*1024;before=dispatches;
 assert(LLParticleCompute::generate(buffer,small));
 assert(LLParticleCompute::generate(other,small));
 assert(dispatches==before+1 && !buffer.pending && other.pending);
 assert(LLParticleCompute::flush());buffer.bytes=8192;restored();
 // Allocation failure must not clear pending callbacks and expose stale VBOs.
 assert(LLParticleCompute::generate(buffer,faces));
 fail_allocation=1;assert(!LLParticleCompute::flush());assert(buffer.pending);restored();fail_allocation=0;
 rejected(); // shader service failure is reset only by explicit reload
 LLParticleCompute::destroyGL();assert(deletes==4 && !buffer.pending);
 assert(LLParticleCompute::generate(buffer,faces));assert(LLParticleCompute::flush());restored();
 assert(LLParticleCompute::generate(buffer,small));before=dispatches;
 LLParticleCompute::destroyGL();LLParticleCompute::destroyGL();assert(deletes==8 && !buffer.pending && dispatches==before);
 std::cout<<"PASS: production admission, packing, bindings, failure handling and cleanup\n";
}
'''
source=(ROOT/'indra/newview/llparticlecompute.cpp').read_text(encoding='utf-8')
source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
(ROOT/'.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-host-',dir=ROOT/'.tmp') as temp:
    temp=Path(temp)
    (temp/'test.cpp').write_text(fixture+source+checks,encoding='utf-8')
    (temp/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(particle_host LANGUAGES CXX)\nadd_executable(particle_host test.cpp)\ntarget_compile_features(particle_host PRIVATE cxx_std_17)\n',encoding='utf-8')
    subprocess.run(['cmake','-S',str(temp),'-B',str(temp/'build')],check=True)
    subprocess.run(['cmake','--build',str(temp/'build'),'--config','Debug'],check=True)
    exe=temp/'build/Debug/particle_host.exe'
    if not exe.exists(): exe=temp/'build/particle_host'
    subprocess.run([str(exe),str(ROOT/'indra/newview/app_settings/shaders/class1/objects/particleGeometryC.glsl')],check=True)
