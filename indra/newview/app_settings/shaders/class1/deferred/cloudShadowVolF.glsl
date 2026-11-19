/**
 * @file class1/deferred/cloudShadowVolF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The cloud transmittance map from the volumetric clouds (stage L7-3, doc/light/volumetric-clouds.md): the same RG8
 * map, layout and lookup as the flat layer's (cloudShadowMapF.glsl, llcloudshadowmap.cpp), so the sky and the ground
 * show the same clouds. Each texel is a ground point; its light ray is marched through the bands present (16 steps
 * through the creator's band, 6 through each other) on the cheap density (cloudsVolFuncsF.glsl), for the optical
 * depth tau; then the optics of stage L2b (llcloudoptics.h): R the direct beam exp(-(1 - g^2) tau), G the diffuse
 * light under the cloud, the total 1 / (1 + 3/4 (1 - g) tau) minus the beam. A clear texel is exactly (1, 0). A light
 * lower than about 2.9 degrees is followed as if at that elevation (CV_LIGHT_Y_MIN), so its samples stay at the band's
 * heights and a low sun keeps the shadows of a closed deck.
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec2 vary_fragcoord;

uniform vec4 cv_map;    // (cloudsVolFuncsF.glsl) w the band count
uniform vec4 cv_layer[3];
uniform vec4 cv_shape[3];
uniform vec4 cv_place[3]; // w the band's weather channel: 0 the creator's band
uniform vec4 cvs_map;    // xy the map's south-west corner relative to the camera (km, east and north), z its extent (km)
uniform vec4 cvs_light;  // the light the ground is lit by (the sun or the moon, as the flat layer's map), y up
uniform vec4 ctm_optics; // (tau_v, 1 - g^2, 3/4 (1 - g), the drawn opacity's step), llcloudoptics.h

float cv_density(vec3 p, int b, bool full);

void main()
{
    vec2 ground = cvs_map.xy + vary_fragcoord * cvs_map.z;
    // the direction itself is clamped (not only the divisor), so the samples below stand at the heights hh
    vec3 s = cvs_light.y >= 0.05 ? cvs_light.xyz : normalize(vec3(cvs_light.x, 0.05, cvs_light.z));
    float sy = s.y;
    float tau = 0.0;
    int count = int(cv_map.w);
    for (int b = 0; b < 3; ++b)
    {
        if (b >= count)
        {
            break;
        }
        float h0 = cv_layer[b].x;
        float h1 = cv_layer[b].y;
        int n = int(cv_place[b].w) == 0 ? 16 : 6; // the creator's band, wherever it sorts
        float seg = (h1 - h0) / sy / float(n);
        float od = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float hh = h0 + (h1 - h0) * (float(i) + 0.5) / float(n);
            vec3 p = vec3(ground.x, 0.0, ground.y) + s * (hh / sy);
            od += cv_density(p, b, false) * seg;
        }
        tau += od * cv_shape[b].x;
    }
    float t_beam = exp(-ctm_optics.y * tau);
    float t_total = 1.0 / (1.0 + ctm_optics.z * tau);
    frag_color = vec4(t_beam, max(t_total - t_beam, 0.0), 0.0, 1.0);
}
