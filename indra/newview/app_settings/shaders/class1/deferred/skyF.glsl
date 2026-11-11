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

// Inputs
in vec3 vary_HazeColor;
in float vary_LightNormPosDot;
in float vary_MoonNormPosDot; // <Vulkanstorm/> L3

#ifdef HAS_HDRI
in vec4 vary_position;
in vec3 vary_rel_pos;
uniform float hdri_split_screen;
uniform mat3 env_mat;
uniform sampler2D environmentMap;
#endif

uniform sampler2D rainbow_map;
uniform sampler2D halo_map;

uniform float sun_moon_blend;   // <Vulkanstorm/> L3
uniform float moon_halo_weight; // <Vulkanstorm/> L3
uniform float moisture_level;
uniform float droplet_radius;
uniform float ice_level;

out vec4 frag_data[4];

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
// Rendering 2 > Night sky): the naked-eye stars of app_settings/stars and the
// Milky Way of the fainter stars (milky_way.bin, Tycho-2), turning about the
// celestial pole as seen from the place of "Sky seen from" (Rome by default).
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
// the Milky Way's light at its brightest, and the size of its points
// (sigma, pixels)
#define NIGHT_SKY_MW_GLOW 0.12
#define NIGHT_SKY_MW_GRAIN_SIGMA 0.6
// the band's light left as a haze, the rest in points; points per lattice
// cell at the band's brightest; the floor of the points' mean (see
// night_sky_mw)
#define NIGHT_SKY_MW_HAZE 0.45
#define NIGHT_SKY_MW_POINTS 2.0
#define NIGHT_SKY_MW_POINT_MEAN_MIN 0.6
// The Milky Way map (llnightsky.h MILKY_WAY_*): galactic longitude -180 to
// 180 degrees across, latitude -NIGHT_SKY_MW_REACH to +NIGHT_SKY_MW_REACH down.
#define NIGHT_SKY_MW_WIDTH 2048
#define NIGHT_SKY_MW_HEIGHT 512
#define NIGHT_SKY_MW_REACH 0.785398163 // 45 degrees

in vec3 vary_sky_dir;

uniform sampler2D night_sky_cells;  // R32F: the first star of each cell, then the end
uniform sampler2D night_sky_stars;  // RGBA32F: per star, direction + intensity, colour + size
uniform usampler2D night_sky_milky_way; // R32UI: glow, grain, colour (llnightsky.h parseMilkyWay)
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

// One texel of the Milky Way map: glow (square root of the light), grain
// (stars of V 6.5 to 9.5 per texel, x 16) and B-V colour (x 100), 0..255.
// Longitude wraps, latitude clamps.
vec3 night_sky_mw_texel(int x, int y)
{
    int xi = (x + NIGHT_SKY_MW_WIDTH) % NIGHT_SKY_MW_WIDTH;
    int yi = clamp(y, 0, NIGHT_SKY_MW_HEIGHT - 1);
    uint t = texelFetch(night_sky_milky_way, ivec2(xi, yi), 0).r;
    return vec3(float(t & 255u), float((t >> 8u) & 255u), float((t >> 16u) & 255u));
}

