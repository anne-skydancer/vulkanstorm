/**
 * @file stochasticAlphaSamples.slang
 * @brief Stochastic transparency: this frame's accumulation, its grain and its depths, measured once per pixel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 * Portions adapted from Aurora Viewer PR 3663 (oit_stochastic.wgsl), Gaalrauch / Rohacan Hirons,
 * GPL-3.0-or-later (the grain measure, the frame weight, the smooth, the non-finite guards)
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
module stochasticAlphaSamples;
import glsl;

public static const float HDR_MAX = 65504.0;       // the largest value an RGBA16F target holds

public bool st_nan(float x)
{
    return (asuint(x) & 0x7fffffffu) > 0x7f800000u;
}

// A sample as the resolve takes it: nothing where a part is not a number, no part under 0 nor over what the target holds
public vec4 st_sample(vec4 a)
{
    bool bad = st_nan(a.r) || st_nan(a.g) || st_nan(a.b) || st_nan(a.a);
    return bad ? vec4(0.0) : clamp(a, vec4(0.0), vec4(HDR_MAX));
}

// A sample of the transparency: between nothing and all of the light, and all of it where it is not a number
public float st_through(float x)
{
    return st_nan(x) ? 1.0 : clamp(x, 0.0, 1.0);
}

// A sample of the glow: what the composite takes (alphaStochasticCompositeF.glsl st_amount)
public float st_amount(float x)
{
    return st_nan(x) ? 0.0 : clamp(x, 0.0, HDR_MAX);
}
