/**
 * @file class1/deferred/cloudsVolFuncsF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The volumetric clouds (stage L7 of the light study, doc/light/volumetric-clouds.md): the density of a band, the
 * light on a cloud, the march along a view and the haze in front of the clouds. Linked into the trace, the still
 * trace (cloudsVolTraceF.glsl) and the volumetric cloud shadow map (cloudShadowVolF.glsl).
 *
 * The model is the Aurora Viewer's volumetric clouds (crates/aurora-render/src/shaders/clouds.wgsl, the owner's code,
 * GPL-3.0-or-later), after Schneider and Vos, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn" (SIGGRAPH
 * 2015; Nubis 2017): a weather map, a height profile per cloud type and a Perlin-Worley base noise eroded by a Worley
 * noise; Hillaire's energy-conserving integration (Frostbite, SIGGRAPH 2016) with Wrenninge's multiple-scattering
 * octaves; the approximate Mie phase of Jendersie and d'Eon (SIGGRAPH 2023). Extended to the bands of the cloud
 * kind and variability layer (llcloudscape.h): each band has its own genus, height range, extinction, erosion, noise
 * scale, offset and drift.
 *
 * Frame: kilometres, y up, x east, z north, the camera at the origin; the clouds' light cv_light in the same frame.
 * The light inputs and the haze are the flat layer's own terms (cloudsV.glsl), read from the sky's uniforms (the
 * programs are in the sky shader group): the EEP filter on the clouds is the creator's look of today's clouds.
 */

uniform sampler3D cloud_vol_base;    // 128^3 RGBA8: R Perlin-Worley, G B A Worley FBMs (TileableVolumeNoise, MIT)
uniform sampler3D cloud_vol_erosion; // 32^3 RGBA8: R G B Worley FBMs
uniform sampler2D cloud_vol_curl;    // 128^2 RGBA8: RG the curl * 0.5 + 0.5
uniform sampler2D cloud_vol_weather; // the weather map (cloudsVolWeatherF.glsl): R the creator's band, G middle, B high, A detail

// the bands (llcloudscape.h), sorted by height; cv_map.w of them are present
uniform vec4 cv_layer[3];  // x base km, y top km, z profile (0 sheet .. 1 heap), w cover gain
uniform vec4 cv_shape[3];  // x extinction km^-1 at density 1, y erosion, z anvil, w the base noise's tile (km)
uniform vec4 cv_place[3];  // xy the band's offset in the weather map (km), z sheet, w the weather channel
uniform vec4 cv_noise[3];  // xy the band's offset in the noises (km), z fibre stretch along the wind, w far (km)
uniform vec4 cv_march[3];  // x steps, y light steps, z the step count's cap, w unused
uniform vec4 cv_map;       // x the weather map's period (km), yz the wind's direction (east, north; unit), w band count
uniform vec4 cv_light;     // xyz the clouds' light (y up, unit)
uniform vec4 cv_gain;      // x sun gain, y sky light gain (the Aurora Viewer's, fitted side by side with the flat layer)

// the sky's uniforms the flat clouds read (cloudsV.glsl)
uniform vec3 sunlight_color;
uniform vec3 ambient_color;
uniform vec3 blue_horizon;
uniform vec3 blue_density;
uniform float haze_horizon;
uniform float haze_density;
uniform float cloud_shadow;
uniform float density_multiplier;
uniform float max_y;
uniform vec3 glow;
uniform vec3 cloud_color;
uniform vec3 cloud_light_transmittance; // AT1: the sun's transmittance at the cloud layer (linear)
uniform float cloud_moon_blend;         // L3: the clouds' own handover, 0 the sun, 1 the moon or night
uniform float sky_hdr_scale;

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb_unclamped(vec3 c);

