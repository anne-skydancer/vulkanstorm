/**
 * @file class1\windlight\skyFilterF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * Stage AT2-1: the EEP filter's tables (LLAtmosphereLUTs, llatmosphereluts.cpp). Each EEP sky is a filter on the physical sky
 * (doc/light/atmosphere-study.md section 8): the sky you see is today's look exactly. skyV.glsl's haze colour is linear in its glow
 * term, colour = A + B haze_glow, so two tables hold it (variant c of doc/light/at2-qualification-phase1.md): A, the colour at
 * haze_glow 0, and B, its slope. A draw evaluates the glow exactly (one pow) and takes skyF.glsl's x2 and clamp 5 after the lookup.
 * A and B depend on the direction of rel_pos only through its elevation, so they are one column, a row per elevation of the sky-view
 * grid. The uniforms are the dome's own (LLAtmosphereLUTs sets them from LLEnvironment's lists, the values the tables' key is formed
 * from), and this is skyV.glsl's arithmetic with the glow factored out; LLSkyFilter::haze_ab (llskyfilter.h) is its C++ twin, held to
 * it by scripts/light/gen_sky_filter_vectors.py. SF_PASS selects the pass (a permutation); the grid's size is SF_SKY_W x SF_SKY_H
 * (defines from LLAtmosphereLUTs; 192 x 108):
 *
 *   1  A, B     1 x 108    out 0 A, out 1 B, row j at the elevation of the sky-view grid's row j (atmosphereTablesF.glsl's
 *                          parameterisation, in the dome's frame: the elevation of skyV's rel_pos)
 *   2  k rows   1 x 54     per row of the upper hemisphere: the cosine-weighted sums of the luminance of the physical sky (the sky view)
 *                          and of the filtered sky W (today's look in the lighting pass's linear units: the row's A and B, each
 *                          texel's glow)
 *   3  k        1 x 1      x the base's irradiance, y the filtered sky's, z the cast-light level k = x / y, w 1 when k is defined
 *
 * The light the sky casts (probes, shade) is physical in level and takes the creator's tint: k x W. k is defined only where both skies
 * cast light: y above 1e-6 and x above 1e-6 of y (LLSkyFilter::cast_light_defined). Elsewhere (a creator's black sky; the base at night
 * with the moon down) w is 0 and the cast light is today's (AT2-2).
 */

#ifndef SF_PASS
#define SF_PASS 1
#endif
#if !defined(SF_SKY_W) || !defined(SF_SKY_H)
#error the sky-view grid's size is a define (LLAtmosphereLUTs)
#endif

layout(location = 0) out vec4 frag_data0;
#if SF_PASS == 1
layout(location = 1) out vec4 frag_data1;
#endif

// skyV.glsl's uniforms, the values the dome gets (set by LLAtmosphereLUTs)
uniform vec3  lightnorm;
uniform vec3  sunlight_color;
uniform vec3  moonlight_color;
uniform vec3  ambient_color;
uniform vec3  blue_horizon;
uniform vec3  blue_density;
uniform float haze_horizon;
uniform float haze_density;
uniform float cloud_shadow;
uniform float density_multiplier;
uniform float max_y;
uniform vec3  glow;
uniform float sun_moon_blend;
uniform vec3  sun_glow_norm;
// the lighting pass's decode of a sky pixel (softenLightF.glsl)
uniform float sky_hdr_scale;
uniform int   classic_mode;

uniform sampler2D sf_sky_view;               // the physical sky (atmosphereTablesF.glsl, pass 6)
uniform sampler2D sf_table_a;                // pass 1's columns
uniform sampler2D sf_table_b;
uniform sampler2D sf_rows;                   // pass 2's rows

vec3 srgb_to_linear(vec3 c);

const ivec2 SF_SIZE = ivec2(SF_SKY_W, SF_SKY_H);
const float SF_PI = 3.14159265358979;
const float SF_IRR_MIN = 1e-6;               // LLSkyFilter::IRR_MIN, K_MIN
const float SF_K_MIN = 1e-6;

// the sky-view parameterisation (atmosphereTablesF.glsl at_sky_view_dir, LLSkyFilter::sky_view_dir)
float sf_latitude(float j)
{
    float v = j / float(SF_SIZE.y - 1) * 2.0 - 1.0;
    return sign(v) * v * v * 0.5 * SF_PI;
}

vec3 sf_dir(ivec2 t)
{
    float el = sf_latitude(float(t.y));
    float az = (float(t.x) / float(SF_SIZE.x - 1) * 2.0 - 1.0) * SF_PI;
    return vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
}

