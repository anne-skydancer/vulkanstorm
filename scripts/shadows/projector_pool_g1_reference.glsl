// SPDX-License-Identifier: GPL-3.0-or-later
// Reference for scripts/shadows/projector_pool_g1.py: the projector shadow lookup of commit 261253bd43 (Stochastic projector
// shadows: a filtered lookup), on a standalone map with CLAMP_TO_EDGE, unchanged except for the filter radius, which follows
// the blocker nearest the receiver (the largest blocking depth) instead of the blockers' mean (projector shadow sharpness P2). G1 checks that the pool's tile lookup
// (shadowStochasticUtil.glsl, stSpotFilterTile) gives the same value (2 ulp against this reference; exactly the same against the same code on a standalone map). The helpers stSpotDisc and the ST_SPOT_*
// defines come from the util under test; ST_SPOT_KERNEL_MAX is the 10.0 the reference had.
// Edge reconstruction (PR A: projector edge revectorization): stRv* below is the util's, with the tile's origin and size read from
// the standalone map (texelFetch and textureGather on `map`); stSpotFilter takes the same branch rule as stSpotFilterTile.
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


// the blocking test of one texel of the map for receiver c: the depth at the texel clamped into the tile (CLAMP_TO_EDGE, as
// stSpotQuadTile), the reference at the texel's own (unclamped) centre, lowered by the receiver plane (stSpotQuadTile's rule)
bool stRvBlk(sampler2D map, ivec2 q, vec3 c, vec2 g, float z_bias, float rp_max)
{
    int n = textureSize(map, 0).x;
    ivec2 qc = clamp(q, ivec2(0), ivec2(n - 1));
    float zs = texelFetch(map, qc, 0).r;
    vec2 d = (vec2(q) + 0.5) / float(n) - c.xy;
    return zs < c.z - z_bias + clamp(dot(g, d), -rp_max, 0.0);
}

// the share of a uniform disc (the emitter's own shape: for a straight edge this is the exact penumbra of a disc light) on the
// side x > t of the line x = t, t in units of the disc's radius
float stRvSegment(float t)
{
    t = clamp(t, -1.0, 1.0);
    return (acos(t) - t * sqrt(max(1.0 - t * t, 0.0))) / ST_RV_PI;
}

// one texel of the edge frame (a along the edge, b across) around texel p
bool stRvB(sampler2D map, ivec2 p, bool sw, int a, int b, vec3 c, vec2 g, float z_bias, float rp_max)
{
    return stRvBlk(map, p + (sw ? ivec2(b, a) : ivec2(a, b)), c, g, z_bias, rp_max);
}

