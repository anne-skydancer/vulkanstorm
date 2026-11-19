/**
 * @file class1/deferred/cloudsVolReconF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The volumetric clouds' temporal reconstruction (stage L7, doc/light/volumetric-clouds.md; the Aurora Viewer's
 * `recon`, clouds.wgsl): the half-resolution image from this frame's quarter of fresh pixels (cloudsVolTraceF.glsl)
 * and the last frame's image, reprojected for the camera's rotation and its horizontal move (the clouds are anchored
 * to the world; their drift and growth come through the clamp), clamped to the fresh pixels' 3x3 neighbourhood.
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D depthMap;          // the deferred depth: 1 where only the sky was drawn
uniform sampler2D cloud_vol_trace;   // this frame's trace (W/4 x H/4): rgb radiance, a transmittance
uniform sampler2D cloud_vol_tdepth;  // its distances (km)
uniform sampler2D cloud_vol_history; // the last frame's half-resolution image
uniform mat4 cv_inv_vp;              // clip -> world direction (rotation only, z up)
uniform mat4 cv_prev_vp;             // the last frame's rotation-only view-projection (world z up -> clip)
uniform vec4 cv_sizes;               // the full target's width and height, the half-resolution width and height
uniform vec4 cv_frame;               // x the frame, y 1 = the history is valid
uniform vec4 cv_delta;               // xyz the camera's move since the last frame (km, world z up), w 1 = reproject it

uvec2 cv_checker(uint frame)
{
    uint k = frame % 4u;
    return k == 0u ? uvec2(0u, 0u) : (k == 1u ? uvec2(1u, 1u) : (k == 2u ? uvec2(1u, 0u) : uvec2(0u, 1u)));
}

bool cv_is_sky(ivec2 px)
{
    return texelFetch(depthMap, clamp(px, ivec2(0), ivec2(cv_sizes.xy) - 1), 0).r >= 1.0;
}

bool cv_near_sky(uvec2 hp)
{
    ivec2 lo = ivec2(hp) * 2 - 1;
    for (int dy = 0; dy < 4; ++dy)
    {
        for (int dx = 0; dx < 4; ++dx)
        {
            if (cv_is_sky(lo + ivec2(dx, dy)))
            {
                return true;
            }
        }
    }
    return false;
}

void main()
{
    uvec2 hp = uvec2(gl_FragCoord.xy);
    if (!cv_near_sky(hp))
    {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    ivec2 c = ivec2(hp / 2u);
    ivec2 ts = textureSize(cloud_vol_trace, 0) - 1;
    vec4 mn = vec4(1e9);
    vec4 mx = vec4(-1e9);
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec4 s = texelFetch(cloud_vol_trace, clamp(c + ivec2(dx, dy), ivec2(0), ts), 0);
            mn = min(mn, s);
            mx = max(mx, s);
        }
    }
    c = clamp(c, ivec2(0), ts);
    vec4 fresh = texelFetch(cloud_vol_trace, c, 0);
    float depth = texelFetch(cloud_vol_tdepth, c, 0).r;
    uint frame = uint(cv_frame.x);
    bool is_fresh = all(equal(hp % 2u, cv_checker(frame)));

    // the direction of this pixel (world, z up) and where its cloud was a frame ago
    vec2 ndc = (vec2(hp) + 0.5) * 2.0 / cv_sizes.xy * 2.0 - 1.0;
    vec4 w = cv_inv_vp * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w);
    vec3 pd = dir;
    if (cv_delta.w > 0.5)
    {
        pd = normalize(dir * depth + cv_delta.xyz);
    }
    vec4 clip = cv_prev_vp * vec4(pd, 1.0);
    vec2 puv = clip.xy / clip.w * 0.5 + 0.5;
    bool valid = cv_frame.y > 0.5 && clip.w > 0.0 && all(greaterThanEqual(puv, vec2(0.0))) && all(lessThanEqual(puv, vec2(1.0)));
    vec4 o = fresh;
    if (valid)
    {
        vec4 h = clamp(textureLod(cloud_vol_history, puv, 0.0), mn, mx);
        o = is_fresh ? mix(h, fresh, 0.25) : h;
    }
    frag_color = o;
}
