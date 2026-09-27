/**
 * @file alphaF.glsl
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

//class2/deferred/alphaF.glsl

#extension GL_ARB_shader_storage_buffer_object : enable
#extension GL_ARB_shader_image_load_store : enable
#extension GL_ARB_shader_atomic_counters : enable

/*[EXTRA_CODE_HERE]*/

#define INDEXED 1
#define NON_INDEXED 2
#define NON_INDEXED_NO_COLOR 3

out vec4 frag_color;

#ifdef GPU_PARTICLE_RENDER
layout(pixel_interlock_ordered) in;
// Color and virtual depth are detached from the drawing framebuffer. Writable
// depth must be resolved back before the next non-particle alpha draw.
layout(binding=0,rgba16f) uniform coherent image2D particle_color;
layout(binding=1,r32f) uniform coherent image2D particle_depth;
uniform int particle_depth_mode; // 0 disabled, 1 LEQUAL read, 2 LEQUAL write
flat in uint particle_glow_pass;
uniform int particle_impostor;
uniform int particle_depth_only;
uniform sampler2D alpha_peel_depth; // scene depth, detached from our drawing FBO
in float particle_glow;
// Material values are constant for each indirect draw. Loading them in the
// single vertex module avoids native AMD's bindless SSBO failure when a fragment
// program links multiple shader objects, and removes per-fragment buffer loads.
flat in sampler2D particle_texture;
flat in uint particle_blend, particle_flags;
vec3 particleBlendFactor(uint code, vec4 source, vec4 destination)
{
    if(code==0u) return vec3(1);
    if(code==1u) return vec3(0);
    if(code==2u) return destination.rgb;
    if(code==3u) return source.rgb;
    if(code==4u) return vec3(1)-destination.rgb;
    if(code==5u) return vec3(1)-source.rgb;
    if(code==7u) return vec3(source.a);
    return vec3(1-source.a);
}
void calcAtmosphericVars(vec3 pos, vec3 light_dir, float ambFactor,
    out vec3 sunlit, out vec3 amblit, out vec3 additive, out vec3 atten);
#endif


#if defined(ALPHA_OIT) || defined(ALPHA_DEPTH_PEEL)
uniform sampler2D alpha_peel_depth; // detached PPLL opaque depth or selected peel depth
uniform int oit_mode;       // 0 normal, 1 PPLL capture, 2 peel select, 3 peel replay, 4 legacy tail
#endif

#ifdef ALPHA_OIT
// ---- alpha OIT (per-pixel linked list) capture ----
    // Capture-only depth rejection below; normal alpha shader depth behaviour is unchanged.
layout(binding = 0, r32ui) uniform coherent uimage2D oit_head;
layout(std430, binding = 0) buffer OITNodePool { uint oit_nodes[]; };
layout(binding = 0, offset = 0) uniform atomic_uint oit_counter;
uniform int oit_node_cap;   // node pool capacity; overflow falls through to legacy blending
bool oit_append(vec4 c, float z)
{
    // Match resolve LEQUAL, including equality; reject before allocator/SSBO work.
    // discard also prevents hidden fragments from taking the overflow blend path.
    if (!(z <= texelFetch(alpha_peel_depth, ivec2(gl_FragCoord.xy), 0).r)) discard;
    uint idx = atomicCounterIncrement(oit_counter);
    if (idx >= uint(oit_node_cap)) return false;
    uint prev = imageAtomicExchange(oit_head, ivec2(gl_FragCoord.xy), idx);
    uint base = idx * 4u;
    oit_nodes[base + 0u] = packHalf2x16(max(c.rg, vec2(0.0)));
    oit_nodes[base + 1u] = packHalf2x16(vec2(max(c.b, 0.0), clamp(c.a, 0.0, 1.0)));
    oit_nodes[base + 2u] = floatBitsToUint(z);
    oit_nodes[base + 3u] = prev;
    return true;
}
#endif