// The edge line of the silhouette near texel p in one frame (sw: a along v, b along u; else a along u, b along v), in that
// frame's texel coordinates relative to p. The boundary of column 0 nearest the receiver (fb), between rows j0 and j0 + 1; then
// both ways along the edge while the boundary stays a monotone staircase of one- or two-row steps (the necessary condition of a digital
// straight segment), up to ST_RV_S columns a side, keeping the risers within ST_RV_D columns (at most ST_RV_K a side). A riser is where the
// boundary moves by one row between two columns; its midpoint (the column boundary; the old and new boundary's mean height) is
// where a straight edge crosses a row-centre height, to +- half a texel along the edge.
//   risers on both sides: the least-squares line through the same number from each side (the receiver's column lies inside the
//     span they cover: an interpolation, never an extrapolation; a curve is cut by a short chord);
//   else MLAA's line over the receiver's own run (the RBSM papers' discontinuity shapes): between the run's ends at half a step
//     (Z and L; an end that is no step stays at the boundary), or two half-lines meeting at its middle (U).
// Returns 0: no single boundary in column 0 (no line here); 1: a line; 2: a line, but a side ended in a jump of three or more rows
// (or a non-monotone column) within two columns (the edge is steeper than 45 degrees in this frame there: the caller tries the other frame first).
int stRvLine(sampler2D map, ivec2 p, bool sw, float fb, float fa, vec3 c, vec2 g, float z_bias, float rp_max, out bool bh, out vec2 e0, out vec2 e1)
{
    bh = false;
    e0 = vec2(0.0);
    e1 = vec2(1.0, 0.0);
    int j0 = 99;
    int nb = 0;
    float best = 1.0e9;
    for (int j = -2; j <= 1; ++j)
    {
        bool lo = stRvB(map, p, sw, 0, j, c, g, z_bias, rp_max);
        bool hi = stRvB(map, p, sw, 0, j + 1, c, g, z_bias, rp_max);
        if (lo != hi)
        {
            ++nb;
            float dd = abs(fb - float(j + 1));
            if (dd < best) { best = dd; j0 = j; }
        }
    }
    if (nb != 1) return 0;
    bool bl = stRvB(map, p, sw, 0, j0, c, g, z_bias, rp_max);
    bh = !bl;
    float b0 = float(j0 + 1);
    int dirc = 0;
    float ra_r[ST_RV_K], rh_r[ST_RV_K], ra_l[ST_RV_K], rh_l[ST_RV_K];
    int n_r = 0, n_l = 0;
    float kr = float(ST_RV_S + 1), kl = -float(ST_RV_S + 1), sr = 0.0, sl = 0.0;
    bool steep = false;
    for (int side = 0; side < 2; ++side)
    {
        int stp = side == 0 ? 1 : -1;
        int cur = j0;               // the boundary lies between rows cur and cur + 1 in the column last visited
        int nr = 0;
        bool first = true;
        for (int i = 1; i <= ST_RV_S; ++i)
        {
            int k = stp * i;
            bool a0 = stRvB(map, p, sw, k, cur, c, g, z_bias, rp_max);
            bool a1 = stRvB(map, p, sw, k, cur + 1, c, g, z_bias, rp_max);
            if (a0 == bl && a1 == bh) continue;
            // the boundary's move in this column: +-1 or +-2 rows (a slope up to 2 in this frame: near 45 degrees a curve turns
            // steeper than 1 on one side, and a move of two rows is two risers at the same column boundary)
            int mv = 0;
            bool up2 = stRvB(map, p, sw, k, cur + 2, c, g, z_bias, rp_max);
            bool dn1 = stRvB(map, p, sw, k, cur - 1, c, g, z_bias, rp_max);
            if (a0 == bl && a1 == bl)
            {
                if (up2 == bh) mv = 1;
                else if (stRvB(map, p, sw, k, cur + 3, c, g, z_bias, rp_max) == bh) mv = 2;
            }
            else if (a0 == bh && a1 == bh)
            {
                if (dn1 == bl) mv = -1;
                else if (stRvB(map, p, sw, k, cur - 2, c, g, z_bias, rp_max) == bl) mv = -2;
            }
            if (mv == 0 && i <= 2 && ((a0 == bl && a1 == bl) || (a0 == bh && a1 == bh))) steep = true;
            int sg = mv > 0 ? 1 : (mv < 0 ? -1 : 0);
            if (first)
            {
                first = false;
                if (side == 0) { kr = float(k); sr = float(sg); } else { kl = float(k); sl = float(sg); }
            }
            int d = sg * stp;       // left to right
            if (mv == 0 || (dirc != 0 && d != dirc)) break;
            dirc = d;
            if (i > ST_RV_D || nr >= ST_RV_K) break;
            float ra = side == 0 ? float(k) : float(k + 1);
            for (int m = 0; m < 2; ++m)
            {
                if (m >= abs(mv) || nr >= ST_RV_K) break;
                float rh = float(cur + 1 + sg * m) + 0.5 * float(sg);
                if (side == 0) { ra_r[nr] = ra; rh_r[nr] = rh; } else { ra_l[nr] = ra; rh_l[nr] = rh; }
                ++nr;
            }
            cur += mv;
        }
        if (side == 0) n_r = nr; else n_l = nr;
    }
    int nu = min(n_r, n_l);
    if (nu >= 1)
    {
        float sa = 0.0, sh = 0.0, saa = 0.0, sah = 0.0;
        for (int i = 0; i < ST_RV_K; ++i)
        {
            if (i >= nu) break;
            sa += ra_r[i] + ra_l[i];
            sh += rh_r[i] + rh_l[i];
            saa += ra_r[i] * ra_r[i] + ra_l[i] * ra_l[i];
            sah += ra_r[i] * rh_r[i] + ra_l[i] * rh_l[i];
        }
        float fn = float(2 * nu);
        float den = fn * saa - sa * sa;
        float beta = den > 1.0e-6 ? (fn * sah - sa * sh) / den : 0.0;
        float alpha = (sh - beta * sa) / fn;
        e0 = vec2(-1.0, alpha - beta);
        e1 = vec2(1.0, alpha + beta);
        return 1;
    }
    float al = kl + 1.0, ar = kr;
    e0 = vec2(al, b0 + 0.5 * sl);
    e1 = vec2(ar, b0 + 0.5 * sr);
    if (sl == sr && sl != 0.0)
    {
        float mid = 0.5 * (al + ar);
        if (fa < mid) e1 = vec2(mid, b0); else e0 = vec2(mid, b0);
    }
    return steep ? 2 : 1;
}

