// SPDX-License-Identifier: LGPL-2.1-only
// Converted from deferred/rlvV.glsl and deferred/rlvF.glsl.
/**
 *
 * Copyright (c) 2018-2020, Kitty Barnett
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to
 * abide by those obligations.
 *
 */
/**
 *
 * Copyright (c) 2018, Kitty Barnett
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to
 * abide by those obligations.
 *
 */
import glsl;
import postfullscreen;

uniform sampler2D diffuseRect;
uniform sampler2D depthMap;
uniform mat4 inv_proj;
uniform vec2 screen_res;

uniform int rlvEffectMode;
uniform vec4 rlvEffectParam1; // Sphere origin in view space.
uniform vec4 rlvEffectParam2; // Minimum value, minimum distance, maximum value, maximum distance.
uniform vec2 rlvEffectParam3; // The viewer uploads the distance-extension flags as floats.
uniform vec4 rlvEffectParam4; // Mode-specific parameters.
uniform vec2 rlvEffectParam5; // Blur direction.

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

vec3 blur13(sampler2D source, vec2 tc, vec2 direction)
{
  vec4 color = vec4(0.0);
  vec2 off1 = vec2(1.411764705882353) * direction;
  vec2 off2 = vec2(3.2941176470588234) * direction;
  vec2 off3 = vec2(5.176470588235294) * direction;

  color += texture(source, tc) * 0.1964825501511404;

  color += texture(source, tc + off1 / screen_res) * 0.2969069646728344;
  color += texture(source, tc - off1 / screen_res) * 0.2969069646728344;

  color += texture(source, tc + off2 / screen_res) * 0.09447039785044732;
  color += texture(source, tc - off2 / screen_res) * 0.09447039785044732;

  color += texture(source, tc + off3 / screen_res) * 0.010381362401148057;
  color += texture(source, tc - off3 / screen_res) * 0.010381362401148057;

  return color.xyz;
}

static const float pi = 3.14159265;

// http://callumhay.blogspot.com/2010/09/gaussian-blur-shader-glsl.html
vec3 blurVariable(sampler2D source, vec2 tc, float kernelSize, vec2 direction, float strength) {
  // Vulkanstorm: the kernel size comes from a script (@setsphere_param). A float loop
  // counter never terminates above 2^24 or for inf, and NaN is not safe to clamp, so
  // use an explicit NaN/non-positive guard, a cap of 64 samples per side (kernel 128)
  // and an integer counter.
  int numBlurPixelsPerSide = !(kernelSize > 0.0) ? 0 : int(min(kernelSize * 0.5, 64.0));

  // Incremental Gaussian Coefficent Calculation (See GPU Gems 3 pp. 877 - 889)
  vec3 incrementalGaussian;
  incrementalGaussian.x = 1.0 / (sqrt(2.0 * pi) * strength);
  incrementalGaussian.y = exp(-0.5 / (strength * strength));
  incrementalGaussian.z = incrementalGaussian.y * incrementalGaussian.y;

  vec4 avgValue = vec4(0.0, 0.0, 0.0, 0.0);
  float coefficientSum = 0.0;

  // Take the central sample first...
  avgValue += texture(source, tc) * incrementalGaussian.x;
  coefficientSum += incrementalGaussian.x;
  incrementalGaussian.xy *= incrementalGaussian.yz;

  // Go through the remaining 8 vertical samples (4 on each side of the center)
  for (int n = 1; n <= numBlurPixelsPerSide; n++) {
    float i = float(n);
    avgValue += texture(source, tc - i * direction / screen_res) * incrementalGaussian.x;
    avgValue += texture(source, tc + i * direction / screen_res) * incrementalGaussian.x;
    coefficientSum += 2.0 * incrementalGaussian.x;
    incrementalGaussian.xy *= incrementalGaussian.yz;
  }

  return (avgValue / coefficientSum).rgb;
}

vec3 chromaticAberration(sampler2D source, vec2 tc, vec2 redDrift, vec2 blueDrift, float strength)
{
    vec3 sourceColor = texture(source, tc).rgb;

    // Sample the color components
    vec3 driftColor;
    driftColor.r = texture(source, tc + redDrift / screen_res).r;
    driftColor.g = sourceColor.g;
    driftColor.b = texture(source, tc + blueDrift / screen_res).b;

    // Adjust the strength of the effect
    return mix(sourceColor, driftColor, strength);
}


vec3 sphereColor(vec2 tc, float strength)
{
    switch (rlvEffectMode)
    {
    case 0: // Blend
        return mix(texture(diffuseRect, tc).rgb, rlvEffectParam4.rgb, strength);
    case 1: // Fixed blur
        return blur13(diffuseRect, tc, rlvEffectParam5 * vec2(strength));
    case 2: // Variable blur
        return mix(texture(diffuseRect, tc).rgb,
                   blurVariable(diffuseRect, tc, rlvEffectParam4.x, rlvEffectParam5, strength),
                   bvec3(strength > 0.0));
    case 3: // Chromatic aberration
        return chromaticAberration(diffuseRect, tc, rlvEffectParam4.xy, rlvEffectParam4.zw, strength);
    case 4: // Pixelate
        {
            vec2 pixel_size = max(vec2(1.0), round(rlvEffectParam4.xy * sign(strength))) / screen_res;
            tc = pixel_size * floor(tc / pixel_size);
            return texture(diffuseRect, tc).rgb;
        }
    default:
        return vec3(0.0);
    }
}

[shader("fragment")]
vec4 fsMain(PostVaryings i) : SV_Target0
{
    float depth = texture(depthMap, i.tc).r;
    vec2 screen = i.tc * 2.0;
    vec3 ndc = vec3(screen - vec2(1.0), 2.0 * depth - 1.0);
    vec4 position = inv_proj * vec4(ndc, 1.0);
    float fragment_distance = length(position.xyz / position.w - rlvEffectParam1.xyz);

    float min_value = rlvEffectParam2.x;
    float min_distance = rlvEffectParam2.y;
    float max_value = rlvEffectParam2.z;
    float max_distance = rlvEffectParam2.w;
    float strength = min_value + mix(0.0, max_value - min_value,
                                      (fragment_distance - min_distance) / (max_distance - min_distance));
    strength = mix(strength, mix(0.0, min_value, rlvEffectParam3.x != 0.0), fragment_distance < min_distance);
    strength = mix(strength, mix(0.0, max_value, rlvEffectParam3.y != 0.0), fragment_distance > max_distance);
    return vec4(sphereColor(i.tc, strength), 0.0);
}
