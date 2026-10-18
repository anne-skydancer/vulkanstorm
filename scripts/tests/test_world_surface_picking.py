"""Exercise exact production surface intersections without a graphics device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class WorldSurfacePicking(unittest.TestCase):
    def test_admitted_triangles_mask_filter_lines_and_points(self):
        body = method((ROOT / 'indra/newview/vsworldrenderer.cpp').read_text(),
                      'bool VSWorldRenderer::intersect(')
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
class VSWorldRenderer {
public:
 struct Vertex {float x,y,z,nx,ny,nz,u,v;float tx=1,ty=0,tz=0,tw=-1;};
 enum class Topology {Triangles,Lines,Points};
 struct Geometry {
  std::vector<Vertex> cpuVertices;std::vector<unsigned> cpuIndices;
  std::array<std::array<float,3>,2> bounds;
  Topology topology=Topology::Triangles;
 };
 using Mesh=std::shared_ptr<const Geometry>;
 struct SurfaceHit {float fraction=1;std::array<float,3> point{},normal{};std::array<float,2> uv{};std::array<float,4> tangent{};std::array<float,3>rawNormal{};std::array<unsigned,3> triangle{};std::array<float,3>barycentric{};bool triangleKnown=false;};
 static bool intersect(const Mesh&,const std::array<float,3>&,const std::array<float,3>&,
                       SurfaceHit&,float=0,const std::function<bool(const SurfaceHit&)>&={});
};
BODY
int main() {
 auto triangles=std::make_shared<VSWorldRenderer::Geometry>();
 triangles->cpuVertices={{-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},{0,1,0,0,0,1,.5f,1},
                        {-1,-1,1,0,0,1,0,0},{1,-1,1,0,0,1,1,0},{0,1,1,0,0,1,.5f,1}};
 triangles->cpuIndices={0,1,2,3,4,5};triangles->bounds={{{-1,-1,0},{1,1,1}}};
 VSWorldRenderer::SurfaceHit hit;
 const std::array<float,3> start{0,0,2},end{0,0,-2};
 assert(VSWorldRenderer::intersect(triangles,start,end,hit));
 assert(std::abs(hit.fraction-.25f)<1e-6f && hit.point[2]==1);
 assert(std::abs(hit.uv[0]-.5f)<1e-6f && std::abs(hit.uv[1]-.5f)<1e-6f && hit.normal[2]==1);
 assert(hit.triangleKnown && hit.triangle[0]==3 && hit.triangle[1]==4 && hit.triangle[2]==5);
 assert(hit.barycentric[0]==.25f && hit.barycentric[1]==.25f && hit.barycentric[2]==.5f);
 assert(hit.tangent[0]==1 && hit.tangent[1]==0 && hit.tangent[2]==0 && hit.tangent[3]==-1);
 // A clipped first surface must not conceal a valid farther triangle of the same mesh.
 assert(VSWorldRenderer::intersect(triangles,start,end,hit,0,[](const auto& candidate){return candidate.point[2]<.5f;}));
 assert(std::abs(hit.fraction-.5f)<1e-6f && hit.point[2]==0);assert(hit.triangleKnown && hit.triangle[0]==0);
 assert(!VSWorldRenderer::intersect(triangles,start,end,hit,0,[](const auto&){return false;}));
 assert(!VSWorldRenderer::intersect(triangles,{3,0,2},{3,0,-2},hit));
 assert(!VSWorldRenderer::intersect(triangles,{0,0,2},{0,0,2},hit));
 // Picking follows the immutable animated generation supplied, without consulting a bind pose.
 auto animated=std::make_shared<VSWorldRenderer::Geometry>(*triangles);
 for (auto& vertex:animated->cpuVertices) vertex.x+=10;
 animated->bounds={{{9,-1,0},{11,1,1}}};
 assert(!VSWorldRenderer::intersect(animated,start,end,hit));
 assert(VSWorldRenderer::intersect(animated,{10,0,2},{10,0,-2},hit));
 assert(VSWorldRenderer::intersect(triangles,start,end,hit));
 auto line=std::make_shared<VSWorldRenderer::Geometry>();line->topology=VSWorldRenderer::Topology::Lines;
 line->cpuVertices={{-1,0,0,0,0,1,0,0},{1,0,0,0,0,1,1,0}};line->cpuIndices={0,1};line->bounds={{{-1,0,0},{1,0,0}}};
 assert(VSWorldRenderer::intersect(line,{0,.05f,1},{0,.05f,-1},hit,.1f));
 assert(std::abs(hit.uv[0]-.5f)<1e-6f);assert(!hit.triangleKnown);
 assert(!VSWorldRenderer::intersect(line,{0,.2f,1},{0,.2f,-1},hit,.1f));
 auto point=std::make_shared<VSWorldRenderer::Geometry>(*line);point->topology=VSWorldRenderer::Topology::Points;
 point->cpuIndices={0};
 assert(VSWorldRenderer::intersect(point,{-1,0,1},{-1,0,-1},hit,.01f));
 assert(!VSWorldRenderer::intersect(point,start,end,hit,.01f));
 assert(!VSWorldRenderer::intersect({},start,end,hit));
}
'''.replace('BODY', body).replace('&={}', '& = {}')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'surface.cpp'
            binary = source.with_suffix('.exe')
            source.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            tested = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)


if __name__ == '__main__':
    unittest.main()
