"""Compile production post-deferred mask and particle boundary expressions.

The pixel fixture draws directly and cannot detect an upstream mask dropping the
particle consumer. This test covers that integration boundary and inherited
visibility, plus legacy/OIT versus interleaved group ordering.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
pipeline = (ROOT / 'indra/newview/pipeline.cpp').read_text()
alpha = (ROOT / 'indra/newview/lldrawpoolalpha.cpp').read_text()

def function(signature):
    start = pipeline.index(signature)
    brace = pipeline.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (pipeline[end] == '{') - (pipeline[end] == '}')
        end += 1
    return pipeline[start:end]

block = pipeline.split('// render non-deferred geometry (alpha, fullbright, glow)', 1)[1]
call = re.search(r'andRenderTypeMask\([\s\S]*?\);', block).group()
names = list(dict.fromkeys(re.findall(r'RENDER_TYPE_[A-Z_]+', call)))
for required in ('RENDER_TYPE_PARTICLES', 'RENDER_TYPE_HUD_PARTICLES'):
    if required not in names:
        names.append(required)
expr = re.search(r'boundaries\.push_back\(([^;]+)\);', alpha).group(1)
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <stack>
#include <string>
#include <utility>
#include <vector>
using U32=unsigned;
#define LL_ERRS() std::cerr
#define LL_ENDL std::endl
struct LLPipeline {
 enum : U32 { INVALID=0, ENUMS, NUM_RENDER_TYPES, END_RENDER_TYPES=NUM_RENDER_TYPES };
 bool mRenderTypeEnabled[NUM_RENDER_TYPES]{};
 std::stack<std::string> mRenderTypeEnableStack;
 void andRenderTypeMask(U32,...);
 void pushRenderTypeMask(); void popRenderTypeMask();
 void postDeferred() { MASK_CALL }
};
'''.replace('ENUMS', ','.join(names)).replace('MASK_CALL', call)
source += '\n'.join(function('void LLPipeline::' + name) for name in
                     ('andRenderTypeMask(U32 type, ...)', 'pushRenderTypeMask()', 'popRenderTypeMask()'))
source += r'''
struct Group {
 float mDepth, mAvatarDepth;
 bool attached;
 float worldAlphaDepth() const { return attached?mAvatarDepth:mDepth; }
};
std::vector<float> boundariesFor(const std::vector<std::pair<Group*,bool>>& walk,bool merged) {
 std::vector<float> boundaries;
 for (const auto& entry:walk) boundaries.push_back(BOUNDARY);
 return boundaries;
}
int main() {
 LLPipeline p;
 for (bool world:{false,true}) for (bool hud:{false,true}) {
  std::fill(std::begin(p.mRenderTypeEnabled),std::end(p.mRenderTypeEnabled),true);
  p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_PARTICLES]=world;
  p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_HUD_PARTICLES]=hud;
  p.pushRenderTypeMask(); p.postDeferred();
  assert(p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_PARTICLES]==world);
  assert(p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_HUD_PARTICLES]==hud);
  assert(p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_ALPHA_POST_WATER]);
  p.popRenderTypeMask();
  assert(p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_PARTICLES]==world);
  assert(p.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_HUD_PARTICLES]==hud);
 }
 // Legacy/OIT bounds order differs deliberately from the attachment's avatar
 // depth. Using worldAlphaDepth unconditionally produces rejected boundaries.
 Group attachment{30,10,true}, world{20,20,false}, rigged{2,10,true};
 assert((boundariesFor({{&attachment,false},{&world,false}},false)==std::vector<float>{30,20}));
 assert((boundariesFor({{&world,false},{&rigged,true},{&attachment,false}},true)==std::vector<float>{20,10,10}));
 std::cout << "PASS production post-deferred particle visibility and alpha stream boundaries\n";
}
'''.replace('BOUNDARY', expr)
(ROOT / '.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-integration-', dir=ROOT / '.tmp') as folder:
    folder = Path(folder)
    (folder / 'test.cpp').write_text(source)
    (folder / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(particle_integration LANGUAGES CXX)\nadd_executable(particle_integration test.cpp)\ntarget_compile_features(particle_integration PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake', '-S', str(folder), '-B', str(folder / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(folder / 'build'), '--config', 'Debug'], check=True)
    exe = folder / 'build/Debug/particle_integration.exe'
    if not exe.exists(): exe = folder / 'build/particle_integration'
    subprocess.run([str(exe)], check=True)
