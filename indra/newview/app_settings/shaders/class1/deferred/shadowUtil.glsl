/**
 * @file class1/deferred/shadowUtil.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

uniform sampler2D   normalMap;

#if defined(SUN_SHADOW)
uniform sampler2DShadow shadowMap0;
uniform sampler2DShadow shadowMap1;
uniform sampler2DShadow shadowMap2;
uniform sampler2DShadow shadowMap3;
#endif

#if defined(SPOT_SHADOW)
uniform sampler2DShadow shadowMap4;
uniform sampler2DShadow shadowMap5;
#endif

uniform vec3 primary_light_dir; // <Vulkanstorm/> L3: eye space, unit
uniform float primary_shadow_strength; // <Vulkanstorm/> L3: 1 full shadow, 0 none (also in windlight/atmosphericsFuncs.glsl)
uniform vec2 shadow_res;
uniform vec2 proj_shadow_res;
uniform mat4 shadow_matrix[6];
uniform vec4 shadow_clip;
uniform float shadow_bias;
uniform float shadow_offset;
uniform float spot_shadow_bias;
uniform float spot_shadow_offset;
uniform float spot_shadow_reach; // <Vulkanstorm/> metres: the projector shadows fade out towards this camera distance
uniform mat4 inv_proj;
uniform vec2 screen_res;

// <Vulkanstorm> receiver-plane depth bias of the sun PCF: the depth slope of the receiver in
// shadow texture space (d depth / d uv), from two 5 cm tangent steps of the view space position
// pos (normal norm) through the cascade's matrix m; c is pos projected and divided. Each tap's
// reference depth is lowered by the slope times the tap's distance from the receiver point, so
// a slanted lit surface does not shadow itself between the taps (acne on walls under a low sun).
// Replaces the altitude term of the bias (RenderShadowBiasError), which only widened a constant
// bias with height. Licensed LGPL-2.1 or GPL-3.
#define PCF_RPDB_STEP 0.05      // metres
#define PCF_RPDB_MAX  0.004     // cap per tap, shadow depth units (as PCSS)
vec2 pcfDepthGradient(mat4 m, vec3 pos, vec3 norm, vec3 c)
{
    if (dot(norm, norm) < 1.0e-8)
    {
        return vec2(0.0);
    }
    vec3 n = normalize(norm);
    vec3 t1 = normalize(cross(n, abs(n.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(n, t1);
    vec4 a = m * vec4(pos + t1 * PCF_RPDB_STEP, 1.0);
    vec4 b = m * vec4(pos + t2 * PCF_RPDB_STEP, 1.0);
    vec3 d1 = a.xyz / a.w - c;
    vec3 d2 = b.xyz / b.w - c;
    float det = d1.x * d2.y - d1.y * d2.x;
    if (abs(det) < 1.0e-14)
    {
        return vec2(1.0e4);
    }
    return abs(vec2(d1.z * d2.y - d1.y * d2.z, d1.x * d2.z - d1.z * d2.x) / det);
}
// </Vulkanstorm>

float pcfShadow(sampler2DShadow shadowMap, vec3 norm, vec4 stc, float bias_mul, vec2 pos_screen, vec3 light_dir, mat4 m, vec3 pos) // <Vulkanstorm/> m, pos: receiver-plane bias
{
#if defined(SUN_SHADOW)
    float offset = shadow_bias * bias_mul;
    stc.xyz /= stc.w;
    vec2 grad = pcfDepthGradient(m, pos, norm, stc.xyz); // <Vulkanstorm/>
    stc.z += offset * 2.0;
    float x0 = stc.x; // <Vulkanstorm/>
    stc.x = floor(stc.x*shadow_res.x + fract(pos_screen.y*shadow_res.y))/shadow_res.x; // add some chaotic jitter to X sample pos according to Y to disguise the snapping going on here
    // <Vulkanstorm> per-tap receiver-plane bias: the snap's own shift plus the tap's offset and
    // half a texel of bilinear footprint, in uv
    vec2 texel = 1.0 / shadow_res;
    vec2 snap = vec2(abs(stc.x - x0), 0.0) + 0.5 * texel;
    float cs = texture(shadowMap, vec3(stc.xy, stc.z - min(dot(grad, snap), PCF_RPDB_MAX)));
    float shadow = cs * 4.0;
    shadow += texture(shadowMap, vec3(stc.xy + vec2( 1.5,  0.5) * texel, stc.z - min(dot(grad, snap + vec2(1.5, 0.5) * texel), PCF_RPDB_MAX)));
    shadow += texture(shadowMap, vec3(stc.xy + vec2( 0.5, -1.5) * texel, stc.z - min(dot(grad, snap + vec2(0.5, 1.5) * texel), PCF_RPDB_MAX)));
    shadow += texture(shadowMap, vec3(stc.xy + vec2(-1.5, -0.5) * texel, stc.z - min(dot(grad, snap + vec2(1.5, 0.5) * texel), PCF_RPDB_MAX)));
    shadow += texture(shadowMap, vec3(stc.xy + vec2(-0.5,  1.5) * texel, stc.z - min(dot(grad, snap + vec2(0.5, 1.5) * texel), PCF_RPDB_MAX)));
    // </Vulkanstorm>
    return clamp(shadow * 0.125, 0.0, 1.0);
#else
    return 1.0;
#endif
}

float pcfSpotShadow(sampler2DShadow shadowMap, vec4 stc, float bias_scale, vec2 pos_screen)
{
#if defined(SPOT_SHADOW)
    stc.xyz /= stc.w;
    stc.z += spot_shadow_bias * bias_scale;
    stc.x = floor(proj_shadow_res.x * stc.x + fract(pos_screen.y*0.666666666)) / proj_shadow_res.x; // snap

    float cs = texture(shadowMap, stc.xyz);
    float shadow = cs;

    vec2 off = 1.0/proj_shadow_res;
    off.y *= 1.5;

    shadow += texture(shadowMap, stc.xyz+vec3(off.x*2.0, off.y, 0.0));
    shadow += texture(shadowMap, stc.xyz+vec3(off.x, -off.y, 0.0));
    shadow += texture(shadowMap, stc.xyz+vec3(-off.x, off.y, 0.0));
    shadow += texture(shadowMap, stc.xyz+vec3(-off.x*2.0, -off.y, 0.0));
    return shadow*0.2;
#else
    return 1.0;
#endif
}

#if defined(PCSS_SHADOW) && !defined(SHADOW_MODERN)
#define SHADOW_MODERN 1 // <Vulkanstorm/> the PCSS build is one of the modern methods
#endif

#if defined(SHADOW_MODERN) && defined(SUN_SHADOW)
// <Vulkanstorm> Stage S2: groundwork shared by the PCSS and the Stochastic shadow methods (SHADOW_MODERN
// is set by both; Legacy never sets it and keeps its own code above untouched). Written once for
// the orthographic sun cascades of stage S1 (their matrices rotate and scale only: the rows of the
// matrix are orthogonal, and the rows' lengths are the map's scales). Licensed LGPL-2.1 or GPL-3.
//   * the receiver plane: the geometric normal of the surface, rebuilt from the depth buffer, instead
//     of the G-buffer normal, which carries the normal map's bumps;
//   * the plane's signed depth gradient in the map, and the bias in world units (texels of the map
//     at the pixel, times the map's depth scale), the same in every cascade;
//   * the 25 cm footprint SHADOW_FOOTPRINT_M a filter may reach (the caster cull guard of
//     llshadowcascadefit.h keeps every caster inside that reach);
//   * a signed-plane bilinear compare tap through textureGather (SHADOW_GATHER).
// The constants SHADOW_BIAS_TEXELS, SHADOW_NORMAL_OFFSET_TEXELS, SHADOW_PLANE_BIAS_MAX_TEXELS and
// SHADOW_FOOTPRINT_M come from llshadowcascadefit.h through the shader manager (one source of truth).

vec4 getPosition(vec2 pos_screen); // deferredUtil.glsl: view space position of a screen position

#define SHADOW_GRAD_MAX        1.0e4  // depth gradient used where the plane is edge-on to the light
#define SHADOW_EDGE_REL        0.03   // a depth jump to the neighbour pixel beyond this fraction of the
#define SHADOW_EDGE_ABS        0.02   //   depth plus this many metres is an edge, not a slope
#define SHADOW_PLANAR_MAX      0.35   // a neighbour more than this (sine of the angle) off the G-buffer normal's tangent plane is another surface

// The geometric normal (view space, towards the eye) at the pixel pos_screen (position pos). Per screen
// axis it takes the neighbour pixel that lies best in the tangent plane of the G-buffer normal gnorm (the
// normal map's bumps are small against that test), so that neither a depth edge nor a crease (a wall's
// foot, where one neighbour is the ground) on one side bends the plane. gnorm itself is the result only where
// no neighbour of an axis fits (a thin or isolated surface) or at the screen's edge.
vec3 shadowGeometricNormal(vec2 pos_screen, vec3 pos, vec3 gnorm)
{
    vec3 gn = dot(gnorm, gnorm) > 1.0e-8 ? normalize(gnorm) : vec3(0.0, 0.0, 1.0);
    vec2 px = 1.0 / screen_res;
    float tol = SHADOW_EDGE_REL * abs(pos.z) + SHADOW_EDGE_ABS;
    float big = 1.0e30;

    vec3 xr = getPosition(pos_screen + vec2(px.x, 0.0)).xyz - pos;
    vec3 xl = pos - getPosition(pos_screen - vec2(px.x, 0.0)).xyz;
    vec3 yu = getPosition(pos_screen + vec2(0.0, px.y)).xyz - pos;
    vec3 yd = pos - getPosition(pos_screen - vec2(0.0, px.y)).xyz;

    // how far each neighbour is off the tangent plane (sine), big where it is off the screen or across an edge
    float sxr = pos_screen.x + px.x < 1.0 && abs(xr.z) <= tol ? abs(dot(xr, gn)) / max(length(xr), 1.0e-6) : big;
    float sxl = pos_screen.x - px.x > 0.0 && abs(xl.z) <= tol ? abs(dot(xl, gn)) / max(length(xl), 1.0e-6) : big;
    float syu = pos_screen.y + px.y < 1.0 && abs(yu.z) <= tol ? abs(dot(yu, gn)) / max(length(yu), 1.0e-6) : big;
    float syd = pos_screen.y - px.y > 0.0 && abs(yd.z) <= tol ? abs(dot(yd, gn)) / max(length(yd), 1.0e-6) : big;

    vec3 ex = sxr < sxl ? xr : xl;
    vec3 ey = syu < syd ? yu : yd;

    vec3 n = cross(ex, ey);
    float len2 = dot(n, n);
    if (min(sxr, sxl) > SHADOW_PLANAR_MAX || min(syu, syd) > SHADOW_PLANAR_MAX || !(len2 > 1.0e-14))
    {
        return gn;
    }
    n *= inversesqrt(len2);
    return dot(n, pos) > 0.0 ? -n : n; // facing the eye
}

// the rows of a cascade matrix: texture space (u, v, depth) of a view space point is (r0.p, r1.p, r2.p) + m[3].xyz
vec3 shadowRow(mat4 m, int i)
{
    return vec3(m[0][i], m[1][i], m[2][i]);
}

// uv per metre across the light's rays, per map axis
vec2 shadowUVScale(mat4 m)
{
    vec3 r0 = shadowRow(m, 0);
    vec3 r1 = shadowRow(m, 1);
    return sqrt(vec2(dot(r0, r0), dot(r1, r1)));
}

// depth per metre along the light's rays
float shadowDepthScale(mat4 m)
{
    return length(shadowRow(m, 2));
}

// metres per texel of the cascade, the larger of the two axes (a cascade's texels are square)
float shadowTexelMetres(mat4 m, vec2 res)
{
    vec2 s = shadowUVScale(m) * res;
    return 1.0 / max(min(s.x, s.y), 1.0e-6);
}

// the signed depth gradient (d depth / d uv) of the plane with view space normal n in the map
vec2 shadowPlaneGradient(mat4 m, vec3 n)
{
    vec3 r0 = shadowRow(m, 0);
    vec3 r1 = shadowRow(m, 1);
    vec3 r2 = shadowRow(m, 2);
    vec3 np = vec3(dot(r0, n) / dot(r0, r0), dot(r1, n) / dot(r1, r1), dot(r2, n) / dot(r2, r2));
    float nz = abs(np.z) < 1.0e-7 ? (np.z < 0.0 ? -1.0e-7 : 1.0e-7) : np.z;
    return clamp(-np.xy / nz, vec2(-SHADOW_GRAD_MAX), vec2(SHADOW_GRAD_MAX));
}

// depth-unit bias of the cascade's reference depth, in world units: a length in texels of the map at
// the pixel, converted by the map's depth scale (positive; the reference moves towards the light)
float shadowWorldBias(float texel_m, float s_z, float texels)
{
    return texels * texel_m * s_z;
}

// the position the cascade samples at: the surface point moved along its geometric normal by
// SHADOW_NORMAL_OFFSET_TEXELS texels of the cascade, scaled by sin(angle to the light) so that a surface
// facing the light is not moved at all
vec3 shadowNormalOffsetPos(vec3 pos, vec3 ng, float sin_theta, float texel_m)
{
    return pos + ng * (SHADOW_NORMAL_OFFSET_TEXELS * texel_m * sin_theta);
}

#if defined(SHADOW_GATHER)
// One bilinear compare tap at c.xy against the receiver plane with the signed gradient g (d depth /
// d uv): the four texels around c.xy are gathered as raw depth and each compared with the plane's depth at its
// own centre, then weighted bilinearly. c.z: the receiver's depth with the constant bias added.
// plane_max: the cap of the plane's offset, shadow depth units. The plane only ever lowers the reference
// (a texel the plane puts deeper is compared at the receiver's own depth): past a crease, where a wall meets the
// ground, the extrapolated plane is not the surface, and raising the reference there darkened the wall's foot.
// The gather is taken at the 2x2 block's
// centre, a texel corner, so it never disagrees with the floor() below.
float shadowPlaneTap(sampler2D depthMap, vec3 c, vec2 g, vec2 res, float plane_max)
{
    vec2 t = c.xy * res - 0.5;
    vec2 base = floor(t);
    vec2 f = t - base;
    vec4 d = textureGather(depthMap, (base + 1.0) / res, 0); // (i0,j1) (i1,j1) (i1,j0) (i0,j0)
    vec2 p = (base + 0.5) / res - c.xy;                      // centre of texel (i0,j0) from the receiver
    vec2 q = 1.0 / res;
    vec4 ref = c.z + clamp(vec4(dot(g, p + vec2(0.0, q.y)), dot(g, p + q), dot(g, p + vec2(q.x, 0.0)), dot(g, p)),
                           vec4(-plane_max), vec4(0.0));
    vec4 lit = step(ref, d);
    vec4 w = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
    return lit.x + lit.y + lit.z + lit.w == 4.0 ? 1.0 : dot(lit, w); // exactly 1 where all four are lit
}
#endif
// </Vulkanstorm>
#endif

#if defined(PCSS_SHADOW)
// <Vulkanstorm> PCSS stage 1: rotated Poisson PCF with a receiver-plane depth bias.
// Compiled only into the deferred sun program (gDeferredSunProgram) when the shadow method is PCSS;
// every other consumer of this file gets the PCF above, unchanged. Licensed LGPL-2.1 or GPL-3.

// angular size of the shadowing light, L = 2 tan(half angle) (physical sun times the sky's
// sun_scale times RenderShadowPCSSLightSize); the penumbra width uses it from stage 2 on
uniform float shadow_light_size;

#define PCSS_PCF_TAPS   12
#define PCSS_PCF_RADIUS 1.5     // kernel radius in shadow map texels
#define PCSS_PLANE_STEP 0.05    // metres: tangent step that measures the receiver plane
#define PCSS_RPDB_MAX   0.004   // cap of the receiver-plane bias, shadow depth units
#define PCSS_GRAD_MAX   1.0e4   // depth gradient used where the plane is edge-on to the light

// Best-candidate Poisson disk, mean at the centre, farthest point on the unit circle:
// scripts/perf/gen_poisson_disk.py 12
const vec2 pcss_poisson12[12] = vec2[12](
    vec2(-0.789262,  0.127632),
    vec2( 0.406628, -0.132460),
    vec2(-0.032561,  0.702528),
    vec2(-0.350013, -0.648809),
    vec2( 0.580484,  0.508811),
    vec2(-0.653347,  0.747330),
    vec2( 0.265165, -0.741572),
    vec2(-0.182968,  0.036783),
    vec2(-0.897737, -0.440532),
    vec2( 0.819877,  0.077022),
    vec2( 0.186699,  0.290105),
    vec2( 0.647037, -0.526838));

// interleaved gradient noise (Jimenez 2014) of the pixel: the kernel's rotation
float pcssNoise(vec2 frag)
{
    return fract(52.9829189 * fract(0.06711056 * frag.x + 0.00583715 * frag.y));
}

// Receiver plane in shadow texture space: d(depth)/d(uv) of the surface (view space position
// pos, normal norm) under the projective shadow matrix m, from two tangent steps. c is the
// pixel's own projected and divided position.
vec2 pcssDepthGradient(mat4 m, vec3 pos, vec3 norm, vec3 c)
{
    if (dot(norm, norm) < 1.0e-8)
    {
        return vec2(0.0); // no normal (sky): constant bias only
    }
    vec3 n = normalize(norm);
    vec3 t1 = normalize(cross(n, abs(n.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(n, t1);
    vec4 a = m * vec4(pos + t1 * PCSS_PLANE_STEP, 1.0);
    vec4 b = m * vec4(pos + t2 * PCSS_PLANE_STEP, 1.0);
    vec3 d1 = a.xyz / a.w - c;
    vec3 d2 = b.xyz / b.w - c;
    float det = d1.x * d2.y - d1.y * d2.x;
    if (abs(det) < 1.0e-14)
    {
        return vec2(PCSS_GRAD_MAX);
    }
    vec2 g = vec2(d1.z * d2.y - d1.y * d2.z, d1.x * d2.z - d1.z * d2.x) / det;
    return clamp(g, vec2(-PCSS_GRAD_MAX), vec2(PCSS_GRAD_MAX));
}

// Fraction (0..1) of the kernel radius that keeps every tap, with its bilinear footprint,
// inside the map: the kernel shrinks towards the map's edges (the cascade blend hides it).
float pcssEdgeScale(vec2 uv, vec2 extent, vec2 texel)
{
    vec2 room = min(uv, vec2(1.0) - uv) - 0.5 * texel;
    return clamp(min(room.x / extent.x, room.y / extent.y), 0.0, 1.0);
}

// the kernel's per pixel rotation (cos, sin)
vec2 pcssRotation()
{
    float angle = 6.28318531 * pcssNoise(gl_FragCoord.xy);
    return vec2(cos(angle), sin(angle));
}

// 12 rotated Poisson taps through the compare sampler (each tap a hardware 2x2 PCF) over an
// ellipse of half axes extent (uv). c: divided position with the constant bias already added
// to its depth; grad: |receiver plane gradient|; the plane's bias at a tap is grad . (|offset| + foot)
// capped at rpdb_max (foot: the bilinear half texel of the tap's compare, zero for the projectors).
float pcssPCF(sampler2DShadow shadowMap, vec3 c, vec2 grad, vec2 extent, vec2 rot, float rpdb_max, vec2 foot)
{
    float shadow = 0.0;
    for (int i = 0; i < PCSS_PCF_TAPS; ++i)
    {
        vec2 p = pcss_poisson12[i];
        vec2 off = vec2(rot.x * p.x - rot.y * p.y, rot.y * p.x + rot.x * p.y) * extent;
        float rpdb = min(dot(grad, abs(off) + foot), rpdb_max);
        shadow += texture(shadowMap, vec3(c.xy + off, c.z - rpdb));
    }
    return shadow * (1.0 / float(PCSS_PCF_TAPS));
}

// Fixed radius rotated PCF (stage 1): cascades 2-3, and everything without PCSS_SHADOW_FETCH.
// stc: projected, not yet divided; grad: |receiver plane gradient|; z_bias: the constant bias added to
// the reference depth; rpdb_max, foot: as pcssPCF.
float pcssPoissonPCF(sampler2DShadow shadowMap, vec4 stc, vec2 grad, float z_bias, vec2 res, float rpdb_max, vec2 foot)
{
    stc.xyz /= stc.w;
    stc.z += z_bias;

    vec2 texel = 1.0 / res;
    vec2 extent = PCSS_PCF_RADIUS * texel;
    extent *= pcssEdgeScale(stc.xy, extent, texel);

    return pcssPCF(shadowMap, stc.xyz, grad, extent, pcssRotation(), rpdb_max, foot);
}

#if defined(PCSS_SHADOW_FETCH)
// Stage 2, contact hardening (sun cascades 0-1 and the spot shadows): a blocker search through a
// plain depth read of the same map (shadowDepthN, bound with a GL 3.3 sampler object that has
// compare mode off), then a penumbra from the blockers' distance and the light's size, then the
// rotated PCF above with that radius.
#if defined(SUN_SHADOW)
uniform sampler2D shadowDepth0;
uniform sampler2D shadowDepth1;
uniform sampler2D shadowDepth2; // <Vulkanstorm/> S2: bound for the shared groundwork; unused (and dropped by the compiler) until stage S3
uniform sampler2D shadowDepth3; // <Vulkanstorm/> S2: as shadowDepth2
#endif
#if defined(SPOT_SHADOW)
uniform sampler2D shadowDepth4;
uniform sampler2D shadowDepth5;
#endif

#define PCSS_BLOCKER_DIST0     4.0   // metres: farthest blocker that softens cascade 0
#define PCSS_BLOCKER_DIST1     8.0   // metres: the same for cascade 1
#define PCSS_BLOCKER_DIST_SPOT 4.0   // metres: the same for the spot shadows
#define PCSS_SPOT_LIGHT_SIZE   0.15  // metres: width of a projector's emitter
#define PCSS_PCF_RADIUS_MIN    1.0   // texels: narrowest PCF (contact)
#define PCSS_PCF_RADIUS_MAX    12.0  // texels at shadow resolution scale 1: widest PCF and search
#define PCSS_SEARCH_RADIUS_MIN 2.0   // texels: narrowest blocker search
#define PCSS_SCALE_STEP        0.25  // metres: step that measures the map's scale at the pixel

// texture space position (uv, depth) of a view space point
vec3 pcssProject(mat4 m, vec3 p)
{
    vec4 s = m * vec4(p, 1.0);
    return s.xyz / s.w;
}

// uv per metre, per map axis, across the light ray dir at pos (c: pos projected): the bounding
// box of the image of a unit disc perpendicular to the ray, so right for warped and perspective maps
vec2 pcssUVScale(mat4 m, vec3 pos, vec3 c, vec3 dir)
{
    vec3 t1 = normalize(cross(dir, abs(dir.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(dir, t1);
    vec2 a = (pcssProject(m, pos + t1 * PCSS_SCALE_STEP).xy - c.xy) / PCSS_SCALE_STEP;
    vec2 b = (pcssProject(m, pos + t2 * PCSS_SCALE_STEP).xy - c.xy) / PCSS_SCALE_STEP;
    return vec2(length(vec2(a.x, b.x)), length(vec2(a.y, b.y)));
}

// the map's resolution scale (RenderShadowResolutionScale): its width over the screen's
float pcssResScale(vec2 res)
{
    return max(res.x / screen_res.x, 1.0);
}

vec2 pcssClampExtent(vec2 extent, vec2 texel, float lo, float hi)
{
    return clamp(extent, texel * lo, texel * hi);
}

// mean raw depth of the blockers within the search ellipse (half axes extent, uv), -1 if none:
// texels nearer the light than the receiver plane at the tap, less the bias
float pcssBlockerDepth(sampler2D depthMap, vec3 c, float z_bias, vec2 grad, vec2 extent, vec2 rot, float rpdb_max, vec2 foot)
{
    // the centre first: the disk has no tap there, and a thin caster right under the
    // receiver would otherwise slip between the taps of a wide search and read as lit
    float sum = 0.0;
    float count = 0.0;
    float centre = texture(depthMap, c.xy).r;
    if (centre < c.z + z_bias)
    {
        sum += centre;
        count += 1.0;
    }
    for (int i = 0; i < PCSS_PCF_TAPS; ++i)
    {
        vec2 p = pcss_poisson12[i];
        vec2 off = vec2(rot.x * p.x - rot.y * p.y, rot.y * p.x + rot.x * p.y) * extent;
        float d = texture(depthMap, c.xy + off).r;
        if (d < c.z + z_bias - min(dot(grad, abs(off) + foot), rpdb_max))
        {
            sum += d;
            count += 1.0;
        }
    }
    return count > 0.0 ? sum / count : -1.0;
}

// Sun cascade with contact hardening. ng: the geometric normal (shadowGeometricNormal); d_max: farthest
// blocker. Penumbra (full width, metres on the receiver) = blocker distance * light size; the PCF disc
// takes that width as its diameter, which gives a disc light's edge profile. The map is orthographic,
// so its scales come from the matrix (S2), and the search and the filter never reach past
// SHADOW_FOOTPRINT_M, the distance the caster cull guard covers.
float pcssSunContactShadow(sampler2DShadow shadowMap, sampler2D depthMap, mat4 m, vec4 stc, vec3 ng, float d_max)
{
    vec3 c = stc.xyz / stc.w;
    vec2 texel = 1.0 / shadow_res;
    vec2 s_uv = shadowUVScale(m);
    float s_z = shadowDepthScale(m);
    float texel_m = shadowTexelMetres(m, shadow_res);
    float z_bias = -shadowWorldBias(texel_m, s_z, SHADOW_BIAS_TEXELS);
    float rpdb_max = shadowWorldBias(texel_m, s_z, SHADOW_PLANE_BIAS_MAX_TEXELS);
    vec2 g = shadowPlaneGradient(m, ng);
    vec2 grad = abs(g);
    vec2 foot = 0.5 * texel;
    float r_max = PCSS_PCF_RADIUS_MAX * pcssResScale(shadow_res);
    vec2 reach = max(s_uv * SHADOW_FOOTPRINT_M, texel * PCSS_SEARCH_RADIUS_MIN);
    vec2 rot = pcssRotation();

    vec2 search = min(pcssClampExtent(s_uv * shadow_light_size * d_max, texel, PCSS_SEARCH_RADIUS_MIN, r_max), reach);
    search *= pcssEdgeScale(c.xy, search, texel);
    float z_blocker = pcssBlockerDepth(depthMap, c, z_bias, grad, search, rot, rpdb_max, foot);
    if (z_blocker < 0.0)
    {
        // no blocker in the search: one comparison tap at the centre still catches a caster
        // thinner than the search taps' spacing inside the bilinear footprint
#if defined(SHADOW_GATHER)
        return shadowPlaneTap(depthMap, vec3(c.xy, c.z + z_bias), g, shadow_res, rpdb_max);
#else
        return texture(shadowMap, vec3(c.xy, c.z + z_bias));
#endif
    }

    float w_world = max(c.z - z_blocker, 0.0) / max(s_z, 1.0e-8) * shadow_light_size;
    vec2 extent = min(pcssClampExtent(0.5 * w_world * s_uv, texel, PCSS_PCF_RADIUS_MIN, r_max), reach);
    extent *= pcssEdgeScale(c.xy, extent, texel);
    return pcssPCF(shadowMap, vec3(c.xy, c.z + z_bias), grad, extent, rot, rpdb_max, foot);
}

// a projector's position in view space: the point its matrix maps to x = y = w = 0
vec3 pcssProjectorEye(mat4 m)
{
    vec4 r0 = vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
    vec4 r1 = vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
    vec4 r3 = vec4(m[0][3], m[1][3], m[2][3], m[3][3]);
    mat3 rows_t = mat3(r0.xyz, r1.xyz, r3.xyz); // its columns are the rows
    return -vec3(r0.w, r1.w, r3.w) * inverse(rows_t);
}

// Spot shadow with contact hardening, the textbook perspective form: an emitter of width
// PCSS_SPOT_LIGHT_SIZE at the projector, blockers at linear distance d_b, receiver at d_r:
// penumbra (full width, metres on the receiver) = PCSS_SPOT_LIGHT_SIZE * (d_r - d_b) / d_b.
float pcssSpotContactShadow(sampler2DShadow shadowMap, sampler2D depthMap, mat4 m, vec4 stc, vec3 pos, vec3 norm, float bias_scale)
{
    vec3 c = stc.xyz / stc.w;
    float d_r = stc.w;                       // linear distance along the projector's axis
    vec2 grad = abs(pcssDepthGradient(m, pos, norm, c));
    float z_bias = spot_shadow_bias * bias_scale;
    vec2 texel = 1.0 / proj_shadow_res;
    float r_max = PCSS_PCF_RADIUS_MAX * pcssResScale(proj_shadow_res);
    vec2 rot = pcssRotation();

    vec3 eye = pcssProjectorEye(m);
    vec2 s_uv = pcssUVScale(m, pos, c, normalize(eye - pos));
    // texture z (before the divide) at the projector itself: linearises a raw depth below
    float z_eye = m[0][2] * eye.x + m[1][2] * eye.y + m[2][2] * eye.z + m[3][2];

    float d_near = max(d_r - PCSS_BLOCKER_DIST_SPOT, 0.25 * d_r);
    vec2 search = pcssClampExtent(s_uv * PCSS_SPOT_LIGHT_SIZE * (d_r - d_near) / d_near, texel, PCSS_SEARCH_RADIUS_MIN, r_max);
    search *= pcssEdgeScale(c.xy, search, texel);
    float z_blocker = pcssBlockerDepth(depthMap, c, z_bias, grad, search, rot, PCSS_RPDB_MAX, vec2(0.0));
    if (z_blocker < 0.0)
    {
        // no blocker in the search: one comparison tap at the centre still catches a caster
        // thinner than the search taps' spacing inside the bilinear footprint
        return texture(shadowMap, vec3(c.xy, c.z + z_bias));
    }

    // along the ray eye + s (pos - eye): w = s d_r and z = z_eye + s (stc.z - z_eye); z / w = z_blocker
    // (z_eye and the denominator share their sign: negative for a GL style projection)
    float denom = z_blocker * d_r - stc.z + z_eye;
    float d_b = abs(denom) > 1.0e-7 ? z_eye * d_r / denom : d_r;
    d_b = clamp(d_b, 1.0e-3, d_r);
    float w_world = PCSS_SPOT_LIGHT_SIZE * (d_r - d_b) / d_b;
    vec2 extent = pcssClampExtent(0.5 * w_world * s_uv, texel, PCSS_PCF_RADIUS_MIN, r_max);
    extent *= pcssEdgeScale(c.xy, extent, texel);
    return pcssPCF(shadowMap, vec3(c.xy, c.z + z_bias), grad, extent, rot, PCSS_RPDB_MAX, vec2(0.0));
}
#endif // PCSS_SHADOW_FETCH

// sun cascade index (0-3), the fixed radius filter: the geometric plane (ng) and the world unit bias (S2)
float pcssShadow(sampler2DShadow shadowMap, int index, vec4 stc, vec3 ng)
{
    mat4 m = shadow_matrix[index];
    float s_z = shadowDepthScale(m);
    float texel_m = shadowTexelMetres(m, shadow_res);
    return pcssPoissonPCF(shadowMap, stc, abs(shadowPlaneGradient(m, ng)), -shadowWorldBias(texel_m, s_z, SHADOW_BIAS_TEXELS), shadow_res,
                          shadowWorldBias(texel_m, s_z, SHADOW_PLANE_BIAS_MAX_TEXELS), 0.5 / shadow_res);
}

// the sun cascade's sample position: the surface point moved along the geometric normal (S2)
vec3 pcssCascadePos(int index, vec3 pos, vec3 ng, float sin_theta)
{
    return shadowNormalOffsetPos(pos, ng, sin_theta, shadowTexelMetres(shadow_matrix[index], shadow_res));
}

// spot shadow index (4-5): the constant bias as pcfSpotShadow applies it
float pcssSpotShadow(sampler2DShadow shadowMap, int index, vec4 stc, vec3 pos, vec3 norm, float bias_scale)
{
    vec3 c = stc.xyz / stc.w;
    vec2 grad = abs(pcssDepthGradient(shadow_matrix[index], pos, norm, c));
    return pcssPoissonPCF(shadowMap, stc, grad, spot_shadow_bias * bias_scale, proj_shadow_res, PCSS_RPDB_MAX, vec2(0.0));
}
// </Vulkanstorm>
#endif

float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen)
{
#if defined(SUN_SHADOW)
    float shadow = 0.0f;
    vec3 light_dir = normalize(primary_light_dir); // <Vulkanstorm/> L3: the primary light

    float dp_directional_light = max(0.0, dot(norm.xyz, light_dir));
          dp_directional_light = clamp(dp_directional_light, 0.0, 1.0);

    vec3 shadow_pos = pos.xyz;

    vec3 offset = light_dir.xyz * (1.0 - dp_directional_light);

#if defined(PCSS_SHADOW)
    // <Vulkanstorm> S2: the receiver plane is the surface's geometric normal, rebuilt from the depth buffer,
    // and each cascade moves the sample along it by texels of its own (no light direction offset)
    vec3 ng = shadowGeometricNormal(pos_screen, pos, norm);
    float dp_geometric = clamp(dot(ng, light_dir), 0.0, 1.0);
    float sin_theta = sqrt(max(1.0 - dp_geometric * dp_geometric, 0.0));
    // </Vulkanstorm>
#else
    shadow_pos += offset * shadow_offset * 2.0;
#endif

    vec4 spos = vec4(shadow_pos.xyz, 1.0);

    if (spos.z > -shadow_clip.w)
    {
        vec4 lpos;
        vec4 near_split = shadow_clip*-0.75;
        vec4 far_split = shadow_clip*-1.25;
        vec4 transition_domain = near_split-far_split;
        float weight = 0.0;

        if (spos.z < near_split.z)
        {
#if defined(PCSS_SHADOW)
            lpos = shadow_matrix[3]*vec4(pcssCascadePos(3, spos.xyz, ng, sin_theta), 1.0);
#else
            lpos = shadow_matrix[3]*spos;
#endif

            float w = 1.0;
            w -= max(spos.z-far_split.z, 0.0)/transition_domain.z;
            //w = clamp(w, 0.0, 1.0);
#if defined(PCSS_SHADOW)
            float contrib = pcssShadow(shadowMap3, 3, lpos, ng)*w;
#else
            float contrib = pcfShadow(shadowMap3, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[3], spos.xyz)*w;
#endif
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
            shadow += max((pos.z+shadow_clip.z)/(shadow_clip.z-shadow_clip.w)*2.0-1.0, 0.0);
        }

        if (spos.z < near_split.y && spos.z > far_split.z)
        {
#if defined(PCSS_SHADOW)
            lpos = shadow_matrix[2]*vec4(pcssCascadePos(2, spos.xyz, ng, sin_theta), 1.0);
#else
            lpos = shadow_matrix[2]*spos;
#endif

            float w = 1.0;
            w -= max(spos.z-far_split.y, 0.0)/transition_domain.y;
            w -= max(near_split.z-spos.z, 0.0)/transition_domain.z;
            //w = clamp(w, 0.0, 1.0);
#if defined(PCSS_SHADOW)
            float contrib = pcssShadow(shadowMap2, 2, lpos, ng)*w;
#else
            float contrib = pcfShadow(shadowMap2, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[2], spos.xyz)*w;
#endif
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
        }

        if (spos.z < near_split.x && spos.z > far_split.y)
        {
#if defined(PCSS_SHADOW)
            lpos = shadow_matrix[1]*vec4(pcssCascadePos(1, spos.xyz, ng, sin_theta), 1.0);
#else
            lpos = shadow_matrix[1]*spos;
#endif

            float w = 1.0;
            w -= max(spos.z-far_split.x, 0.0)/transition_domain.x;
            w -= max(near_split.y-spos.z, 0.0)/transition_domain.y;
            //w = clamp(w, 0.0, 1.0);
#if defined(PCSS_SHADOW) && defined(PCSS_SHADOW_FETCH)
            float contrib = pcssSunContactShadow(shadowMap1, shadowDepth1, shadow_matrix[1], lpos, ng, PCSS_BLOCKER_DIST1)*w;
#elif defined(PCSS_SHADOW)
            float contrib = pcssShadow(shadowMap1, 1, lpos, ng)*w;
#else
            float contrib = pcfShadow(shadowMap1, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[1], spos.xyz)*w;
#endif
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
        }

        if (spos.z > far_split.x)
        {
#if defined(PCSS_SHADOW)
            lpos = shadow_matrix[0]*vec4(pcssCascadePos(0, spos.xyz, ng, sin_theta), 1.0);
#else
            lpos = shadow_matrix[0]*spos;
#endif

            float w = 1.0;
            w -= max(near_split.x-spos.z, 0.0)/transition_domain.x;
            //w = clamp(w, 0.0, 1.0);
#if defined(PCSS_SHADOW) && defined(PCSS_SHADOW_FETCH)
            float contrib = pcssSunContactShadow(shadowMap0, shadowDepth0, shadow_matrix[0], lpos, ng, PCSS_BLOCKER_DIST0)*w;
#elif defined(PCSS_SHADOW)
            float contrib = pcssShadow(shadowMap0, 0, lpos, ng)*w;
#else
            float contrib = pcfShadow(shadowMap0, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[0], spos.xyz)*w;
#endif
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
        }

        shadow /= weight;
    }
    else
    {
        return 1.0f; // lit beyond the far split...
    }
    //shadow = min(dp_directional_light,shadow);
    // <Vulkanstorm> the sun's shadow fades out while the light hands over to the moon, and in again for the moon (L3)
    return mix(1.0, shadow, primary_shadow_strength);
    // </Vulkanstorm>
#else
    return 1.0;
#endif
}

float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen)
{
#if defined(SPOT_SHADOW)
    pos += norm * spot_shadow_offset;

    vec4 spos = vec4(pos,1.0);

    // <Vulkanstorm> The projector shadow fades out with the distance from the camera to the lit
    // pixel (Euclidean, so it does not depend on which way the camera looks), over the last 20 %
    // of spot_shadow_reach. It used to fade with the sun's cascade splits (shadow_clip z and w),
    // which follow what the camera sees: the fade-out distance went from 16 m to 256 m with the
    // camera's pitch and yaw, and projector shadows came and went with the view.
    float reach = spot_shadow_reach > 1.0 ? spot_shadow_reach : 128.0;
    float fade = clamp((reach - length(pos)) / (0.2 * reach), 0.0, 1.0);
    if (fade <= 0.0)
    {
        return 1.0;
    }

    float shadow;
    vec4 lpos;
    if (index == 0)
    {
        lpos = shadow_matrix[4]*spos;
#if defined(PCSS_SHADOW) && defined(PCSS_SHADOW_FETCH)
        shadow = pcssSpotContactShadow(shadowMap4, shadowDepth4, shadow_matrix[4], lpos, spos.xyz, norm, 0.8);
#elif defined(PCSS_SHADOW)
        shadow = pcssSpotShadow(shadowMap4, 4, lpos, spos.xyz, norm, 0.8);
#else
        shadow = pcfSpotShadow(shadowMap4, lpos, 0.8, spos.xy);
#endif
    }
    else
    {
        lpos = shadow_matrix[5]*spos;
#if defined(PCSS_SHADOW) && defined(PCSS_SHADOW_FETCH)
        shadow = pcssSpotContactShadow(shadowMap5, shadowDepth5, shadow_matrix[5], lpos, spos.xyz, norm, 0.8);
#elif defined(PCSS_SHADOW)
        shadow = pcssSpotShadow(shadowMap5, 5, lpos, spos.xyz, norm, 0.8);
#else
        shadow = pcfSpotShadow(shadowMap5, lpos, 0.8, spos.xy);
#endif
    }
    return mix(1.0, shadow, fade);
    // </Vulkanstorm>
#else
    return 1.0;
#endif
}