#ifdef ALPHA_DEPTH_PEEL
uniform int alpha_peel_first;
void alpha_depth_peel(inout vec4 c, float z)
{
    if (oit_mode == 2)
    {
        if (alpha_peel_first == 0)
        {
            float selected = texelFetch(alpha_peel_depth, ivec2(gl_FragCoord.xy), 0).r;
            if (z >= selected) discard;
        }
        c = vec4(z);
    }
    else if (oit_mode == 3)
    {
        float selected = texelFetch(alpha_peel_depth, ivec2(gl_FragCoord.xy), 0).r;
        if (z != selected) discard;
    }
    else if (oit_mode == 4)
    {
        float selected = texelFetch(alpha_peel_depth, ivec2(gl_FragCoord.xy), 0).r;
        if (z >= selected) discard;
    }
}
#endif

uniform mat3 env_mat;
uniform vec3 sun_dir;
uniform vec3 moon_dir;
uniform int classic_mode;

#ifdef USE_DIFFUSE_TEX
uniform sampler2D diffuseMap;
#endif

in vec3 vary_fragcoord;
in vec3 vary_position;
in vec2 vary_texcoord0;
in vec3 vary_norm;

#ifdef USE_VERTEX_COLOR
in vec4 vertex_color; //vertex color should be treated as sRGB
#endif

uniform float minimum_alpha;

uniform mat4 proj_mat;
uniform mat4 inv_proj;
uniform vec2 screen_res;
uniform int sun_up_factor;
uniform vec4 light_position[8];
uniform vec3 light_direction[8];
uniform vec4 light_attenuation[8];
uniform vec3 light_diffuse[8];

void waterClip(vec3 pos);

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color);
void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
#endif

float getAmbientClamp();

void mirrorClip(vec3 pos);

void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit_linear);

vec3 calcPointLightOrSpotLight(vec3 light_col, vec3 diffuse, vec3 v, vec3 n, vec4 lp, vec3 ln, float la, float fa, float is_pointlight, float ambiance)
{
    // SL-14895 inverted attenuation work-around
    // This routine is tweaked to match deferred lighting, but previously used an inverted la value. To reconstruct
    // that previous value now that the inversion is corrected, we reverse the calculations in LLPipeline::setupHWLights()
    // to recover the `adjusted_radius` value previously being sent as la.
    float falloff_factor = (12.0 * fa) - 9.0;
    float inverted_la = falloff_factor / la;
    // Yes, it makes me want to cry as well. DJH

    vec3 col = vec3(0);

    //get light vector
    vec3 lv = lp.xyz-v;

    //get distance
    float dist = length(lv);
    float da = 1.0;

    /*if (dist > inverted_la)
    {
        return col;
    }

    clip to projector bounds
     vec4 proj_tc = proj_mat * lp;

    if (proj_tc.z < 0
     || proj_tc.z > 1
     || proj_tc.x < 0
     || proj_tc.x > 1
     || proj_tc.y < 0
     || proj_tc.y > 1)
    {
        return col;
    }*/

    if (dist > 0.0 && inverted_la > 0.0)
    {
        dist /= inverted_la;

        //normalize light vector
        lv = normalize(lv);

        //distance attenuation
        float dist_atten = clamp(1.0-(dist-1.0*(1.0-fa))/fa, 0.0, 1.0);
        dist_atten *= dist_atten;
        dist_atten *= 2.0f;

        if (dist_atten <= 0.0)
        {
           return col;
        }

        // spotlight coefficient.
        float spot = max(dot(-ln, lv), is_pointlight);
        da *= spot*spot; // GL_SPOT_EXPONENT=2

        //angular attenuation
        da *= dot(n, lv);
        da = max(0.0, da);

        float lit = 0.0f;

        float amb_da = 0.0;//ambiance;
        if (da > 0)
        {
            lit = clamp(da * dist_atten, 0.0, 1.0);
            col = lit * light_col * diffuse;
            amb_da += (da*0.5+0.5) * ambiance;
        }
        amb_da += (da*da*0.5 + 0.5) * ambiance;
        amb_da *= dist_atten;
        amb_da = min(amb_da, 1.0f - lit);

        // SL-10969 ... need to work out why this blows out in many setups...
        //col.rgb += amb_da * light_col * diffuse;

        // no spec for alpha shader...
    }
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;
    col = max(col * final_scale, vec3(0));
    return col;
}

