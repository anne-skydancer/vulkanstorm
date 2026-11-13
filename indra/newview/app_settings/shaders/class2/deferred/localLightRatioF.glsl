/**
 * @file localLightRatioF.glsl
 * @brief The shadow pool's local light ratio (unified lighting U2): every light that lights a pixel casts, by one stochastic pick
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

#extension GL_ARB_derivative_control : enable
// The quad's merge needs the fine derivatives (GLSL 4.50 or GL_ARB_derivative_control, optional on a GL 4.0/4.1 device); without
// them the pixels keep their own estimates (noisier; the temporal resolve averages them).
#if (defined(GL_ARB_derivative_control) || __VERSION__ >= 450) && !defined(RATIO_NO_QUAD_MERGE)
#define RATIO_QUAD_MERGE 1
#endif

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// The ratio of the local lights' light at a pixel that the shadows leave: R = sum_i w_i V_i / sum_i w_i per colour
// channel, over the lights of the pixel's 32 x 32 screen tile (the light pool's table and tile list), with w_i the
// light's unshadowed contribution (localLightW, the light pool's) and V_i its visibility. The light passes multiply their
// direct light by R (after the temporal resolve, localLightRatioResolveF), which is exact when the weights are
// proportional to the passes' light (diffuse; a projector's texture enters only as its average luminance).
//
// Stage U2 estimates the visibilities. The pixel picks ONE light j of the list with probability p_j = ws_j / sum ws (ws the
// sum of the weight's channels, LOCAL_PICK stream; one uniform number, remapped after each candidate so that the draws are
// independent: exactly proportional) and shoots 2 of the 8 rays at its emitter disc (a quad of 2 x 2 pixels
// shoots all 8 rays, LOCAL_RAYS stream, the rays' rotation shared by the quad): each ray is a march through the camera's
// depth buffer (stMarchRayScreen) and, where the light is one of the two mapped projectors, also through its depth map
// (blocked by either). The occluded part of the pixel's light, per channel, is estimated by
//     D_c = (blocked rays / rays) * w_jc / p_j,        R_c = 1 - D_c / sum_i w_ic,
// which is unbiased (every light that reaches the pixel has p > 0, so every light casts). The four pixels of the quad
// average their D over the pixels at the same depth (3 %): a half-resolution march, four independent picks. The resolve
// (localLightRatioResolveF) averages the frames. A light under 0.5 % of the pixel's total light is lit unshadowed (its
// estimate is weighted by smoothstep(0.5 t, t, ws_j), t = 1 % of the total: no pop along a light's radius), and the march is
// skipped for it. The estimate is signed (a single pick's share of a channel can exceed the channel's total: a clamp at 1 biases a
// coloured light's shadow) and capped at RATIO_EST_MAX (a share of 1 / 0.005 held the history under 0 for dozens of frames); the history is
// wide (RGBA16F) and the readers clamp.
//
// Output: rgb the ratio, a the share of the pixel's light that comes from a projector that changed since last frame
// (its map's visibility has no history).
//
// The quad's merge uses derivatives: nothing may return or discard before it, and the program draws with the lanes of a
// quad all running (a full-screen triangle).

in vec2 vary_fragcoord;

uniform sampler2D depthMap;
uniform usampler2DArray local_light_tiles;   // RGBA32UI, 3 layers: tile (tx, ty)'s 48 bytes, the count, then the light indices (the GPU cull's, or the CPU's coarse list)
uniform vec2 spot_slot_light;            // the table row of each projector slot's light, -1: none this frame
uniform vec2 spot_slot_changed;          // 1: that light changed since last frame (its visibility has no history)
uniform vec2 spot_shadow_fade;           // 1 - the slot's fade-in (the projectors cross-fade when they swap)
uniform int ratio_steps_max;             // the most steps of a screen march (at least 2)
uniform int ratio_rays;                  // rays per pixel: 2 (the quad shoots the 8), 1 (the floor tier: half the rays)
uniform int shadow_st_frame;             // the frame of the seeds (shadowStochasticUtil.glsl)
uniform mat4 st_march_proj;              // the camera's projection (stochasticScreenMarchUtil.glsl)

#define ST_STREAM_LOCAL_PICK 1
#define ST_STREAM_LOCAL_RAYS 2

vec4 getNorm(vec2 screenpos);
vec4 getPosition(vec2 pos_screen);
vec2 stNoiseStream(vec2 px, int frame, int stream);   // stochasticNoiseUtil.glsl: the core's
int sampleSpotShadowRays(vec3 pos, vec3 norm, int index, vec2 pos_screen, vec2 nz, int k0, int kn, out float fade);
vec3 localLightWeight(int idx, vec3 pos, vec3 n);   // localLightWeightUtil.glsl: the light pool's
void localLightEmitter(int idx, out vec3 eye, out float radius, out float r_excl);
float localLightReach(int idx);
float stMarchRayScreen(vec3 p, vec4 c0, vec3 e, float xr, float reach, float jitter, int k, int steps_max, vec3 eye, float rej);   // the core's

#define RATIO_SOFT_TAU 0.01
#ifdef RATIO_PICK_DEBUG
uniform int dbg_q;   // the offline gate's: which list position's share to report
#endif
#define RATIO_MAX_RAYS 8
#define RATIO_EST_MAX 3.0

#ifdef RATIO_QUAD_MERGE
float qx(float v) { float d = dFdxFine(v); return ((int(gl_FragCoord.x) & 1) == 0) ? v + d : v - d; }
float qy(float v) { float d = dFdyFine(v); return ((int(gl_FragCoord.y) & 1) == 0) ? v + d : v - d; }
vec3 qx3(vec3 v) { return vec3(qx(v.x), qx(v.y), qx(v.z)); }
vec3 qy3(vec3 v) { return vec3(qy(v.x), qy(v.y), qy(v.z)); }
#endif

void main()
{
    vec2 pos_screen = vary_fragcoord.xy;
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(depthMap, px, 0).r;
    bool geo = depth < 1.0;

    ivec2 tile = px >> 5;
    uvec4 tl = uvec4(0u);
    int count = 0;
    if (geo)
    {
        tl = texelFetch(local_light_tiles, ivec3(tile, 0), 0);
        count = min(int(tl.x & 0xFFu), 32);
    }
    int slot0 = int(spot_slot_light.x);
    int slot1 = int(spot_slot_light.y);

    vec3 est = vec3(0.0);    // this pixel's estimate of the occluded part of each channel
    float valid = 0.0;       // 1: this pixel has light and an estimate
    float lz = 1.0e6;        // the pixel's depth, for the merge
    float changed = 0.0;
    if (count > 0)
    {
        vec4 pos = getPosition(pos_screen);
        vec3 n = normalize(getNorm(pos_screen).xyz);
        lz = -pos.z;
        float u = stNoiseStream(gl_FragCoord.xy, shadow_st_frame, ST_STREAM_LOCAL_PICK).x;
        vec3 den = vec3(0.0);
        float den_all = 0.0;
        float den_changed = 0.0;
        int pick = -1;
        int pick_pos = -1;
        float dbg_ws = 0.0;
        vec3 w_pick = vec3(0.0);
        float ws_pick = 0.0;
        for (int i = 0; i < count; ++i)
        {
            int b = 1 + i;
            if ((b & 15) == 0)
            {
                tl = texelFetch(local_light_tiles, ivec3(tile, b >> 4), 0);
            }
            int idx = int((tl[(b >> 2) & 3] >> uint(8 * (b & 3))) & 0xFFu);
            vec3 w = localLightWeight(idx, pos.xyz, n);
            float ws = w.r + w.g + w.b;
            if (!(ws > 0.0))   // also a non-finite weight: one NaN would poison the pixel's history for as long as it is kept
            {
                continue;
            }
            den += w;
            den_all += ws;
#ifdef RATIO_PICK_DEBUG
            if (i == dbg_q) { dbg_ws = ws; }
#endif
            den_changed += (idx == slot0 ? spot_slot_changed.x : (idx == slot1 ? spot_slot_changed.y : 0.0)) * ws;
            // a reservoir: the light replaces the pick with probability pp = ws / (the sum so far); u is remapped so that it stays
            // uniform given the decision (the next candidate's draw is independent of this one's)
            float pp = ws / den_all;
            bool take = u < pp;
            u = take ? u / pp : (u - pp) / (1.0 - pp);
            u = min(u, 0.99999994);
            if (take)
            {
                pick = idx;
                pick_pos = i;
                w_pick = w;
                ws_pick = ws;
            }
        }
#ifdef RATIO_PICK_DEBUG
        frag_color = vec4(float(pick_pos), den_all > 0.0 ? dbg_ws / den_all : 0.0, float(count), den_all);   // the offline gate reads the pick distribution
        return;
#endif
        float t0 = RATIO_SOFT_TAU * den_all;
        float soft_a = smoothstep(0.5 * t0, t0, ws_pick);
        if (pick >= 0)
        {
            changed = den_changed / den_all;
            valid = 1.0;
            int kn = clamp(ratio_rays, 1, 2);
            int lane = (px.x & 1) + 2 * (px.y & 1);
            int k0 = kn == 2 ? 2 * lane : 2 * lane + (shadow_st_frame & 1);   // the floor: one of the lane's two rays, alternating by frame
            vec2 nz = stNoiseStream(vec2(px & ~1), shadow_st_frame, ST_STREAM_LOCAL_RAYS);   // shared by the quad
            vec3 eye;
            float er;
            float xr;
            localLightEmitter(pick, eye, er, xr);
            float reach = localLightReach(pick);
            // the rays toward the emitter disc (a Vogel spiral turned by the quad's noise), as the maps' march
            vec3 po = pos.xyz + n * (0.005 + 0.001 * lz);
            vec3 ld = normalize(eye - po);
            vec3 t1 = normalize(cross(ld, abs(ld.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
            vec3 t2 = cross(ld, t1);
            vec4 c0 = st_march_proj * vec4(po, 1.0);
            float theta = 6.28318531 * nz.x;
            int map_mask = 0;
            float map_weight = 0.0;
            if (pick == slot0 || pick == slot1)
            {
                float fade;
                bool first = pick == slot0;
                map_mask = sampleSpotShadowRays(pos.xyz, n, first ? 0 : 1, pos_screen, nz, k0, kn, fade);
                map_weight = fade * (1.0 - (first ? spot_shadow_fade.x : spot_shadow_fade.y));
            }
            float blocked = 0.0;
            for (int i = 0; i < (soft_a > 0.0 ? kn : 0); ++i)
            {
                int k = k0 + i;
                float r = sqrt((float(k) + 0.5) / float(RATIO_MAX_RAYS));
                float phi = float(k) * 2.39996323 + theta;
                vec3 e = eye + (er * r) * (cos(phi) * t1 + sin(phi) * t2);
                float b = stMarchRayScreen(po, c0, e, xr, reach, nz.y, k, max(ratio_steps_max, 2), eye, xr + 0.05);
                float m = float((map_mask >> i) & 1) * map_weight;
                blocked += max(b, m);
            }
            est = soft_a * (blocked / float(kn)) * w_pick * (den_all / ws_pick) / max(den, vec3(1.0e-20));
            // a channel's share of a faint coloured light that is picked and blocked can reach 1 / 0.005: one such sample would hold
            // the history under 0 (the readers clamp) for dozens of frames. Capped at 3 (R >= -2): the bias is for a pixel whose
            // channel is lit by one light in a few hundred only
            est = clamp(est, vec3(0.0), vec3(RATIO_EST_MAX));
        }
    }

#ifdef RATIO_QUAD_MERGE
    // the quad's merge: the estimates of the quad's pixels that have one, at the same depth (3 %)
    vec3 ex = qx3(est), ey = qy3(est), ed = qy3(ex);
    float vx = qx(valid), vy = qy(valid), vd = qy(vx);
    float zx = qx(lz), zy = qy(lz), zd = qy(zx);
    float t = 0.03 * lz;
    float wx = vx * (abs(zx - lz) < t ? 1.0 : 0.0);
    float wy = vy * (abs(zy - lz) < t ? 1.0 : 0.0);
    float wd = vd * (abs(zd - lz) < t ? 1.0 : 0.0);
    float wsum = valid + wx + wy + wd;
    vec3 merged = wsum > 0.0 ? (valid * est + wx * ex + wy * ey + wd * ed) / wsum : vec3(0.0);
#else
    vec3 merged = est;
#endif
    // signed and unclamped (the history is RGBA16F, the readers clamp); a pixel with no light is R = 1
    vec3 ratio = valid > 0.0 ? vec3(1.0) - merged : vec3(1.0);
    frag_color = vec4(ratio, changed);
}
