"""Exercise the production graphics reset identity policy without a GPU."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class GraphicsIdentityTest(unittest.TestCase):
    def test_renderer_and_adapter_transitions(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if not compiler:
            self.skipTest("requires clang++ or a GCC-compatible compiler in CXX")
        headers = Path(__file__).resolve().parents[2] / "indra" / "newview"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "probe.cpp"
            source.write_text(r'''
#include "llgraphicsidentity.h"
#include <cassert>
int main() {
    using LLGraphicsIdentity::changed;
    const std::string native = "ATI Technologies Inc. AMD Radeon RX 9070 XT";
    const std::string zink = "Mesa zink Vulkan 1.4(AMD Radeon RX 9070 XT (Driver Unknown))";
    assert(!changed(native, zink, "OpenGL", false));
    assert(!changed(zink, native, "OpenGL", false));
    assert(!changed(native, zink, "", false)); // migrate existing settings
    assert(!changed(zink, native, "", false));
    assert(changed(native, native, "OpenGL", true));
    assert(changed(native, native, "Vulkan", false));
    assert(!changed(native, native, "Vulkan", true));
    assert(changed("", native, "", false));
    assert(changed(zink, "ATI Technologies Inc. AMD Radeon RX 7900 XT", "OpenGL", false));
    assert(!changed("NVIDIA Corporation NVIDIA GeForce RTX 4090/PCIe/SSE2",
        "Mesa zink Vulkan 1.4(NVIDIA GeForce RTX 4090 (NVIDIA_PROPRIETARY))", "OpenGL", false));
    assert(!changed("AMD AMD Radeon RX 9070 XT (radeonsi, gfx1201, LLVM 20.1.8)",
        zink, "OpenGL", false));
    assert(changed("unknown adapter A", "unknown adapter B", "OpenGL", false));
    assert(changed("Mesa zink Vulkan malformed", native, "OpenGL", false));
}
''')
            binary = root / "probe.exe"
            subprocess.run([compiler, "-std=c++17", "-I", str(headers),
                            str(source), "-o", str(binary)], check=True,
                           capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
