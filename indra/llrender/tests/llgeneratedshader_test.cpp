// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Standalone test of the generated-shader source format (llgeneratedshader.h). The same literal is checked by
// scripts/tests/test_slang_pipeline.py against slang_check.py's assemble_source().
#include "llgeneratedshader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond); std::exit(1); } } while (0)

// std140 packing of the uniform setters into the block copy (the pilot left these unchecked: a transposed matrix and
// the 16 byte array stride are where a wrong layout shows as a wrong image, not as an error)
namespace
{
    struct Block
    {
        std::vector<unsigned char> bytes;
        int puts = 0;
        explicit Block(size_t size) : bytes(size, 0xAB) {}   // 0xAB marks a byte nobody wrote
        void put(int offset, const void* src, unsigned count)
        {
            CHECK(offset >= 0 && (size_t) offset + count <= bytes.size());
            std::memcpy(bytes.data() + offset, src, count);
            ++puts;
        }
        float at(unsigned float_index) const
        {
            float f;
            std::memcpy(&f, bytes.data() + 4 * float_index, 4);
            return f;
        }
        bool untouched(unsigned byte_index) const { return bytes[byte_index] == 0xAB; }
    };
}

static void testStd140()
{
    using namespace LLGeneratedShader;

    // mat4, column order: the 64 bytes as they are
    {
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = (float) i;
        Block b(64);
        putMatrix(0, 1, false, m, 4, 4, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        for (int i = 0; i < 16; ++i) CHECK(b.at(i) == (float) i);
        CHECK(b.puts == 1);
    }
    // mat4 with transpose: the values come in row order, the block holds columns
    {
        float rows[16];
        for (int i = 0; i < 16; ++i) rows[i] = (float) i;   // row r, column c at r * 4 + c
        Block b(64);
        putMatrix(0, 1, true, rows, 4, 4, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r) CHECK(b.at(c * 4 + r) == (float) (r * 4 + c));   // column c, row r
        }
    }
    // mat3 (std140: three columns of three floats, each on a 16 byte boundary, the fourth float untouched)
    {
        float m[9];
        for (int i = 0; i < 9; ++i) m[i] = 1.f + i;   // column c, row r at c * 3 + r
        Block b(48);
        putMatrix(0, 1, false, m, 3, 3, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        for (int c = 0; c < 3; ++c)
        {
            for (int r = 0; r < 3; ++r) CHECK(b.at(c * 4 + r) == m[c * 3 + r]);
            CHECK(b.untouched(c * 16 + 12));
        }
        Block t(48);
        putMatrix(0, 1, true, m, 3, 3, [&](int o, const void* s, unsigned n) { t.put(o, s, n); });
        for (int c = 0; c < 3; ++c)
        {
            for (int r = 0; r < 3; ++r) CHECK(t.at(c * 4 + r) == m[r * 3 + c]);   // row-order input: transposed
            CHECK(t.untouched(c * 16 + 12));
        }
    }
    // mat2: two columns of two floats, 16 bytes apart
    {
        const float m[4] = { 1, 2, 3, 4 };
        Block b(32);
        putMatrix(0, 1, false, m, 2, 2, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        CHECK(b.at(0) == 1 && b.at(1) == 2 && b.at(4) == 3 && b.at(5) == 4);
        CHECK(b.untouched(8) && b.untouched(24));
    }
    // mat3x4 palette (three columns of four rows), two matrices, one copy: the rigged object's skin
    {
        float palette[24];
        for (int i = 0; i < 24; ++i) palette[i] = (float) (100 + i);
        Block b(96);
        putMatrix(0, 2, false, palette, 3, 4, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        for (int i = 0; i < 24; ++i) CHECK(b.at(i) == palette[i]);
        CHECK(b.puts == 1);
        // the same with transpose (rows of three): each column is gathered, the second matrix 48 bytes on
        Block t(96);
        putMatrix(0, 2, true, palette, 3, 4, [&](int o, const void* s, unsigned n) { t.put(o, s, n); });
        for (int m = 0; m < 2; ++m)
        {
            for (int c = 0; c < 3; ++c)
            {
                for (int r = 0; r < 4; ++r) CHECK(t.at(m * 12 + c * 4 + r) == palette[m * 12 + r * 3 + c]);
            }
        }
    }
    // an offset inside the block is added to every write
    {
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = (float) i;
        Block b(80);
        putMatrix(16, 1, false, m, 4, 4, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        CHECK(b.untouched(0) && b.untouched(15) && b.at(4) == 0.f && b.at(19) == 15.f);
    }

    // arrays of scalars and vectors: every element starts on a 16 byte boundary
    {
        const float v3[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        Block b(48);
        putArray(0, v3, 12, 3, [&](int o, const void* s, unsigned n) { b.put(o, s, n); });
        for (int e = 0; e < 3; ++e)
        {
            for (int k = 0; k < 3; ++k) CHECK(b.at(e * 4 + k) == v3[e * 3 + k]);
            CHECK(b.untouched(e * 16 + 12));
        }
        const float f[4] = { 10, 20, 30, 40 };
        Block s(64);
        putArray(0, f, 4, 4, [&](int o, const void* src, unsigned n) { s.put(o, src, n); });
        for (int e = 0; e < 4; ++e)
        {
            CHECK(s.at(e * 4) == f[e]);
            CHECK(s.untouched(e * 16 + 4) && s.untouched(e * 16 + 15));
        }
        const float v4[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        Block q(32);
        putArray(0, v4, 16, 2, [&](int o, const void* src, unsigned n) { q.put(o, src, n); });
        for (int i = 0; i < 8; ++i) CHECK(q.at(i) == v4[i]);
        // one element, or none: written as it is
        Block one(16);
        putArray(0, v3, 12, 1, [&](int o, const void* src, unsigned n) { one.put(o, src, n); });
        CHECK(one.at(0) == 1 && one.at(2) == 3 && one.untouched(12) && one.puts == 1);
        Block none(16);
        putArray(0, v3, 12, 0, [&](int o, const void* src, unsigned n) { none.put(o, src, n); });
        CHECK(none.puts == 1 && none.untouched(0));   // a zero-byte write, nothing changed
        // ints are arrays of 4 byte elements like floats
        const int iv[3] = { 7, 8, 9 };
        Block ib(48);
        putArray(0, iv, 4, 3, [&](int o, const void* src, unsigned n) { ib.put(o, src, n); });
        int got;
        std::memcpy(&got, ib.bytes.data() + 32, 4);
        CHECK(got == 9);
    }
}

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
    testStd140();
    std::puts("llgeneratedshader_test: ok");
    return 0;
}
