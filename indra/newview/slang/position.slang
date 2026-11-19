// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The shared position module: every vertex program takes its clip position from here (slang_check.py position fails
// a program that writes gl_Position or SV_Position any other way). One text for the position keeps depth and coverage
// identical between the passes that must agree. The AMD decal `invariant`/`precise` goes here when it is needed.
module position;
import glsl;

// A model-space point through the model-view-projection matrix.
public vec4 vkPositionClip(mat4 mvp, vec3 p)
{
    return mvp * vec4(p, 1.0);
}

// A point already in normalised device coordinates (full-screen quads and cube faces drawn as -1..1).
public vec4 vkPositionNdc(vec3 p)
{
    return vec4(p, 1.0);
}
