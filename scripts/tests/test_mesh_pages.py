"""Check production resident page admission/lifetime and batch compatibility."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'indra/newview/llcomputelod.cpp').read_text()
def method(signature):
    start=source.index(signature); brace=source.index('{',start); end=brace+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
fixture=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include "llmeshranges.h"
using U32=uint32_t; using U64=uint64_t;
template<class T>T llmax(T a,T b){return std::max(a,b);}
template<class T> struct LLPointer : std::shared_ptr<T> {
    using std::shared_ptr<T>::shared_ptr;
    LLPointer& operator=(T* p){this->reset(p);return *this;}
    bool notNull() const{return bool(*this);}
};
struct LLVertexBuffer {
    U32 mask,v=0,i=0; static inline bool fail=false;
    LLVertexBuffer(U32 m):mask(m){}
    bool allocateBuffer(U32 vertices,U32 indices){v=vertices;i=indices;return !fail;}
    U32 getTypeMask()const{return mask;}
    U32 getNumVerts()const{return v;}
    U32 getNumIndices()const{return i;}
    U64 getSize()const{return U64(v)*128;}
    U64 getIndicesSize()const{return U64(i)*2;}
    static U32 calcVertexSize(U32){return 128;}
};
U64 resident_bytes=0, resource_epoch=0, budget=64*1024*1024;
U64 byteBudget(){return budget;}
namespace LLComputeMesh {
struct Page { LLPointer<LLVertexBuffer> buffer; Ranges vertices,indices;U64 bytes=0;~Page(); };
struct PageRange {std::shared_ptr<Page> page;U32 vertex=0,index=0,vertices=0,indices=0;~PageRange();};
struct Resident {bool valid=true;std::shared_ptr<Page> page;bool avatar=false;U32 generation=1;};
}
std::vector<std::weak_ptr<LLComputeMesh::Page>> pages;
U32 generation=1;
struct LLGLTFMaterial {static constexpr int ALPHA_MODE_OPAQUE=0;int mAlphaMode=0;};
struct LLDrawInfo {std::shared_ptr<LLComputeMesh::Resident> mComputeLOD; void* mModelMatrix=nullptr;void* mTextureMatrix=nullptr;
LLPointer<LLGLTFMaterial> mGLTFMaterial;void* mTexture=nullptr;bool mAvatar=false;};
namespace LLComputeMesh {bool compatibleBatch(const LLDrawInfo&,const LLDrawInfo&);}
'''
production=method('LLComputeMesh::PageRange::~PageRange()')+'\n'+method('LLComputeMesh::Page::~Page()')+'\n'+method('std::shared_ptr<LLComputeMesh::Page> acquirePage')+'\n'+method('bool LLComputeMesh::compatibleBatch')
tests=r'''
int main(){
 LLComputeMesh::Ranges ranges(65532);
 std::vector<std::pair<U32,U32>> live;
 for(U32 i=1;i<200;++i){auto start=ranges.take(i);assert(start);live.push_back({*start,i});}
 for(size_t i=0;i<live.size();i+=2) ranges.release(live[i].first,live[i].second);
 for(size_t i=1;i<live.size();i+=2) ranges.release(live[i].first,live[i].second);
 assert(!ranges.take(65533));assert(*ranges.take(65532)==0);assert(!ranges.take(1));
 ranges.release(0,65532);assert(!ranges.take(0));assert(ranges.fits(65532));
 auto a=acquirePage(1,100,300);assert(a);auto cost=resident_bytes;
 assert(cost==a->buffer->getSize()+a->buffer->getIndicesSize());
 assert(*a->vertices.take(100)==0);assert(*a->indices.take(300)==0);
 auto b=acquirePage(1,100,300);assert(a==b && resident_bytes==cost);
 auto other=acquirePage(2,100,300);assert(other && other!=a);
 LLDrawInfo x,y;
 x.mComputeLOD=std::make_shared<LLComputeMesh::Resident>();x.mComputeLOD->page=a;
 y.mComputeLOD=std::make_shared<LLComputeMesh::Resident>();y.mComputeLOD->page=a;
 x.mGLTFMaterial=new LLGLTFMaterial;y.mGLTFMaterial=x.mGLTFMaterial;
 assert(LLComputeMesh::compatibleBatch(x,y));
 y.mModelMatrix=&x;assert(!LLComputeMesh::compatibleBatch(x,y));y.mModelMatrix=nullptr;
 y.mTextureMatrix=&x;assert(!LLComputeMesh::compatibleBatch(x,y));y.mTextureMatrix=nullptr;
 y.mTexture=&x;assert(!LLComputeMesh::compatibleBatch(x,y));y.mTexture=nullptr;
 y.mComputeLOD->page=other;assert(!LLComputeMesh::compatibleBatch(x,y));y.mComputeLOD->page=a;
 y.mComputeLOD->generation=2;assert(!LLComputeMesh::compatibleBatch(x,y));y.mComputeLOD->generation=1;
 y.mComputeLOD->valid=false;assert(!LLComputeMesh::compatibleBatch(x,y));y.mComputeLOD->valid=true;
 y.mAvatar=true;assert(!LLComputeMesh::compatibleBatch(x,y));y.mAvatar=false;
 x.mGLTFMaterial->mAlphaMode=1;assert(!LLComputeMesh::compatibleBatch(x,y));x.mGLTFMaterial->mAlphaMode=0;
 auto large=acquirePage(1,65532,200000);assert(large && large!=a);
 assert(large->vertices.take(65532));assert(large->indices.take(200000));
 budget=resident_bytes;assert(!acquirePage(3,10,30));
 budget=64*1024*1024;LLVertexBuffer::fail=true;auto before=resident_bytes;
 assert(!acquirePage(3,10,30));assert(before==resident_bytes);LLVertexBuffer::fail=false;
 auto lease=std::make_shared<LLComputeMesh::PageRange>();
 lease->vertices=64;lease->indices=96;lease->vertex=*a->vertices.take(64);lease->index=*a->indices.take(96);lease->page=a;
 const auto oldVertex=lease->vertex,oldIndex=lease->index;
 auto retained=lease;lease.reset();assert(*a->vertices.take(64)!=oldVertex);
 retained.reset();assert(*a->vertices.take(64)==oldVertex);assert(*a->indices.take(96)==oldIndex);
 a.reset();b.reset();other.reset();large.reset();assert(resident_bytes>0); // draw records own the page
 x.mComputeLOD.reset();y.mComputeLOD.reset();assert(!resident_bytes);
 auto fresh=acquirePage(1,10,30);assert(fresh && pages.size()==1);fresh.reset();assert(!resident_bytes);
}
'''
with tempfile.TemporaryDirectory(prefix='mesh-pages-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture + production + tests)
    (path / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(mesh_pages LANGUAGES CXX)\n'
        'add_executable(mesh_pages test.cpp)\ntarget_compile_features(mesh_pages PRIVATE cxx_std_17)\n')
    (path / 'llmeshranges.h').write_text((ROOT/'indra/newview/llmeshranges.h').read_text())
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(path / 'build'), '--config', 'Debug'], check=True)
    executable = path / 'build' / ('Debug/mesh_pages.exe' if sys.platform == 'win32' else 'mesh_pages')
    subprocess.run([str(executable)], check=True)
