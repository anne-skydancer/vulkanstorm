/**
 * @file preview.slang (converted from objects/previewV.glsl, objects/previewF.glsl)
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
import glsl;
import position;

// HAS_SKIN 1 is the rigged variant (the object skin palette and weight4)
uniform mat4 texture_matrix0;
uniform vec4 ambient_color;
uniform vec4 color;
uniform vec4 light_position[8];
uniform vec3 light_diffuse[8];
#if HAS_SKIN
uniform mat4 modelview_matrix;
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat3 normal_matrix;
uniform mat4 modelview_projection_matrix;
#endif
uniform sampler2D diffuseMap;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec4 vertex_color : VERTEX_COLOR;
    [[vk::location(1)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
};

float calcDirectionalLight(vec3 n, vec3 l)
{
    float a = max(dot(n, l), 0.0);
    return a;
}

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    vec3 norm;
#if HAS_SKIN
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 eye;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position.xyz, eye);
    norm = normalize((mat * vec4(v.normal.xyz + v.position.xyz, 1.0)).xyz - eye);
#else
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
    norm = normalize(normal_matrix * v.normal);
#endif
    o.vary_texcoord0 = (texture_matrix0 * vec4(v.texcoord0, 0, 1)).xy;

    // <FS:Beq/> add ambient color to preview shader
    vec4 col = ambient_color;

    // Collect normal lights (need to be divided by two, as we later multiply by 2)
    col.rgb += light_diffuse[1].rgb * calcDirectionalLight(norm, light_position[1].xyz);
    col.rgb += light_diffuse[2].rgb * calcDirectionalLight(norm, light_position[2].xyz);
    col.rgb += light_diffuse[3].rgb * calcDirectionalLight(norm, light_position[3].xyz);

    o.vertex_color = col * color;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 color = texture(diffuseMap, i.vary_texcoord0.xy) * i.vertex_color;
    return max(color, vec4(0));
}