// The Milky Way at catalogue direction q (J2000 equatorial), linear, from
// the map of the real stars too faint for the naked eye (Tycho-2): their
// glow, with its clouds and dark lanes, and their grain, faint points
// scattered as densely as the counted stars, fixed in the sky. ax, ay: the
// pixel's footprint across and down (catalogue frame), pixel its angle
// (radians).
vec3 night_sky_mw(vec3 q, vec3 ax, vec3 ay, float pixel)
{
    // J2000 equatorial to galactic (IAU 1958, Hipparcos values; the same
    // rows as scripts/generate-star-catalog.py)
    vec3 m0 = vec3(-0.0548756, -0.8734371, -0.4838350);
    vec3 m1 = vec3(0.4941094, -0.4448296, 0.7469822);
    vec3 m2 = vec3(-0.8676661, -0.1980764, 0.4559838);
    vec3 g = vec3(dot(m0, q), dot(m1, q), dot(m2, q));
    float b = asin(clamp(g.z, -1.0, 1.0));
    if (abs(b) >= NIGHT_SKY_MW_REACH)
    {
        return vec3(0.0);
    }
    float l = atan(g.y, g.x);
    // texel coordinates, centres on the integers
    vec2 uv = vec2((l / 6.28318531 + 0.5) * float(NIGHT_SKY_MW_WIDTH),
                   (b / NIGHT_SKY_MW_REACH * 0.5 + 0.5) * float(NIGHT_SKY_MW_HEIGHT)) - 0.5;
    ivec2 i = ivec2(floor(uv));
    vec2 f = uv - floor(uv);
    vec3 t = mix(mix(night_sky_mw_texel(i.x, i.y), night_sky_mw_texel(i.x + 1, i.y), f.x),
                 mix(night_sky_mw_texel(i.x, i.y + 1), night_sky_mw_texel(i.x + 1, i.y + 1), f.x), f.y);
    if (t.x + t.y < 0.5)
    {
        return vec3(0.0);
    }
    float glow = (t.x / 255.0) * (t.x / 255.0);
    // dust reddens the light towards the centre; the eye sees little colour
    float warm = clamp((t.z / 100.0 - 0.45) / 0.9, 0.0, 1.0);
    vec3 tint = mix(vec3(0.82, 0.88, 1.0), vec3(1.0, 0.87, 0.70), warm);
    vec3 light = tint * glow * NIGHT_SKY_MW_GLOW;

    // The band is made of stars: part of its light stays a haze (the stars
    // too faint to tell apart), the rest is gathered into faint points on a
    // lattice fixed in the sky, two cells per texel each way, each holding a
    // star or none, as many as the map's light and its counted stars say.
    // The points keep the band's mean light; where a pixel covers more than
    // a cell (wide views, reflection probes) they would shimmer, and give
    // way to that mean.
    float cell = NIGHT_SKY_MW_REACH * 2.0 / float(NIGHT_SKY_MW_HEIGHT * 2);
    float sparkle = 1.0 - smoothstep(0.8 * cell, 1.6 * cell, pixel);
    float p = 1.0 - exp(-(glow * NIGHT_SKY_MW_POINTS + t.y / 16.0 / 4.0));
    if (sparkle <= 0.0 || p < 0.002)
    {
        return light;
    }
    // the pixel's footprint along longitude (east) and latitude (north),
    // and back: a point's offset in pixels
    vec3 east = vec3(-sin(l), cos(l), 0.0);
    vec3 north = vec3(-sin(b) * cos(l), -sin(b) * sin(l), cos(b));
    vec3 gx = vec3(dot(m0, ax), dot(m1, ax), dot(m2, ax));
    vec3 gy = vec3(dot(m0, ay), dot(m1, ay), dot(m2, ay));
    // columns: the footprint across, then down (as WGSL's mat2x2)
    mat2 j = mat2(vec2(dot(east, gx), dot(north, gx)), vec2(dot(east, gy), dot(north, gy)));
    float det = j[0].x * j[1].y - j[1].x * j[0].y;
    if (abs(det) < 1e-20)
    {
        return light;
    }
    mat2 inv = mat2(vec2(j[1].y, -j[0].y), vec2(-j[1].x, j[0].x)) * (1.0 / det);
    vec2 c = uv * 2.0 + 0.5; // cell coordinates
    // the pixel's cell and its eight neighbours: a point of any other cell
    // is at least a cell away, where each point has faded to nothing
    // (reach), so none is cut off as the sky turns
    ivec2 base = ivec2(floor(c)) - 1;
    float reach = cell * max(cos(b), 0.1) / max(pixel, 1e-9);
    vec3 field = vec3(0.0);
    for (int k = 0; k < 9; k++)
    {
        ivec2 ci = base + ivec2(k % 3, k / 3);
        // ci is at least -2 each way here: the remainder's operands stay
        // positive, where GLSL defines it
        uvec3 h = night_sky_pcg3(uvec3(uint((ci.x + NIGHT_SKY_MW_WIDTH * 2) % (NIGHT_SKY_MW_WIDTH * 2)), uint(ci.y + 4), 0x5717u));
        vec3 r = vec3(h & uvec3(0xffffu)) / 65535.0;
        if (r.z >= p)
        {
            continue;
        }
        vec2 d = (vec2(ci) + r.xy - c) * vec2(cell * cos(b), cell);
        vec2 px = inv * d;
        float r2 = dot(px, px) / (NIGHT_SKY_MW_GRAIN_SIGMA * NIGHT_SKY_MW_GRAIN_SIGMA);
        float fade = 1.0 - smoothstep(0.5 * reach, reach, sqrt(dot(px, px)));
        if (r2 < 9.0 && fade > 0.0)
        {
            // brightness: the faint ones the more numerous (star counts
            // grow about 2.5 times a magnitude over three magnitudes), mean 1
            float u = mix(0.0316, 1.0, float(h.z >> 16u) / 65535.0);
            float w = 0.518 * pow(u, -0.6);
            // a little colour of its own, bluer or redder than the band
            float hue = float((h.x >> 16u) & 255u) / 255.0 - 0.5;
            field += w * fade * exp(-0.5 * r2) * (vec3(1.0) + vec3(0.12, 0.0, -0.12) * hue);
        }
    }
    // the field's mean over a pixel: occupied cells per pixel times a
    // point's area (within its fade, about), not below a floor, so that
    // zooming in shows separate points rather than ever brighter ones. The
    // pixel's sky area is the Jacobian's determinant: the longer derivative
    // squared would overstate it where the footprint is stretched or sheared
    // (towards the edge of a wide view) and dim the points there.
    float cells = abs(det) / (cell * cell * max(cos(b), 0.1));
    float kept = 0.75 * reach / NIGHT_SKY_MW_GRAIN_SIGMA;
    float area = 6.2831853 * NIGHT_SKY_MW_GRAIN_SIGMA * NIGHT_SKY_MW_GRAIN_SIGMA * (1.0 - exp(-0.5 * kept * kept));
    float mean = max(p * area * cells, NIGHT_SKY_MW_POINT_MEAN_MIN);
    vec3 points = mix(vec3(1.0), field / mean, sparkle);
    return light * (NIGHT_SKY_MW_HAZE + (1.0 - NIGHT_SKY_MW_HAZE) * points);
}

