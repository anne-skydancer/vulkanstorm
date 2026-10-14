"""Qualify the shared production terrain topology without windows or graphics."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class TerrainGeometry(unittest.TestCase):
    def test_all_lods_and_half_step_neighbours_cover_patch_without_holes(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/vsterraingeometry.cpp').read_text()
        source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
        fixture=r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
using U16=uint16_t;using U32=uint32_t;using S32=int;
#define llassert(value) assert(value)
HEADER
SOURCE
int main() {
 unsigned cases=0;
 for (const unsigned size:{16u,32u}) for (unsigned stride=1;stride<=size;stride*=2)
  for (const unsigned north:{stride/2,stride,stride*2}) for (const unsigned east:{stride/2,stride,stride*2}) {
   if (!north || north>size || !east || east>size) continue;
   VSTerrainGeometry producer(size,stride,north,east,
    [](U32 x,U32 y,U32,LLVector3* position,LLVector3* normal,LLVector2* uv,LLVector2* detail){
     *position={{float(x),float(y),0}};*normal={{0,0,1}};*uv={{float(x),float(y)}};*detail={{0,0}};
    });
   const auto mesh=producer.mesh();assert(!mesh.vertices.empty() && !mesh.indices.empty() && mesh.indices.size()%3==0);
   double area=0;std::map<std::pair<std::pair<int,int>,std::pair<int,int>>,unsigned> edges;
   for (unsigned offset=0;offset<mesh.indices.size();offset+=3) {
    for (unsigned i=0;i<3;++i) assert(mesh.indices[offset+i]<mesh.vertices.size());
    const auto& a=mesh.vertices[mesh.indices[offset]];const auto& b=mesh.vertices[mesh.indices[offset+1]];const auto& c=mesh.vertices[mesh.indices[offset+2]];
    const float determinant=(b.mV[0]-a.mV[0])*(c.mV[1]-a.mV[1])-(b.mV[1]-a.mV[1])*(c.mV[0]-a.mV[0]);
    assert(determinant>0);area+=determinant*.5;
    for (unsigned i=0;i<3;++i) {
     const auto& p=mesh.vertices[mesh.indices[offset+i]];const auto& q=mesh.vertices[mesh.indices[offset+(i+1)%3]];
     assert(p.mV[0]>=0 && p.mV[0]<=size && p.mV[1]>=0 && p.mV[1]<=size);
     std::pair<int,int> one{int(p.mV[0]),int(p.mV[1])},two{int(q.mV[0]),int(q.mV[1])};
     if (two<one) std::swap(one,two);++edges[{one,two}];
    }
   }
   assert(area==double(size)*size);
   // Every unmatched edge belongs to the patch boundary. Internal strips meet exactly.
   for (const auto& [edge,count]:edges) {
    assert(count==1 || count==2);
    if (count==1) assert((edge.first.first==0 && edge.second.first==0) || (edge.first.first==int(size) && edge.second.first==int(size)) ||
                       (edge.first.second==0 && edge.second.second==0) || (edge.first.second==int(size) && edge.second.second==int(size)));
   }
   ++cases;
  }
 assert(cases>60);
 bool rejected=false;try {VSTerrainGeometry invalid(16,3,1,1,{});}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
}
'''
        header=(ROOT/'indra/newview/vsterraingeometry.h').read_text()
        header='\n'.join(line for line in header.splitlines() if not line.startswith(('#include','#pragma')))
        fixture=fixture.replace('HEADER', header).replace('SOURCE', source)
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            adapter=r'''
struct LLVector3 {float mV[3];};struct LLVector2 {float mV[2];};
template<class T> class LLStrider {
 T* value=nullptr;
public:
 LLStrider& operator=(T* pointer){value=pointer;return *this;}
 T* get()const{return value;}T& operator*()const{return *value;}
 LLStrider operator++(int){const auto old=*this;++value;return old;}
};
'''
            cpp=directory/'terrain.cpp';cpp.write_text(adapter+fixture)
            binary=cpp.with_suffix('.exe')
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            tested=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(tested.returncode,0,tested.stdout+tested.stderr)


if __name__=='__main__': unittest.main()