const float CV_PI = 3.14159265;
const float CV_R_PLANET = 6371.0;
const float CV_BASE_Y = 2.1;       // the base noise is stretched sideways: its tile is 2.1 times wider than tall
const float CV_EROSION_KM = 0.5;   // the erosion noise's tile
const float CV_CURL_KM = 3.0;      // the curl noise's tile
const float CV_STEP_WARP = 1.6;
const float CV_STEP_MAX_KM = 0.3;
const float CV_DENSITY_MIN = 0.002;

float cv_remap(float v, float lo, float hi, float nlo, float nhi)
{
    return nlo + (v - lo) * (nhi - nlo) / (hi - lo);
}

float cv_height(vec3 p)
{
    return length(p + vec3(0.0, CV_R_PLANET, 0.0)) - CV_R_PLANET;
}

// Rounded bottoms and tops for every type: a sheet a slab, a heap a dome whose top follows `top`, a cumulonimbus
// (anvil 1) a column to its top.
float cv_height_grad(float hf, float t, float top, float anvil)
{
    float top_end = mix(mix(1.0, mix(0.45, 1.0, top), t), 1.0, anvil);
    float base_ramp = smoothstep(0.0, mix(0.35, 0.08, t), hf);
    float top_fall = 1.0 - smoothstep(mix(mix(0.55, 0.15, t), 0.85, anvil) * top_end, top_end, hf);
    return base_ramp * top_fall;
}

// The noises' position of a point of band b: its offset, and the fibres of cirrus stretched along the wind.
vec3 cv_noise_pos(vec3 p, float h, int b)
{
    vec2 q = p.xz + cv_noise[b].xy;
    float stretch = cv_noise[b].z;
    if (stretch > 1.0)
    {
        vec2 w = cv_map.yz;
        vec2 n = vec2(-w.y, w.x);
        q = w * (dot(q, w) / stretch) + n * dot(q, n);
    }
    return vec3(q.x, h, q.y);
}

