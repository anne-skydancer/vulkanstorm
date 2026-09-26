"""Compile the real profiler wrapper against a counting Tracy test double.

This checks event generation without running a viewer or a Tracy collector.
Set CXX to a GCC-compatible C++ compiler, or install clang++ on PATH.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class MemoryGateTest(unittest.TestCase):
    def test_configuration_matrix(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if not compiler:
            self.skipTest("requires clang++ or a GCC-compatible compiler in CXX")
        headers = Path(__file__).resolve().parents[2] / "indra" / "llcommon"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "tracy").mkdir()
            (root / "tracy" / "Tracy.hpp").write_text("""
#pragma once
inline int allocations = 0, frees = 0, zones = 0;
#define TracyAlloc(ptr, size) (++allocations)
#define TracyFree(ptr) (++frees)
#define ZoneNamedN(var, name, active) do { if (active) ++zones; } while (0)
""")
            source = root / "probe.cpp"
            source.write_text("""
#include "tracy/Tracy.hpp"
#include "llprofiler.h"
namespace LLProfiler { bool active = true; }
int main() {
    int value = 0;
    for (int i = 0; i < 1000000; ++i) {
        LL_PROFILE_ALLOC(&value, sizeof(value));
        LL_PROFILE_FREE(&value);
    }
    LL_PROFILE_ZONE_NAMED("CPU zone must survive disabling allocation events");
    return allocations != EXPECT_EVENTS || frees != EXPECT_EVENTS
        || zones != EXPECT_ZONES;
}
""")
            # The default (undefined memory flag), explicit OFF, and opt-in ON
            # must work in both Tracy configurations; non-Tracy builds stay off.
            for config, memory in [(0, 1), (1, 1), (2, None), (2, 0),
                                   (2, 1), (3, None), (3, 0), (3, 1)]:
                with self.subTest(config=config, memory=memory):
                    binary = root / "probe.exe"
                    args = [compiler, "-std=c++17", "-O0", "-include",
                            str(root / "tracy" / "Tracy.hpp"),
                            "-I", str(root), "-I", str(headers),
                            f"-DLL_PROFILER_CONFIGURATION={config}",
                            f"-DEXPECT_EVENTS={1000000 if config >= 2 and memory else 0}",
                            f"-DEXPECT_ZONES={1 if config >= 2 else 0}"]
                    if memory is not None:
                        args.append(f"-DLL_PROFILER_ENABLE_TRACY_MEMORY={memory}")
                    subprocess.run(args + [str(source), "-o", str(binary)],
                                   check=True, capture_output=True, text=True)
                    subprocess.run([str(binary)], check=True,
                                   capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
