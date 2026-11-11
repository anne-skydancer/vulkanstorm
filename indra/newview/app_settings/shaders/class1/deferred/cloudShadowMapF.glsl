/**
 * @file class1\deferred\cloudShadowMapF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The cloud transmittance map (stage L2 of the light study, llcloudshadow.h): the share of the sun's
 * (or the moon's) light that the flat cloud layer lets through, 1 - alpha1 of cloudsF.glsl, for
 * a 512x512 patch of the world. Texel (i, j) holds the cloud over the world point it covers, seen in
 * the light's direction. The texture coordinates are the sky's own (cloudsV.glsl) for that cloud:
 * ctm_tc0 + uv * ctm_tc_span, from the C++ unit; the taps below are cloudsF.glsl's, line for line
 * (uniforms come from the sky shader group, so they are the sky's values).
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec2 vary_fragcoord;

uniform sampler2D cloud_noise_texture;
uniform sampler2D cloud_noise_texture_next;
uniform float blend_factor;
uniform vec3 cloud_pos_density1;
uniform vec3 cloud_pos_density2;
uniform float cloud_scale;
uniform float cloud_variance;
uniform float cloud_shadow;
uniform vec3 lightnorm;

uniform vec2 ctm_tc0;
uniform vec2 ctm_tc_span;
uniform float ctm_altitude_blend;
// L2b (llcloudoptics.h): (tau_v, 1 - g^2, 3/4 (1 - g), the drawn opacity's step)
uniform vec4 ctm_optics;

vec4 cloudNoise(vec2 uv)
{
    vec4 a = texture(cloud_noise_texture, uv);
    vec4 b = texture(cloud_noise_texture_next, uv);
    return mix(a, b, blend_factor);
}

void main()
{
    if (cloud_scale < 0.001)
    {
        frag_color = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    // cloudsV.glsl
    vec2 tc0 = ctm_tc0 + vary_fragcoord * ctm_tc_span;
    vec2 tc1 = tc0;
    tc1.x += lightnorm.x * 0.0125;
    tc1.y += lightnorm.z * 0.0125;
    vec2 tc2 = tc0 * 16.;
    vec2 tc3 = tc1 * 16.;

    // cloudsF.glsl
    vec2 uv1 = tc0;
    vec2 uv2 = tc1;
    vec2 uv3 = tc2;
    vec2 uv4 = tc3;

    float cloudDensity = 2. * (cloud_shadow - 0.25);

    vec2 disturbance  = vec2(cloudNoise(uv1 / 8.0f).x, cloudNoise((uv3 + uv1) / 16.0f).x) * cloud_variance * (1.0f - cloud_scale * 0.25f);
    vec2 disturbance2 = vec2(cloudNoise((uv1 + uv3) / 4.0f).x, cloudNoise((uv4 + uv2) / 8.0f).x) * cloud_variance * (1.0f - cloud_scale * 0.25f);

    uv1 += cloud_pos_density1.xy + (disturbance * 0.2);
    uv3 += cloud_pos_density2.xy;

    float density_variance = min(1.0, (disturbance.x* 2.0 + disturbance.y* 2.0 + disturbance2.x + disturbance2.y) * 4.0);

    cloudDensity *= 1.0 - (density_variance * density_variance);

    float alpha1 = (cloudNoise(uv1).x - 0.5) + (cloudNoise(uv3).x - 0.5) * cloud_pos_density2.z;
    alpha1 = min(max(alpha1 + cloudDensity, 0.) * 10 * cloud_pos_density1.z, 1.);

    alpha1 = 1. - alpha1 * alpha1;
    alpha1 = 1. - alpha1 * alpha1;

    alpha1 *= ctm_altitude_blend;
    alpha1 = clamp(alpha1, 0.0, 1.0);

    // L2b (llcloudoptics.h): the drawn opacity as an optical depth, then the light that gets through:
    // R the direct beam (what casts the shadow), G the diffuse light under the cloud (total - beam).
    // A clear texel is exactly (1, 0).
    float tau = -ctm_optics.x * log(max(1.0 - alpha1, ctm_optics.w));
    float t_beam = exp(-ctm_optics.y * tau);
    float t_total = 1.0 / (1.0 + ctm_optics.z * tau);
    frag_color = vec4(t_beam, max(t_total - t_beam, 0.0), 0.0, 1.0);
}
