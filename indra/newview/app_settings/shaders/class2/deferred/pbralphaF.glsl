/**
 * @file class1\deferred\pbralphaF.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#extension GL_ARB_shader_image_load_store : enable

/*[EXTRA_CODE_HERE]*/

#if defined(ALPHA_OIT)
uniform sampler2D alpha_oit_opaque_depth; // detached opaque depth
uniform int oit_mode;       // 0 normal, 1 OIT capture
#endif

#ifdef ALPHA_OIT
// ---- alpha OIT capture: per-pixel slot array (A-buffer in a 2D-array image) ----
// Each pixel owns oit_slot_count slots (one array layer each); a per-pixel atomic count in
// layer 0 of oit_meta hands out slot indices. Fragments past the last slot fold into the
// order-independent tail accumulators in layers 1..5 (fixed-point atomics) and record their
// nearest depth in layer 6; the resolve composites the tail at that depth among the sorted
// slots. Scales must match alphaOITResolveF.glsl.
// Capture-only depth rejection below; normal alpha shader depth behaviour is unchanged.
layout(binding = 0, r32ui) uniform coherent uimage2DArray oit_meta;      // 0 count, 1..3 sum rgb*a, 4 sum a, 5 sum -log2(1-a), 6 ~bits(nearest z)
layout(binding = 1, rg32ui) uniform writeonly uimage2DArray oit_slots;   // { R11F_G11F_B10F premultiplied rgb, depth24 << 8 | alpha8 }
// Node encoding: alphaOITPackF.glsl, attached to every fragment program when ALPHA_OIT is defined.
uint oit_pack_rgb(vec3 c);
uint oit_pack_depth_alpha(float z, float a);
uniform int oit_slot_count;  // slots per pixel (layers of oit_slots)
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0
void oit_append(vec4 c, float z)
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    // Match resolve LEQUAL, including equality; reject before touching the A-buffer.
    if (!(z <= texelFetch(alpha_oit_opaque_depth, p, 0).r)) discard;
    float a   = clamp(c.a, 0.0, 1.0);
    // A fully transparent fragment adds no colour and no coverage; keep it out of the slots
    // and away from the tail's nearest depth, which would otherwise reorder a visible tail.
    if (a <= 0.0) return;
    vec3  rgb = max(c.rgb, vec3(0.0));
    uint k = imageAtomicAdd(oit_meta, ivec3(p, 0), 1u);
    if (k < uint(oit_slot_count))
    {
        imageStore(oit_slots, ivec3(p, int(k)),
                   uvec4(oit_pack_rgb(rgb * a), oit_pack_depth_alpha(z, a), 0u, 0u));
        return;
    }
    // Slot overflow: coverage and the coverage-weighted colour average are order independent.
    // Per-fragment clamps keep the fixed-point sums from wrapping on HDR emissive stacks.
    vec3 w = min(rgb * a, vec3(1024.0));
    imageAtomicAdd(oit_meta, ivec3(p, 1), uint(w.r * OIT_TAIL_COLOR_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 2), uint(w.g * OIT_TAIL_COLOR_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 3), uint(w.b * OIT_TAIL_COLOR_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 4), uint(a * OIT_TAIL_ALPHA_SCALE + 0.5));
    imageAtomicAdd(oit_meta, ivec3(p, 5), uint(-log2(max(1.0 - a, 0.0009765625)) * OIT_TAIL_LOG_SCALE + 0.5));
    // Nearest tail depth: window z is a non-negative float, so its bits order like the value;
    // inverted, the maximum over a zero-cleared layer is the minimum depth.
    imageAtomicMax(oit_meta, ivec3(p, 6), ~floatBitsToUint(z));
}
#endif


#ifndef IS_HUD

uniform sampler2D diffuseMap;  //always in sRGB space
uniform sampler2D bumpMap;
uniform sampler2D emissiveMap;
uniform sampler2D specularMap; // PBR: Packed: Occlusion, Metal, Roughness

uniform float metallicFactor;
uniform float roughnessFactor;
uniform vec3 emissiveColor;

#if defined(HAS_SUN_SHADOW) || defined(HAS_SSAO)
uniform sampler2D lightMap;
#endif

uniform int sun_up_factor;
uniform vec3 sun_dir;
uniform vec3 moon_dir;
uniform int classic_mode;

out vec4 frag_color;

in vec3 vary_fragcoord;

#ifdef HAS_SUN_SHADOW
  uniform vec2 screen_res;
#endif

in vec3 vary_position;

in vec2 base_color_texcoord;
in vec2 normal_texcoord;
in vec2 metallic_roughness_texcoord;
in vec2 emissive_texcoord;

in vec4 vertex_color;

in vec3 vary_normal;
in vec3 vary_tangent;
flat in float vary_sign;


#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
#endif

// Lights
// See: LLRender::syncLightState()
uniform vec4 light_position[8];
uniform vec3 light_direction[8]; // spot direction
uniform vec4 light_attenuation[8]; // linear, quadratic, is omni, unused, See: LLPipeline::setupHWLights() and syncLightState()
uniform vec3 light_diffuse[8];
uniform vec2 light_deferred_attenuation[8]; // light size and falloff

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);
vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color);

void calcHalfVectors(vec3 lv, vec3 n, vec3 v, out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float calcLegacyDistanceAttenuation(float distance, float falloff);
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit_linear);

void mirrorClip(vec3 pos);
void waterClip(vec3 pos);

void calcDiffuseSpecular(vec3 baseColor, float metallic, inout vec3 diffuseColor, inout vec3 specularColor);

