/**
 * @file alphaStochasticExpandF.glsl
 * @brief Stochastic transparency: the opaque depth spread over the pass's samples
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of either the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the License or
 * (at your option) any later version, or the GNU General Public License as
 * published by the Free Software Foundation, version 3 of the License or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE,
 * LICENSE-LGPL-2.1, LICENSE-GPL-3.0 and NOTICE.md in the source tree.
 */

/*[EXTRA_CODE_HERE]*/

// The single-sampled opaque depth given to every sample of the stochastic passes' depth
// (the author's Aurora Viewer, oit_stochastic_expand.wgsl fs_expand_ss): a full-screen
// triangle, depth test ALWAYS with writes, no colour. Run before the total alpha and before
// each iteration of the stochastic depth pass.

uniform sampler2D depthMap; // the scene depth (DEFERRED_DEPTH)

void main()
{
    gl_FragDepth = texelFetch(depthMap, ivec2(gl_FragCoord.xy), 0).r;
}