// skyV.glsl for the direction d of rel_pos, with haze_glow factored out: colour = A + B haze_glow. rel_pos is clamped in altitude as
// skyV clamps it (only its length changes), so A and B depend on d.y only; a non-finite colour (max_y 0 collapses rel_pos to a point,
// normalize(0)) is black, as the lighting pass's clampHDRRange makes today's NaN
void sf_haze_ab(vec3 d, out vec3 A, out vec3 B)
{
    vec3 rel_pos = d;
    if (rel_pos.y > 0.)
    {
        rel_pos *= (max_y / rel_pos.y);
    }
    if (rel_pos.y < 0.)
    {
        rel_pos *= (-32000. / rel_pos.y);
    }
    vec3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    vec3 sunlight = mix(sunlight_color, moonlight_color * 0.7, sun_moon_blend);
    vec3 light_atten = (blue_density + vec3(haze_density * 0.25)) * (density_multiplier * max_y);
    vec3 combined_haze = max(abs(blue_density) + vec3(abs(haze_density)), vec3(1e-6));
    vec3 blue_weight   = blue_density / combined_haze;
    vec3 haze_weight   = haze_density / combined_haze;
    float off_axis = 1.0 / max(1e-6, max(0., rel_pos_norm.y) + lightnorm.y);
    sunlight *= exp(-light_atten * off_axis);
    float density_dist = rel_pos_len * density_multiplier;
    combined_haze = exp(-combined_haze * density_dist);

    vec3 kb = blue_horizon * blue_weight;
    vec3 kh = haze_horizon * haze_weight;
    // the haze above the cloud, at haze_glow 0 and its slope
    vec3 above_a = (kb * (sunlight + ambient_color) + kh * ambient_color) * (1. - combined_haze);
    vec3 above_b = (kh * sunlight) * (1. - combined_haze);
    // below the cloud
    vec3 ambient = ambient_color + max(vec3(0), (1. - ambient_color)) * cloud_shadow * 0.5;
    vec3 sun_below = sunlight * max(0.0, (1. - cloud_shadow));
    vec3 below_a = kb * (sun_below + ambient) + kh * ambient;
    vec3 below_b = kh * sun_below;
    vec3 f = vec3(1.) - sqrt(sqrt(combined_haze));
    A = above_a + (below_a - above_a) * f;
    B = above_b + (below_b - above_b) * f;
    if (any(isnan(A)) || any(isinf(A)) || any(isnan(B)) || any(isinf(B)))
    {
        A = vec3(0.0);
        B = vec3(0.0);
    }
}

// skyV.glsl's haze_glow for the direction d (the sun's glow at the sun, fading with the band, L3)
float sf_haze_glow(vec3 d)
{
    float g = max(1.0 - dot(d, sun_glow_norm), .001) * glow.x;
    return (1.0 - sun_moon_blend) * (pow(g, glow.z) + 0.25);
}

// the filtered sky W in the lighting pass's linear units: skyF's x2 and clamp 5, then softenLightF's decode and the classic scale
vec3 sf_look(vec3 A, vec3 B, float haze_glow)
{
    vec3 c = clamp((A + B * haze_glow) * 2.0, vec3(0.0), vec3(5.0));
    return srgb_to_linear(c) * sky_hdr_scale * (classic_mode > 0 ? 1.1 : 1.0);
}

float sf_lum(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
#if SF_PASS == 1
    // row j: the elevation of the grid's row j, towards +x (any azimuth gives the same A and B; LLSkyFilter::row_dir)
    float el = sf_latitude(float(p.y));
    vec3 A;
    vec3 B;
    sf_haze_ab(vec3(cos(el), sin(el), 0.0), A, B);
    frag_data0 = vec4(A, 1.0);
    frag_data1 = vec4(B, 1.0);
#elif SF_PASS == 2
    // row j of the upper hemisphere (the grid's rows SF_SIZE.y / 2 .. SF_SIZE.y - 1); the columns 0 .. w - 2 (the last is the first
    // again); weight cos(zenith) x the cell's solid angle, cos(el) d_el d_az, d_el the half span to the neighbouring rows
    int j = SF_SIZE.y / 2 + p.y;
    float el = sf_latitude(float(j));
    float el_lo = sf_latitude(float(j - 1));
    float el_hi = j + 1 < SF_SIZE.y ? sf_latitude(float(j + 1)) : el;
    float w = sin(max(el, 0.0)) * cos(el) * 0.5 * (el_hi - el_lo) * (2.0 * SF_PI / float(SF_SIZE.x - 1));
    vec3 A = texelFetch(sf_table_a, ivec2(0, j), 0).rgb;
    vec3 B = texelFetch(sf_table_b, ivec2(0, j), 0).rgb;
    float sum_p = 0.0;
    float sum_w = 0.0;
    for (int i = 0; i < SF_SIZE.x - 1; ++i)
    {
        ivec2 t = ivec2(i, j);
        sum_p += sf_lum(texelFetch(sf_sky_view, t, 0).rgb);
        sum_w += sf_lum(sf_look(A, B, sf_haze_glow(sf_dir(t))));
    }
    frag_data0 = vec4(sum_p * w, sum_w * w, 0.0, 1.0);
#else
    float irr_p = 0.0;
    float irr_w = 0.0;
    for (int r = 0; r < SF_SIZE.y / 2; ++r)
    {
        vec2 s = texelFetch(sf_rows, ivec2(0, r), 0).rg;
        irr_p += s.x;
        irr_w += s.y;
    }
    // both skies cast light (LLSkyFilter::cast_light_defined): the filtered sky some, the physical more than a millionth of it
    bool defined = !isnan(irr_p) && !isnan(irr_w) && irr_w > SF_IRR_MIN && irr_p > SF_K_MIN * irr_w;
    frag_data0 = vec4(irr_p, irr_w, defined ? irr_p / irr_w : 0.0, defined ? 1.0 : 0.0);
#endif
}
