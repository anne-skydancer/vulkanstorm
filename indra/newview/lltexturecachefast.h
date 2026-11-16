// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LL_LLTEXTURECACHEFAST_H
#define LL_LLTEXTURECACHEFAST_H

#include <cstdint>

// Validation of the 16-byte header of a texture.fast entry (width, height, components,
// discard level). The header is file data: every field is checked before any product
// is formed, so corrupted or tampered values can neither overflow nor go positive from
// two negatives. The discard level (head[3]) is bounded too: readFromFastCache shifts the
// 16x16 entry left by (discard - MAX_DISCARD_LEVEL), which is undefined for a shift of 32 or more
// and allocates 16 << shift squared pixels. The writer stores the raw image's discard level
// (5 at most, 6 and 7 seen in practice); 12 covers every source up to 16384 pixels and keeps the
// expanded image at 2048 x 2048. Returns the payload size in bytes, or -1 when the header is invalid.
inline std::int32_t llTextureCacheFastPayloadSize(const std::int32_t head[4], std::int32_t max_dimension, std::int32_t max_payload,
                                                  std::int32_t max_discard = 12)
{
    if (head[0] <= 0 || head[0] > max_dimension
        || head[1] <= 0 || head[1] > max_dimension
        || head[2] < 1 || head[2] > 4
        || head[3] < 0 || head[3] > max_discard)
    {
        return -1;
    }
    const std::int64_t size = static_cast<std::int64_t>(head[0]) * head[1] * head[2];
    return size > max_payload ? -1 : static_cast<std::int32_t>(size);
}

#endif // LL_LLTEXTURECACHEFAST_H
