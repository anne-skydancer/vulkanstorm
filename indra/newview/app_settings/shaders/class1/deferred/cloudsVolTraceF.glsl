/**
 * @file class1/deferred/cloudsVolTraceF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The volumetric clouds' trace (stage L7, doc/light/volumetric-clouds.md; the march is cloudsVolFuncsF.glsl).
 *
 * Live (the main view): drawn into a W/4 x H/4 target, a quarter of the half-resolution pixels a frame on a 2x2
 * checkerboard; cloudsVolReconF.glsl rebuilds the half-resolution image from it and the history. Only pixels near the
 * sky are traced: the deferred depth at the far plane (the sky's pool clears the depth after the dome).
 *
 * CLOUDS_VOL_STILL (a photo, a tile of one): every sky pixel of the frame, twice the steps, cv_frame.w sub-samples
 * whose jitter is keyed on the pixel's place in the whole photo (cv_frame.yz is the tile's first pixel), so that the
 * tiles of a photo agree along their borders. No history: the photo shows the clouds of the screen, traced in full.
 * It is drawn straight onto the lit image as the composite is (cloudsVolCompositeV.glsl's screen triangle at the far
 * plane, LEQUAL against the deferred depth, blended dst * T + L), in strips of rows (LLVolumetricClouds), so the
 * depth test passes the sky's pixels alone.
 */

/*[EXTRA_CODE_HERE]*/

#ifdef CLOUDS_VOL_STILL
out vec4 frag_color; // rgb the clouds' radiance (linear, hazed), a their transmittance: blended dst * a + rgb
#else
layout(location = 0) out vec4 frag_trace; // rgb the clouds' radiance (linear, hazed), a their transmittance
layout(location = 1) out vec4 frag_depth; // r the clouds' distance (km)

uniform sampler2D depthMap;  // the deferred depth: 1 where only the sky was drawn
#endif

uniform mat4 cv_inv_vp;      // clip -> world direction (rotation only, z up)
uniform vec4 cv_sizes;       // the full target's width and height, the half-resolution width and height
uniform vec4 cv_frame;       // x the frame (checkerboard, jitter); still: yz the tile's first pixel in the photo, w sub-samples

void cv_trace(vec3 dir, float jitter, bool still, out vec3 col, out float T, out float depth);

// the world direction of a full-resolution pixel position, y up (x east, z north)
vec3 cv_view_dir(vec2 px)
{
    vec2 ndc = px / cv_sizes.xy * 2.0 - 1.0;
    vec4 w = cv_inv_vp * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w);
    return vec3(d.x, d.z, d.y);
}

// interleaved gradient noise
float cv_ign(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

#ifdef CLOUDS_VOL_STILL
void main()
{
    // the depth test let only the sky's pixels through; the still is full size, so no fringe is needed
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 dir = cv_view_dir(gl_FragCoord.xy);
    vec2 g = vec2(px) + cv_frame.yz;
    int n = max(int(cv_frame.w), 1);
    vec3 col = vec3(0.0);
    float t = 0.0;
    for (int s = 0; s < n; ++s)
    {
        vec3 c;
        float tr;
        float depth;
        cv_trace(dir, fract(cv_ign(g) + float(s) / float(n)), true, c, tr, depth);
        col += c;
        t += tr;
    }
    frag_color = vec4(col / float(n), t / float(n));
}
#else
bool cv_is_sky(ivec2 px)
{
    return texelFetch(depthMap, clamp(px, ivec2(0), ivec2(cv_sizes.xy) - 1), 0).r >= 1.0;
}

uvec2 cv_checker(uint frame)
{
    uint k = frame % 4u;
    return k == 0u ? uvec2(0u, 0u) : (k == 1u ? uvec2(1u, 1u) : (k == 2u ? uvec2(1u, 0u) : uvec2(0u, 1u)));
}

// a half-resolution pixel takes clouds when its centre is sky or any pixel of the 4x4 full-resolution block around it
// is: the blocks on a silhouette hold the sky samples the full-resolution composite still reads
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
    uvec2 id = uvec2(gl_FragCoord.xy);
    uint frame = uint(cv_frame.x);
    uvec2 hp = id * 2u + cv_checker(frame);
    if (any(greaterThanEqual(hp, uvec2(cv_sizes.zw))) || !cv_near_sky(hp))
    {
        frag_trace = vec4(0.0, 0.0, 0.0, 1.0);
        frag_depth = vec4(45.0);
        return;
    }
    vec3 dir = cv_view_dir((vec2(hp) + 0.5) * 2.0);
    // interleaved gradient noise, rotated each frame by the golden ratio
    float jitter = fract(cv_ign(vec2(hp)) + float(frame) * 0.618034);
    vec3 col;
    float T;
    float depth;
    cv_trace(dir, jitter, false, col, T, depth);
    frag_trace = vec4(col, T);
    frag_depth = vec4(depth);
}
#endif
