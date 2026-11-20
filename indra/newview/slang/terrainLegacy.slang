// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file terrainLegacy.slang (converted from class1/deferred/terrainV.glsl and terrainF.glsl)
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, Inc.,
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
 * $/LicenseInfo$
 */
import glsl;
import position;
import gbuffer;
import mirrorclip;

uniform mat4 modelview_projection_matrix;
uniform mat4 modelview_matrix;
uniform mat3 normal_matrix;
uniform mat4 texture_matrix0;
uniform vec4 object_plane_s;
uniform vec4 object_plane_t;

// The terrain pool restores texture unit 0 through the detail_0 binding.
uniform sampler2D detail_0;
uniform sampler2D detail_1;
uniform sampler2D detail_2;
uniform sampler2D detail_3;
uniform sampler2D alpha_ramp;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
};

struct Varyings
{
    vec4 clip : SV_Position;
    [[vk::location(0)]] vec3 pos : VARY_POSITION;
    [[vk::location(1)]] vec3 vary_normal : VARY_NORMAL;
    [[vk::location(2)]] vec4 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(3)]] vec4 vary_texcoord1 : VARY_TEXCOORD1;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    vec4 pre_pos = vec4(v.position, 1.0);
    vec4 tcoord = vec4(dot(pre_pos, object_plane_s), dot(pre_pos, object_plane_t), 0.0, 1.0);
    tcoord = texture_matrix0 * tcoord;

    Varyings o;
    o.clip = vkPositionClip(modelview_projection_matrix, v.position);
    o.pos = (modelview_matrix * pre_pos).xyz;
    o.vary_normal = normalize(normal_matrix * v.normal);
    o.vary_texcoord0 = vec4(tcoord.xy, v.texcoord1);
    o.vary_texcoord1 = vec4(v.texcoord1 - vec2(2.0, 0.0), v.texcoord1 - vec2(1.0, 0.0));
    return o;
}

struct GBufferOut
{
    vec4 data0 : SV_Target0;
    vec4 data1 : SV_Target1;
    vec4 data2 : SV_Target2;
#if HAS_EMISSIVE
    vec4 data3 : SV_Target3;
#endif
};

[shader("fragment")]
GBufferOut fsMain(Varyings i)
{
    mirrorClip(i.pos);

    vec4 color0 = texture(detail_0, i.vary_texcoord0.xy);
    vec4 color1 = texture(detail_1, i.vary_texcoord0.xy);
    vec4 color2 = texture(detail_2, i.vary_texcoord0.xy);
    vec4 color3 = texture(detail_3, i.vary_texcoord0.xy);

    float alpha1 = texture(alpha_ramp, i.vary_texcoord0.zw).a;
    float alpha2 = texture(alpha_ramp, i.vary_texcoord1.xy).a;
    float alphaFinal = texture(alpha_ramp, i.vary_texcoord1.zw).a;
    vec4 outColor = mix(mix(color3, color2, alpha2), mix(color1, color0, alpha1), alphaFinal);
    outColor.a = 0.0;

    GBufferOut o;
    o.data0 = max(outColor, vec4(0));
    o.data1 = vec4(0.0, 0.0, 0.0, -1.0);
    o.data2 = encodeNormal(normalize(i.vary_normal), 0, GBUFFER_FLAG_HAS_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0);
#endif
    return o;
}