vec3 pbrBaseLight(vec3 diffuseColor,
                  vec3 specularColor,
                  float metallic,
                  vec3 pos,
                  vec3 norm,
                  float perceptualRoughness,
                  vec3 light_dir,
                  vec3 sunlit,
                  float scol,
                  vec3 radiance,
                  vec3 irradiance,
                  vec3 colorEmissive,
                  float ao,
                  vec3 additive,
                  vec3 atten);

vec3 pbrCalcPointLightOrSpotLight(vec3 diffuseColor, vec3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    vec3 n, // normal
                    vec3 p, // pixel position
                    vec3 v, // view vector (negative normalized pixel position)
                    vec3 lp, // light position
                    vec3 ld, // light direction (for spotlights)
                    vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight, float ambiance);

void main()
{
    mirrorClip(vary_position);

    vec3 color = vec3(0,0,0);

    vec3  light_dir   = (sun_up_factor == 1) ? sun_dir : moon_dir;
    vec3  pos         = vary_position;

    waterClip(pos);

    vec4 basecolor = texture(diffuseMap, base_color_texcoord.xy).rgba;
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }
#endif

    vec3 col = vertex_color.rgb * basecolor.rgb;

    vec3 vNt = texture(bumpMap, normal_texcoord.xy).xyz*2.0-1.0;
    float sign = vary_sign;
    vec3 vN = vary_normal;
    vec3 vT = vary_tangent.xyz;

    vec3 vB = sign * cross(vN, vT);
    vec3 norm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    norm *= gl_FrontFacing ? 1.0 : -1.0;

    float scol = 1.0;
    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;
    calcAtmosphericVarsLinear(pos.xyz, norm, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    vec3 sunlit_linear = sunlit;

    vec2 frag = vary_fragcoord.xy/vary_fragcoord.z*0.5+0.5;

#ifdef HAS_SUN_SHADOW
    scol = sampleDirectionalShadow(pos.xyz, norm.xyz, frag);
#endif

    vec3 orm = texture(specularMap, metallic_roughness_texcoord.xy).rgb; //orm is packed into "emissiveRect" to keep the data in linear color space

    float perceptualRoughness = orm.g * roughnessFactor;
    float metallic = orm.b * metallicFactor;
    float ao = orm.r;

    // emissiveColor is the emissive color factor from GLTF and is already in linear space
    vec3 colorEmissive = emissiveColor;
    // emissiveMap here is a vanilla RGB texture encoded as sRGB, manually convert to linear
    colorEmissive *= srgb_to_linear(texture(emissiveMap, emissive_texcoord.xy).rgb);

    // PBR IBL
    float gloss      = 1.0 - perceptualRoughness;
    vec3  irradiance = amblit;
    vec3  radiance  = vec3(0);
    sampleReflectionProbes(irradiance, radiance, vary_position.xy*0.5+0.5, pos.xyz, norm.xyz, gloss, true, amblit);

    vec3 diffuseColor;
    vec3 specularColor;
    calcDiffuseSpecular(col.rgb, metallic, diffuseColor, specularColor);

    vec3 v = -normalize(pos.xyz);

    color = pbrBaseLight(diffuseColor, specularColor, metallic, v, norm.xyz, perceptualRoughness, light_dir, sunlit_linear, scol, radiance, irradiance, colorEmissive, ao, additive, atten);

    vec3 light = vec3(0);

    // Punctual lights
#define LIGHT_LOOP(i) light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm.xyz, pos.xyz, v, light_position[i].xyz, light_direction[i].xyz, light_diffuse[i].rgb, light_deferred_attenuation[i].x, light_deferred_attenuation[i].y, light_attenuation[i].z, light_attenuation[i].w);

    LIGHT_LOOP(1)
    LIGHT_LOOP(2)
    LIGHT_LOOP(3)
    LIGHT_LOOP(4)
    LIGHT_LOOP(5)
    LIGHT_LOOP(6)
    LIGHT_LOOP(7)

    color.rgb += light.rgb;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, vec4(color, 1.0)).rgb;

    float a = basecolor.a*vertex_color.a;
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
#ifdef ALPHA_OIT
    vec4 oit_out = max(vec4(color.rgb * final_scale,a), vec4(0));
    if (oit_mode == 1) { oit_append(oit_out, gl_FragCoord.z); discard; }
    frag_color = oit_out;
#else
    vec4 oit_out = max(vec4(color.rgb * final_scale,a), vec4(0));
    frag_color = oit_out;
#endif
}

#else

uniform sampler2D diffuseMap;  //always in sRGB space
uniform sampler2D emissiveMap;

uniform vec3 emissiveColor;

out vec4 frag_color;

in vec3 vary_position;

in vec2 base_color_texcoord;
in vec2 emissive_texcoord;

in vec4 vertex_color;

#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
#endif

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);


void main()
{
    vec3 color = vec3(0,0,0);

    vec3  pos         = vary_position;

    vec4 basecolor = texture(diffuseMap, base_color_texcoord.xy).rgba;
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }
#endif

    color = vertex_color.rgb * basecolor.rgb;

    // emissiveColor is the emissive color factor from GLTF and is already in linear space
    vec3 colorEmissive = emissiveColor;
    // emissiveMap here is a vanilla RGB texture encoded as sRGB, manually convert to linear
    colorEmissive *= srgb_to_linear(texture(emissiveMap, emissive_texcoord.xy).rgb);


    float a = basecolor.a*vertex_color.a;
    color += colorEmissive;

    color = linear_to_srgb(color);
    frag_color = max(vec4(color.rgb,a), vec4(0));
}

#endif
