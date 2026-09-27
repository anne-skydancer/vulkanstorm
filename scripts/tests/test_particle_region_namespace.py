"""Compile production region declarations with the X11 global Region typedef present."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
viewer = (ROOT / 'indra/newview/llparticleviewer.cpp').read_text()
pipeline = (ROOT / 'indra/newview/llparticlepipeline.cpp').read_text()
declarations = [re.search(pattern, text).group(0) for pattern, text in [
    (r'std::vector<[^>]+> regions;', viewer),
    (r'(?:LLParticlePipeline::)?Region r\{\};', viewer),
    (r'(?:LLParticlePipeline::)?Region empty\{\};', pipeline),
]]
sizes = re.findall(r'sizeof\((?:LLParticlePipeline::)?Region\)', pipeline)
assert sizes
source = '\n'.join([
    '#include <vector>',
    '#include "llparticlepipelinelayout.h"',
    'typedef struct _XRegion *Region;',
    'using namespace LLParticlePipeline;',
    'int main() {', *declarations,
    *('static_assert(' + size + ' == 32);' for size in sizes),
    'regions.push_back(r); return regions.size() != 1;', '}',
])
with tempfile.TemporaryDirectory(prefix='particle-region-') as directory:
    root = Path(directory)
    (root / 'test.cpp').write_text(source)
    (root / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(region_collision LANGUAGES CXX)\n'
        'add_executable(region_collision test.cpp)\n'
        'target_compile_features(region_collision PRIVATE cxx_std_17)\n'
        f'target_include_directories(region_collision PRIVATE "{ROOT.as_posix()}/indra/newview")\n')
    subprocess.run(['cmake', '-S', directory, '-B', str(root / 'build')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(root / 'build'), '--config', 'Debug'], check=True, stdout=subprocess.DEVNULL)
    exe = root / 'build/Debug/region_collision.exe'
    if not exe.exists(): exe = root / 'build/region_collision'
    subprocess.run([str(exe)], check=True)
print('PASS: particle region declarations coexist with X11 Region')
