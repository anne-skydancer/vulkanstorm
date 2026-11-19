// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Standalone test of the generated-shader source format (llgeneratedshader.h). The same literal is checked by
// scripts/tests/test_slang_pipeline.py against slang_check.py's assemble_source().
#include "llgeneratedshader.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond); std::exit(1); } } while (0)

int main()
{
    using namespace LLGeneratedShader;

    CHECK(isGeneratedFile("generated/tint.frag.glsl"));
    CHECK(!isGeneratedFile("deferred/softenLightF.glsl"));
    CHECK(!isGeneratedFile("generate/x"));
    CHECK(!isGeneratedFile(""));

    CHECK(hooksOf("// hooks:\n\nvoid main(){}\n").empty());
    CHECK(hooksOf("// hooks: SSR  REFMAP_LEVEL\r\n\nx").size() == 2);
    CHECK(hooksOf("// a\n// hooks: A\n\nx")[0] == "A");
    // the hooks line only counts inside the leading comment block
    CHECK(hooksOf("// a\n\n// hooks: A\n").empty());
    CHECK(hooksOf("void main() {}\n// hooks: A\n").empty());
    CHECK(hooksOf("").empty());

    const std::string file = "// Generated\n// hooks: SSR IS_AMD_CARD HERO\n\nvoid main(){}\n";
    const std::string expected =
        "#version 460\n#define SSR 1\n#define IS_AMD_CARD 0\n#define HERO 0\n"
        "// Generated\n// hooks: SSR IS_AMD_CARD HERO\n\nvoid main(){}\n";
    CHECK(assemble(file, {{"SSR", "1"}, {"UNRELATED", "7"}}, false) == expected);
    // AMD sets IS_AMD_CARD, a define of the same name wins
    CHECK(assemble(file, {{"SSR", "1"}}, true).find("#define IS_AMD_CARD 1\n") != std::string::npos);
    CHECK(assemble(file, {{"IS_AMD_CARD", "0"}}, true).find("#define IS_AMD_CARD 0\n") != std::string::npos);
    // a file with no hooks gets #version and the file, nothing else
    CHECK(assemble("// hooks:\nvoid main(){}\n", {{"SSR", "1"}}, true) == "#version 460\n// hooks:\nvoid main(){}\n");
    // the whole file, however long: no piece or line limit
    std::string big = "// hooks:\n";
    for (int i = 0; i < 20000; ++i)
    {
        big += "float v" + std::to_string(i) + " = 1.0;\n";
    }
    const std::string out = assemble(big, {}, false);
    CHECK(out.size() == std::string("#version 460\n").size() + big.size());
    CHECK(out.compare(out.size() - 20, 20, big, big.size() - 20, 20) == 0);
    std::puts("llgeneratedshader_test: ok");
    return 0;
}
