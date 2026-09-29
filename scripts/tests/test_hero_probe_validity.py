"""Compile and exercise production hero mip/readiness bookkeeping."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = r"""
#include "llheroprobevalidity.h"
#include <cassert>
#include <cmath>
using namespace LLHeroProbeValidity;
int main() {
 assert(scratchLevels(0)==0 && scratchLevels(1)==0 && scratchLevels(1000)==0);
 for (unsigned r=2; r<=16384; r*=2) {
  const unsigned s=scratchLevels(r), n=outputLevels(s);
  assert(n>0 && n<=s);
  for(int i=7501;i<=10000;++i) {
   const double lod=(1.0-i/10000.0)*s;
   const double bounded = std::fmin(lod, double(n-1));
   assert(std::ceil(bounded)<=n-1);
   if(s>1) assert(std::ceil(lod)<=n-1);
  }
 }
 assert(scratchLevels(1024)==10 && outputLevels(10)==4);
 for(unsigned rate : {1u,2u,3u,6u}) {
  Contents c;
  assert(!c.ready); c.publish(); assert(!c.ready);
  for(unsigned frame=0;frame<rate;++frame) {
   for(unsigned f=0;f<6;++f) if(frame%rate==f%rate)c.completeFace(f);
   c.publish(); assert(c.ready==(frame==rate-1));
  }
  c.invalidate();assert(!c.ready && !c.canFilter());
  c.completeFace(9);assert(!c.canFilter());
  c.completeFace(0);c.completeFace(0);c.publish();assert(!c.ready);
 }
}
"""
# std::initializer_list is not an implicit dependency of the production header.
source = '#include <initializer_list>\n' + source
with tempfile.TemporaryDirectory(prefix='hero-validity-') as d:
 p=Path(d);(p/'test.cpp').write_text(source)
 (p/'CMakeLists.txt').write_text(
  'cmake_minimum_required(VERSION 3.20)\nproject(hero_validity LANGUAGES CXX)\n'
  'add_executable(hero_validity test.cpp)\n'
  f'target_include_directories(hero_validity PRIVATE "{(ROOT/"indra/newview").as_posix()}")\n')
 subprocess.run(['cmake','-S',str(p),'-B',str(p/'build')],check=True)
 subprocess.run(['cmake','--build',str(p/'build'),'--config','Debug'],check=True)
 subprocess.run([str(p/'build'/('Debug/hero_validity.exe' if sys.platform=='win32' else 'hero_validity'))],check=True)
