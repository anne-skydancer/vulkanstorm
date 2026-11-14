/**
 * @file shadowStochasticUtil.glsl
 * @brief The Stochastic shadow method's shadow util (RenderShadowMethod 2)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * Replaces deferred/shadowUtil.glsl for every program that links a shadow util when the method is
 * Stochastic (llviewershadermgr.cpp builds it only then; LLShaderMgr::attachShaderFeatures picks it).
 * Its only other source is the method-neutral SHADOW_COMMON block of shadowUtil.glsl (compiled alone, linked
 * next to this object).
 *
 * Per pixel, 8 rays toward points on the light's disc (the sun or moon: the angular size
 * shadow_light_size), each marched in a few steps through the
 * cascade's depth map, which is read as plain depth (the method switches the maps' compare mode off).
 * A ray is blocked when it crosses a stored surface between two nodes of its march, or is still behind
 * one at the end of it, the surface nearer the light than the receiver's plane at the texel read. The pixel's value is the fraction of free rays. The deferred sun pass
 * changes the rays every frame (shadow_st_frame) and the temporal resolve averages the frames; every
 * other program keeps shadow_st_frame at 0, which fixes each pixel's rays.
 * A projector is not marched and not looked up in the sun pass: spotLightF.glsl calls sampleProjectorShadow (the projector
 * shadow pool, PROJECTOR_SHADOW_POOL): a filtered lookup in the light's own tile of the pool's atlas (a blocker search, then a
 * percentage-closer filter on bilinear gathers, at a fixed disc in the tile's uv, so the value depends on the tile alone).
 * sampleSpotShadow, which the sun programs still call for their b and a channels, returns 1.
 */

#define ST_RAYS 8
#define ST_GOLDEN 2.39996323
#define ST_STEPS_MAX 8
// the projectors' filtered lookup (sampleProjectorShadow)
#define ST_SPOT_LIGHT_SIZE 0.15    // metres: diameter of a projector's emitter disc, at its centre, at most the emitter prim's face
#define ST_SPOT_SEARCH 8           // gathers of the blocker search
#define ST_SPOT_TAPS 32            // bilinear gathers of the filter (penumbra pixels only)
#define ST_SPOT_KERNEL_MIN 1.0     // map texels: the filter's radius at contact
#define ST_SPOT_PLANE_SLOPE 8.0    // texels of depth per texel of offset the receiver plane may lower a tap's reference by

uniform sampler2D shadowMap0; // plain depth: the method sets the maps' compare mode off
uniform sampler2D shadowMap1;
uniform sampler2D shadowMap2;
uniform sampler2D shadowMap3;

uniform vec3 primary_light_dir;       // eye space, unit
uniform float primary_shadow_strength; // 1 full shadow, 0 none
uniform vec2 shadow_res;
uniform mat4 shadow_matrix[6];
uniform vec4 shadow_clip;
uniform float spot_shadow_bias;
uniform float spot_shadow_offset;
uniform float spot_shadow_reach;

// angular size of the shadowing light, L = 2 tan(half angle): the physical sun times the sky's
// sun scale times RenderShadowLightSize (the moon uses the same size)
uniform float shadow_light_size;
// the frame of the rays' seeds; 0 fixes them (every program but the deferred sun pass)
uniform int shadow_st_frame;

// SHADOW_COMMON (shadowUtil.glsl, linked next to this object)
vec2 shadowUVScale(mat4 m);
float shadowDepthScale(mat4 m);
float shadowTexelMetres(mat4 m, vec2 res);
vec2 shadowPlaneGradient(mat4 m, vec3 n);
float shadowWorldBias(float texel_m, float s_z, float texels);
vec3 shadowNormalOffsetPos(vec3 pos, vec3 ng, float sin_theta, float texel_m);
vec3 shadowReceiverGeometry(vec2 pos_screen, vec3 pos, vec3 gnorm, vec3 light_dir, out float sin_theta);