// The revectorized visibility of receiver c: the share of a disc of radius rw (texels per axis) on the lit side of the edge line
// (stRvLine) through the receiver's neighbourhood. The frame tried first is the one whose rows the 4 x 4 texels around p flip
// between more often (an edge nearer u than v runs along u). ok = false where no single edge line holds (a feature thinner than
// the window, an end or a corner next to p): the caller uses P2's PCF there.
float stRvVisibility(sampler2D map, vec3 c, vec2 g, float z_bias, float rp_max, vec2 rw, out bool ok)
{
    ok = true;
    float n = float(textureSize(map, 0).x);
    vec2 t = c.xy * n;
    ivec2 p = ivec2(floor(t));
    vec2 f = t - vec2(p);
    // the 4 x 4 texels p + [-1, 2]^2: four gathers (texel reads where the window leaves the tile, CLAMP_TO_EDGE as stRvBlk)
    uint bits = 0u;
    int ni = int(n);
    if (all(greaterThanEqual(p, ivec2(1))) && all(lessThanEqual(p, ivec2(ni - 3))))
    {
        for (int q = 0; q < 4; ++q)
        {
            ivec2 o = ivec2((q & 1) * 2 - 1, (q >> 1) * 2 - 1);      // the quad's lower left texel, relative to p
            vec4 z = textureGather(map, (vec2(p + o) + 1.0) / n);
            vec2 d0 = (vec2(p + o) + 0.5) / n - c.xy;
            vec4 dx = d0.x + vec4(0.0, 1.0, 1.0, 0.0) / n;
            vec4 dy = d0.y + vec4(1.0, 1.0, 0.0, 0.0) / n;
            vec4 ref = c.z - z_bias + clamp(g.x * dx + g.y * dy, -rp_max, 0.0);
            bvec4 b = lessThan(z, ref);                              // gather order (0,1) (1,1) (1,0) (0,0)
            int bx = o.x + 1, by = o.y + 1;                          // window coordinates of the quad's lower left texel
            if (b.w) bits |= 1u << uint(by * 4 + bx);
            if (b.z) bits |= 1u << uint(by * 4 + bx + 1);
            if (b.x) bits |= 1u << uint((by + 1) * 4 + bx);
            if (b.y) bits |= 1u << uint((by + 1) * 4 + bx + 1);
        }
    }
    else
    {
        for (int j = -1; j <= 2; ++j)
            for (int i = -1; i <= 2; ++i)
                if (stRvBlk(map, p + ivec2(i, j), c, g, z_bias, rp_max)) bits |= 1u << uint((j + 1) * 4 + (i + 1));
    }
    if (bits == 0u || bits == 0xFFFFu)
    {
        // a clean window: the disc stays inside it (at least one texel to its edge) or P2's filter, which sees past it, decides
        if (max(rw.x, rw.y) > 1.0) ok = false;
        return bits == 0u ? 1.0 : 0.0;
    }
    int hx = 0, vy = 0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 3; ++i)
        {
            hx += int(((bits >> uint(j * 4 + i)) ^ (bits >> uint(j * 4 + i + 1))) & 1u);
            vy += int(((bits >> uint(i * 4 + j)) ^ (bits >> uint(i * 4 + j + 4))) & 1u);
        }
    bool sw = hx > vy;
    bool bh = false, bh2 = false;
    vec2 e0, e1, e0b, e1b;
    int r = stRvLine(map, p, sw, sw ? f.x : f.y, sw ? f.y : f.x, c, g, z_bias, rp_max, bh, e0, e1);
    if (r != 1)
    {
        int r2 = stRvLine(map, p, !sw, sw ? f.y : f.x, sw ? f.x : f.y, c, g, z_bias, rp_max, bh2, e0b, e1b);
        if (r2 == 1 || (r2 == 2 && r == 0))
        {
            sw = !sw;
            bh = bh2;
            e0 = e0b;
            e1 = e1b;
            r = 1;
        }
    }
    if (r == 0)
    {
        ok = false;
        return 1.0;
    }
    // to tile texels, then to units of the filter radius per axis (the disc is an ellipse in texels when k.x != k.y)
    vec2 q0 = (sw ? e0.yx : e0) / rw;
    vec2 q1 = (sw ? e1.yx : e1) / rw;
    vec2 fq = f / rw;
    vec2 dir = normalize(q1 - q0);
    vec2 nrm = vec2(-dir.y, dir.x);
    vec2 up = sw ? vec2(1.0 / rw.x, 0.0) : vec2(0.0, 1.0 / rw.y);   // +b, the high row's side
    float side = dot(nrm, up) >= 0.0 ? 1.0 : -1.0;
    float dist = dot(fq - q0, nrm) * side;                           // > 0: on the high row's side
    float lit = bh ? -dist : dist;                                   // > 0: on the lit side
    return stRvSegment(-lit);
}


