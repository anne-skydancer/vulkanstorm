/**
 * @file postcolor.slang (converted from srgbF.glsl)
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */
import glsl;

public vec3 srgb_to_linear(vec3 color)
{
    vec3 low_range = color / vec3(12.92);
    vec3 high_range = pow((color + vec3(0.055)) / vec3(1.055), vec3(2.4));
    return mix(high_range, low_range, lessThanEqual(color, vec3(0.04045)));
}

public vec3 linear_to_srgb(vec3 color)
{
    color = clamp(color, vec3(0.0), vec3(1.0));
    vec3 low_range = color * 12.92;
    vec3 high_range = 1.055 * pow(color, vec3(0.41666)) - 0.055;
    return mix(high_range, low_range, lessThan(color, vec3(0.0031308)));
}

public vec3 legacyGamma(vec3 color, float gamma)
{
    vec3 c = 1.0 - clamp(color, vec3(0.0), vec3(1.0));
    return 1.0 - pow(c, vec3(gamma)); // gamma is inverted on the CPU
}
