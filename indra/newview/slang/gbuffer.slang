// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// G-buffer constants and packing shared by the converted programs.
module gbuffer;
import glsl;

// the G-buffer flag of a PBR material (the value of GBUFFER_FLAG_HAS_PBR in the hand-written helper code, llshadermgr.cpp)
public static const float GBUFFER_FLAG_HAS_PBR = 0.67;
// the G-buffer flag of a world surface that takes atmospherics (llshadermgr.cpp: GBUFFER_FLAG_HAS_ATMOS)
public static const float GBUFFER_FLAG_HAS_ATMOS = 0.34;
// The flag values for unlit surfaces and the HDRI sky (llshadermgr.cpp).
public static const float GBUFFER_FLAG_SKIP_ATMOS = 0.0;
public static const float GBUFFER_FLAG_HAS_HDRI = 1.0;

public bool hasGBufferFlag(float data, float flag)
{
    return abs(data - flag) < 0.1;
}

// the normal and the flag of a G-buffer pixel, as globalF.glsl packs them (Lambert azimuthal equal-area; the flag in alpha)
public vec4 encodeNormal(vec3 n, float env, float gbuffer_flag)
{
    float f = sqrt(8 * n.z + 8);
    return vec4(n.xy / f + 0.5, env, gbuffer_flag);
}