// The percentage-closer filter at radius rf (the second loop of the lookup before PR A)
float stSpotPCF(sampler2D map, vec2 res, vec3 c, vec2 g, float z_bias, float rp_max, vec2 rf)
{
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

// The visibility of receiver c (map uv and depth) through one projector map. pen: the filter's radius in texels per
// unit of map depth between the receiver and its blockers (x and y: the map's axes may differ in metres).
float stSpotFilter(sampler2D map, vec2 res, vec3 c, vec2 g, float z_bias, float z_tex, vec2 pen)
{
    // the search: any blocker between the near plane (z = 0) and the receiver, in the disc its farthest one needs
    vec2 rs = clamp(pen * c.z, vec2(ST_SPOT_KERNEL_MIN), vec2(ST_SPOT_KERNEL_MAX));
    float rp_max = z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(rs.x, rs.y) + 1.0));
    float z_near = 0.0;
    float n_blk = 0.0;
    for (int k = 0; k < ST_SPOT_SEARCH; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuad(map, c.xy + stSpotDisc(k, ST_SPOT_SEARCH) * rs / res, res, c, g, z_bias, rp_max, ref, f);
        vec4 blk = vec4(lessThan(z, ref));
        n_blk += blk.x + blk.y + blk.z + blk.w;
        vec4 bz = blk * z;
        z_near = max(z_near, max(max(bz.x, bz.y), max(bz.z, bz.w)));
    }
    if (n_blk == 0.0)
    {
        return 1.0; // nothing between the receiver and the light
    }
    if (n_blk == float(4 * ST_SPOT_SEARCH))
    {
        return 0.0; // the umbra: the filter's disc lies inside the search's
    }
    // the filter: its radius from the nearest blocker's depth (contact hardening), weighted 1 - 0.9 r^2 over the disc
    vec2 rphys = pen * (c.z - z_near);
    float rmax = max(rphys.x, rphys.y);
    vec2 rf = clamp(rphys, vec2(ST_SPOT_KERNEL_MIN), vec2(ST_SPOT_KERNEL_MAX));
    if (rmax >= ST_RV_HI)
    {
        return stSpotPCF(map, res, c, g, z_bias, rp_max, rf);
    }
    float rp_rv = z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(max(rs.x, rs.y), ST_RV_HI) + 1.0));
    bool ok;
    float v = stRvVisibility(map, c, g, z_bias, rp_rv, max(min(rphys, vec2(ST_SPOT_KERNEL_MAX)), vec2(ST_RV_MIN)), ok);
    if (!ok)
    {
        return stSpotPCF(map, res, c, g, z_bias, rp_max, rf);
    }
    if (rmax > ST_RV_LO)
    {
        v = mix(v, stSpotPCF(map, res, c, g, z_bias, rp_max, rf), smoothstep(ST_RV_LO, ST_RV_HI, rmax));
    }
    return v;
}
