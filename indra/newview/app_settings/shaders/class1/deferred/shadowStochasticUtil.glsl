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
 * It shares nothing with the PCSS code: no blocker search, filter, gather tap or PCSS define.
 *
 * Stage M0: the entry points report the surface lit; the estimator follows (stage M1).
 */

// angular size of the shadowing light, L = 2 tan(half angle): the physical sun times the sky's
// sun scale times RenderShadowPCSSLightSize (the moon uses the same size)
uniform float shadow_light_size;

float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen)
{
    return 1.0;
}

float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen)
{
    return 1.0;
}