// The density of band b at p (0..1); `full` adds the erosion (the cheap density is for the light, the sky light,
// the edge search and the shadow map).
float cv_density(vec3 p, int b, bool full)
{
    vec4 L = cv_layer[b];
    float h = cv_height(p);
    float hf = (h - L.x) / (L.y - L.x);
    if (hf <= 0.0 || hf >= 1.0)
    {
        return 0.0;
    }
    vec4 w = textureLod(cloud_vol_weather, (p.xz + cv_place[b].xy) / cv_map.x, 0.0);
    int ch = int(cv_place[b].w);
    float cover = w[ch] * L.w;
    if (cover < 0.01)
    {
        return 0.0;
    }
    float t = L.z;
    float sheet = cv_place[b].z;
    float anvil = cv_shape[b].z;
    vec3 q = cv_noise_pos(p, h, b);
    vec4 n = textureLod(cloud_vol_base, vec3(q.x, q.y * CV_BASE_Y, q.z) / cv_shape[b].w, 0.0);
    float low = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = clamp(cv_remap(n.r, low - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
    // a sheet is thicker and thinner from place to place; a heap's base and top rise and sink with the noise's cells
    // (up to 12 % of the layer), so that a grazing ray does not meet the layer's planes as flat ledges
    float thick = mix(mix(0.25, 1.0, clamp(n.g * 1.5, 0.0, 1.0)), 1.0, t);
    float lift = 0.12 * t;
    float hv = (hf - lift * clamp(n.b * 1.6 - 0.2, 0.0, 1.0)) / (1.0 - lift);
    base *= cv_height_grad(hv / thick, t, ch == 0 ? w.a : 0.5, anvil);
    base *= mix(mix(0.45, 1.0, clamp(n.g * 1.8, 0.0, 1.0)), 1.0, t);
    // heaps: the cover shrinks with height (domes, not columns); a cumulonimbus's anvil spreads it again under its
    // top (Nubis 2017's anvil bias); a sheet (an overcast sky) has no gaps
    float spread = mix(1.0, mix(1.0, 0.55, hf), t) + anvil * smoothstep(0.75, 0.9, hf) * 0.9;
    float cov = clamp(mix(cover * 0.85 * spread, 1.0, sheet), 0.0, 1.0);
    base = clamp(cv_remap(base, 1.0 - cov, 1.0, 0.0, 1.0), 0.0, 1.0);
    // a shape that only just passes the cut is a speck, not a cloud (heaps only)
    base *= mix(smoothstep(0.04, 0.24, base), 1.0, sheet) * mix(0.55, 1.0, cov);
    if (!full || base <= 0.0)
    {
        return base;
    }
    // wispy at the base, billowy towards the top: the erosion eats the thin edges and leaves the core
    vec2 c = textureLod(cloud_vol_curl, q.xz / CV_CURL_KM, 0.0).xy * 2.0 - 1.0;
    vec3 qd = q + vec3(c.x, 0.0, c.y) * 0.12 * (1.0 - hf);
    vec4 d = textureLod(cloud_vol_erosion, qd / CV_EROSION_KM, 0.0);
    float df = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
    float m = mix(df, 1.0 - df, clamp(hf * 4.0, 0.0, 1.0));
    return clamp(cv_remap(base, m * cv_shape[b].y, 1.0, 0.0, 1.0), 0.0, 1.0);
}

float cv_hg(float c, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * CV_PI * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

float cv_draine(float c, float g, float a)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * CV_PI * (1.0 + a * (1.0 + 2.0 * g2) / 3.0)) * (1.0 + a * c * c) / pow(1.0 + g2 - 2.0 * g * c, 1.5);
}

// The approximate Mie phase of cloud droplets (Jendersie and d'Eon 2023, the fog fit at a droplet diameter of 20 um):
// a narrow HG lobe, the glory that silvers the thin edges, and a Draine lobe for the wide scatter.
float cv_mie(float c)
{
    return mix(cv_hg(c, 0.9946), cv_draine(c, 0.5938, 27.11), 0.4982);
}

// Exit distance of a ray from the camera (inside the sphere) through the sphere of radius r.
float cv_sphere_far(vec3 d, float r)
{
    float b = CV_R_PLANET * d.y;
    float c = CV_R_PLANET * CV_R_PLANET - r * r;
    return -b + sqrt(b * b - c);
}

// The light on a cloud, the flat layer's terms (cloudsV.glsl): the sun at the cloud layer (AT1's transmittance) tinted
// by the cloud colour, without the glow's angular shape (the phase function draws it), fading with the clouds' own
// handover; the ambient, lifted by the cloud cover. Doubled, clamped and made linear as the flat clouds are in
// softenLightF.glsl. The C++ twin is LLCloudscape::cloud_light().
void cv_light_inputs(out vec3 sun_col, out vec3 amb_col)
{
    vec3 sun_cloud = linear_to_srgb_unclamped(srgb_to_linear(max(sunlight_color, vec3(0.0))) * cloud_light_transmittance);
    vec3 sun_srgb = sun_cloud * (1.0 - cloud_moon_blend) * cloud_color;
    vec3 tmp_ambient = ambient_color + (1.0 - ambient_color) * cloud_shadow * 0.5;
    vec3 amb_srgb = tmp_ambient * cloud_color;
    sun_col = srgb_to_linear(clamp(sun_srgb * 2.0, vec3(0.0), vec3(2.0))) * sky_hdr_scale;
    amb_col = srgb_to_linear(clamp(amb_srgb * 2.0, vec3(0.0), vec3(2.0))) * sky_hdr_scale;
}

// The haze between the eye and a cloud dist_km away along dir: cloudsV.glsl's haze below the clouds, at the cloud's
// own distance (the haze ends at max_y, the flat layer's height). col is the clouds' radiance, T their transmittance.
vec3 cv_haze(vec3 dir, float dist_km, vec3 col, float T)
{
    vec3 sunlight = sunlight_color * (1.0 - cloud_moon_blend);
    vec3 light_atten = (blue_density + vec3(haze_density * 0.25)) * (density_multiplier * max_y);
    vec3 combined = abs(blue_density) + vec3(abs(haze_density));
    vec3 blue_weight = blue_density / max(combined, vec3(1e-6));
    vec3 haze_weight = haze_density / max(combined, vec3(1e-6));
    float off_axis = 1.0 / max(1e-6, max(0.0, dir.y) + cv_light.y);
    sunlight *= exp(-light_atten * off_axis);
    float dist_m = min(dist_km * 1000.0, max_y / max(dir.y, 0.02));
    vec3 transparency = sqrt(exp(-combined * dist_m * density_multiplier));
    float haze_glow = max(1.0 - dot(dir, cv_light.xyz), 0.001) * glow.x;
    haze_glow = pow(haze_glow, glow.z);
    haze_glow = (1.0 - cloud_moon_blend) * (haze_glow + 0.25);
    vec3 tmp_ambient = ambient_color + (1.0 - ambient_color) * cloud_shadow * 0.5;
    sunlight *= (1.0 - cloud_shadow);
    vec3 below = blue_horizon * blue_weight * (sunlight + tmp_ambient) + (haze_horizon * haze_weight) * (sunlight * haze_glow + tmp_ambient);
    vec3 below_lin = srgb_to_linear(clamp(below * 2.0, vec3(0.0), vec3(2.0))) * sky_hdr_scale;
    // the flat layer attenuates in its sRGB-like space: as a linear factor that is the power 2.2
    vec3 tr = pow(clamp(transparency, vec3(0.0), vec3(1.0)), vec3(2.2));
    return col * tr + (1.0 - T) * below_lin * (1.0 - tr);
}

// March band b along dir (y up, unit). Adds to the radiance `col`, the transmittance `T` and the sum of the distances
// weighted by what each step absorbs (`wsum`, for the clouds' distance).
void cv_march_band(int b, vec3 dir, int steps, int lsteps, float jitter, float ct, vec3 sun_col, vec3 amb_col,
                   inout vec3 col, inout float T, inout float wsum)
{
    vec4 L = cv_layer[b];
    float t0 = cv_sphere_far(dir, CV_R_PLANET + L.x);
    float t1 = min(cv_sphere_far(dir, CV_R_PLANET + L.y), cv_noise[b].w);
    if (t0 >= t1)
    {
        return;
    }
    float len = t1 - t0;
    vec3 sun = cv_light.xyz;
    float sigma0 = cv_shape[b].x;
    // Steps grow with the distance; a grazing ray crosses tens of km of the layer: the warp is eased so that no step
    // is longer than CV_STEP_MAX_KM, and past that the count grows with the length up to the band's cap
    int n = max(steps, min(int(ceil(len / CV_STEP_MAX_KM)), int(cv_march[b].z)));
    float warp = clamp(CV_STEP_MAX_KM * float(n) / len, 1.0, CV_STEP_WARP);
    float prev_t = t0;
    bool prev_in = false;
    for (int i = 0; i < n; ++i)
    {
        float x = (float(i) + max(jitter, 0.02)) / float(n);
        float tt = t0 + len * pow(x, warp);
        float ds = len * warp * pow(x, warp - 1.0) / float(n);
        vec3 p = dir * tt;
        float dens = cv_density(p, b, true);
        // the step that enters a cloud: the edge is found by bisection, so that it does not sit on the step grid
        if (dens > CV_DENSITY_MIN && !prev_in && i > 0)
        {
            float lo = prev_t;
            float hi = tt;
            for (int k = 0; k < 4; ++k)
            {
                float mid = 0.5 * (lo + hi);
                if (cv_density(dir * mid, b, false) > CV_DENSITY_MIN)
                {
                    hi = mid;
                }
                else
                {
                    lo = mid;
                }
            }
            ds = min(ds, tt - hi + 0.5 * (hi - lo));
            tt = 0.5 * (hi + tt);
            p = dir * tt;
            dens = cv_density(p, b, true);
        }
        prev_t = t0 + len * pow(x, warp);
        prev_in = dens > CV_DENSITY_MIN;
        if (dens > CV_DENSITY_MIN)
        {
            float sigma = dens * sigma0;
            // the light march: a cone of growing taps on the cheap density, and one far tap
            float od = 0.0;
            float ls = 0.05;
            vec3 lp = p;
            for (int j = 0; j < lsteps; ++j)
            {
                lp += sun * ls;
                od += cv_density(lp, b, false) * ls;
                ls *= 1.7;
            }
            od += cv_density(lp + sun * 1.2, b, false);
            od *= sigma0;
            // each octave scatters more evenly (multiple scattering)
            float s = 0.0;
            float oa = 1.0;
            float ob = 1.0;
            float oc = 1.0;
            for (int o = 0; o < 3; ++o)
            {
                s += oa * exp(-od * ob) * mix(0.25 / CV_PI, min(cv_mie(ct), 1.2), oc);
                oa *= 0.5;
                ob *= 0.5;
                oc *= 0.5;
            }
            float powder = 1.0 - 0.6 * exp(-sigma * 0.15);
            float h = cv_height(p);
            float hf = clamp((h - L.x) / (L.y - L.x), 0.0, 1.0);
            // the sky light reaches a point through the cloud above it
            vec3 up = normalize(p + vec3(0.0, CV_R_PLANET, 0.0));
            float sky_od = (cv_density(p + up * 0.3, b, false) * 0.3 + cv_density(p + up * 0.6, b, false) * 0.4
                            + cv_density(p + up * 1.0, b, false) * 0.4) * sigma0;
            float sky_vis = mix(0.25, 1.0, exp(-sky_od * 0.012));
            vec3 ls_rad = sun_col * cv_gain.x * s * 4.0 * CV_PI * powder + amb_col * cv_gain.y * mix(0.5, 1.0, hf) * sky_vis;
            // a sheet is lit unevenly where it is thick or thin
            vec3 q = cv_noise_pos(p, h, b);
            float nb = textureLod(cloud_vol_base, vec3(q.x, q.y * CV_BASE_Y, q.z) / cv_shape[b].w, 0.0).g;
            float sheet_shade = mix(mix(0.55, 1.0, clamp(nb * 1.8, 0.0, 1.0)), 1.0, L.z);
            float ext = exp(-sigma * ds);
            float absorbed = T * (1.0 - ext);
            col += ls_rad * sheet_shade * absorbed;
            wsum += tt * absorbed;
            T *= ext;
            if (T < 0.01)
            {
                break;
            }
        }
    }
}

// The clouds along a view (dir y up, unit): their radiance with the haze in front of them, their transmittance and
// their distance (km). `still` doubles the steps and adds a light tap (photos).
void cv_trace(vec3 dir, float jitter, bool still, out vec3 col, out float T, out float depth)
{
    col = vec3(0.0);
    T = 1.0;
    depth = 45.0;
    // no clouds below the horizon (as the flat layer, SL-11589)
    if (dir.y <= 0.0)
    {
        return;
    }
    vec3 sun_col;
    vec3 amb_col;
    cv_light_inputs(sun_col, amb_col);
    float ct = dot(dir, cv_light.xyz);
    float wsum = 0.0;
    int count = int(cv_map.w);
    for (int b = 0; b < 3; ++b)
    {
        if (b >= count || T < 0.01)
        {
            break;
        }
        int steps = int(cv_march[b].x) * (still ? 2 : 1);
        int lsteps = int(cv_march[b].y) + (still ? 1 : 0);
        cv_march_band(b, dir, steps, lsteps, jitter, ct, sun_col, amb_col, col, T, wsum);
    }
    if (T < 0.999)
    {
        depth = wsum / (1.0 - T);
        col = cv_haze(dir, depth, col, T);
    }
}
