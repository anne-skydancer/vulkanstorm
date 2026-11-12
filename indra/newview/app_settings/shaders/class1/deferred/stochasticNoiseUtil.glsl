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
//   2 LOCAL_RAYS        the rays toward a local light's emitter. In stage U1 it is the formula of SUN_RAYS, so the
//                       projectors' rays are the ones stage U0 drew and the image stays the same; stage U2 gives it its own
//   3 CASTER_COVERAGE   the shadow casters' alpha coverage
//   4 TRANSPARENCY      the transparency pool
// Streams 1, 3 and 4 shift the pixel by a per-stream offset and the frame phase by a per-stream amount.

#define ST_STREAM_SUN_RAYS 0
#define ST_STREAM_LOCAL_PICK 1
#define ST_STREAM_LOCAL_RAYS 2
#define ST_STREAM_CASTER_COVERAGE 3
#define ST_STREAM_TRANSPARENCY 4

float stIGN(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// two numbers in [0, 1) of the pixel px, the frame (wrapping at 64) and the stream
vec2 stNoiseStream(vec2 px, int frame, int stream)
{
    float phase = 5.588238 * float(frame & 63);
    vec2 p;
    if (stream == ST_STREAM_SUN_RAYS || stream == ST_STREAM_LOCAL_RAYS)
    {
        p = px + phase;
    }
    else
    {
        // R2 offset and a golden phase per stream
        p = px + vec2(0.754877669, 0.569840291) * (97.0 * float(stream)) + (phase + 0.618034 * float(stream));
    }
    return vec2(stIGN(p), stIGN(p.yx + vec2(113.0, 71.0)));
}
