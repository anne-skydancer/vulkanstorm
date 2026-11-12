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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// The ratio of the local lights' light at a pixel that the shadows leave: R = sum_i w_i V_i / sum_i w_i per colour
// channel, over the lights of the pixel's 32 x 32 screen tile (the light pool's table and tile list), with w_i the
// light's unshadowed contribution (localLightW, the light pool's) and V_i its visibility. The light passes multiply their
// direct light by R (after the temporal resolve, localLightRatioResolveF), which is exact when the weights are
// proportional to the passes' light (diffuse; a projector's texture enters only as its average luminance).
//
// Stage U2 estimates the visibilities. The pixel picks ONE light j of the list with probability p_j = ws_j / sum ws (ws the
// sum of the weight's channels, LOCAL_PICK stream) and shoots 2 of the 8 rays at its emitter disc (a quad of 2 x 2 pixels
// shoots all 8 rays, LOCAL_RAYS stream, the rays' rotation shared by the quad): each ray is a march through the camera's
// depth buffer (stMarchRayScreen) and, where the light is one of the two mapped projectors, also through its depth map
// (blocked by either). The occluded part of the pixel's light, per channel, is estimated by
//     D_c = (blocked rays / rays) * w_jc / p_j,        R_c = 1 - D_c / sum_i w_ic,
// which is unbiased (every light that reaches the pixel has p > 0, so every light casts). The four pixels of the quad
// average their D over the pixels at the same depth (3 %): a half-resolution march, four independent picks. The resolve
// (localLightRatioResolveF) averages the frames. A light under 2 % of its own peak at the pixel is left out of the
// weights (it is lit unshadowed there).
//
// Output: rgb the ratio, a the share of the pixel's light that comes from a projector that changed since last frame
// (its map's visibility has no history).
//
// The quad's merge uses derivatives: nothing may return or discard before it, and the program draws with the lanes of a
// quad all running (a full-screen triangle).

in vec2 vary_fragcoord;

uniform sampler2D depthMap;
uniform usampler2D local_light_tiles;    // R8UI, 33 texels per tile, tile (tx, ty) at x = 33 tx, y = ty: the count, then the light indices
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
vec3 localLightWR(int idx, vec3 pos, vec3 n, out float rel);   // localLightWeightUtil.glsl: the light pool's
void localLightEmitter(int idx, out vec3 eye, out float radius, out float r_excl);
float localLightReach(int idx);
float stMarchRayScreen(vec3 p, vec4 c0, vec3 e, float xr, float reach, float jitter, int k, int steps_max);   // the core's

#define RATIO_REL_MIN 0.02
#define RATIO_MAX_RAYS 8

float qx(float v) { float d = dFdxFine(v); return ((int(gl_FragCoord.x) & 1) == 0) ? v + d : v - d; }
float qy(float v) { float d = dFdyFine(v); return ((int(gl_FragCoord.y) & 1) == 0) ? v + d : v - d; }
vec3 qx3(vec3 v) { return vec3(qx(v.x), qx(v.y), qx(v.z)); }
vec3 qy3(vec3 v) { return vec3(qy(v.x), qy(v.y), qy(v.z)); }

void main()
{
    vec2 pos_screen = vary_fragcoord.xy;
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(depthMap, px, 0).r;
    bool geo = depth < 1.0;

    ivec2 tile = ivec2((px.x >> 5) * 33, px.y >> 5);
    int count = geo ? int(texelFetch(local_light_tiles, tile, 0).r) : 0;
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
        vec3 w_pick = vec3(0.0);
        float ws_pick = 0.0;
        for (int i = 0; i < count; ++i)
        {
            int idx = int(texelFetch(local_light_tiles, tile + ivec2(1 + i, 0), 0).r);
            float rel;
            vec3 w = localLightWR(idx, pos.xyz, n, rel);
            float ws = w.r + w.g + w.b;
            if (ws <= 0.0 || rel < RATIO_REL_MIN)
            {
                continue;
            }
            den += w;
            den_all += ws;
            den_changed += (idx == slot0 ? spot_slot_changed.x : (idx == slot1 ? spot_slot_changed.y : 0.0)) * ws;
            u = fract(u + 0.618034);   // a stratified reservoir: the light replaces the pick with probability ws / (the sum so far)
            if (u * den_all < ws)
            {
                pick = idx;
                w_pick = w;
                ws_pick = ws;
            }
        }
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
            for (int i = 0; i < kn; ++i)
            {
                int k = k0 + i;
                float r = sqrt((float(k) + 0.5) / float(RATIO_MAX_RAYS));
                float phi = float(k) * 2.39996323 + theta;
                vec3 e = eye + (er * r) * (cos(phi) * t1 + sin(phi) * t2);
                float b = stMarchRayScreen(po, c0, e, xr, reach, nz.y, k, max(ratio_steps_max, 2));
                float m = float((map_mask >> i) & 1) * map_weight;
                blocked += max(b, m);
            }
            est = (blocked / float(kn)) * w_pick * (den_all / ws_pick) / max(den, vec3(1.0e-20));
        }
    }

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
    // a pixel with no light is R = 1: the estimates of its neighbours are not its own
    vec3 ratio = valid > 0.0 ? clamp(vec3(1.0) - merged, vec3(0.0), vec3(1.0)) : vec3(1.0);
    frag_color = vec4(ratio, changed);
}
