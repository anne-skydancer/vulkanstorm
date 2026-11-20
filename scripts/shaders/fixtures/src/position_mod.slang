// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Fixture: stands in for the shared position module. Every vertex program takes its position from here.
module position_mod;
import glsl;

public vec4 vkPositionClip(mat4 mvp, vec3 p)
{
    return mvp * vec4(p, 1.0);
}

public vec4 vkPositionPassthrough(vec4 clip)
{
    return clip;
}