#ifdef GPU_PARTICLE_RENDER
vec4 shadeParticle()
#else
void main()
#endif
{
    float alpha_cutoff=minimum_alpha;
#ifdef GPU_PARTICLE_RENDER
    // Match legacy custom-blend admission: invisible source-alpha fragments can
    // still change destination color with these factors (except impostors).
    if (particle_impostor==0 && (particle_blend&255u)!=7u && ((particle_blend>>8u)&255u)!=7u)
        alpha_cutoff=0.;
#endif
    mirrorClip(vary_position);

    vec2 frag = vary_fragcoord.xy/vary_fragcoord.z*0.5+0.5;

    vec4 pos = vec4(vary_position, 1.0);
#ifndef IS_AVATAR_SKIN
    // clip against water plane unless this is a legacy avatar skin
    waterClip(pos.xyz);
#endif
    vec3 norm = vary_norm;

    float shadow = 1.0f;

#ifdef HAS_SUN_SHADOW
#ifdef GPU_PARTICLE_RENDER
    if ((particle_flags & 1u)==0u)
#endif
    shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, frag);
#endif

#ifdef USE_DIFFUSE_TEX
    vec4 diffuse_tap = texture(diffuseMap,vary_texcoord0.xy);
#endif

#ifdef USE_INDEXED_TEX
    vec4 diffuse_tap = diffuseLookup(vary_texcoord0.xy);
#endif

#ifdef GPU_PARTICLE_RENDER
    vec4 diffuse_tap = texture(particle_texture,vary_texcoord0.xy);
    if ((particle_flags & 1u)!=0u)
    {
        // Match the existing fullbright-alpha shader, including its texture-alpha
        // cutoff and HUD color space. The lit path below remains shared.
        if (diffuse_tap.a < alpha_cutoff) discard;
        vec4 fullbright = diffuse_tap * vertex_color;
#ifndef IS_HUD
        fullbright.rgb = srgb_to_linear(fullbright.rgb);
        vec3 sunlit, amblit, additive, atten;
        calcAtmosphericVars(pos.xyz,vec3(0),1.0,sunlit,amblit,additive,atten);
        fullbright.rgb = applySkyAndWaterFog(pos.xyz,additive,atten,fullbright).rgb;
#endif
        return max(fullbright,vec4(0));
    }
#endif
    vec4 diffuse_srgb = diffuse_tap;

#ifdef FOR_IMPOSTOR
    vec4 color;
    color.rgb = diffuse_srgb.rgb;
    color.a = 1.0;

    float final_alpha = diffuse_srgb.a * vertex_color.a;
    diffuse_srgb.rgb *= vertex_color.rgb;

    // Insure we don't pollute depth with invis pixels in impostor rendering
    //
    if (final_alpha < alpha_cutoff)
    {
        discard;
    }

    color.rgb = diffuse_srgb.rgb;
    color.a = final_alpha;

#else // FOR_IMPOSTOR

    vec4 diffuse_linear = vec4(srgb_to_linear(diffuse_srgb.rgb), diffuse_srgb.a);

    vec3 light_dir = (sun_up_factor == 1) ? sun_dir: moon_dir; // TODO -- factor out "sun_up_factor" and just send in the appropriate light vector

    float final_alpha = diffuse_linear.a;

#ifdef IS_AVATAR_SKIN
    if(final_alpha < alpha_cutoff)
    {
        discard;
    }
#endif

#ifdef USE_VERTEX_COLOR
    final_alpha *= vertex_color.a;

    if (final_alpha < alpha_cutoff)
    { // TODO: figure out how to get invisible faces out of
        // render batches without breaking glow
        discard;
    }

    diffuse_srgb.rgb *= vertex_color.rgb;
    diffuse_linear.rgb = srgb_to_linear(diffuse_srgb.rgb);
