// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone check of lltexturecachefast.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lltexturecachefast_test.cpp
// A corrupted texture.fast header must be refused: negative dimensions (two negatives multiply
// to a positive), zero, oversized values whose 32-bit product overflows, bad channel counts.

#include "lltexturecachefast.h"

#include <cstdint>
#include <cstdio>

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

    constexpr std::int32_t MAX_DIM = 4096;
    constexpr std::int32_t MAX_PAYLOAD = 16 * 16 * 4;

    std::int32_t size_of(std::int32_t w, std::int32_t h, std::int32_t c, std::int32_t level = 0)
    {
        const std::int32_t head[4] = { w, h, c, level };
        return llTextureCacheFastPayloadSize(head, MAX_DIM, MAX_PAYLOAD);
    }
}

int main()
{
    check(size_of(16, 16, 4) == 1024, "16x16x4 is valid");
    check(size_of(8, 8, 3, 2) == 192, "8x8x3 level 2 is valid");
    check(size_of(1, 1, 1) == 1, "1x1x1 is valid");
    check(size_of(-16, -16, 4) == -1, "two negative dimensions (product 1024) are refused");
    check(size_of(-1, 16, 4) == -1, "negative width");
    check(size_of(16, -1, 4) == -1, "negative height");
    check(size_of(0, 16, 4) == -1, "zero width");
    check(size_of(16, 0, 4) == -1, "zero height");
    check(size_of(16, 16, 0) == -1, "zero channels");
    check(size_of(16, 16, -4) == -1, "negative channels");
    check(size_of(16, 16, 5) == -1, "five channels");
    check(size_of(16, 16, 4, -1) == -1, "negative discard level");
    check(size_of(65536, 65536, 1) == -1, "65536 x 65536 (32-bit product wraps to 0)");
    check(size_of(MAX_DIM + 1, 1, 1) == -1, "wider than the maximum dimension");
    check(size_of(2147483647, 2, 1) == -1, "INT_MAX width");
    check(size_of(16, 16, 4, 12) == 1024, "the highest accepted discard level");
    check(size_of(16, 16, 4, 13) == -1, "discard level above the bound");
    check(size_of(16, 16, 4, 40) == -1, "discard level that would shift by 35 (undefined)");
    check(size_of(16, 16, 4, 2147483647) == -1, "INT_MAX discard level");
    check(size_of(32, 32, 4) == -1, "payload larger than the fast cache entry");
    if (failures == 0)
    {
        std::printf("lltexturecachefast_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
