/**
 * @file class1/deferred/cloudsVolWeatherF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The volumetric clouds' weather map (stage L7, doc/light/volumetric-clouds.md): where each band has cloud. The map
 * is periodic; it covers cvw_sky.w tiles of the creator's cloud texture (1, or 16 with cloud_variance, the period of
 * the flat layer's disturbance), x east and y north. It is drawn in strips (a scissor) when what it is made of changes,
 * and slowly over time for the growth and decay. The C++ twin of the arithmetic is llcloudscape.h.
 *
 * R  the creator's band: the flat layer's own taps (cloudsF.glsl alpha1: the cloud texture blended with the next one,
 *    cloud_variance's disturbance, the 2 (cloud_shadow - 0.25) shift and the detail tap; no scroll, which the trace
 *    applies as a drift, and without the self-shadow taps' light shift of 0.0125 of a tile), cut as the Aurora
 *    Viewer's weather (env.wgsl fs_cloud_weather: heaps with a knee, a sheet over one half of cover);
 * G  the middle band's cover (generated altocumulus); B the high band's (generated cirrus);
 * A  the detail tap: a heap's top follows it.
 * The three covers are modulated by c (1 + (1 - c) M), with M a zero-mean field: a mesoscale patch field (plane waves,
 * mean 0.5 over the map exactly) and a growth and decay field (the base noise's Worley FBM, moving through its third
 * axis with time; its mean over the map is the slice's, cvw_growth), so that every band keeps its mean cover.
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D cloud_noise_texture;
uniform sampler2D cloud_noise_texture_next;
uniform float blend_factor;
uniform sampler3D cloud_vol_base;

uniform vec4 cvw_sky;      // x cloud_shadow, y cloud_scale, z cloud_variance, w tiles across the map
uniform vec4 cvw_density1; // xy the creator's offset (cloud_pos_density1, no scroll), z the map's side in texels, w 0.5 - 1 / cloud_scale
uniform vec4 cvw_density2; // xyz the detail tap's offset and weight (cloud_pos_density2)
uniform vec4 cvw_cover;    // x the middle band's mean cover, y the high band's, z the patch amplitude, w the growth amplitude
uniform vec4 cvw_growth;   // xyz the growth field's z for the three bands (noise periods), w cells across the map (noise periods)
uniform vec4 cvw_mean;     // xyz the growth field's mean at those z (the slices' means)

float cvw_tex(vec2 uv)
{
    return mix(texture(cloud_noise_texture, uv).x, texture(cloud_noise_texture_next, uv).x, blend_factor);
}

// a zero-mean field in [-1, 1] from a field v in [0, 1] of mean `mean` (llcloudscape.h centred())
float cvw_centred(float v, float mean)
{
    return clamp((v - mean) / max(max(mean, 1.0 - mean), 1e-6), -1.0, 1.0);
}

// plane waves over the map, mean 0.5 over it exactly (llcloudscape.h patch_field())
float cvw_patch(vec2 x, float phase)
{
    const float TAU = 6.283185307179586;
    float a = cos(TAU * (x.x + phase * 0.37));
    float b = cos(TAU * (x.y + phase * 0.61));
    float c = cos(TAU * (x.x + x.y + phase * 0.13));
    float d = cos(TAU * (2.0 * x.x - x.y + phase * 0.79));
    return 0.5 + 0.5 * (0.3 * a + 0.3 * b + 0.2 * c + 0.2 * d);
}

// the modulation of a cover c (llcloudscape.h modulate())
float cvw_modulate(float c, float m)
{
    return c * (1.0 + (1.0 - c) * m);
}

void main()
{
    vec2 x = gl_FragCoord.xy / cvw_density1.z; // the map's coordinate, 0..1 (east, north)
    float tiles = cvw_sky.w;
    float cloud_shadow = cvw_sky.x;
    float cloud_scale = cvw_sky.y;
    float variance = cvw_sky.z;

    // the flat layer's texture coordinate here (cloudsV.glsl texcoord0: east increases it, north decreases it)
    vec2 uv1 = vec2(cvw_density1.w + x.x * tiles, 0.5 - x.y * tiles);
    vec2 uv3 = uv1 * 16.0;
    // cloudsF.glsl alpha1
    float k = variance * (1.0 - cloud_scale * 0.25);
    vec2 disturbance = vec2(cvw_tex(uv1 / 8.0), cvw_tex((uv3 + uv1) / 16.0)) * k;
    vec2 disturbance2 = vec2(cvw_tex((uv1 + uv3) / 4.0), cvw_tex((uv3 + uv1) / 8.0)) * k;
    uv1 += cvw_density1.xy + disturbance * 0.2;
    uv3 += cvw_density2.xy;
    float density_variance = min(1.0, (disturbance.x * 2.0 + disturbance.y * 2.0 + disturbance2.x + disturbance2.y) * 4.0);
    float cloud_density = 2.0 * (cloud_shadow - 0.25) * (1.0 - density_variance * density_variance);
    float n1 = cvw_tex(uv1) - 0.5;
    float detail = cvw_tex(uv3);
    float fine = n1 + (detail - 0.5) * cvw_density2.z;
    // the Aurora Viewer's cut: soft, so that the 3D shape draws the cloud's edge; heaps keep their gaps until the sheet
    // takes over (HEAP_KNEE 0.4, HEAP_SLOPE 0.2)
    float wc0 = smoothstep(-0.05, 0.45, fine + cloud_density);
    float gc = smoothstep(0.0, 1.0, clamp((cloud_shadow - 0.1) / 0.56, 0.0, 1.0));
    float wc1 = 0.55 + 0.45 * smoothstep(-0.25, 0.25, fine);
    float sheet = clamp((gc - 0.5) * 2.0, 0.0, 1.0);
    float heaps = mix(min(wc0, 0.4) + max(wc0 - 0.4, 0.0) * 0.2, wc0, sheet);
    float primary = mix(heaps, wc1, sheet);

    // the variability: patches and gaps, growth and decay (a + g <= 1, the C++ side)
    float a = cvw_cover.z;
    float g = cvw_cover.w;
    vec2 cells = x * cvw_growth.w;
    float q0 = textureLod(cloud_vol_base, vec3(cells, cvw_growth.x), 0.0).g;
    float q1 = textureLod(cloud_vol_base, vec3(cells + vec2(0.31, 0.57), cvw_growth.y), 0.0).g;
    float q2 = textureLod(cloud_vol_base, vec3(cells + vec2(0.73, 0.19), cvw_growth.z), 0.0).g;
    float m0 = a * cvw_centred(cvw_patch(x, 0.0), 0.5) + g * cvw_centred(q0, cvw_mean.x);
    // the generated bands have no pattern of their own: their patches take what the growth leaves
    float m1 = (1.0 - g) * cvw_centred(cvw_patch(x, 0.37), 0.5) + g * cvw_centred(q1, cvw_mean.y);
    float m2 = (1.0 - g) * cvw_centred(cvw_patch(x, 0.71), 0.5) + g * cvw_centred(q2, cvw_mean.z);

    frag_color = vec4(cvw_modulate(primary, m0), cvw_modulate(cvw_cover.x, m1), cvw_modulate(cvw_cover.y, m2), detail);
}
