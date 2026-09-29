"""Exercise the production alpha/glow handoff with shared texture-unit state.

Glow and alpha programs have independent sampler layouts. Switching programs
does not restore texture bindings; a following group with the same alpha
program skips setup. Verify both that handoff and the old-code negative control.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'indra/newview/lldrawpoolalpha.cpp').read_text()
start = source.index('if (lastShader && rebind)')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
handoff = source[start:end]

fixture = r'''
#include <array>
#include <iostream>
std::array<int, 8> textures{};
struct Shader {
 static Shader* current;
 void bind() { current = this; }
};
Shader* Shader::current = nullptr;
struct Pipeline {
 int restores = 0;
 void bindDeferredShader(Shader& shader) {
  ++restores; shader.bind();
  textures = {101, 102, 103, 104, 105, 106, 107, 108};
 }
} gPipeline;
void restore(Shader* lastShader, bool rebind) { HANDOFF }
int main() {
 Shader alpha, glow;
 for (int overwritten = 0; overwritten < 8; ++overwritten) {
  gPipeline.bindDeferredShader(alpha);
  const auto expected = textures;
  glow.bind();
  textures[overwritten] = 999;
  restore(&alpha, true);
  // Same alpha shader in the next spatial group: production skips setup.
  if (Shader::current != &alpha) gPipeline.bindDeferredShader(alpha);
  if (textures != expected) return 1;
 }
 const int before = gPipeline.restores;
 const auto expected = textures;
 restore(&alpha, false);
 restore(nullptr, true);
 if (gPipeline.restores != before || textures != expected) return 2;
 std::cout << "PASS alpha state restored after glow, no replay when unused\n";
}
'''

(ROOT / '.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='alpha-glow-', dir=ROOT / '.tmp') as directory:
    folder = Path(directory)
    (folder / 'fixed.cpp').write_text(fixture.replace('HANDOFF', handoff))
    (folder / 'old.cpp').write_text(fixture.replace(
        'HANDOFF', 'if (lastShader && rebind) { lastShader->bind(); }'))
    (folder / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(alpha_glow LANGUAGES CXX)\n'
        'add_executable(fixed fixed.cpp)\nadd_executable(old old.cpp)\n'
        'target_compile_features(fixed PRIVATE cxx_std_17)\n'
        'target_compile_features(old PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake', '-S', str(folder), '-B', str(folder / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(folder / 'build'), '--config', 'Debug'], check=True)
    def executable(name):
        return folder / 'build' / (f'Debug/{name}.exe' if sys.platform == 'win32' else name)
    assert subprocess.run([str(executable('old'))]).returncode == 1, 'Negative control did not reproduce'
    subprocess.run([str(executable('fixed'))], check=True)
    print('PASS old program-only restore reproduces the stale-texture failure')