#endif // USE_VERTEX_COLOR

    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;

    calcAtmosphericVarsLinear(pos.xyz, norm, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    vec3 sunlit_linear = sunlit;
    vec3 amblit_linear = amblit;

    vec3 irradiance = amblit;
    vec3 glossenv;
    vec3 legacyenv;
    sampleReflectionProbesLegacy(irradiance, glossenv, legacyenv, frag, pos.xyz, norm.xyz, 0.0, 0.0, true, amblit_linear);


    float da = dot(norm.xyz, light_dir.xyz);
          da = clamp(da, -1.0, 1.0);

    float final_da = da;
          final_da = clamp(final_da, 0.0f, 1.0f);

    vec4 color = vec4(0.0);

    color.a   = final_alpha;

    color.rgb = irradiance;
    if (classic_mode > 0)
    {
        final_da = pow(final_da,1.2);
        vec3 sun_contrib = vec3(min(final_da, shadow));

        color.rgb = srgb_to_linear(color.rgb * 0.9 + linear_to_srgb(sun_contrib) * sunlit_linear * 0.7);
        sunlit_linear = srgb_to_linear(sunlit_linear);
    }
    else
    {
        vec3 sun_contrib = min(final_da, shadow) * sunlit_linear;
        color.rgb += sun_contrib;
    }

    color.rgb *= diffuse_linear.rgb;

    vec4 light = vec4(0,0,0,0);

   #define LIGHT_LOOP(i) light.rgb += calcPointLightOrSpotLight(light_diffuse[i].rgb, diffuse_linear.rgb, pos.xyz, norm, light_position[i], light_direction[i].xyz, light_attenuation[i].x, light_attenuation[i].y, light_attenuation[i].z, light_attenuation[i].w);

    LIGHT_LOOP(1)
    LIGHT_LOOP(2)
    LIGHT_LOOP(3)
    LIGHT_LOOP(4)
    LIGHT_LOOP(5)
    LIGHT_LOOP(6)
    LIGHT_LOOP(7)

    // sum local light contrib in linear colorspace
    color.rgb += light.rgb;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, color).rgb;

#endif // #else // FOR_IMPOSTOR
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
#ifdef IS_HUD
    color.rgb = linear_to_srgb(color.rgb);
    final_scale = 1;
#endif

    color.rgb *= final_scale;
#ifdef GPU_PARTICLE_RENDER
    return max(color, vec4(0));
#else
#ifdef ALPHA_OIT
    vec4 oit_out = max(color, vec4(0));
    if (oit_mode == 1 && oit_append(oit_out, gl_FragCoord.z)) { discard; }
#ifdef ALPHA_DEPTH_PEEL
    alpha_depth_peel(oit_out, gl_FragCoord.z);
#endif
    frag_color = oit_out;
#else
    vec4 oit_out = max(color, vec4(0));
#ifdef ALPHA_DEPTH_PEEL
    alpha_depth_peel(oit_out, gl_FragCoord.z);
#endif
    frag_color = oit_out;
#endif
#endif // GPU_PARTICLE_RENDER
}


#ifdef GPU_PARTICLE_RENDER
void main()
{
    vec4 source;
    if (particle_glow_pass!=0)
    {
        mirrorClip(vary_position);
        waterClip(vary_position);
        source=vec4(0,0,0,texture(particle_texture,vary_texcoord0).a*particle_glow);
    }
    else source=shadeParticle();
    beginInvocationInterlockARB();
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    if (particle_depth_mode==0 || gl_FragCoord.z<=min(imageLoad(particle_depth,pixel).r,texelFetch(alpha_peel_depth,pixel,0).r))
    {
        vec4 destination=imageLoad(particle_color,pixel);
        vec4 result;
        if (particle_glow_pass!=0)
            result=vec4(destination.rgb,destination.a+source.a);
        else
        {
            uint src=particle_blend & 255u, dst=(particle_blend>>8u) & 255u;
            result=vec4(source.rgb*particleBlendFactor(src,source,destination)+
                        destination.rgb*particleBlendFactor(dst,source,destination),
                        destination.a*(1.0-source.a));
        }
        if (particle_depth_only==0) imageStore(particle_color,pixel,result);
        if (particle_depth_mode==2 && particle_glow_pass==0)
            imageStore(particle_depth,pixel,vec4(gl_FragCoord.z));
    }
    endInvocationInterlockARB();
}
#endif
