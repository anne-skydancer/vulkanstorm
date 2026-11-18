# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Generates indra/newview/llstochasticbluenoise.h: the 64 x 64 blue-noise tile of the stochastic engine's dither, by
void-and-cluster (R. Ulichney, "The void-and-cluster method for dither array generation", SPIE 1993) on the torus with a
Gaussian of sigma 1.5.

After Aurora Viewer crates/aurora-render/src/oit_stochastic.rs (`void_and_cluster`, the tests' generator of
oit_stochastic_tile.bin), Anne Skydancer, GPL-3.0-or-later: the same tile bit for bit. The energies are integers (the
kernel in 16.16, cut at 4 sigma), the initial pattern a partial Fisher-Yates shuffle of N / 10 cells by splitmix64, and a
tie goes to the first cell in index order, so the result is the same on every platform. The SHA-256 of the tile as 16-bit
little-endian ranks (Aurora's file) is pinned in indra/newview/tests/llstochastichistory_test.cpp.

The tile holds the ranks 0 .. 4095 (each once). The engine's dither reads rank (x & 63, y & 63) from the two spare rows of
its mask table: the stratum (2 rank + 1) / 8192 of the dither xi, in 24-bit fixed point (stochasticSampleF.glsl stoch_xi24).

    python scripts/perf/gen_stochastic_blue_noise.py            # writes the header
    python scripts/perf/gen_stochastic_blue_noise.py --check    # exits 1 if the header differs from the generator's output

Pure Python (no NumPy), deterministic, well under a second.
"""
import hashlib
import math
import sys
from pathlib import Path

N = 64
SIGMA = 1.5
RADIUS = 6   # the kernel is cut at 4 sigma
TILE_SHA256 = '8c547004839c1d151a35efb2528733d5e8eed0387ca39cafce969fa932e9c0ad'   # Aurora's oit_stochastic_tile.bin
HEADER = Path(__file__).resolve().parents[2] / 'indra/newview/llstochasticbluenoise.h'
M64 = (1 << 64) - 1


def generate():
    area = N * N
    # the toroidal Gaussian in 16.16 (Rust's f64::round: half away from zero; every weight is positive)
    kernel = []
    for dy in range(-RADIUS, RADIUS + 1):
        for dx in range(-RADIUS, RADIUS + 1):
            w = math.exp(-float(dx * dx + dy * dy) / (2.0 * SIGMA * SIGMA))
            kernel.append((dx, dy, int(math.floor(w * 65536.0 + 0.5))))
    touch = [[(((i // N + dy) % N) * N + (i % N + dx) % N, w) for dx, dy, w in kernel] for i in range(area)]

    def splat(energy, i, sign):
        for j, w in touch[i]:
            energy[j] += sign * w

    def pick(energy, pattern, ones, highest):
        # the first cell with the highest (tightest cluster) or lowest (largest void) energy
        cells = [i for i in range(area) if pattern[i] == ones]
        if not cells:
            return 0
        return max(cells, key=energy.__getitem__) if highest else min(cells, key=energy.__getitem__)

    state = 0x1234_5678_9abc_def0

    def splitmix64():
        nonlocal state
        state = (state + 0x9e37_79b9_7f4a_7c15) & M64
        z = state
        z = ((z ^ (z >> 30)) * 0xbf58_476d_1ce4_e5b9) & M64
        z = ((z ^ (z >> 27)) * 0x94d0_49bb_1331_11eb) & M64
        return z ^ (z >> 31)

    # an initial pattern of N^2 / 10 pseudo-random ones
    cells = list(range(area))
    ones = area // 10
    pattern = [False] * area
    energy = [0] * area
    for i in range(ones):
        j = i + splitmix64() % (area - i)
        cells[i], cells[j] = cells[j], cells[i]
        pattern[cells[i]] = True
        splat(energy, cells[i], 1)
    # swap the tightest cluster to the largest void until they agree
    while True:
        tight = pick(energy, pattern, True, True)
        pattern[tight] = False
        splat(energy, tight, -1)
        void = pick(energy, pattern, False, False)
        pattern[void] = True
        splat(energy, void, 1)
        if void == tight:
            break
    rank = [0] * area
    # ranks ones - 1 .. 0: remove the tightest cluster each time
    p, e = list(pattern), list(energy)
    for r in range(ones - 1, -1, -1):
        t = pick(e, p, True, True)
        rank[t] = r
        p[t] = False
        splat(e, t, -1)
    # ranks ones .. N^2 - 1: fill the largest void each time
    for r in range(ones, area):
        v = pick(energy, pattern, False, False)
        rank[v] = r
        pattern[v] = True
        splat(energy, v, 1)
    assert sorted(rank) == list(range(area))
    return rank


def tile_sha256(ranks):
    return hashlib.sha256(b''.join(r.to_bytes(2, 'little') for r in ranks)).hexdigest()


def header(ranks):
    rows = []
    for i in range(0, len(ranks), 16):
        rows.append('        ' + ', '.join(f'{v}' for v in ranks[i:i + 16]) + ',')
    return f'''/**
 * @file llstochasticbluenoise.h
 * @brief The stochastic engine's 64 x 64 blue-noise tile (data; generated)
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

// Generated by scripts/perf/gen_stochastic_blue_noise.py (void-and-cluster, Ulichney 1993, on the torus, Gaussian sigma
// {SIGMA}): do not edit. The ranks 0 .. 4095, each once, row by row (y * 64 + x). The Aurora Viewer's tile
// (oit_stochastic_tile.bin, Anne Skydancer, GPL-3.0-or-later) bit for bit: SHA-256 {TILE_SHA256[:8]}... of the ranks as
// 16-bit little-endian words, pinned in tests/llstochastichistory_test.cpp.

#ifndef LL_LLSTOCHASTICBLUENOISE_H
#define LL_LLSTOCHASTICBLUENOISE_H

#include <cstdint>

namespace LLStochasticEngine
{{
    inline constexpr uint32_t BLUE_NOISE_SIZE = {N};     // the tile is BLUE_NOISE_SIZE x BLUE_NOISE_SIZE
    inline constexpr uint16_t BLUE_NOISE[BLUE_NOISE_SIZE * BLUE_NOISE_SIZE] = {{
{chr(10).join(rows)}
    }};
}}

#endif // LL_LLSTOCHASTICBLUENOISE_H
'''


if __name__ == '__main__':
    ranks = generate()
    digest = tile_sha256(ranks)
    if digest != TILE_SHA256:
        sys.exit('the generator no longer makes the Aurora tile: SHA-256 ' + digest)
    text = header(ranks)
    if '--check' in sys.argv:
        sys.exit(0 if HEADER.read_text(encoding='utf-8') == text else 1)
    HEADER.write_text(text, encoding='utf-8', newline='\n')
    print('wrote', HEADER, 'SHA-256', digest)
