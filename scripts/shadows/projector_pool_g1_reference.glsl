// SPDX-License-Identifier: GPL-3.0-or-later
// Reference for scripts/shadows/projector_pool_g1.py: the projector shadow lookup of commit 261253bd43 (Stochastic projector
// shadows: a filtered lookup), on a standalone map with CLAMP_TO_EDGE, unchanged. G1 checks that the pool's tile lookup
// (shadowStochasticUtil.glsl, stSpotFilterTile) gives the same value (2 ulp against this reference; exactly the same against the same code on a standalone map). The helpers stSpotDisc and the ST_SPOT_*
// defines come from the util under test; ST_SPOT_KERNEL_MAX is the 10.0 the reference had.
#define ST_SPOT_KERNEL_MAX 10.0

// The 2 x 2 texels whose bilinear footprint holds uv, read with one gather at their shared corner (so the hardware
// picks the four texels the weights below assume, whatever its sub-texel precision), in gather order: (0,1) (1,1)
// (1,0) (0,0) from the quad's lower left texel. Returns the stored depths z, and in ref the receiver's reference
// at each texel's centre: its depth c.z less the constant bias, lowered (never raised) by the signed receiver plane
// g over the texel's offset, at most rp_max (S2's form). f: uv's bilinear fraction in the quad.
vec4 stSpotQuad(sampler2D map, vec2 uv, vec2 res, vec3 c, vec2 g, float z_bias, float rp_max, out vec4 ref, out vec2 f)
{
    vec2 t = uv * res - 0.5;
    vec2 b = floor(t);
    f = t - b;
    vec2 o = (b + 0.5) / res - c.xy;
    vec2 e = 1.0 / res;
    vec4 dx = o.x + vec4(0.0, e.x, e.x, 0.0);
    vec4 dy = o.y + vec4(e.y, e.y, 0.0, 0.0);
    ref = c.z - z_bias + clamp(g.x * dx + g.y * dy, -rp_max, 0.0);
    return textureGather(map, (b + 1.0) / res);
}


// The visibility of receiver c (map uv and depth) through one projector map. pen: the filter's radius in texels per
// unit of map depth between the receiver and its blockers (x and y: the map's axes may differ in metres).
float stSpotFilter(sampler2D map, vec2 res, vec3 c, vec2 g, float z_bias, float z_tex, vec2 pen)
{
    // the search: any blocker between the near plane (z = 0) and the receiver, in the disc its farthest one needs
    vec2 rs = clamp(pen * c.z, vec2(ST_SPOT_KERNEL_MIN), vec2(ST_SPOT_KERNEL_MAX));
    float rp_max = z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(rs.x, rs.y) + 1.0));
    float z_sum = 0.0;
    float n_blk = 0.0;
    for (int k = 0; k < ST_SPOT_SEARCH; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuad(map, c.xy + stSpotDisc(k, ST_SPOT_SEARCH) * rs / res, res, c, g, z_bias, rp_max, ref, f);
        vec4 blk = vec4(lessThan(z, ref));
        z_sum += dot(blk, z);
        n_blk += blk.x + blk.y + blk.z + blk.w;
    }
    if (n_blk == 0.0)
    {
        return 1.0; // nothing between the receiver and the light
    }
    if (n_blk == float(4 * ST_SPOT_SEARCH))
    {
        return 0.0; // the umbra: the filter's disc lies inside the search's
    }
    // the filter: its radius from the blockers' mean depth (map depth is linear in 1 / distance, so the mean of z is
    // the mean of 1 / distance the penumbra's width follows), weighted 1 - 0.9 r^2 over the disc
    vec2 rf = clamp(pen * (c.z - z_sum / n_blk), vec2(ST_SPOT_KERNEL_MIN), vec2(ST_SPOT_KERNEL_MAX));
    float lit = 0.0;
    for (int k = 0; k < ST_SPOT_TAPS; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuad(map, c.xy + stSpotDisc(k, ST_SPOT_TAPS) * rf / res, res, c, g, z_bias, rp_max, ref, f);
        vec4 bw = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
        lit += (1.0 - 0.9 * (float(k) + 0.5) / float(ST_SPOT_TAPS)) * dot(vec4(greaterThanEqual(z, ref)), bw);
    }
    return lit / (0.55 * float(ST_SPOT_TAPS)); // the weights' sum
}
