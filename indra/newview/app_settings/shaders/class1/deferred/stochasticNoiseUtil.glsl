/**
 * @file stochasticNoiseUtil.glsl
 * @brief The stochastic core's noise: one function, named seed streams
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

// Attached next to shadowStochasticUtil.glsl (LLShaderMgr::attachShaderFeatures). The core has no knowledge of its
// consumers: each consumer asks for its own stream, so that no two of them draw the same numbers at a pixel.
//
// The stream ids below are the contract; a consumer that declares them in its own text must keep the values.
//   0 SUN_RAYS          the sun's rays and cascade pick (the formula of the shipped Stochastic method, kept exactly)
//   1 LOCAL_PICK        the light a pixel picks (unified lighting, stage U2)
//   2 LOCAL_RAYS        the rays toward a local light's emitter (stage U2: its own offset and phase, decorrelated from SUN_RAYS)
//   3 CASTER_COVERAGE   the shadow casters' alpha coverage
//   4 TRANSPARENCY      the transparency pool
// Every stream but SUN_RAYS shifts the pixel by a per-stream offset and the frame phase by a per-stream amount; SUN_RAYS is
// the shipped sun formula, kept exactly (the sun channel is bit identical to the stages before).

#define ST_STREAM_SUN_RAYS 0
#define ST_STREAM_LOCAL_PICK 1
#define ST_STREAM_LOCAL_RAYS 2
#define ST_STREAM_CASTER_COVERAGE 3
#define ST_STREAM_TRANSPARENCY 4

float stIGN(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// two numbers in [0, 1) of the pixel px, the frame (wrapping at 64) and the stream. SUN_RAYS is the shipped formula. The other
// streams turn the pixel lattice by their own angle before the same two-term hash, with their own multiplier and R2 offset and a
// golden phase: a mere shift of the lattice would leave a stream a fixed function of another (the hash is a sawtooth of a
// linear form, so its correlation with its own shift is 1 - 6c(1 - c) for the shift c), and the gates measure the streams'
// pairwise correlations at under 1 % (test_local_light_u2.py).
vec2 stNoiseStream(vec2 px, int frame, int stream)
{
    float phase = 5.588238 * float(frame & 63);
    if (stream == ST_STREAM_SUN_RAYS)
    {
        vec2 p = px + phase;
        return vec2(stIGN(p), stIGN(p.yx + vec2(113.0, 71.0)));
    }
    const float ANGLE[5] = float[5](0.0, 0.4, 2.7, 1.3, 4.2);
    const float MULT[5] = float[5](52.9829189, 63.1, 41.3, 57.7, 47.9);
    int s = clamp(stream, 0, 4);
    vec2 p = px + vec2(0.754877669, 0.569840291) * (97.0 * float(s)) + (phase + 0.618034 * float(s));
    float c = cos(ANGLE[s]);
    float sn = sin(ANGLE[s]);
    vec2 q = vec2(c * p.x - sn * p.y, sn * p.x + c * p.y);
    float m = MULT[s];
    return vec2(fract(m * fract(0.06711056 * q.x + 0.00583715 * q.y)), fract(m * fract(0.06711056 * q.y + 0.00583715 * q.x + 0.06711056 * 113.0 + 0.00583715 * 71.0)));
}
