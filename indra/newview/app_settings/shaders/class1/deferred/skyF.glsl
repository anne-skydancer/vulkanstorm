/**
 * @file class1/deferred/skyF.glsl
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, Linden Research, Inc.
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

// Outputs
out vec4 frag_data[4];

// Inputs
in vec3 pos;

#ifdef HAS_HDRI
in vec4 vary_position;
uniform float hdri_split_screen;
uniform mat3 env_mat;
uniform sampler2D environmentMap;
#endif

uniform sampler2D rainbow_map;
uniform sampler2D halo_map;

uniform float moisture_level;
uniform float droplet_radius;
uniform float ice_level;

uniform vec3 camPosLocal;

uniform vec3  lightnorm;
uniform vec3  sunlight_color;
uniform vec3  moonlight_color;
uniform int   sun_up_factor;
uniform vec3  ambient_color;
uniform vec3  blue_horizon;
uniform vec3  blue_density;
uniform float haze_horizon;
uniform float haze_density;

uniform float cloud_shadow;
uniform float density_multiplier;
uniform float distance_multiplier;
uniform float max_y;

uniform vec3  glow;
uniform float sun_moon_glow_factor;

uniform int cube_snapshot;

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

#define PI 3.14159265

/////////////////////////////////////////////////////////////////////////
// The fragment shader for the sky
/////////////////////////////////////////////////////////////////////////


vec3 rainbow(float d)
{
    // 'Interesting' values of d are -0.75 .. -0.825, i.e. when view vec nearly opposite of sun vec
    // Rainbox tex is mapped with REPEAT, so -.75 as tex coord is same as 0.25.  -0.825 -> 0.175. etc.
    // SL-13629
    // Unfortunately the texture is inverted, so we need to invert the y coord, but keep the 'interesting'
    // part within the same 0.175..0.250 range, i.e. d = (1 - d) - 1.575
    d         = clamp(-0.575 - d, 0.0, 1.0);

    // With the colors in the lower 1/4 of the texture, inverting the coords leaves most of it inaccessible.
    // So, we can stretch the texcoord above the colors (ie > 0.25) to fill the entire remaining coordinate
    // space. This improves gradation, reduces banding within the rainbow interior. (1-0.25) / (0.425/0.25) = 4.2857
    float interior_coord = max(0.0, d - 0.25) * 4.2857;
    d = clamp(d, 0.0, 0.25) + interior_coord;

    float rad = (droplet_radius - 5.0f) / 1024.0f;
    return pow(texture(rainbow_map, vec2(rad+0.5, d)).rgb, vec3(1.8)) * moisture_level;
}

vec3 halo22(float d)
{
    d       = clamp(d, 0.1, 1.0);
    float v = sqrt(clamp(1 - (d * d), 0, 1));
    return texture(halo_map, vec2(0, v)).rgb * ice_level;
}

// <Vulkanstorm> The real night sky (llnightsky.cpp, Preferences > Graphics >
// Rendering 2 > Night sky): the naked-eye stars of app_settings/stars and a
// procedural Milky Way, turning about the celestial pole as seen from Rome.
// Firestorm's random stars (starsF.glsl) are the other choice; this pass
// is off for them (night_sky_params.x 0). Design by Anne Skydancer, LGPL 2.1.
//
// The catalogue is sorted into the cells of a cube map, each star listed in
// every cell its point reaches, so that a pixel reads its own cell only.

#define NIGHT_SKY_FACE_CELLS 24       // llnightsky.h FACE_CELLS
#define NIGHT_SKY_MARGIN 0.021        // llnightsky.h MARGIN (radians)
#define NIGHT_SKY_CELLS_WIDTH 64      // lldrawpoolwlsky.cpp, texels per row
#define NIGHT_SKY_STARS_WIDTH 1024    // lldrawpoolwlsky.cpp, texels per row
// a cell holds about 6 stars, up to a few dozen in the Milky Way
#define NIGHT_SKY_MAX_PER_CELL 96

uniform sampler2D night_sky_cells;  // R32F: the first star of each cell, then the end
uniform sampler2D night_sky_stars;  // RGBA32F: per star, direction + intensity, colour + size
uniform vec3 night_sky_axes[3];    // region directions of the catalogue's x, y and z axes
uniform vec4 night_sky_params;      // x gain (0: off), y Milky Way strength, z time (s)
uniform float sky_hdr_scale;

// llnightsky.cpp cellOf
int night_sky_cell(vec3 q)
{
    vec3 a = abs(q);
    int face;
    vec2 uv;
    float m;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = q.x >= 0.0 ? 0 : 1;
        uv = q.yz;
        m = a.x;
    }
    else if (a.y >= a.z)
    {
        face = q.y >= 0.0 ? 2 : 3;
        uv = q.xz;
        m = a.y;
    }
    else
    {
        face = q.z >= 0.0 ? 4 : 5;
        uv = q.xy;
        m = a.z;
    }
    float n = float(NIGHT_SKY_FACE_CELLS);
    ivec2 c = ivec2(clamp(floor((uv / max(m, 1e-20) * 0.5 + 0.5) * n), vec2(0.0), vec2(n - 1.0)));
    return face * NIGHT_SKY_FACE_CELLS * NIGHT_SKY_FACE_CELLS + c.y * NIGHT_SKY_FACE_CELLS + c.x;
}

int night_sky_offset(int i)
{
    return int(texelFetch(night_sky_cells, ivec2(i % NIGHT_SKY_CELLS_WIDTH, i / NIGHT_SKY_CELLS_WIDTH), 0).r);
}

vec4 night_sky_texel(int i)
{
    return texelFetch(night_sky_stars, ivec2(i % NIGHT_SKY_STARS_WIDTH, i / NIGHT_SKY_STARS_WIDTH), 0);
}

// PCG hash of a lattice point (Jarzynski and Olano 2020, pcg3d): integer
// only, so it stays random far from the origin, unlike a sine hash.
uvec3 night_sky_pcg3(uvec3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> uvec3(16u);
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

vec3 night_sky_gradient(vec3 i)
{
    uvec3 h = night_sky_pcg3(uvec3(ivec3(i) + ivec3(4096)));
    return normalize(vec3(h & uvec3(0xffffu)) / 32767.5 - 1.0 + vec3(1e-4));
}

float night_sky_corner(vec3 i, vec3 f, vec3 o)
{
    return dot(night_sky_gradient(i + o), f - o);
}

// Gradient noise in 0..1, quintic fade: no facets along the lattice.
float night_sky_noise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float x00 = mix(night_sky_corner(i, f, vec3(0.0, 0.0, 0.0)), night_sky_corner(i, f, vec3(1.0, 0.0, 0.0)), u.x);
    float x10 = mix(night_sky_corner(i, f, vec3(0.0, 1.0, 0.0)), night_sky_corner(i, f, vec3(1.0, 1.0, 0.0)), u.x);
    float x01 = mix(night_sky_corner(i, f, vec3(0.0, 0.0, 1.0)), night_sky_corner(i, f, vec3(1.0, 0.0, 1.0)), u.x);
    float x11 = mix(night_sky_corner(i, f, vec3(0.0, 1.0, 1.0)), night_sky_corner(i, f, vec3(1.0, 1.0, 1.0)), u.x);
    float n = mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
    return clamp(0.5 + 0.75 * n, 0.0, 1.0);
}

// octaves of noise, each turned by a fixed rotation so that the lattices
// do not line up from one octave to the next
float night_sky_fbm(vec3 p, int octaves)
{
    mat3 turn = mat3(
        vec3(0.00, 0.80, 0.60),
        vec3(-0.80, 0.36, -0.48),
        vec3(-0.60, -0.48, 0.64));
    float sum = 0.0;
    float amp = 0.5;
    float total = 0.0;
    vec3 q = turn * p;
    for (int i = 0; i < octaves; i++)
    {
        sum += night_sky_noise(q) * amp;
        total += amp;
        q = turn * q * 2.03 + vec3(1.7, 9.2, 5.3);
        amp *= 0.5;
    }
    return sum / total;
}

// The Milky Way at catalogue direction q (J2000 equatorial), linear, laid
// on the real galactic plane: a band a few degrees wide, brighter and wider
// towards the centre (Sagittarius, l = 0), faint towards the anticentre
// (Auriga), with the dark Great Rift from Cygnus to Sagittarius just north
// of the plane, and mottling.
vec3 night_sky_milky_way(vec3 q)
{
    // J2000 equatorial to galactic (IAU 1958, Hipparcos values)
    vec3 g = vec3(
        dot(vec3(-0.0548756, -0.8734371, -0.4838350), q),
        dot(vec3(0.4941094, -0.4448296, 0.7469822), q),
        dot(vec3(-0.8676661, -0.1980764, 0.4559838), q));
    float l = atan(g.y, g.x);
    float b = asin(clamp(g.z, -1.0, 1.0));
    float centre = exp(-l * l / (0.5 * 0.5));
    float width = 0.075 + 0.1 * centre;
    float along = 0.3 + 0.7 * (0.5 + 0.5 * cos(l));
    float band = exp(-(b * b) / (width * width)) * along;
    float bulge = exp(-(l * l + 2.4 * b * b) / (0.2 * 0.2));
    // most of the sky is far from the band: no noise there
    if (band + bulge < 0.004)
    {
        return vec3(0.0);
    }
    float coarse = night_sky_fbm(g * 5.0, 4);
    float fine = night_sky_fbm(g * 21.0 + vec3(7.1, 3.3, 1.9), 3);
    // star clouds: the band is patchy, not a smooth stripe
    float clouds = smoothstep(0.3, 0.75, coarse) * (0.45 + 0.75 * fine);
    float light = band * (0.15 + 1.2 * clouds) + bulge * (0.6 + 0.6 * fine);
    // the Great Rift: l from -10 to 90 degrees, centred a little north of the plane
    float rift_l = smoothstep(-0.35, -0.1, l) * smoothstep(1.75, 1.3, l);
    float rift_b = (b - 0.025 - 0.02 * sin(l * 3.0)) / 0.035;
    float rift_shape = exp(-rift_b * rift_b) * rift_l;
    if (rift_shape > 0.01)
    {
        light *= 1.0 - 0.8 * rift_shape * (0.55 + 0.45 * night_sky_fbm(g * 13.0 + vec3(2.0), 3));
    }
    vec3 tint = mix(vec3(0.80, 0.86, 1.0), vec3(1.0, 0.86, 0.68), clamp(centre * 0.7 + bulge, 0.0, 1.0));
    return tint * light * 0.09;
}

// Stars and Milky Way in region direction d (unit, x east, y north, z up),
// linear. pixel is the angle one pixel covers there; background the sky's
// own linear colour, which drowns them when bright (twilight shows the
// brightest first, daylight none).
vec3 night_sky(vec3 d, float pixel, vec3 background)
{
    float gain = night_sky_params.x;
    if (gain <= 0.0 || d.z <= 0.0)
    {
        return vec3(0.0);
    }
    // atmospheric extinction: stars dim and vanish towards the horizon
    float airmass = 1.0 / (d.z + 0.025);
    float extinction = exp(-0.3 * (airmass - 1.0));
    float glow = dot(background, vec3(0.2126, 0.7152, 0.0722)) / 0.045;
    float glow2 = glow * glow;
    float glare = 1.0 / (1.0 + glow2 * glow2);
    // daylight, or right on the horizon: nothing would show
    if (gain * extinction * glare < 0.003)
    {
        return vec3(0.0);
    }
    vec3 q = vec3(dot(d, night_sky_axes[0]), dot(d, night_sky_axes[1]), dot(d, night_sky_axes[2]));
    float t = night_sky_params.z;
    int cell = night_sky_cell(q);
    int first = night_sky_offset(cell);
    int last = min(night_sky_offset(cell + 1), first + NIGHT_SKY_MAX_PER_CELL);
    vec3 sum = vec3(0.0);
    for (int i = first; i < last; i++)
    {
        vec4 pos = night_sky_texel(2 * i);
        vec4 col = night_sky_texel(2 * i + 1);
        float sigma = min(pixel * col.w, NIGHT_SKY_MARGIN / 12.0);
        vec3 off = q - pos.xyz;
        float r2 = dot(off, off) / (sigma * sigma);
        // the bright stars' soft glow, four times wider (the eye's
        // scattering, as on a photograph of the sky)
        if (pos.w > 0.3 && r2 < 144.0)
        {
            sum += col.rgb * (pos.w - 0.3) * 0.05 * exp(-0.5 * r2 / 16.0);
        }
        if (r2 < 9.0)
        {
            // scintillation: slow and slight overhead, stronger low down
            float seed = float(night_sky_pcg3(uvec3(ivec3(floor(pos.xyz * 4096.0)) + ivec3(8192))).x >> 8u) / 16777216.0;
            float depth = mix(0.35, 0.05, clamp(d.z * 2.0, 0.0, 1.0));
            float wave = sin(t * (2.3 + 3.1 * seed) + seed * 40.0) * sin(t * (0.9 + 1.3 * seed) + seed * 17.0);
            sum += col.rgb * pos.w * exp(-0.5 * r2) * (1.0 + depth * wave);
        }
    }
    if (night_sky_params.y > 0.0)
    {
        sum += night_sky_milky_way(q) * night_sky_params.y;
    }
    return sum * gain * extinction * glare;
}

// srgb_to_linear's inverse without linear_to_srgb's clamp to 1: the sky
// target holds HDR values (softenLightF.glsl decodes them).
vec3 night_sky_encode(vec3 c)
{
    vec3 low_range = c * 12.92;
    vec3 high_range = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high_range, low_range, lessThan(c, vec3(0.0031308)));
}
// </Vulkanstorm>

void main()
{
    // <Vulkanstorm> night sky: the view direction in the region's frame
    // (dome x north, y up, z east; pos is the dome vertex position, as
    // camPosLocal), and the angle of one pixel there, taken here, outside
    // the branches, where the derivatives are defined
    vec3 sky_dir = pos.xyz - camPosLocal.xyz;
    vec3 night_dir = normalize(vec3(sky_dir.z, sky_dir.x, sky_dir.y));
    float night_pixel = max(max(length(dFdx(night_dir)), length(dFdy(night_dir))), 1e-6);
    // </Vulkanstorm>

    // Get relative position
    vec3 rel_pos = pos.xyz - camPosLocal.xyz + vec3(0, 50, 0);

    // Adj position vector to clamp altitude
    if (rel_pos.y > 0.)
    {
        rel_pos *= (max_y / rel_pos.y);
    }
    if (rel_pos.y < 0.)
    {
        rel_pos *= (-32000. / rel_pos.y);
    }

    vec3 color;
#ifdef HAS_HDRI
    vec3 frag_coord = vary_position.xyz/vary_position.w;
    if (-frag_coord.x > ((1.0-hdri_split_screen)*2.0-1.0))
    {
        vec3 pos = normalize(rel_pos);
        pos = env_mat * pos;
        vec2 texCoord = vec2(atan(pos.z, pos.x) + PI, acos(pos.y)) / vec2(2.0 * PI, PI);
        color = textureLod(environmentMap, texCoord.xy, 0).rgb * sky_hdr_scale;
        color = min(color, vec3(8192*8192*16)); // stupidly large value arrived at by binary search -- avoids framebuffer corruption from some HDRIs

        frag_data[2] = vec4(0.0,0.0,0.0,GBUFFER_FLAG_HAS_HDRI);
    }
    else
#endif
    {
        // Normalized
        vec3  rel_pos_norm = normalize(rel_pos);
        float rel_pos_len  = length(rel_pos);

        // Grab this value and pass to frag shader for rainbows
        float rel_pos_lightnorm_dot = dot(rel_pos_norm, lightnorm.xyz);

        // Initialize temp variables
        vec3 sunlight = (sun_up_factor == 1) ? sunlight_color : moonlight_color * 0.7; //magic 0.7 to match legacy color

        // Sunlight attenuation effect (hue and brightness) due to atmosphere
        // this is used later for sunlight modulation at various altitudes
        vec3 light_atten = (blue_density + vec3(haze_density * 0.25)) * (density_multiplier * max_y);

        // Calculate relative weights
        vec3 combined_haze = max(abs(blue_density) + vec3(abs(haze_density)), vec3(1e-6));
        vec3 blue_weight   = blue_density / combined_haze;
        vec3 haze_weight   = haze_density / combined_haze;

        // Compute sunlight from rel_pos & lightnorm (for long rays like sky)
        float off_axis = 1.0 / max(1e-6, max(0., rel_pos_norm.y) + lightnorm.y);
        sunlight *= exp(-light_atten * off_axis);

        // Distance
        float density_dist = rel_pos_len * density_multiplier;

        // Transparency (-> combined_haze)
        // ATI Bugfix -- can't store combined_haze*density_dist in a variable because the ati
        // compiler gets confused.
        combined_haze = exp(-combined_haze * density_dist);

        // Compute haze glow
        float haze_glow = 1.0 - rel_pos_lightnorm_dot;
        // haze_glow is 0 at the sun and increases away from sun
        haze_glow = max(haze_glow, .001);
        // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)
        haze_glow *= glow.x;
        // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
        haze_glow = pow(haze_glow, glow.z);
        // glow.z should be negative, so we're doing a sort of (1 / "angle") function

        // Add "minimum anti-solar illumination"
        // For sun, add to glow.  For moon, remove glow entirely. SL-13768
        haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (sun_moon_glow_factor * (haze_glow + 0.25));

        // Haze color above cloud
        color = (blue_horizon * blue_weight * (sunlight + ambient_color)
                + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient_color));

        // Final atmosphere additive
        color *= (1. - combined_haze);

        // Increase ambient when there are more clouds
        vec3 ambient = ambient_color + max(vec3(0), (1. - ambient_color)) * cloud_shadow * 0.5;

        // Dim sunlight by cloud shadow percentage
        sunlight *= max(0.0, (1. - cloud_shadow));

        // Haze color below cloud
        vec3 add_below_cloud = (blue_horizon * blue_weight * (sunlight + ambient)
                            + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient));

        // Attenuate cloud color by atmosphere
        combined_haze = sqrt(combined_haze);  // less atmos opacity (more transparency) below clouds

        // At horizon, blend high altitude sky color towards the darker color below the clouds
        color += (add_below_cloud - color) * (1. - sqrt(combined_haze));

        float optic_d = rel_pos_lightnorm_dot;
        vec3  halo_22 = halo22(optic_d);
        color.rgb += rainbow(optic_d);
        color.rgb += halo_22;
        color.rgb *= 2.;
        color.rgb = clamp(color.rgb, vec3(0), vec3(5));

        // <Vulkanstorm> stars and Milky Way, added in linear light: the
        // lighting pass decodes the sky to linear and scales it by
        // sky_hdr_scale (softenLightF.glsl); the stars come unscaled
        if (night_sky_params.x > 0.0)
        {
            float hdr = max(sky_hdr_scale, 0.01);
            vec3 sky_linear = srgb_to_linear(color.rgb);
            vec3 stars = night_sky(night_dir, night_pixel, sky_linear * hdr);
            if (any(greaterThan(stars, vec3(0.0))))
            {
                color.rgb = night_sky_encode(sky_linear + stars / hdr);
            }
        }
        // </Vulkanstorm>

        frag_data[2] = vec4(0.0,0.0,0.0,GBUFFER_FLAG_SKIP_ATMOS);
    }

    frag_data[1] = vec4(0);

#if defined(HAS_EMISSIVE)
    frag_data[0] = vec4(0);
    frag_data[3] = vec4(color.rgb, 1.0);
#else
    frag_data[0] = vec4(color.rgb, 1.0);
#endif
}

