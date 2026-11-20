// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The shared position module: every vertex program takes its clip position from here, skinning included
// (slang_check.py position fails a program that writes gl_Position or SV_Position any other way). One text for the
// position keeps depth and coverage identical between the passes that must agree.
//
// The AMD decal fix. A tattoo or applier is drawn over the body by another program than the body, so the two positions
// must come out bit for bit alike. Two things give that: every program gets the position from the functions below (one
// text, in one compilation unit, where the hand-written GLSL had the skinning in a separate object), and every vertex
// program declares `invariant gl_Position;` (GLSL 4.60 section 4.8.1: the same expression of the same inputs gives the same
// value in every program, whatever the optimiser does with the rest of the shader). Slang emits `invariant` only from
// the prelude below; its `precise` is dropped from the GLSL when the temporary folds away, so it is not used.
// invariant makes the compiler write multiply then add where it would fuse them, which moves depth by a few steps on
// NVIDIA (reports/amd-decal-invariance-2026-11-18/invariance-audit.md 3.4); VK_POSITION_INVARIANT 0 takes it out for
// every program at once.
//
// The system avatar's own skin (the 45-row eye-space palette of avatarV.glsl) joins here with its program, which waits
// for the forward lighting and atmospherics.
module position;
import glsl;

// Off until the switchover: while hand-written programs (not invariant) draw over generated ones, an invariant position
// moves depth by up to 2 ULP against them (rigged bump on NVIDIA, ab_harness.py programs "depth (info)"), so glow or
// an applier over a generated body could z-fight. The switchover turns it on for every program at once.
#ifndef VK_POSITION_INVARIANT
#define VK_POSITION_INVARIANT 0
#endif

// LL_MAX_JOINTS_PER_MESH_OBJECT (indra/llcharacter/lljoint.h); test_slang_pipeline.py checks the two agree.
public static const int VK_MAX_JOINTS = 110;

void vkInvariantPosition()
{
#if VK_POSITION_INVARIANT
    __target_switch
    {
    case glsl:
        __requirePrelude("invariant gl_Position;");
    default:
    }
    // SPIR-V: the Invariant decoration on the Position output replaces this prelude; add it when the SPIR-V line starts,
    // or the AMD fix is silently gone there.
#endif
}

// A model-space point through the model-view-projection matrix (every static prim, attachment and face).
public vec4 vkPositionClip(mat4 mvp, vec3 p)
{
    vkInvariantPosition();
    return mvp * vec4(p, 1.0);
}

// A point already in normalised device coordinates (full-screen quads and cube faces drawn as -1..1).
public vec4 vkPositionNdc(vec3 p)
{
    vkInvariantPosition();
    return vec4(p, 1.0);
}

// The diagnostic line endpoints are already in homogeneous eye space.
public vec4 vkPositionProject(mat4 projection, vec4 eye)
{
    vkInvariantPosition();
    return projection * eye;
}

// A geometry stage emits the clip-space points computed by its vertex stage.
public vec4 vkPositionPassthrough(vec4 clip)
{
    vkInvariantPosition();
    return clip;
}

// The skin of a rigged object: up to four joints of the 110-matrix palette (3x4, the translation in the last row of
// each column) blended by weight4 (joint index in the integer part, weight in the fraction).
public mat4 vkSkinObject(mat3x4 palette[VK_MAX_JOINTS], vec4 weight4)
{
    vec4 w = fract(weight4);
    vec4 index = floor(weight4);

    index = min(index, vec4(VK_MAX_JOINTS - 1));
    index = max(index, vec4(0.0));

    w *= 1.0 / (w.x + w.y + w.z + w.w);

    int i1 = int(index.x);
    int i2 = int(index.y);
    int i3 = int(index.z);
    int i4 = int(index.w);

    mat3 m = mat3(palette[i1]) * w.x;
    m += mat3(palette[i2]) * w.y;
    m += mat3(palette[i3]) * w.z;
    m += mat3(palette[i4]) * w.w;

    vec3 t = vec3(palette[i1][0].w, palette[i1][1].w, palette[i1][2].w) * w.x;
    t += vec3(palette[i2][0].w, palette[i2][1].w, palette[i2][2].w) * w.y;
    t += vec3(palette[i3][0].w, palette[i3][1].w, palette[i3][2].w) * w.z;
    t += vec3(palette[i4][0].w, palette[i4][1].w, palette[i4][2].w) * w.w;

    mat4 ret;
    ret[0] = vec4(m[0], 0);
    ret[1] = vec4(m[1], 0);
    ret[2] = vec4(m[2], 0);
    ret[3] = vec4(t, 1.0);
    return ret;
}

// A rigged object's point: `modelview_skin` is modelview_matrix * vkSkinObject(...). `eye` is the eye-space position
// the normals and the varyings use, so they come from the very same product as the clip position.
public vec4 vkPositionRigged(mat4 projection, mat4 modelview_skin, vec3 p, out vec3 eye)
{
    vkInvariantPosition();
    vec3 e = (modelview_skin * vec4(p, 1.0)).xyz;
    eye = e;
    return projection * vec4(e, 1.0);
}
