// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llcloudnoise.h's container reader (no viewer libraries, OpenSSL only):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llcloudnoise_test.cpp -lcrypto
// A synthetic container with the layout of 3p-cloudnoise's cloudnoise_gen is made here; the reader must accept
// it and reject: a bad magic, version, header size or entry count; a wrong kind, format, dimension, offset,
// length or entry hash; a truncated, extended or empty file; a flipped byte in each of the three payloads; and
// a whole-file hash that is not the expected one. With a path as the first argument (and the expected SHA-256
// as the second) the real file is checked too.

#include "llcloudnoise.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace
{
int failures = 0;

void check(bool cond, const char* what)
{
    if (!cond)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

using namespace LLCloudNoise;

void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void put64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }

std::vector<uint8_t> make_container()
{
    const uint32_t sizes[3][4] = { { 1, 128, 128, 128 }, { 2, 32, 32, 32 }, { 3, 128, 128, 1 } };
    std::vector<uint8_t> out(MAGIC, MAGIC + 8);
    put32(out, 1); put32(out, HEADER_BYTES); put32(out, 3);
    const char* id = "TVN 1f879a26 glm 0.9.7 gen 1.0.0";
    out.insert(out.end(), id, id + 32);
    std::vector<std::vector<uint8_t>> payload(3);
    uint64_t offset = HEADER_BYTES;
    for (int i = 0; i < 3; ++i)
    {
        payload[i].resize(4ull * sizes[i][1] * sizes[i][2] * sizes[i][3]);
        for (size_t k = 0; k < payload[i].size(); ++k) payload[i][k] = uint8_t(k * 7 + i);
        put32(out, sizes[i][0]); put32(out, 1); put32(out, sizes[i][1]); put32(out, sizes[i][2]); put32(out, sizes[i][3]);
        put64(out, offset); put64(out, payload[i].size());
        uint8_t digest[32];
        sha256(payload[i].data(), payload[i].size(), digest);
        out.insert(out.end(), digest, digest + 32);
        offset += payload[i].size();
    }
    out.resize(HEADER_BYTES, 0);
    for (int i = 0; i < 3; ++i) out.insert(out.end(), payload[i].begin(), payload[i].end());
    return out;
}

bool accepts(const std::vector<uint8_t>& file)
{
    Parsed parsed;
    std::string error;
    return parse(file.data(), file.size(), parsed, error);
}

void rejects_flip(const std::vector<uint8_t>& good, size_t at, const char* what)
{
    std::vector<uint8_t> bad = good;
    bad[at] ^= 1;
    check(!accepts(bad), what);
}
}

int main(int argc, char** argv)
{
    // SHA-256 known answer: "abc"
    check(sha256Hex(reinterpret_cast<const uint8_t*>("abc"), 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");

    const std::vector<uint8_t> good = make_container();
    check(good.size() == FILE_BYTES && FILE_BYTES == 8585472, "container size");
    {
        Parsed parsed;
        std::string error;
        check(parse(good.data(), good.size(), parsed, error), "good file parses");
        check(parsed.base.texels == good.data() + 256 && parsed.base.bytes == 8388608 && parsed.base.depth == 128, "base view");
        check(parsed.erosion.texels == good.data() + 256 + 8388608 && parsed.erosion.bytes == 131072, "erosion view");
        check(parsed.curl.texels == good.data() + 256 + 8388608 + 131072 && parsed.curl.depth == 1 && parsed.curl.bytes == 65536, "curl view");
    }

    rejects_flip(good, 0, "bad magic");
    rejects_flip(good, 8, "bad version");
    rejects_flip(good, 12, "bad header size");
    rejects_flip(good, 16, "bad entry count");
    rejects_flip(good, FIRST_ENTRY, "bad kind");
    rejects_flip(good, FIRST_ENTRY + 4, "bad format");
    rejects_flip(good, FIRST_ENTRY + 8, "bad width");
    rejects_flip(good, FIRST_ENTRY + 12, "bad height");
    rejects_flip(good, FIRST_ENTRY + 16, "bad depth");
    rejects_flip(good, FIRST_ENTRY + ENTRY_BYTES + 20, "bad offset");
    rejects_flip(good, FIRST_ENTRY + 2 * ENTRY_BYTES + 28, "bad length");
    rejects_flip(good, FIRST_ENTRY + 36, "bad entry hash");
    rejects_flip(good, 256 + 5, "flipped byte in base");
    rejects_flip(good, 256 + 8388608 + 5, "flipped byte in erosion");
    rejects_flip(good, 256 + 8388608 + 131072 + 5, "flipped byte in curl");
    check(!accepts(std::vector<uint8_t>(good.begin(), good.end() - 1)), "truncated by one");
    check(!accepts(std::vector<uint8_t>(good.begin(), good.begin() + 255)), "shorter than the header");
    check(!accepts(std::vector<uint8_t>()), "empty");
    std::vector<uint8_t> longer = good;
    longer.push_back(0);
    check(!accepts(longer), "one byte too many");

    // the whole-file hash
    {
        Parsed parsed;
        std::string error;
        const std::string sha = sha256Hex(good.data(), good.size());
        check(verify(good.data(), good.size(), sha, parsed, error), "verify with the right hash");
        check(!verify(good.data(), good.size(), std::string(64, '0'), parsed, error), "verify with a wrong hash");
        std::vector<uint8_t> flipped = good;
        flipped[256 + 5] ^= 1;
        // a whole-file hash that matches the damaged file is still stopped by the entry's own hash
        check(!verify(flipped.data(), flipped.size(), sha256Hex(flipped.data(), flipped.size()), parsed, error), "damaged entry, matching file hash");
    }

    if (argc > 1)
    {
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<uint8_t> real((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Parsed parsed;
        std::string error;
        check(file.is_open() && verify(real.data(), real.size(), argc > 2 ? argv[2] : sha256Hex(real.data(), real.size()), parsed, error), "the real file verifies");
        if (!error.empty())
        {
            std::printf("real file: %s\n", error.c_str());
        }
    }

    std::printf(failures ? "%d FAILED\n" : "llcloudnoise_test: all passed\n", failures);
    return failures ? 1 : 0;
}
