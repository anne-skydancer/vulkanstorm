// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Generated shaders: the GLSL that scripts/shaders/slang_gen.py writes under app_settings/shaders/generated/.
// A generated stage is one whole file. The loader hands the driver exactly this, in one piece:
//
//     #version 460
//     #define <hook> <value>        one line per hook the file lists in its "// hooks:" header line
//     <the whole file>
//
// No other text is added: no class search, no helper objects (attachShaderFeatures), no extra-code marker, no line
// or piece limit and no fallback to hand-written GLSL. A hook is a settings or device value the Slang source reads
// through a macro (`((SSR))`); its value is the program's define of that name, or 0 when the viewer sets none.
// IS_AMD_CARD is set to 1 on AMD cards, as the hand-written path does.
//
// Header-only and free of GL, so the format is tested on its own (tests/llgeneratedshader_test.cpp) and against the
// same literal as scripts/shaders/slang_check.py's assemble_source().
#ifndef LL_LLGENERATEDSHADER_H
#define LL_LLGENERATEDSHADER_H

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace LLGeneratedShader
{
    // Shader files the program lists with this prefix (relative to app_settings/shaders) are generated.
    inline constexpr std::string_view kDirectory = "generated/";

    inline bool isGeneratedFile(std::string_view filename)
    {
        return filename.substr(0, kDirectory.size()) == kDirectory;
    }

    // The program's one uniform block, as slangc names it (every generated stage that has global uniforms declares it).
    inline constexpr std::string_view kBlockName = "block_GlobalParams_0";

    // One generated stage's interface from the manifest (programs.json): what the C++ side needs to bind it without
    // asking the driver by name. Attribute slots are the viewer's reserved attribute indices; a uniform is a member
    // of the block at a byte offset; a texture is a sampler with its explicit texture unit.
    struct StageInterface
    {
        struct Input { std::string name; int location = -1; };
        struct Member { std::string name; unsigned offset = 0; unsigned size = 0; };
        struct Texture { std::string name; int binding = -1; };
        std::vector<Input> inputs;
        std::vector<Member> uniforms;
        std::vector<Texture> textures;
    };

    // std140 packing of the viewer's uniform setters into a generated program's block. `put(offset, src, bytes)` writes
    // into the block copy (LLGLSLShader::blockPut). Free of GL, so the layout is tested on its own.
    //
    // An array of scalars or vectors starts every element on a 16 byte boundary (the elements of one-element arrays and
    // single values are packed as they are).
    template <class Put>
    inline void putArray(int offset, const void* src, unsigned element_bytes, unsigned count, Put&& put)
    {
        if (count <= 1)
        {
            put(offset, src, element_bytes * count);
            return;
        }
        const unsigned char* bytes = static_cast<const unsigned char*>(src);
        for (unsigned i = 0; i < count; ++i)
        {
            put(offset + (int) (16 * i), bytes + (size_t) element_bytes * i, element_bytes);
        }
    }

    // A matrix in a column_major std140 block is an array of its columns, each starting on a 16 byte boundary. `v` holds
    // `count` matrices of cols x rows values, in column order, or in row order when `transpose` (glUniformMatrix's flag).
    template <class Put>
    inline void putMatrix(int offset, unsigned count, bool transpose, const float* v, unsigned cols, unsigned rows, Put&& put)
    {
        if (!transpose && count == 1 && cols == 4 && rows == 4)
        {
            put(offset, v, 64); // the four columns are the 64 bytes
            return;
        }
        if (!transpose && rows == 4)
        {
            put(offset, v, count * cols * 16); // every column is a vec4 and the matrices follow each other: one copy (the rigged palette)
            return;
        }
        for (unsigned m = 0; m < count; ++m)
        {
            const float* mat = v + (size_t) m * cols * rows;
            for (unsigned c = 0; c < cols; ++c)
            {
                float column[4] = { 0.f, 0.f, 0.f, 0.f };
                for (unsigned r = 0; r < rows; ++r)
                {
                    column[r] = transpose ? mat[r * cols + c] : mat[c * rows + r];
                }
                put(offset + (int) (16 * (m * cols + c)), column, rows * 4);
            }
        }
    }

    // The hook names in the file's leading comment block: the line "// hooks: A B C" (none when it lists none).
    inline std::vector<std::string> hooksOf(std::string_view text)
    {
        std::vector<std::string> hooks;
        constexpr std::string_view tag = "// hooks:";
        while (text.substr(0, 2) == "//")
        {
            const size_t eol = text.find('\n');
            std::string_view line = text.substr(0, eol);
            if (line.size() >= tag.size() && line.substr(0, tag.size()) == tag)
            {
                line.remove_prefix(tag.size());
                size_t i = 0;
                while (i < line.size())
                {
                    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
                    {
                        ++i;
                    }
                    size_t j = i;
                    while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r')
                    {
                        ++j;
                    }
                    if (j > i)
                    {
                        hooks.emplace_back(line.substr(i, j - i));
                    }
                    i = j;
                }
                return hooks;
            }
            if (eol == std::string_view::npos)
            {
                break;
            }
            text.remove_prefix(eol + 1);
        }
        return hooks;
    }

    // The source string for glShaderSource: #version 460, the hook defines, then the whole file.
    inline std::string assemble(std::string_view file_text, const std::map<std::string, std::string>& defines, bool is_amd)
    {
        std::string out = "#version 460\n";
        for (const std::string& hook : hooksOf(file_text))
        {
            std::string value = "0";
            auto it = defines.find(hook);
            if (it != defines.end())
            {
                value = it->second;
            }
            else if (is_amd && hook == "IS_AMD_CARD")
            {
                value = "1";
            }
            out += "#define " + hook + " " + value + "\n";
        }
        out.append(file_text);
        return out;
    }
}

#endif // LL_LLGENERATEDSHADER_H
