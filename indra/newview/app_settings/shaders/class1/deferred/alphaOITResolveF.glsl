/**
 * @file alphaOITResolveF.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

// Alpha OIT resolve: read each pixel's slot array (filled by the capture pass), depth-sort
// it, and composite it "over" the current screen back-to-front, with the pixel's
// order-independent tail (fragments that arrived after the last slot) inserted at the depth
// of its nearest fragment: alpha groups are submitted far to near, so the tail is usually
// the nearest block, but rigged alpha is captured first whatever its depth. Output is
// premultiplied color + coverage; the composite blend is ONE, ONE_MINUS_SRC_ALPHA with the
// alpha channel masked off so emissive glow already in screen.a survives.

out vec4 frag_color;

layout(binding = 0, r32ui) uniform readonly uimage2DArray oit_meta;     // 0 count, 1..3 sum rgb*a, 4 sum a, 5 sum -log2(1-a), 6 ~bits(nearest z)
layout(binding = 2, r32ui) uniform writeonly uimage2DArray oit_meta_clear; // the same image, written only to zero a consumed tail
layout(binding = 1, rg32ui) uniform readonly uimage2DArray oit_slots;   // { R11F_G11F_B10F premultiplied rgb, depth24 << 8 | alpha8 }
uniform sampler2D depthMap;   // opaque scene depth (DEFERRED_DEPTH) -- occlusion reject vs captured slots
uniform int oit_slot_count;   // slots per pixel (layers of oit_slots)

// Must match the capture shaders' tail encoding.
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0

// Upper bound of the slot array (RenderAlphaOITNodesPerPixel is clamped to it on the CPU).
#define OIT_MAX_SLOTS 32

// Node encoding (alphaOITPackF.glsl, shared with the capture shaders).
vec3 oit_unpack_rgb(uint p);
uint oit_depth24(float z);

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);

    uint count = imageLoad(oit_meta, ivec3(coord, 0)).r;
    if (count == 0u)
    {
        discard;   // no transparent fragments at this pixel
    }

    // Detached opaque-depth snapshot shared with capture. Retain the final LEQUAL guard.
    float opaque_z = texelFetch(depthMap, coord, 0).r;

    int slots = min(oit_slot_count, OIT_MAX_SLOTS);
    int n = int(min(count, uint(slots)));

    // The capture compared full-precision depths against the same snapshot; the quantised
    // test is monotonic, so it keeps everything the capture kept.
    uint opaque_q = oit_depth24(opaque_z);

    uint ncol[OIT_MAX_SLOTS];   // packed premultiplied colour
    uint nkey[OIT_MAX_SLOTS];   // depth24 << 8 | alpha8: orders by depth
    int cnt = 0;
    for (int i = 0; i < n; i++)
    {
        uvec2 s = imageLoad(oit_slots, ivec3(coord, i)).xy;
        if ((s.y >> 8) <= opaque_q)   // visible (in front of opaque) -> keep; else occluded -> skip
        {
            ncol[cnt] = s.x;
            nkey[cnt] = s.y;
            cnt++;
        }
    }

    // insertion sort DESCENDING by window depth (the key's high 24 bits). Standard depth
    // (near=0, far=1): largest z is farthest, so descending order = back-to-front for the
    // "over" composite below. Equal depths order by alpha, a deterministic tie break.
    for (int i = 1; i < cnt; i++)
    {
        uint c = ncol[i];
        uint k = nkey[i];
        int j = i;
        while (j > 0 && nkey[j - 1] < k)
        {
            ncol[j] = ncol[j - 1];
            nkey[j] = nkey[j - 1];
            j--;
        }
        ncol[j] = c;
        nkey[j] = k;
    }

    // Deep stacks degrade continuously instead of dropping layers: fragments past the last
    // slot were folded into an order-independent coverage-weighted colour average, placed at
    // the depth of the nearest of them.
    bool  has_tail  = false;
    vec3  tail_rgb  = vec3(0.0);   // premultiplied
    float tail_cov  = 0.0;
    uint  tail_q    = 0u;
    if (count > uint(slots))
    {
        vec3 tail_wsum = vec3(float(imageLoad(oit_meta, ivec3(coord, 1)).r),
                              float(imageLoad(oit_meta, ivec3(coord, 2)).r),
                              float(imageLoad(oit_meta, ivec3(coord, 3)).r)) / OIT_TAIL_COLOR_SCALE;
        float tail_asum  = float(imageLoad(oit_meta, ivec3(coord, 4)).r) / OIT_TAIL_ALPHA_SCALE;
        float tail_trans = exp2(-float(imageLoad(oit_meta, ivec3(coord, 5)).r) / OIT_TAIL_LOG_SCALE);
        tail_q = oit_depth24(uintBitsToFloat(~imageLoad(oit_meta, ivec3(coord, 6)).r));
        if (tail_asum > 0.0)
        {
            has_tail = true;
            tail_cov = 1.0 - tail_trans;
            tail_rgb = (tail_wsum / tail_asum) * tail_cov;
        }
    }

    // Back to front: slots at or behind the tail's nearest depth, the tail, the nearer slots.
    vec3  accum = vec3(0.0);
    float cov   = 0.0;
    bool  tail_pending = has_tail;
    for (int i = 0; i < cnt; i++)
    {
        if (tail_pending && (nkey[i] >> 8) < tail_q)
        {
            accum = tail_rgb + accum * (1.0 - tail_cov);
            cov   = tail_cov + cov   * (1.0 - tail_cov);
            tail_pending = false;
        }
        vec3  prem = oit_unpack_rgb(ncol[i]);
        float a    = float(nkey[i] & 0xFFu) / 255.0;
        accum = prem + accum * (1.0 - a);
        cov   = a    + cov   * (1.0 - a);
    }
    if (tail_pending)
    {
        accum = tail_rgb + accum * (1.0 - tail_cov);
        cov   = tail_cov + cov   * (1.0 - tail_cov);
    }

    frag_color = vec4(accum, cov);

    // Leave the tail clean: only overflowing pixels write it and only this pass reads it, so
    // the per-frame clear can be limited to the count layer (beginAlphaOITCapture()).
    if (count > uint(slots))
    {
        for (int l = 1; l < 7; l++)
        {
            imageStore(oit_meta_clear, ivec3(coord, l), uvec4(0u));
        }
    }
}
