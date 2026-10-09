"""A native primary face must own glyphs even without GL texture backing."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
class NativeFontIdentity(unittest.TestCase):
    def test_head_and_fallback_identity_across_graphics_owners(self):
        root=Path(__file__).resolve().parents[2]
        code=(root/"indra/llrender/llfontregistry.cpp").read_text(encoding="utf-8")
        a=code.index("        bool is_fallback =",code.index("LLFontGL *result = NULL"))
        b=code.index("        bool is_font_loaded",a)
        decision=code[a:b]
        fixture=r"""
#include <cassert>
#include <stdexcept>
using F32=float;
struct LLFontGL { inline static bool native=false;static bool hasNativeDraw() { return native; } };
struct Face { bool fallback; float scale; void generateASCII() { if(fallback) throw std::logic_error("Fallback faces cannot own glyphs"); } };
Face select(bool is_first_found,bool mCreateGLTextures,bool native) {
    LLFontGL::native=native;const float fallback_scale=.8f,point_size=12.f;
"""+decision+r"""
    return {is_fallback,point_size_scale};
}
int main() {
    auto native=select(true,false,true);native.generateASCII();assert(native.scale==12.f);
    auto gl=select(true,true,false);gl.generateASCII();assert(gl.scale==12.f);
    auto fallback=select(false,false,true);assert(fallback.fallback && fallback.scale<12.f);
    auto test=select(true,false,false);assert(test.fallback);
    bool rejected=false;try { fallback.generateASCII(); } catch(const std::logic_error&) { rejected=true; }
    assert(rejected);
}
"""
        compiler=shutil.which("clang++") or shutil.which("g++");self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/"font_identity.cpp";source.write_text(fixture,encoding="utf-8")
            exe=Path(temp)/"font_identity.exe"
            build=subprocess.run([compiler,"-std=c++17","-Wall","-Werror",str(source),"-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=="__main__": unittest.main()