float stIGN(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// the rays' rotation and the step jitter of this pixel and frame
vec2 stNoise()
{
    vec2 p = gl_FragCoord.xy + 5.588238 * float(shadow_st_frame & 63);
    return vec2(stIGN(p), stIGN(p.yx + vec2(113.0, 71.0)));
}

// d(depth)/d(uv) of the plane of a perspective projector's receiver, from two tangent steps
vec2 stSpotGradient(mat4 m, vec3 pos, vec3 norm, vec3 c)
{
    if (dot(norm, norm) < 1.0e-8)
    {
        return vec2(0.0);
    }
    vec3 n = normalize(norm);
    vec3 t1 = normalize(cross(n, abs(n.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(n, t1);
    vec4 a = m * vec4(pos + t1 * 0.05, 1.0);
    vec4 b = m * vec4(pos + t2 * 0.05, 1.0);
    vec3 d1 = a.xyz / a.w - c;
    vec3 d2 = b.xyz / b.w - c;
    float det = d1.x * d2.y - d1.y * d2.x;
    if (abs(det) < 1.0e-14)
    {
        return vec2(1.0e4);
    }
    return clamp(vec2(d1.z * d2.y - d1.y * d2.z, d1.x * d2.z - d1.z * d2.x) / det, vec2(-1.0e4), vec2(1.0e4));
}

// One read of the map near uv (offset by up to half a texel per ray): the stored depth zs, and the receiver's
// reference depth at the centre of the texel read: the receiver's depth with the constant bias taken off, and
// the signed plane (S2's form, d depth / d uv = g) lowering it where the plane is nearer the light over the
// offset from the receiver, never raising it (past a crease the extrapolated plane is not the surface).
float stReference(sampler2D m, vec2 uv, vec2 res, vec2 jit, vec3 c, vec2 g, float z_bias, float rp_max, out float zs)
{
    ivec2 p = clamp(ivec2(floor(uv * res + jit)), ivec2(0), ivec2(res) - 1);
    zs = texelFetch(m, p, 0).r;
    vec2 d = (vec2(p) + 0.5) / res - c.xy;
    return c.z - z_bias + clamp(dot(g, d), -rp_max, 0.0);
}

// The march. s(t) = s0 + t ds[k] is linear in t for orthographic and perspective maps. The nodes
// are t_0 = 0 (the receiver), jittered nodes denser near it, and t_max. A ray is blocked when the
// stored surface at its position at t_i lies between its depths at t_i and t_{i+1} (a surface
// crossed on the way), or when it is still behind the stored surface at t_max; in both the surface
// must be nearer the light than the receiver's plane at that texel (stReference). The map holds one
// layer: a higher caster (a canopy, a bird) hides a lower one (a roof) under it, so a ray that passed
// the roof's height beneath the higher caster reads no crossing there, and drifts out of its outline
// to find the roof "below" it. Such a ray is behind the stored surface at some step (the surface is
// nearer the light than the step's far end); once it is, a texel holding a surface nearer the light
// than the receiver that the ray has already passed (deeper than the ray) counts as blocking too.
// Each read is one
// texel, offset by up to half a texel per ray, so the frames' average is the texel-box filtered
// visibility. g: the signed plane gradient; z_bias: the constant bias that moves the receiver's
// reference depth towards the light; rp_max: the cap of the plane's offset.
float stMarch(sampler2D m, vec2 res, vec4 s0, vec2 g, float z_bias, float rp_max, vec4 ds[ST_RAYS], float t_max, int steps, float jitter)
{
    vec3 c = s0.xyz / s0.w;
    float vis = 0.0;
    float inv_n1 = 1.0 / float(steps + 1);
    for (int k = 0; k < ST_RAYS; ++k)
    {
        float j = fract(jitter + 0.618034 * float(k));
        vec2 jit = vec2(fract(j * 7.31 + 0.137 * float(k)), fract(j * 3.17 + 0.711 * float(k))) - 0.5;
        float blocked = 0.0;
        bool behind = false; // the ray was behind the stored surface at an earlier step (a surface hid the ones below it)
        vec3 q = c; // the first interval starts at the receiver
        for (int i = 0; i < steps && blocked == 0.0; ++i)
        {
            float un = i + 1 < steps ? (float(i + 1) + j) * inv_n1 : 1.0;
            vec4 sn = s0 + (t_max * un * un) * ds[k];
            vec3 qn = sn.xyz / sn.w;
            float zs;
            float ref = stReference(m, q.xy, res, jit, c, g, z_bias, rp_max, zs);
            float slack = 0.25 * (q.z - qn.z) + 1.0e-4;
            if (zs < ref && zs < q.z && zs > qn.z - slack)
            {
                blocked = 1.0;
            }
            else if (behind && zs < ref && zs > q.z)
            {
                blocked = 1.0; // the ray passed this surface's depth beneath the surface that hid it
            }
            behind = behind || zs < qn.z - slack;
            q = qn;
        }
        if (blocked == 0.0)
        {
            float zs;
            float ref = stReference(m, q.xy, res, jit, c, g, z_bias, rp_max, zs);
            if ((zs < q.z || (behind && zs > q.z)) && zs < ref)
            {
                blocked = 1.0;
            }
        }
        vis += 1.0 - blocked;
    }
    return vis / float(ST_RAYS);
}

// the 8 directions of a cascade, in the cascade's texture space: toward points of the light's disc
// (a Vogel spiral, rotated per pixel and frame)
void stSunRays(mat4 m, vec3 L, float theta, out vec4 ds[ST_RAYS])
{
    vec3 t1 = normalize(cross(L, abs(L.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(L, t1);
    float h = 0.5 * shadow_light_size;
    for (int k = 0; k < ST_RAYS; ++k)
    {
        float r = sqrt((float(k) + 0.5) / float(ST_RAYS));
        float phi = float(k) * ST_GOLDEN + theta;
        ds[k] = m * vec4(L + (h * r * cos(phi)) * t1 + (h * r * sin(phi)) * t2, 0.0);
    }
}

// the visibility through sun cascade j. spos: the receiver (view space), ng: its geometric normal
float stCascade(sampler2D map, int j, vec3 spos, vec3 ng, float sin_theta, vec3 L, vec2 nz)
{
    mat4 m = shadow_matrix[j];
    float texel_m = shadowTexelMetres(m, shadow_res);
    float s_z = shadowDepthScale(m);
    float z_bias = shadowWorldBias(texel_m, s_z, SHADOW_BIAS_TEXELS);
    float rp_max = shadowWorldBias(texel_m, s_z, SHADOW_PLANE_BIAS_MAX_TEXELS);
    vec4 s0 = m * vec4(shadowNormalOffsetPos(spos, ng, sin_theta, texel_m), 1.0);
    vec2 grad = shadowPlaneGradient(m, ng);
    vec4 ds[ST_RAYS];
    stSunRays(m, L, 6.28318531 * nz.x, ds);
    // the reach of the march: 4, 8, 16 and 32 m, and no farther than a ray can wander from the
    // casters the cull guard keeps (the footprint) at the disc's angular radius
    float reach = j == 0 ? 4.0 : j == 1 ? 8.0 : j == 2 ? 16.0 : 32.0;
    float half_l = max(0.5 * shadow_light_size, 1.0e-6);
    float t_max = min(reach, SHADOW_FOOTPRINT_M / half_l);
    int steps = clamp(int(ceil(t_max * half_l / texel_m)), 2, ST_STEPS_MAX);
    return stMarch(map, shadow_res, s0, grad, z_bias, rp_max, ds, t_max, steps, nz.y);
}

float stCascadeVisibility(int j, vec3 spos, vec3 ng, float sin_theta, vec3 L, vec2 nz)
{
    if (j == 0) return stCascade(shadowMap0, 0, spos, ng, sin_theta, L, nz);
    if (j == 1) return stCascade(shadowMap1, 1, spos, ng, sin_theta, L, nz);
    if (j == 2) return stCascade(shadowMap2, 2, spos, ng, sin_theta, L, nz);
    return stCascade(shadowMap3, 3, spos, ng, sin_theta, L, nz);
}

float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen)
{
    vec3 light_dir = normalize(primary_light_dir);
    if (pos.z <= -shadow_clip.w)
    {
        return 1.0; // lit beyond the far split
    }
    float sin_theta;
    vec3 ng = shadowReceiverGeometry(pos_screen, pos, norm, light_dir, sin_theta);

    // the weights of the cascade blend (the same bands as the other methods); the pixel draws one cascade by them
    vec4 near_split = shadow_clip * -0.75;
    vec4 far_split = shadow_clip * -1.25;
    vec4 dom = near_split - far_split;
    float w[4];
    w[3] = pos.z < near_split.z ? 1.0 - max(pos.z - far_split.z, 0.0) / dom.z : 0.0;
    w[2] = (pos.z < near_split.y && pos.z > far_split.z) ? 1.0 - max(pos.z - far_split.y, 0.0) / dom.y - max(near_split.z - pos.z, 0.0) / dom.z : 0.0;
    w[1] = (pos.z < near_split.x && pos.z > far_split.y) ? 1.0 - max(pos.z - far_split.x, 0.0) / dom.x - max(near_split.y - pos.z, 0.0) / dom.y : 0.0;
    w[0] = pos.z > far_split.x ? 1.0 - max(near_split.x - pos.z, 0.0) / dom.x : 0.0;
    float total = max(w[0], 0.0) + max(w[1], 0.0) + max(w[2], 0.0) + max(w[3], 0.0);
    if (total <= 0.0)
    {
        return 1.0;
    }
    vec2 nz = stNoise();
    float pick = fract(nz.x * 7.0 + nz.y) * total;
    int j = 3;
    float acc = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        acc += max(w[i], 0.0);
        if (pick < acc)
        {
            j = i;
            break;
        }
    }
    float shadow = stCascadeVisibility(j, pos, ng, sin_theta, light_dir, nz);
    if (j == 3)
    {
        // the far fade of the last cascade: a term of the blend's sum, so its share of one draw is divided by the cascade's weight
        shadow += max((pos.z + shadow_clip.z) / (shadow_clip.z - shadow_clip.w) * 2.0 - 1.0, 0.0) / max(w[3], 1.0e-2);
    }
    return mix(1.0, clamp(shadow, 0.0, 1.0), primary_shadow_strength);
}

// <G1-BEGIN> (scripts/shadows/projector_pool_g1.py compiles everything from here to G1-END, with stubs for the rest)
// ---- projectors: a filtered lookup. A blocker search, then a percentage-closer filter whose radius follows the
// blockers' distance (PCSS, RTR4 7.6), on bilinear gathers of the light's tile at a fixed disc of offsets in the
// tile's own uv (no screen, frame or pixel term: the value of a receiver depends on the tile alone).

uniform sampler2D proj_shadow_atlas;  // the pool's live atlas (D32F, depth as is: no compare, NEAREST, CLAMP_TO_EDGE)
uniform mat4 proj_shadow_matrix;      // view space -> this light's tile, [0,1]^3 (of the pose the tile was drawn with)
uniform vec4 proj_shadow_tile;        // atlas texel origin x, y; tile size N (texels); 1 / atlas size
uniform float proj_shadow_kernel_max; // texels: the cap of the search's and the filter's radius (10 at the tile's base size, kept in metres)

// What the filter needs of a projector, from its matrix (bias * glm::perspective * rigid view * rigid inv_view, the
// frustum symmetric; llprojectorshadows.cpp setupLight): with w = s.w the axial distance from the projector's centre, the
// map depth is z = c0 - c1 / w and uv moves k / w per metre across the axis. The near plane is at w = c1 / c0.
struct StSpot
{
    float c0;
    float c1;
    vec2 k;
};

StSpot stSpotSetup(mat4 m)
{
    vec3 r0 = vec3(m[0][0], m[1][0], m[2][0]);
    vec3 r1 = vec3(m[0][1], m[1][1], m[2][1]);
    vec4 r2 = vec4(m[0][2], m[1][2], m[2][2], m[3][2]);
    vec4 r3 = vec4(m[0][3], m[1][3], m[2][3], m[3][3]);
    StSpot sp;
    sp.c0 = dot(r2.xyz, r3.xyz) / dot(r3.xyz, r3.xyz); // s.z = c0 s.w - c1 for every point
    sp.c1 = sp.c0 * r3.w - r2.w;
    sp.k = vec2(length(r0 - 0.5 * r3.xyz), length(r1 - 0.5 * r3.xyz));
    return sp;
}

// a fixed Vogel disc of n points (unit radius, equal area each), the same orientation everywhere in the map
vec2 stSpotDisc(int k, int n)
{
    float r = sqrt((float(k) + 0.5) / float(n));
    float phi = float(k) * ST_GOLDEN;
    return r * vec2(cos(phi), sin(phi));
}

// The 2 x 2 texels of the tile whose bilinear footprint holds uv (tile-local), read with one gather at their shared
// corner in the atlas (so the hardware picks the four texels the weights assume, whatever its sub-texel precision; the
// atlas size is a power of two, so the corner is exact), in gather order (0,1) (1,1) (1,0) (0,0) from the quad's lower left
// texel. Returns the stored depths z, and in ref the receiver's reference at each texel's centre: its depth c.z less the
// constant bias, lowered (never raised) by the signed receiver plane g over the texel's offset, at most rp_max (S2's
// form). f: uv's bilinear fraction in the quad.
// edge (once per pixel): a tap of this pixel may leave the tile. The corner is then clamped into the tile and the
// components are selected again so that every texel index is clamped to the tile, as CLAMP_TO_EDGE does on a standalone
// map of the tile (bit for bit; no guard texels).
vec4 stSpotQuadTile(vec2 uv, vec3 c, vec2 g, float z_bias, float rp_max, bool edge, out vec4 ref, out vec2 f)
{
    float n = proj_shadow_tile.z;
    vec2 res = vec2(n);
    vec2 t = uv * res - 0.5;
    vec2 b = floor(t);
    f = t - b;
    vec2 o = (b + 0.5) / res - c.xy;
    vec2 e = 1.0 / res;
    vec4 dx = o.x + vec4(0.0, e.x, e.x, 0.0);
    vec4 dy = o.y + vec4(e.y, e.y, 0.0, 0.0);
    ref = c.z - z_bias + clamp(g.x * dx + g.y * dy, -rp_max, 0.0);
    vec2 q = edge ? clamp(b, vec2(0.0), vec2(n - 2.0)) : b;
    vec4 z = textureGather(proj_shadow_atlas, (proj_shadow_tile.xy + q + 1.0) * proj_shadow_tile.w);
    if (edge)
    {
        z = b.x >= n - 1.0 ? z.yyzz : (b.x < 0.0 ? z.xxww : z); // the left column read past the right edge / the right one past the left
        z = b.y >= n - 1.0 ? z.xyyx : (b.y < 0.0 ? z.wzzw : z); // the lower row read past the top edge / the top one past the bottom
    }
    return z;
}

// The visibility of receiver c (tile uv and depth) through the light's tile. pen: the filter's radius in texels per
// unit of map depth between the receiver and its blockers (x and y: the map's axes may differ in metres).
float stSpotFilterTile(vec3 c, vec2 g, float z_bias, float z_tex, vec2 pen)
{
    vec2 res = vec2(proj_shadow_tile.z);
    vec2 kmax = vec2(proj_shadow_kernel_max);
    // the search: any blocker between the near plane (z = 0) and the receiver, in the disc its farthest one needs
    vec2 rs = clamp(pen * c.z, vec2(ST_SPOT_KERNEL_MIN), kmax);
    float rp_max = z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(rs.x, rs.y) + 1.0));
    // the filter's radius never exceeds the search's (the blockers' mean depth is >= 0), so the search radius bounds every tap;
    // one texel of margin covers the rounding of uv * res per tap
    vec2 tc = c.xy * res - 0.5;
    float r = max(rs.x, rs.y);
    bool edge = any(lessThan(tc - r, vec2(1.0))) || any(greaterThan(tc + r, res - 2.0));
    float z_sum = 0.0;
    float n_blk = 0.0;
    for (int k = 0; k < ST_SPOT_SEARCH; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuadTile(c.xy + stSpotDisc(k, ST_SPOT_SEARCH) * rs / res, c, g, z_bias, rp_max, edge, ref, f);
        vec4 blk = vec4(lessThan(z, ref));
        z_sum += dot(blk, z);
        n_blk += blk.x + blk.y + blk.z + blk.w;
    }
    if (n_blk == 0.0)
    {
        return 1.0; // nothing between the receiver and the light
    }
    if (n_blk == float(4 * ST_SPOT_SEARCH))
    {
        return 0.0; // the umbra: the filter's disc lies inside the search's
    }
    // the filter: its radius from the blockers' mean depth (map depth is linear in 1 / distance, so the mean of z is
    // the mean of 1 / distance the penumbra's width follows), weighted 1 - 0.9 r^2 over the disc
    vec2 rf = clamp(pen * (c.z - z_sum / n_blk), vec2(ST_SPOT_KERNEL_MIN), kmax);
    float lit = 0.0;
    for (int k = 0; k < ST_SPOT_TAPS; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuadTile(c.xy + stSpotDisc(k, ST_SPOT_TAPS) * rf / res, c, g, z_bias, rp_max, edge, ref, f);
        vec4 bw = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
        lit += (1.0 - 0.9 * (float(k) + 0.5) / float(ST_SPOT_TAPS)) * dot(vec4(greaterThanEqual(z, ref)), bw);
    }
    return lit / (0.55 * float(ST_SPOT_TAPS)); // the weights' sum
}
// <G1-END>

// The visibility of this projector at the pixel (spotLightF.glsl, PROJECTOR_SHADOW_POOL). pos: view space; norm: the
// G-buffer normal; pos_screen: the pixel's screen uv. 1 outside the light's frustum and beyond the shadows' reach.
float sampleProjectorShadow(vec3 pos, vec3 norm, vec2 pos_screen)
{
    vec3 pos_px = pos; // the pixel's own position, for its geometric normal below
    pos += norm * spot_shadow_offset;
    // fades out with the camera distance, over the last 20 % of spot_shadow_reach
    float reach = spot_shadow_reach > 1.0 ? spot_shadow_reach : 128.0;
    float fade = clamp((reach - length(pos)) / (0.2 * reach), 0.0, 1.0);
    if (fade <= 0.0)
    {
        return 1.0;
    }
    mat4 m = proj_shadow_matrix;
    vec4 s0 = m * vec4(pos, 1.0);
    // outside the projector's frustum it lights nothing: no lookup
    if (s0.w <= 0.0 || any(greaterThan(abs(s0.xyz / s0.w - 0.5), vec3(0.5))))
    {
        return 1.0;
    }
    vec3 c = s0.xyz / s0.w;
    // the receiver plane: the geometric normal from the depth buffer (S2), not the normal-mapped one, from the pixel's
    // own position (the light direction argument only feeds sin_theta, unused here)
    float sin_theta;
    vec3 ng = shadowReceiverGeometry(pos_screen, pos_px, norm, norm, sin_theta);
    StSpot sp = stSpotSetup(m);
    vec2 res = vec2(proj_shadow_tile.z);
    // one texel of the map at the receiver, in map depth: the constant bias (S2's SHADOW_BIAS_TEXELS) and the unit of
    // the plane bias's cap; dz/dw = c1 / w^2 times the texel's width w / (k N)
    float z_tex = sp.c1 / (min(sp.k.x * res.x, sp.k.y * res.y) * s0.w);
    // the emitter disc: ST_SPOT_LIGHT_SIZE, at most the emitter prim's face (the frustum's width at the near plane)
    float light = min(ST_SPOT_LIGHT_SIZE, (sp.c1 / sp.c0) / max(sp.k.x, sp.k.y));
    // a blocker at depth zb gives a penumbra of radius light / 2 * (1 / wb - 1 / w) metres per unit of axial distance,
    // k * that in uv: 0.5 * light * k * (z - zb) / c1, here in texels per unit of z - zb
    vec2 pen = (0.5 * light / sp.c1) * sp.k * res;
    vec2 grad = stSpotGradient(m, pos, ng, c);
    float shadow = stSpotFilterTile(c, grad, SHADOW_BIAS_TEXELS * z_tex, z_tex, pen);
    return mix(1.0, shadow, fade);
}

// The sun pass's b and a channels: unlit by the projectors (they shade themselves in spotLightF.glsl), so 1 exactly.
float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen)
{
    return 1.0;
}
