/**
 * @file llshadersource.h
 * @brief Reads a GLSL file whole and assembles its glShaderSource pieces
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 */

#ifndef LL_LLSHADERSOURCE_H
#define LL_LLSHADERSOURCE_H

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Reads every line of the stream, each with its newline, however long the file or the line is.
// Returns false when the stream reports a read error: a caller must not compile a shader it
// has only partly read.
inline bool ll_read_shader_lines(std::FILE* file, std::vector<std::string>& lines)
{
    char buff[1024];
    std::string line;
    while (std::fgets(buff, (int)sizeof(buff), file))
    {
        line.append(buff);
        if (!line.empty() && line.back() == '\n')
        {
            lines.push_back(std::move(line));
            line.clear();
        }
    }
    if (std::ferror(file))
    {
        return false;
    }
    if (!line.empty())
    {
        lines.push_back(std::move(line)); // last line without a newline
    }
    return true;
}

// The pieces in order: the version line, then the file's lines with the extra code in place of
// the first "[EXTRA_CODE_HERE]" line (that line is dropped), or right after the version line
// when the file has no such line.
inline std::vector<std::string> ll_assemble_shader_source(const std::string& version,
                                                          const std::vector<std::string>& extra,
                                                          const std::vector<std::string>& lines)
{
    std::vector<std::string> pieces;
    pieces.reserve(1 + extra.size() + lines.size());
    pieces.push_back(version);

    size_t marker = lines.size();
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].find("[EXTRA_CODE_HERE]") != std::string::npos)
        {
            marker = i;
            break;
        }
    }

    if (marker == lines.size())
    {
        pieces.insert(pieces.end(), extra.begin(), extra.end());
        pieces.insert(pieces.end(), lines.begin(), lines.end());
    }
    else
    {
        pieces.insert(pieces.end(), lines.begin(), lines.begin() + marker);
        pieces.insert(pieces.end(), extra.begin(), extra.end());
        pieces.insert(pieces.end(), lines.begin() + marker + 1, lines.end());
    }
    return pieces;
}

#endif // LL_LLSHADERSOURCE_H
