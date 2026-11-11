/**
 * @file llcloudnoise.h
 * @brief The volumetric clouds' noise file (app_settings/clouds/cloud_noise.vkcn): its container and its check
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#ifndef LL_LLCLOUDNOISE_H
#define LL_LLCLOUDNOISE_H

// The noise textures of the volumetric clouds are one data file made by the generator of
// https://github.com/anne-skydancer/3p-cloudnoise (the "cloudnoise" package; TileableVolumeNoise, MIT, for the
// base and erosion volumes; the curl texture is derived from its Perlin noise, see that repository's README).
//
// Container (little endian, uncompressed), 8,585,472 bytes:
//   header, 256 bytes: magic "VKCLDNZ1", u32 format_version (1), u32 header_bytes (256), u32 entry_count (3),
//                      32 bytes of generator id, then the three entries;
//   entry, 68 bytes:   u32 kind (1 base, 2 erosion, 3 curl), u32 format (1 = RGBA8 UNORM), u32 width, height,
//                      depth, u64 offset, u64 length, 32 bytes SHA-256 of the entry's bytes (the header is exactly
//                      52 + 3 x 68 = 256 bytes, no padding);
//   payload:           base 128^3, erosion 32^3, curl 128^2 (texels RGBA8, x fastest), in that order.
//
// The file is required. The viewer compares the SHA-256 of the whole file with the one compiled in
// (LL_CLOUD_NOISE_SHA256, from indra/cmake/CloudNoise.cmake) and parses the container, checking every entry's own
// hash; if anything is wrong it tells the user to reinstall and exits (LLAppViewer::init), not LL_ERRS.
//
// This header is renderer-free and standalone (OpenSSL's EVP only), so that
// indra/newview/tests/llcloudnoise_test.cpp builds without the viewer libraries.

#include <openssl/evp.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace LLCloudNoise
{
constexpr char MAGIC[8] = { 'V', 'K', 'C', 'L', 'D', 'N', 'Z', '1' };
constexpr uint32_t FORMAT_VERSION = 1;
constexpr uint32_t HEADER_BYTES = 256;
constexpr uint32_t ENTRY_COUNT = 3;
constexpr size_t ENTRY_BYTES = 68;
constexpr size_t FIRST_ENTRY = 52; // magic 8, version, header size, count 12, generator id 32
constexpr uint32_t FORMAT_RGBA8_UNORM = 1;

constexpr uint32_t BASE_SIZE = 128;    // 128^3
constexpr uint32_t EROSION_SIZE = 32;  // 32^3
constexpr uint32_t CURL_SIZE = 128;    // 128^2
constexpr uint64_t FILE_BYTES = HEADER_BYTES + 4ull * (BASE_SIZE * BASE_SIZE * BASE_SIZE + EROSION_SIZE * EROSION_SIZE * EROSION_SIZE + CURL_SIZE * CURL_SIZE);

enum Kind : uint32_t { KIND_BASE = 1, KIND_EROSION = 2, KIND_CURL = 3 };

// where the file is, below app_settings
constexpr const char* APP_SETTINGS_DIR = "clouds";
constexpr const char* FILE_NAME = "cloud_noise.vkcn";

struct Texture
{
    const uint8_t* texels = nullptr; // inside the parsed buffer
    uint32_t width = 0, height = 0, depth = 0;
    uint64_t bytes = 0;
};

struct Parsed
{
    Texture base, erosion, curl;
};

inline uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint64_t get64(const uint8_t* p) { return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32); }

// SHA-256 of a buffer as 32 bytes; false if OpenSSL fails
inline bool sha256(const uint8_t* data, size_t size, uint8_t out[32])
{
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        return false;
    }
    unsigned int length = 0;
    const bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1
                 && EVP_DigestUpdate(ctx, data, size) == 1
                 && EVP_DigestFinal_ex(ctx, out, &length) == 1 && length == 32;
    EVP_MD_CTX_free(ctx);
    return ok;
}

// lower case hex of a buffer's SHA-256, or "" if OpenSSL fails
inline std::string sha256Hex(const uint8_t* data, size_t size)
{
    uint8_t digest[32];
    if (!sha256(data, size, digest))
    {
        return std::string();
    }
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (uint8_t b : digest)
    {
        hex += digits[b >> 4];
        hex += digits[b & 15];
    }
    return hex;
}

// Parses and checks a container: magic, version, header size, entry count, each entry's kind, format,
// dimensions, offset (in order, contiguous), length, SHA-256, and the total size. Fills `out` with views into
// `data`, which must stay alive. On failure returns false with a short reason in `error`.
inline bool parse(const uint8_t* data, size_t size, Parsed& out, std::string& error)
{
    out = Parsed();
    if (!data || size < HEADER_BYTES)
    {
        error = "file too small";
        return false;
    }
    if (std::memcmp(data, MAGIC, sizeof MAGIC) != 0)
    {
        error = "bad magic";
        return false;
    }
    if (get32(data + 8) != FORMAT_VERSION)
    {
        error = "unsupported format version " + std::to_string(get32(data + 8));
        return false;
    }
    if (get32(data + 12) != HEADER_BYTES || get32(data + 16) != ENTRY_COUNT)
    {
        error = "bad header";
        return false;
    }
    if (size != FILE_BYTES)
    {
        error = "wrong file size " + std::to_string(size);
        return false;
    }
    const uint32_t sizes[ENTRY_COUNT][4] = { { KIND_BASE, BASE_SIZE, BASE_SIZE, BASE_SIZE },
                                             { KIND_EROSION, EROSION_SIZE, EROSION_SIZE, EROSION_SIZE },
                                             { KIND_CURL, CURL_SIZE, CURL_SIZE, 1 } };
    Texture* textures[ENTRY_COUNT] = { &out.base, &out.erosion, &out.curl };
    uint64_t next = HEADER_BYTES;
    for (uint32_t i = 0; i < ENTRY_COUNT; ++i)
    {
        const uint8_t* e = data + FIRST_ENTRY + i * ENTRY_BYTES;
        const uint64_t length = 4ull * sizes[i][1] * sizes[i][2] * sizes[i][3];
        if (get32(e) != sizes[i][0] || get32(e + 4) != FORMAT_RGBA8_UNORM || get32(e + 8) != sizes[i][1]
            || get32(e + 12) != sizes[i][2] || get32(e + 16) != sizes[i][3])
        {
            error = "entry " + std::to_string(i) + ": unexpected kind, format or size";
            return false;
        }
        if (get64(e + 20) != next || get64(e + 28) != length || next + length > size)
        {
            error = "entry " + std::to_string(i) + ": bad offset or length";
            return false;
        }
        uint8_t digest[32];
        if (!sha256(data + next, (size_t)length, digest))
        {
            error = "SHA-256 unavailable";
            return false;
        }
        if (std::memcmp(digest, e + 36, 32) != 0)
        {
            error = "entry " + std::to_string(i) + ": SHA-256 mismatch";
            return false;
        }
        textures[i]->texels = data + next;
        textures[i]->width = sizes[i][1];
        textures[i]->height = sizes[i][2];
        textures[i]->depth = sizes[i][3];
        textures[i]->bytes = length;
        next += length;
    }
    if (next != size)
    {
        error = "trailing bytes";
        return false;
    }
    return true;
}

// The whole file against the expected SHA-256 (lower case hex), then parse(). One call for the start-up check.
inline bool verify(const uint8_t* data, size_t size, const std::string& expected_sha256, Parsed& out, std::string& error)
{
    const std::string actual = sha256Hex(data, size);
    if (actual.empty())
    {
        error = "SHA-256 unavailable";
        return false;
    }
    if (actual != expected_sha256)
    {
        error = "SHA-256 " + actual + " is not the expected " + expected_sha256;
        return false;
    }
    return parse(data, size, out, error);
}

// ---- the viewer's copy (llcloudnoise.cpp)

// Reads app_settings/clouds/cloud_noise.vkcn and verifies it against the compiled-in SHA-256. On success the
// bytes stay in memory (about 8.6 MB) for the GL upload of the clouds; call release() when they are uploaded.
// On failure returns false with the reason in `error` and the path in `path`.
bool loadAndVerify(std::string& path, std::string& error);

// the verified file, or nullptr before a successful loadAndVerify() and after release()
const Parsed* data();

void release();
}

#endif // LL_LLCLOUDNOISE_H
