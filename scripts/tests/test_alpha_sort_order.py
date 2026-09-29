"""Check the production legacy-alpha order invalidation against camera motion."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
fixture = r'''
#include "llalphasort.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using Order = LLAlphaSortOrder;
Order::Vector direction(float angle) { return {std::cos(angle), std::sin(angle), 0}; }
Order make(const std::vector<Order::Bounds>& bounds) {
 Order order;
 for (const auto& bound : bounds) order.append(bound);
 return order;
}
int main() {
 // Camera position and group center remain fixed. A small turn reverses the
 // two panes' depth order, while the old camera-to-center trigger never moves.
 const std::vector<Order::Bounds> panes{{{10,1,0},{0,0,0}},{{10,-1,0},{0,0,0}}};
 auto order = make(panes);
 assert(!order.needsResort(direction(0.01f)));
 assert(!order.needsResort(direction(0.02f))); // movement, still correctly sorted
 assert(order.needsResort(direction(-0.01f)));
 assert(order.needsResort(direction(-0.01f))); // cached invalid result stays invalid
 assert(!order.needsResort(direction(0.01f)));
 assert(!make({}).needsResort(direction(0)));
 assert(!make({panes.front()}).needsResort(direction(0)));
 assert(!make({panes.front(),panes.front()}).needsResort(direction(0)));
 // The legacy size bias can reverse two faces even with the same center.
 auto sizes = make({{{10,0,0},{0,2,0}},{{10,0,0},{2,0,0}}});
 assert(!sizes.needsResort({1,0,0}));
 assert(sizes.needsResort({0,1,0}));
 // Common translations do not change ordering. Local bridge directions follow
 // the same rule; the producer/consumer both use the transformed camera.
 auto shifted = panes;
 for(auto& p : shifted) { p.center[0] += 8192; p.center[1] += 8192; }
 auto translated = make(shifted);
 for(float a : {-0.2f,-0.01f,0.f,0.01f,0.2f})
  assert(translated.needsResort(direction(a)) == order.needsResort(direction(a)));
 // Cross-check many directions and different extents with an independent
 // double-precision depth oracle, including order refreshed for the new view.
 std::vector<Order::Bounds> faces;
 for(int i=0;i<60;++i) faces.push_back({{float(i%7),float(i%11),float(i%5)},
                                     {float(i%3)*.2f,float(i%4)*.3f,float(i%2)*.1f}});
 auto key = [](const Order::Bounds& b,const Order::Vector& d) {
  double k=0; for(int a=0;a<3;++a) k+=(double(b.center[a])-double(b.quarter_extent[a])*d[a])*d[a]; return k;
 };
 for(int step=0;step<100;++step) {
  auto d=direction(step*.061f);
  bool inversion=false;
  for(size_t i=1;i<faces.size();++i) inversion |= key(faces[i-1],d)-key(faces[i],d)<-1e-5;
  assert(make(faces).needsResort(d)==inversion);
  std::stable_sort(faces.begin(),faces.end(),[&](auto& a,auto& b){return key(a,d)>key(b,d);});
  assert(!make(faces).needsResort(d));
 }
 std::cout << "PASS camera turns, stable order, extents, translations and depth oracle\n";
}
'''
(ROOT / '.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='alpha-sort-', dir=ROOT / '.tmp') as directory:
    folder = Path(directory)
    (folder / 'test.cpp').write_text(fixture)
    (folder / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(alpha_sort LANGUAGES CXX)\n'
        'add_executable(alpha_sort test.cpp)\ntarget_compile_features(alpha_sort PRIVATE cxx_std_17)\n'
        f'target_include_directories(alpha_sort PRIVATE "{(ROOT / "indra/newview").as_posix()}")\n')
    subprocess.run(['cmake', '-S', str(folder), '-B', str(folder / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(folder / 'build'), '--config', 'Debug'], check=True)
    executable = folder / 'build' / ('Debug/alpha_sort.exe' if sys.platform == 'win32' else 'alpha_sort')
    subprocess.run([str(executable)], check=True)
