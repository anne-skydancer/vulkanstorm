/**
 * @file terrainBake.slang (converted from pbrTerrainBakeV.glsl and pbrTerrainBakeF.glsl)
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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
/**
 * @file class1\deferred\pbrterrainUtilF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

uniform mat4 modelview_projection_matrix;
uniform sampler2D alpha_ramp;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 ramp1 : RAMP1;
    [[vk::location(1)]] vec2 ramp2 : RAMP2;
    [[vk::location(2)]] vec2 rampFinal : RAMP_FINAL;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
    o.ramp1 = v.texcoord1;
    o.ramp2 = v.texcoord1 - vec2(2.0, 0.0);
    o.rampFinal = v.texcoord1 - vec2(1.0, 0.0);
    return o;
}

// Preserve the threshold and normalization of pbrterrainUtilF.glsl when baking ramp weights.
vec4 terrainRampWeights(float alpha1, float alpha2, float alphaFinal)
{
    const float TERRAIN_RAMP_MIX_THRESHOLD = 0.1;
    vec4 sample_x = vec4(1, 0, 0, 0);
    vec4 sample_y = vec4(0, 1, 0, 0);
    vec4 sample_z = vec4(0, 0, 1, 0);
    vec4 sample_w = vec4(0, 0, 0, 1);

    vec4 weight = mix(mix(sample_w, sample_z, alpha2), mix(sample_y, sample_x, alpha1), alphaFinal);
    weight -= TERRAIN_RAMP_MIX_THRESHOLD;
    ivec4 usage = max(ivec4(0), ivec4(ceil(weight)));
    weight *= vec4(usage);
    return weight / (weight.x + weight.y + weight.z + weight.w);
}

int terrainPaintPrecision()
{
    __target_switch
    {
    case glsl: __intrinsic_asm "(TERRAIN_PAINT_PRECISION)";
    case spirv: return 31; // The viewer's default five-bit paint precision.
    }
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    float alpha1 = texture(alpha_ramp, i.ramp1).a;
    float alpha2 = texture(alpha_ramp, i.ramp2).a;
    float alphaFinal = texture(alpha_ramp, i.rampFinal).a;
    vec4 weight = terrainRampWeights(alpha1, alpha2, alphaFinal);

    // The first swatch uses the remainder of the weight stored in RGB.
    float precision = max(float(terrainPaintPrecision()), 1.0);
    return max(vec4(round(weight.yzw * precision) / precision, 1.0), vec4(0.0));
}
