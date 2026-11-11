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

#if defined(SHADOW_COMMON_ONLY)
// <Vulkanstorm> the Stochastic method links the method-neutral block (SHADOW_COMMON below) next to its own
// shadow util: this build is that block alone, with the screen size it reads. Nothing else of this file is in it.
uniform vec2 screen_res;
#else
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
#define PCF_RPDB_MAX  0.004     // cap per tap, shadow depth units (as the stable-cascade methods)
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
#endif // !SHADOW_COMMON_ONLY

#if defined(SHADOW_COMMON) && defined(SUN_SHADOW)
// <Vulkanstorm> Stage S2: method-neutral groundwork (SHADOW_COMMON). Any shadow method may use it:
// the Stochastic method links this block (SHADOW_COMMON_ONLY builds), and the Legacy build can define it to
// call the helpers (nothing in Legacy does yet, and without the define the Legacy text below is untouched).
// The block needs no textureGather (GLSL 1.50). Written once for
// the orthographic sun cascades of stage S1 (their matrices rotate and scale only: the rows of the
// matrix are orthogonal, and the rows' lengths are the map's scales). Licensed LGPL-2.1 or GPL-3.
//   * the receiver plane: the geometric normal of the surface, rebuilt from the depth buffer, instead
//     of the G-buffer normal, which carries the normal map's bumps;
//   * the plane's signed depth gradient in the map, and the bias in world units (texels of the map
//     at the pixel, times the map's depth scale), the same in every cascade;
//   * the 25 cm footprint SHADOW_FOOTPRINT_M a filter may reach (the caster cull guard of
//     llshadowcascadefit.h keeps every caster inside that reach);
// The constants SHADOW_BIAS_TEXELS, SHADOW_NORMAL_OFFSET_TEXELS, SHADOW_PLANE_BIAS_MAX_TEXELS,
// SHADOW_GRAD_MAX and SHADOW_FOOTPRINT_M come from llshadowcascadefit.h through the shader manager (one source of truth).

#if !defined(SHADOW_COMMON_NO_DEPTH)
vec4 getPosition(vec2 pos_screen); // deferredUtil.glsl: view space position of a screen position

#define SHADOW_EDGE_REL        0.03   // a depth jump to the neighbour pixel beyond this fraction of the
#define SHADOW_EDGE_ABS        0.02   //   depth plus this many metres is an edge, not a slope
#define SHADOW_STEP_TIE        0.002  // metres: two steps whose depths differ less than this (and half) are alike
#define SHADOW_PARALLEL_MIN    1.0e-4 // squared sine of the angle between two unit steps below which they are parallel

// One screen axis of shadowGeometricNormal: the step (view space, to the neighbour pixel) that lies in the
// surface. fwd and bwd are the steps to the next and the previous pixel and vf and vb tell whether each is on
// the screen and on the same surface (no depth edge). Where one step is clearly flatter in depth than the
// other (a crease or a fold: the other side is another surface) that one is taken. Where they are alike both
// lie in the surface and the one nearer the tangent plane of the G-buffer normal gn is taken, which only
// breaks the tie: the plane itself comes from the depth buffer, so a tilted normal map does not bend it.
// ok is false where no neighbour is usable.
vec3 shadowAxisStep(vec3 fwd, vec3 bwd, bool vf, bool vb, vec3 gn, out bool ok)
{
    ok = vf || vb;
    if (!vb) return fwd;
    if (!vf) return bwd;
    float zf = abs(fwd.z);
    float zb = abs(bwd.z);
    float hi = max(zf, zb);
    if (hi - min(zf, zb) > 0.5 * hi + SHADOW_STEP_TIE)
    {
        return zf < zb ? fwd : bwd;
    }
    float df = dot(fwd, gn);
    float db = dot(bwd, gn);
    return df * df * dot(bwd, bwd) < db * db * dot(fwd, fwd) ? fwd : bwd; // the smaller sine to gn, without roots
}

// The geometric normal (view space, towards the eye) at the pixel pos_screen (position pos), the plane through
// the pixel and its neighbours in the depth buffer. Per screen axis shadowAxisStep picks the neighbour that
// lies in the surface; the G-buffer normal gnorm (the normal-mapped one) is the result only where an axis has
// no usable neighbour (a thin or isolated surface, the screen's edge) or the two steps are parallel.
vec3 shadowGeometricNormal(vec2 pos_screen, vec3 pos, vec3 gnorm)
{
    vec3 gn = dot(gnorm, gnorm) > 1.0e-8 ? normalize(gnorm) : vec3(0.0, 0.0, 1.0);
    vec2 px = 1.0 / screen_res;
    float tol = SHADOW_EDGE_REL * abs(pos.z) + SHADOW_EDGE_ABS;

    vec3 xr = getPosition(pos_screen + vec2(px.x, 0.0)).xyz - pos;
    vec3 xl = pos - getPosition(pos_screen - vec2(px.x, 0.0)).xyz;
    vec3 yu = getPosition(pos_screen + vec2(0.0, px.y)).xyz - pos;
    vec3 yd = pos - getPosition(pos_screen - vec2(0.0, px.y)).xyz;

    bool okx, oky;
    vec3 ex = shadowAxisStep(xr, xl, pos_screen.x + px.x < 1.0 && abs(xr.z) <= tol, pos_screen.x - px.x > 0.0 && abs(xl.z) <= tol, gn, okx);
    vec3 ey = shadowAxisStep(yu, yd, pos_screen.y + px.y < 1.0 && abs(yu.z) <= tol, pos_screen.y - px.y > 0.0 && abs(yd.z) <= tol, gn, oky);
    if (!okx || !oky)
    {
        return gn;
    }

    // unit steps: the parallel test does not depend on the pixel size
    ex *= inversesqrt(max(dot(ex, ex), 1.0e-12));
    ey *= inversesqrt(max(dot(ey, ey), 1.0e-12));
    vec3 n = cross(ex, ey);
    float len2 = dot(n, n);
    if (!(len2 > SHADOW_PARALLEL_MIN))
    {
        return gn;
    }
    n *= inversesqrt(len2);
    return dot(n, pos) > 0.0 ? -n : n; // facing the eye
}

