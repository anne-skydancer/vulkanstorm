/**
 * @file llsdserialize_corpus.h
 * @brief The LLSD values and serialized inputs shared by the serialization
 * parity test and the benchmark/fixture tool (vkstorm-rust rung two).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLSDSERIALIZE_CORPUS_H
#define LL_LLSDSERIALIZE_CORPUS_H

#include "linden_common.h"
#include "llsd.h"
#include "llsdserialize.h"

#include <string>
#include <vector>

namespace llsd_corpus
{
    enum Format
    {
        NOTATION = 0,
        BINARY = 1,
        XML = 2
    };

    struct ValueCase
    {
        std::string name;
        LLSD value;
    };

    /// Values covering every LLSD type and its edge cases, plus structures.
    std::vector<ValueCase> values();

    /// Large documents for benchmarks: a settings-like map, a wide array of
    /// maps, and an inventory-like array.
    std::vector<ValueCase> largeValues();

    struct ParseCase
    {
        std::string name;
        Format format;
        std::string input;
        llssize max_bytes;   // LLSDSerialize::SIZE_UNLIMITED or a budget
        S32 max_depth;       // -1 unlimited
        bool lines;          // XML parseLines() instead of parse()
        /// A C-runtime edge where the oracle's answer depends on the platform
        /// (MSVC vs glibc); the product has one behaviour, stated here.
        bool runtime_edge = false;
        S32 edge_count = 0;
        LLSD edge_value;
    };

    /// Serialized inputs, valid and malformed, with the limits to parse them
    /// under. Expected results come from the oracle, not from this table,
    /// except for the runtime edges, which carry the product's expectation.
    std::vector<ParseCase> parseCases();

    /// Bit-exact LLSD equality (reals and dates compared as bits, so NaN
    /// compares equal to itself), as the parity test needs it.
    bool equalBits(const LLSD& a, const LLSD& b);

    /// A short human-readable rendering for failure messages.
    std::string describe(const LLSD& v);

    /// Reads a whole file; empty when missing.
    std::string readFile(const std::string& path);
}

#endif // LL_LLSDSERIALIZE_CORPUS_H
