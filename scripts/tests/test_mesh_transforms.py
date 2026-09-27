"""Exercise production resident transform admission and incremental publication."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'indra/newview/llcomputelod.cpp').read_text()
def method(signature):
    start=source.index(signature); end=source.index('{',start)+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
fixture=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
using U32=uint32_t;using F32=float;using S32=int;using GLint=int;
constexpr int GL_ACTIVE_TEXTURE=1,GL_TEXTURE_BINDING_BUFFER=2,GL_TEXTURE_BUFFER=3,GL_TEXTURE0=100;
struct LLShaderMgr{enum{MESH_TRANSFORMS,MESH_TRANSFORM_ENABLED};};
struct LLGLSLShader{static inline LLGLSLShader* sCurBoundShaderPtr=nullptr;int channel=3,enabled=0;
 int getTextureChannel(int)const{return channel;}void uniform1i(int,int value){enabled=value;}};
int active_texture=107;std::array<int,16> texture_bindings{};int transform_texture=77;
void glGetIntegerv(int name,int* value){*value=name==GL_ACTIVE_TEXTURE?active_texture:texture_bindings[active_texture-GL_TEXTURE0];}
void glActiveTexture(int unit){active_texture=unit;}
void glBindTexture(int,int value){texture_bindings[active_texture-GL_TEXTURE0]=value;}
struct LLVector3 {F32 mV[3]={};LLVector3()=default;explicit LLVector3(const float* p){std::copy(p,p+3,mV);}
 LLVector3 scaledVec(const LLVector3& v)const{LLVector3 r;for(int i=0;i<3;++i)r.mV[i]=mV[i]*v.mV[i];return r;}
 float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);}
 const float* getF32ptr()const{return mV;}};
struct Matrix4{float mMatrix[4][4]={};};struct Matrix3{float mMatrix[3][3]={};};
struct LLTextureEntry{static constexpr int TEX_GEN_DEFAULT=0;int texgen=0,bump=0;int getTexGen()const{return texgen;}int getBumpmap()const{return bump;}};
struct LLDrawable{enum{REBUILD_VOLUME=1,REBUILD_TCOORD=2,REBUILD_COLOR=4,RIGGED=8,REBUILD_POSITION=16};U32 flags=0;LLVector3 center;
 bool isState(U32 mask)const{return (flags&mask)!=0;}const LLVector3& getPositionGroup()const{return center;}};
struct Volume{LLVector3 mLODScaleBias;};
namespace LLComputeMesh {
 struct Resident{bool valid=true,page=true;U32 generation=1,slot=0;};
 struct Object{std::vector<std::shared_ptr<Resident>> faces;};
}
struct LLVOVolume{
 std::shared_ptr<LLComputeMesh::Object> mComputeLOD=std::make_shared<LLComputeMesh::Object>();
 std::shared_ptr<LLDrawable> mDrawable=std::make_shared<LLDrawable>();
 Matrix4 position;Matrix3 normal;Volume volume;LLVector3 scale;bool allowed=true;
 std::array<LLTextureEntry,2> tes;
 const Matrix4& getRelativeXform()const{return position;}const Matrix3& getRelativeXformInvTrans()const{return normal;}
 int getNumTEs()const{return 2;}const LLTextureEntry* getTE(int i)const{return &tes[i];}
 const Volume* getVolume()const{return &volume;}const LLVector3& getScale()const{return scale;}
};
bool eligible(const LLVOVolume& o){return o.allowed;}
struct Transform{F32 position[16]={},normal[12]={};};
struct Entry{F32 centerRadius[4]={};};
std::array<Transform,4> resident_transforms;std::array<Entry,4> entries;
U32 generation=1;int publications=0;void dirty(U32){++publications;}
namespace LLComputeMesh{bool canUpdateTransform(LLVOVolume&);void updateTransform(LLVOVolume&);}
'''
production=method('struct TransformBinding')+';\n'+'\n'.join(method(signature) for signature in ['Transform objectTransform(', 'bool LLComputeMesh::canUpdateTransform(', 'void LLComputeMesh::updateTransform('])
tests=r'''
int main(){
 LLGLSLShader shader;LLGLSLShader::sCurBoundShaderPtr=&shader;texture_bindings[3]=456;texture_bindings[7]=123;
 {TransformBinding binding(true);assert(binding.ready&&shader.enabled==1&&active_texture==107&&texture_bindings[3]==77);}
 assert(shader.enabled==0&&active_texture==107&&texture_bindings[3]==456&&texture_bindings[7]==123);
 {TransformBinding binding(false);assert(binding.ready&&shader.enabled==0&&texture_bindings[3]==456);}
 shader.channel=-1;{TransformBinding binding(true);assert(!binding.ready);}shader.channel=3;
 transform_texture=0;{TransformBinding binding(true);assert(!binding.ready);}transform_texture=77;
 LLGLSLShader::sCurBoundShaderPtr=nullptr;{TransformBinding binding(true);assert(!binding.ready);}
 assert(shader.enabled==0&&active_texture==107&&texture_bindings[3]==456);
 LLVOVolume o;
 for(U32 slot=0;slot<2;++slot){auto r=std::make_shared<LLComputeMesh::Resident>();r->slot=slot;o.mComputeLOD->faces.push_back(r);}
 for(int i=0;i<3;++i){o.position.mMatrix[i][i]=float(i+1);o.normal.mMatrix[i][i]=1.f/float(i+1);o.scale.mV[i]=1;o.volume.mLODScaleBias.mV[i]=1;}
 o.position.mMatrix[3][3]=1;o.position.mMatrix[3][0]=2;
 o.mDrawable->center.mV[0]=10;
 assert(LLComputeMesh::canUpdateTransform(o));
 LLComputeMesh::updateTransform(o);assert(publications==2);
 assert(resident_transforms[0].position[12]==2&&resident_transforms[1].normal[10]==1.f/3.f);
 for(int slot=0;slot<2;++slot){assert(entries[slot].centerRadius[0]==10);for(int i:{3,7,11})assert(resident_transforms[slot].normal[i]==0);}
 LLComputeMesh::updateTransform(o);assert(publications==2); // unchanged state makes no upload dirty
 o.mDrawable->flags=LLDrawable::REBUILD_POSITION;
 o.position.mMatrix[3][0]=5;LLComputeMesh::updateTransform(o);assert(publications==4);
 o.mDrawable->center.mV[0]=20;LLComputeMesh::updateTransform(o);assert(publications==6);
 o.scale.mV[0]=2;LLComputeMesh::updateTransform(o);assert(publications==8);
 assert(entries[2].centerRadius[0]==0&&resident_transforms[2].position[12]==0);
 for(U32 flag:{LLDrawable::REBUILD_VOLUME,LLDrawable::REBUILD_TCOORD,LLDrawable::REBUILD_COLOR,LLDrawable::RIGGED}){
  o.mDrawable->flags=flag;assert(!LLComputeMesh::canUpdateTransform(o));LLComputeMesh::updateTransform(o);assert(publications==8);
 }
 o.mDrawable->flags=0;o.tes[1].texgen=1;assert(!LLComputeMesh::canUpdateTransform(o));o.tes[1].texgen=0;
 o.tes[1].bump=1;assert(!LLComputeMesh::canUpdateTransform(o));o.tes[1].bump=0;
 o.allowed=false;assert(!LLComputeMesh::canUpdateTransform(o));o.allowed=true;
 auto r=o.mComputeLOD->faces[1];r->valid=false;assert(!LLComputeMesh::canUpdateTransform(o));r->valid=true;
 r->page=false;assert(!LLComputeMesh::canUpdateTransform(o));r->page=true;
 r->generation=0;assert(!LLComputeMesh::canUpdateTransform(o));r->generation=1;
 o.mComputeLOD->faces[1].reset();assert(!LLComputeMesh::canUpdateTransform(o));
 o.mComputeLOD->faces.clear();assert(!LLComputeMesh::canUpdateTransform(o));
 o.mComputeLOD.reset();assert(!LLComputeMesh::canUpdateTransform(o));
}
'''
with tempfile.TemporaryDirectory(prefix='mesh-transforms-') as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(fixture+production+tests)
    (path/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(mesh_transforms LANGUAGES CXX)\nadd_executable(mesh_transforms test.cpp)\ntarget_compile_features(mesh_transforms PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake','-S',str(path),'-B',str(path/'build')],check=True)
    subprocess.run(['cmake','--build',str(path/'build'),'--config','Debug'],check=True)
    executable=path/'build'/('Debug/mesh_transforms.exe' if sys.platform=='win32' else 'mesh_transforms')
    subprocess.run([str(executable)],check=True)
