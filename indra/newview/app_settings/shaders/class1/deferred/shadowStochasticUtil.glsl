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
 * shadow_light_size; a projector: its emitter), each marched in a few steps through the
 * cascade's depth map, which is read as plain depth (the method switches the maps' compare mode off).
 * A ray is blocked when it crosses a stored surface between two nodes of its march, or is still behind
 * one at the end of it, the surface nearer the light than the receiver's plane at the texel read. The pixel's value is the fraction of free rays. The deferred sun pass
 * changes the rays every frame (shadow_st_frame) and the temporal resolve averages the frames; every
 * other program keeps shadow_st_frame at 0, which fixes each pixel's rays.
 */

#define ST_RAYS 8
#define ST_GOLDEN 2.39996323
#define ST_STEPS_MAX 8
#define ST_SPOT_STEPS 4
#define ST_SPOT_MARCH 4.0      // metres: the longest march toward a projector
// the constants below are this method's own, for the projectors
#define ST_SPOT_BIAS_SCALE 0.8 // of spot_shadow_bias
#define ST_SPOT_RPDB_MAX 0.004 // cap of the projectors' receiver-plane bias, shadow depth units

uniform sampler2D shadowMap0; // plain depth: the method sets the maps' compare mode off
uniform sampler2D shadowMap1;
uniform sampler2D shadowMap2;
uniform sampler2D shadowMap3;
uniform sampler2D shadowMap4;
uniform sampler2D shadowMap5;

uniform vec3 primary_light_dir;       // eye space, unit
uniform float primary_shadow_strength; // 1 full shadow, 0 none
uniform vec2 shadow_res;
uniform vec2 proj_shadow_res;
uniform mat4 shadow_matrix[6];
uniform vec4 shadow_clip;
uniform float spot_shadow_bias;
uniform float spot_shadow_offset;
uniform float spot_shadow_reach;
// per projector slot (LLPipeline::bindDeferredShader, lllocallight.h): the radius of the emitter disc its rays go
// toward, and r_excl: the distance from the eye (the apex) to the prim's face plus a small margin
uniform vec2 spot_emitter_radius;
uniform vec2 spot_shadow_excl;

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

// the stochastic core's noise (stochasticNoiseUtil.glsl, linked next to this object); the ids are its streams'
#define ST_STREAM_SUN_RAYS 0
#define ST_STREAM_LOCAL_RAYS 2
vec2 stNoiseStream(vec2 px, int frame, int stream);