// Stars and Milky Way in region direction d (unit, x east, y north, z up),
// linear. dx, dy are its screen derivatives (the pixel's footprint across
// and down), pixel the angle one pixel covers there; background the sky's
// own linear colour, which drowns them when bright (twilight shows the
// brightest first, daylight none).
vec3 night_sky(vec3 d, vec3 dx, vec3 dy, float pixel, vec3 background)
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
        // the pixel's footprint in the catalogue's frame, as q
        vec3 ax = vec3(dot(dx, night_sky_axes[0]), dot(dx, night_sky_axes[1]), dot(dx, night_sky_axes[2]));
        vec3 ay = vec3(dot(dy, night_sky_axes[0]), dot(dy, night_sky_axes[1]), dot(dy, night_sky_axes[2]));
        sum += night_sky_mw(q, ax, ay, pixel) * night_sky_params.y;
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
    vec3 color;
    // <Vulkanstorm> night sky: the view direction in the region's frame
    // (dome x north, y up, z east), and the angle of one pixel there, taken
    // here, outside the branches, where the derivatives are defined
    vec3 night_dir = normalize(vec3(vary_sky_dir.z, vary_sky_dir.x, vary_sky_dir.y));
    vec3 night_dx = dFdx(night_dir);
    vec3 night_dy = dFdy(night_dir);
    float night_pixel = max(max(length(night_dx), length(night_dy)), 1e-6);
    // </Vulkanstorm>
#ifdef HAS_HDRI
    vec3 frag_coord = vary_position.xyz/vary_position.w;
    if (-frag_coord.x > ((1.0-hdri_split_screen)*2.0-1.0))
    {
        vec3 pos = normalize(vary_rel_pos);
        pos = env_mat * pos;
        vec2 texCoord = vec2(atan(pos.z, pos.x) + PI, acos(pos.y)) / vec2(2.0 * PI, PI);
        color = textureLod(environmentMap, texCoord.xy, 0).rgb * sky_hdr_scale;
        color = min(color, vec3(8192*8192*16)); // stupidly large value arrived at by binary search -- avoids framebuffer corruption from some HDRIs

        frag_data[2] = vec4(0.0,0.0,0.0,GBUFFER_FLAG_HAS_HDRI);
    }
    else
#endif
    {
        // Potential Fill-rate optimization.  Add cloud calculation
        // back in and output alpha of 0 (so that alpha culling kills
        // the fragment) if the sky wouldn't show up because the clouds
        // are fully opaque.

        color = vary_HazeColor;

        float  rel_pos_lightnorm = vary_LightNormPosDot;
        float optic_d = rel_pos_lightnorm;
        // <Vulkanstorm> L3: the sun's halo and rainbow at the sun, fading with the band; the moon's at the moon, fading in
        // (weight 1 by day, so the sun's are as before)
        vec3  halo_22 = halo22(optic_d);
        float sun_halo = 1.0 - sun_moon_blend;
        color.rgb += sun_halo * rainbow(optic_d);
        color.rgb += sun_halo * halo_22;
        if (moon_halo_weight > 0.0)
        {
            float moon_d = vary_MoonNormPosDot;
            color.rgb += moon_halo_weight * (rainbow(moon_d) + halo22(moon_d));
        }
        // </Vulkanstorm>
        color.rgb *= 2.;
        color.rgb = clamp(color.rgb, vec3(0), vec3(5));

        // <Vulkanstorm> stars and Milky Way, added in linear light: the
        // lighting pass decodes the sky to linear and scales it by
        // sky_hdr_scale (softenLightF.glsl); the stars come unscaled
        if (night_sky_params.x > 0.0)
        {
            float hdr = max(sky_hdr_scale, 0.01);
            vec3 sky_linear = srgb_to_linear(color.rgb);
            vec3 stars = night_sky(night_dir, night_dx, night_dy, night_pixel, sky_linear * hdr);
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