#endif // !SHADOW_COMMON_NO_DEPTH

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

// What every method needs of one sun cascade, from its matrix, once per pixel and cascade: the uv and depth
// scales, and the constant bias and the cap of one tap's plane bias in depth units (positive; the reference
// moves towards the light by the bias).
struct ShadowCascade
{
    vec2 s_uv;
    float s_z;
    float texel_m;
    float bias;
    float plane_max;
};

ShadowCascade shadowCascadeSetup(mat4 m, vec2 res)
{
    ShadowCascade cs;
    cs.s_uv = shadowUVScale(m);
    cs.s_z = shadowDepthScale(m);
    cs.texel_m = shadowTexelMetres(m, res);
    cs.bias = shadowWorldBias(cs.texel_m, cs.s_z, SHADOW_BIAS_TEXELS);
    cs.plane_max = shadowWorldBias(cs.texel_m, cs.s_z, SHADOW_PLANE_BIAS_MAX_TEXELS);
    return cs;
}

// The receiver's geometry, once per pixel: the geometric normal and the sine of its angle to the light
// (the normal offset's scale). light_dir: view space, towards the light.
vec3 shadowReceiverGeometry(vec2 pos_screen, vec3 pos, vec3 gnorm, vec3 light_dir, out float sin_theta)
{
#if defined(SHADOW_COMMON_NO_DEPTH)
    // no depth buffer behind this program (a forward surface, or one without deferredUtil.glsl): the shading normal is the plane
    vec3 ng = dot(gnorm, gnorm) > 1.0e-8 ? normalize(gnorm) : vec3(0.0, 0.0, 1.0);
#else
    vec3 ng = shadowGeometricNormal(pos_screen, pos, gnorm);
#endif
    float dp = clamp(dot(ng, light_dir), 0.0, 1.0);
    sin_theta = sqrt(max(1.0 - dp * dp, 0.0));
    return ng;
}

#endif // SHADOW_COMMON

// </Vulkanstorm>

#if !defined(SHADOW_COMMON_ONLY)

float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen)
{
#if defined(SUN_SHADOW)
    float shadow = 0.0f;
    vec3 light_dir = normalize(primary_light_dir); // <Vulkanstorm/> L3: the primary light

    vec3 shadow_pos = pos.xyz;

    float dp_directional_light = max(0.0, dot(norm.xyz, light_dir));
          dp_directional_light = clamp(dp_directional_light, 0.0, 1.0);

    vec3 offset = light_dir.xyz * (1.0 - dp_directional_light);

    shadow_pos += offset * shadow_offset * 2.0;

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
            lpos = shadow_matrix[3]*spos;

            float w = 1.0;
            w -= max(spos.z-far_split.z, 0.0)/transition_domain.z;
            //w = clamp(w, 0.0, 1.0);
            float contrib = pcfShadow(shadowMap3, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[3], spos.xyz)*w;
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
            shadow += max((pos.z+shadow_clip.z)/(shadow_clip.z-shadow_clip.w)*2.0-1.0, 0.0);
        }

        if (spos.z < near_split.y && spos.z > far_split.z)
        {
            lpos = shadow_matrix[2]*spos;

            float w = 1.0;
            w -= max(spos.z-far_split.y, 0.0)/transition_domain.y;
            w -= max(near_split.z-spos.z, 0.0)/transition_domain.z;
            //w = clamp(w, 0.0, 1.0);
            float contrib = pcfShadow(shadowMap2, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[2], spos.xyz)*w;
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
        }

        if (spos.z < near_split.x && spos.z > far_split.y)
        {
            lpos = shadow_matrix[1]*spos;

            float w = 1.0;
            w -= max(spos.z-far_split.x, 0.0)/transition_domain.x;
            w -= max(near_split.y-spos.z, 0.0)/transition_domain.y;
            //w = clamp(w, 0.0, 1.0);
            float contrib = pcfShadow(shadowMap1, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[1], spos.xyz)*w;
            //if (contrib > 0)
            {
                shadow += contrib;
                weight += w;
            }
        }

        if (spos.z > far_split.x)
        {
            lpos = shadow_matrix[0]*spos;

            float w = 1.0;
            w -= max(near_split.x-spos.z, 0.0)/transition_domain.x;
            //w = clamp(w, 0.0, 1.0);
            float contrib = pcfShadow(shadowMap0, norm, lpos, 1.0, pos_screen, light_dir, shadow_matrix[0], spos.xyz)*w;
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
        shadow = pcfSpotShadow(shadowMap4, lpos, 0.8, spos.xy);
    }
    else
    {
        lpos = shadow_matrix[5]*spos;
        shadow = pcfSpotShadow(shadowMap5, lpos, 0.8, spos.xy);
    }
    return mix(1.0, shadow, fade);
    // </Vulkanstorm>
#else
    return 1.0;
#endif
}
#endif // !SHADOW_COMMON_ONLY