// the rays' rotation and the step jitter of this pixel and frame, from the stream of the light they go toward
vec2 stNoise(int stream)
{
    return stNoiseStream(gl_FragCoord.xy, shadow_st_frame, stream);
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
// z_floor: a stored depth nearer the light than this is no surface (a light's exclusion zone, stTransmittance); 0 for the sun.
float stReference(sampler2D m, vec2 uv, vec2 res, vec2 jit, vec3 c, vec2 g, float z_bias, float rp_max, float z_floor, out float zs)
{
    ivec2 p = clamp(ivec2(floor(uv * res + jit)), ivec2(0), ivec2(res) - 1);
    zs = texelFetch(m, p, 0).r;
    if (zs < z_floor)
    {
        zs = 1.0;
    }
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
// than the receiver that the ray has already passed (deeper than the ray) counts as blocking too, when it is
// well below the surface that hid the ray (more than a tenth of the way from that surface to the receiver):
// a lace canopy's own texels, or a ground beside it, are not a roof under it, and would darken the lace.
// Each read is one
// texel, offset by up to half a texel per ray, so the frames' average is the texel-box filtered
// visibility. g: the signed plane gradient; z_bias: the constant bias that moves the receiver's
// reference depth towards the light; rp_max: the cap of the plane's offset.
// One ray k of the march (below): 1 when blocked, 0 when free. c: the receiver in the map's texture space (s0.xyz / s0.w).
float stMarchRay(sampler2D m, vec2 res, vec4 s0, vec3 c, vec2 g, float z_bias, float rp_max, float z_floor, vec4 ds_k, int k, float t_max, int steps, float jitter)
{
    float inv_n1 = 1.0 / float(steps + 1);
    float j = fract(jitter + 0.618034 * float(k));
    vec2 jit = vec2(fract(j * 7.31 + 0.137 * float(k)), fract(j * 3.17 + 0.711 * float(k))) - 0.5;
    float blocked = 0.0;
    bool behind = false; // the ray was behind the stored surface at an earlier step (a surface hid the ones below it)
    float zh = 1.0e9;    // the nearest such surface's depth
    vec3 q = c; // the first interval starts at the receiver
    for (int i = 0; i < steps && blocked == 0.0; ++i)
    {
        float un = i + 1 < steps ? (float(i + 1) + j) * inv_n1 : 1.0;
        vec4 sn = s0 + (t_max * un * un) * ds_k;
        vec3 qn = sn.xyz / sn.w;
        float zs;
        float ref = stReference(m, q.xy, res, jit, c, g, z_bias, rp_max, z_floor, zs);
        float slack = 0.25 * (q.z - qn.z) + 1.0e-4;
        if (zs < ref && zs < q.z && zs > qn.z - slack)
        {
            blocked = 1.0;
        }
        else if (behind && zs < ref && zs > q.z && zs > zh + 0.1 * (c.z - zh))
        {
            blocked = 1.0; // the ray passed this surface's depth beneath the surface that hid it
        }
        if (zs < qn.z - slack)
        {
            behind = true;
            zh = min(zh, zs);
        }
        q = qn;
    }
    if (blocked == 0.0)
    {
        float zs;
        float ref = stReference(m, q.xy, res, jit, c, g, z_bias, rp_max, z_floor, zs);
        if ((zs < q.z || (behind && zs > q.z && zs > zh + 0.1 * (c.z - zh))) && zs < ref)
        {
            blocked = 1.0;
        }
    }
    return blocked;
}

float stMarch(sampler2D m, vec2 res, vec4 s0, vec2 g, float z_bias, float rp_max, float z_floor, vec4 ds[ST_RAYS], float t_max, int steps, float jitter)
{
    vec3 c = s0.xyz / s0.w;
    float vis = 0.0;
    for (int k = 0; k < ST_RAYS; ++k)
    {
        vis += 1.0 - stMarchRay(m, res, s0, c, g, z_bias, rp_max, z_floor, ds[k], k, t_max, steps, jitter);
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
    return stMarch(map, shadow_res, s0, grad, z_bias, rp_max, 0.0, ds, t_max, steps, nz.y);
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
    vec2 nz = stNoise(ST_STREAM_SUN_RAYS);
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

// the eye of a projector matrix (view space)
vec3 stProjectorEye(mat4 m)
{
    vec4 r0 = vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
    vec4 r1 = vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
    vec4 r3 = vec4(m[0][3], m[1][3], m[2][3], m[3][3]);
    mat3 rows_t = mat3(r0.xyz, r1.xyz, r3.xyz);
    return -vec3(r0.w, r1.w, r3.w) * inverse(rows_t);
}

// The rays k0 .. k0 + kn - 1 (of the 8) of one local light's visibility (here a projector's emitter) at a receiver, as a mask
// (bit i: ray k0 + i is blocked): the rays go to points of the disc of radius `radius` around the light's eye (a Vogel
// spiral rotated by theta), each marched through the light's depth map m (matrix mat) to r_excl short of the eye. r_excl ends
// just past the prim's face: the stretch from the eye to the face is near-clipped out of the map, and the light's own prim is
// not drawn into it. What stored depth lies nearer the eye than r_excl along the receiver's line (a margin past the face) is
// no occluder, so the face's own rim does not shade the light; every other surface casts, linked parts of a fixture included.
// pos: the receiver (view space, already offset along its geometric normal ng); nz: the noise of the LOCAL_RAYS stream.
int stTransmittanceRays(sampler2D m, vec2 res, mat4 mat, vec3 pos, vec3 ng, vec3 eye, float radius, float r_excl, float z_bias, float rp_max, float march_max, int steps, vec2 nz, int k0, int kn)
{
    vec3 to_eye = eye - pos;
    float dist = length(to_eye);
    r_excl = max(r_excl, 1.0e-3);
    vec4 s0 = mat * vec4(pos, 1.0);
    vec3 c = s0.xyz / s0.w;
    vec2 grad = stSpotGradient(mat, pos, ng, c);
    vec3 ld = to_eye / dist;
    vec3 t1 = normalize(cross(ld, abs(ld.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(ld, t1);
    float theta = 6.28318531 * nz.x;
    float t_max = min(march_max, max(dist - r_excl, 1.0e-3)) / dist; // a receiver within r_excl marches its sliver only
    vec4 sx = mat * vec4(eye - ld * r_excl, 1.0); // the edge of the exclusion zone on the receiver's line to the eye
    float z_floor = sx.z / sx.w;
    int mask = 0;
    for (int i = 0; i < kn; ++i)
    {
        int k = k0 + i;
        float r = sqrt((float(k) + 0.5) / float(ST_RAYS));
        float phi = float(k) * ST_GOLDEN + theta;
        vec3 ek = eye + (radius * r) * (cos(phi) * t1 + sin(phi) * t2);
        vec4 ds_k = mat * vec4(ek - pos, 0.0); // t = 1 reaches the emitter point
        if (stMarchRay(m, res, s0, c, grad, z_bias, rp_max, z_floor, ds_k, k, t_max, steps, nz.y) > 0.0)
        {
            mask |= 1 << i;
        }
    }
    return mask;
}

// the number of rays a mask marks (at most the 8)
float stMaskCount(int mask)
{
    float n = 0.0;
    for (int i = 0; i < ST_RAYS; ++i)
    {
        n += float((mask >> i) & 1);
    }
    return n;
}

// The visibility of one local light at a receiver: the fraction of the 8 rays that reach its emitter disc
float stTransmittance(sampler2D m, vec2 res, mat4 mat, vec3 pos, vec3 ng, vec3 eye, float radius, float r_excl, float z_bias, float rp_max, float march_max, int steps)
{
    int mask = stTransmittanceRays(m, res, mat, pos, ng, eye, radius, r_excl, z_bias, rp_max, march_max, steps, stNoise(ST_STREAM_LOCAL_RAYS), 0, ST_RAYS);
    return 1.0 - stMaskCount(mask) / float(ST_RAYS);
}

// The blocked rays (a mask as stTransmittanceRays, rays k0 .. k0 + kn - 1) of projector slot `index` at a receiver, and the
// weight `fade` of the map's verdict (it fades out with the camera distance, over the last 20 % of spot_shadow_reach).
// 0 rays and fade 1 where the receiver is outside the projector's frustum.
int sampleSpotShadowRays(vec3 pos, vec3 norm, int index, vec2 pos_screen, vec2 nz, int k0, int kn, out float fade)
{
    fade = 1.0;
    mat4 m = shadow_matrix[4 + index];
    // clearly outside the projector's frustum (a margin of 0.05 of its extent, more than the 4 cm normal offset moves a
    // receiver except very near the eye) it lights nothing: no march and no depth-buffer normal, so the pixels in
    // neither cone cost what they did; the exact test follows the offset
    vec4 s0 = m * vec4(pos, 1.0);
    if (s0.w <= 0.0 || any(greaterThan(abs(s0.xyz / s0.w - 0.5), vec3(0.55))))
    {
        return 0;
    }
    float reach = spot_shadow_reach > 1.0 ? spot_shadow_reach : 128.0;
    fade = clamp((reach - length(pos)) / (0.2 * reach), 0.0, 1.0);
    if (fade <= 0.0)
    {
        return 0;
    }
    vec3 eye = stProjectorEye(m);
    // the receiver's geometric normal (from the depth buffer, like the sun's), not the shading normal: a bump or normal
    // map tilts the shading normal, and the receiver plane taken from it, which striped surfaces in columns
    float sin_theta;
    vec3 ng = shadowReceiverGeometry(pos_screen, pos, norm, normalize(eye - pos), sin_theta);
    pos += ng * spot_shadow_offset;
    s0 = m * vec4(pos, 1.0);
    if (s0.w <= 0.0 || any(greaterThan(abs(s0.xyz / s0.w - 0.5), vec3(0.5))))
    {
        fade = 1.0;
        return 0;
    }
    float z_bias = -spot_shadow_bias * ST_SPOT_BIAS_SCALE; // spot_shadow_bias is negative: the reference moves towards the light
    float radius = index == 0 ? spot_emitter_radius.x : spot_emitter_radius.y;
    float r_excl = index == 0 ? spot_shadow_excl.x : spot_shadow_excl.y;
    return index == 0
        ? stTransmittanceRays(shadowMap4, proj_shadow_res, m, pos, ng, eye, radius, r_excl, z_bias, ST_SPOT_RPDB_MAX, ST_SPOT_MARCH, ST_SPOT_STEPS, nz, k0, kn)
        : stTransmittanceRays(shadowMap5, proj_shadow_res, m, pos, ng, eye, radius, r_excl, z_bias, ST_SPOT_RPDB_MAX, ST_SPOT_MARCH, ST_SPOT_STEPS, nz, k0, kn);
}

float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen)
{
    float fade;
    int mask = sampleSpotShadowRays(pos, norm, index, pos_screen, stNoise(ST_STREAM_LOCAL_RAYS), 0, ST_RAYS, fade);
    return mix(1.0, 1.0 - stMaskCount(mask) / float(ST_RAYS), fade);
}
