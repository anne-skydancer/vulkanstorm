/**
 * @file deferred/fullbrightF.glsl
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

#extension GL_ARB_shader_image_load_store : enable

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

#if defined(IS_ALPHA) && (defined(ALPHA_OIT))
uniform sampler2D alpha_oit_opaque_depth; // detached opaque depth
uniform int oit_mode;
#endif

#if defined(IS_ALPHA) && defined(ALPHA_OIT)
// ---- alpha OIT capture: per-pixel slot array (A-buffer in a 2D-array image) ----
// Each pixel owns oit_slot_count slots (one array layer each); a per-pixel atomic count in
// layer 0 of oit_meta hands out slot indices. Fragments past the last slot fold into the
// order-independent tail accumulators in layers 1..5 (fixed-point atomics) and record their
// nearest depth in layer 6; the resolve composites the tail at that depth among the sorted
// slots. Scales must match alphaOITResolveF.glsl.
// Capture-only depth rejection below; normal alpha shader depth behaviour is unchanged.
layout(binding = 0, r32ui) uniform coherent uimage2DArray oit_meta;      // 0 count, 1..3 sum rgb*a, 4 sum a, 5 sum -log2(1-a), 6 ~bits(nearest z)
layout(binding = 1, rg32ui) uniform writeonly uimage2DArray oit_slots;   // { R11F_G11F_B10F premultiplied rgb, depth24 << 8 | alpha8 }
// Node encoding: alphaOITPackF.glsl, attached to every fragment program when ALPHA_OIT is defined.
uint oit_pack_rgb(vec3 c);
uint oit_pack_depth_alpha(float z, float a);
uniform int oit_slot_count;  // slots per pixel (layers of oit_slots)
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0
void oit_append(vec4 c, float z)
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    // Match resolve LEQUAL, including equality; reject before touching the A-buffer.
    if (!(z <= texelFetch(alpha_oit_opaque_depth, p, 0).r)) discard;
    float a   = clamp(c.a, 0.0, 1.0);
    // A fully transparent fragment adds no colour and no coverage; keep it out of the slots
    // and away from the tail's nearest depth, which would otherwise reorder a visible tail.
    if (a <= 0.0) return;
    vec3  rgb = max(c.rgb, vec3(0.0));
    uint k = imageAtomicAdd(oit_meta, ivec3(p, 0), 1u);
    if (k < uint(oit_slot_count))
    {
        imageStore(oit_slots, ivec3(p, int(k)),
                   uvec4(oit_pack_rgb(rgb * a), oit_pack_depth_alpha(z, a), 0u, 0u));
        return;
    }
    // Slot overflow: coverage and the coverage-weighted colour average are order independent.
    // Per-fragment clamps keep the fixed-point sums from wrapping on HDR emissive stacks.
    vec3 w = min(rgb * a, vec3(1024.0));
    imageAtomicAdd(oit_meta, ivec3(p, 1), uint(w.r * OIT_TAIL_COLOR_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 2), uint(w.g * OIT_TAIL_COLOR_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 3), uint(w.b * OIT_TAIL_COLOR_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 4), uint(a * OIT_TAIL_ALPHA_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 5), uint(-log2(max(1.0 - a, 0.0009765625)) * OIT_TAIL_LOG_SCALE + 0.5));
    // Nearest tail depth: window z is a non-negative float, so its bits order like the value;
    // inverted, the maximum over a zero-cleared layer is the minimum depth.
    imageAtomicMax(oit_meta, ivec3(p, 6), ~floatBitsToUint(z));
}
#endif

#if defined(IS_ALPHA) && defined(ALPHA_DEPTH_PEEL)
uniform int alpha_peel_first;
void alpha_depth_peel(inout vec4 c, float z)
{
    if (oit_mode == 2)
    {
        if (alpha_peel_first == 0)
        {
            float selected = texelFetch(alpha_oit_opaque_depth, ivec2(gl_FragCoord.xy), 0).r;
            if (z >= selected) discard;
        }
        c = vec4(z);
    }
    else if (oit_mode == 3)
    {
        float selected = texelFetch(alpha_oit_opaque_depth, ivec2(gl_FragCoord.xy), 0).r;
        if (z != selected) discard;
    }
    else if (oit_mode == 4)
    {
        float selected = texelFetch(alpha_oit_opaque_depth, ivec2(gl_FragCoord.xy), 0).r;
        if (z >= selected) discard;
    }
}
#endif

#if !defined(HAS_DIFFUSE_LOOKUP)
uniform sampler2D diffuseMap;
#endif

in vec3 vary_position;
in vec4 vertex_color;
in vec2 vary_texcoord0;

vec3 srgb_to_linear(vec3 cs);
vec3 linear_to_srgb(vec3 cl);

#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha;
#endif

#ifdef IS_ALPHA
uniform vec4 waterPlane;
void waterClip(vec3 pos);
void calcAtmosphericVars(vec3 inPositionEye, vec3 light_dir, float ambFactor, out vec3 sunlit, out vec3 amblit, out vec3 additive,
                         out vec3 atten);
vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color);
#endif

void mirrorClip(vec3 pos);

void main()
{
    mirrorClip(vary_position);
#ifdef IS_ALPHA
    waterClip(vary_position.xyz);
#endif

#ifdef HAS_DIFFUSE_LOOKUP
    vec4 color = diffuseLookup(vary_texcoord0.xy);
#else
    vec4 color = texture(diffuseMap, vary_texcoord0.xy);
#endif

    float final_alpha = color.a * vertex_color.a;

#ifdef HAS_ALPHA_MASK
    if (color.a < minimum_alpha)
    {
        discard;
    }
#endif

    color.rgb *= vertex_color.rgb;

    vec3 pos = vary_position;

    color.a = final_alpha;
#ifndef IS_HUD
    color.rgb = srgb_to_linear(color.rgb);
#ifdef IS_ALPHA

    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;
    calcAtmosphericVars(pos.xyz, vec3(0), 1.0, sunlit, amblit, additive, atten);

    color.rgb = applySkyAndWaterFog(pos, additive, atten, color).rgb;

#endif

#endif

#if defined(IS_ALPHA) && defined(ALPHA_OIT)
    vec4 oit_out = max(color, vec4(0));
    if (oit_mode == 1) { oit_append(oit_out, gl_FragCoord.z); discard; }
    frag_color = oit_out;
#else
    vec4 oit_out = max(color, vec4(0));
#if defined(IS_ALPHA) && defined(ALPHA_DEPTH_PEEL)
    alpha_depth_peel(oit_out, gl_FragCoord.z);
#endif
    frag_color = oit_out;
#endif
}

