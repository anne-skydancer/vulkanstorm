#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Projector shadow edges (PR A, revectorized edges): the bench and the oracle of the lookup's edge reconstruction.

Offline bench, no viewer. The shader sources of the baselines come from git (--repo, --ref, --baseline-ref), the checkout's own
shader from its working tree:
  today   the release lookup (--baseline-ref, default 5a091b9559: mean-blocker PCSS, 4 cm normal offset, prim emitter)
  P2      the devel lookup (--ref, default origin/vkstorm-devel: #527, nearest blocker, 1-texel normal offset, 1 cm x softness)
  RV      the ORACLE (bench only): P2 with the filter replaced by stSpotFilterTileRV below; the same blocker search; where the
          physical filter radius is under RV_HI texels the visibility is the weighted-disc coverage of a straight edge line
          reconstructed from the shadow map's binary silhouette (MLAA/RBSM-style run traversal), else P2's PCF.
  RVreal  the checkout's shadowStochasticUtil.glsl as the viewer compiles it (its G1 block and sampleProjectorShadow). It must equal
          RV: every scan prints "real against the oracle" and the run fails above a max abs of 1e-5.
  RVINV   a cheaper alternative (bench only, rejected: the step stays at shallow angles; name it in --methods to run it)

Scene for the staircase (--stairs): a projector at H = 2.5 m looking straight down (fov --fovs), a zero-thickness plate at
height --plate-h whose straight edge makes an angle with the tile's u axis (--angles); the receiver is the floor. The true
50 % contour is the point projection of the edge (exact for a disc emitter centred on the projection centre). The reference
of the whole profile is the disc emitter's coverage, analytic. Per scan: the 50 % contour of the lookup across the edge at
512 positions along it; its deviation from the true line (mean = bias, RMS and max about the true line, peak-to-peak =
step amplitude, all in mm), and the 10-90 % width (mm), mean over the positions. Gates (tiles of 256 and up): the worst step 0.25
texel for 2 to 45 degrees and 0.50 texel under 2 degrees (--angles 0.5,1,1.5,2,5,10,20,30,45, --sizes 64,128,...), the width within 5 % of the
analytic one where wider than 0.35 texel (--softness 1 --plate-h 1.2).

Scene for the avatar gates (--gates): the avatar boxes of scripts/shadows/proj_march_bench.py (toe and shoulder edges) under a
1.5 rad lamp, tiles 2048/1024/512, emitter 1 cm x 0.23. Gates: toe and shoulder x50 within 2 mm of the reference or no worse
than P2's + 0.1 mm, the value under the toe at most 0.02, the toe RMS at most P2's + 0.005.

--time: GPU ms of a 2560x1440 view of a floor under a wide lamp with 24 plates at various angles and heights (the edge
pixels a real room has more of than one avatar), plus the avatar (timer queries, median of 9 x 20); the run says which builds
were running.

--circle: round plates (a head, 0.09 m; a table, 0.3 m): the 50 % contour against the true circle, and its staircase part (the
deviation less its harmonics 0..3); reported, not gated (RV makes the same facets sharper: see doc/shadows/projector-shadow-pool.md).
--grazing: a tilted lit plane, the acne check (at 85 degrees this plane faces away from the lamp: dark is right).

The oracle's constants are #defines (--defs NAME=VALUE,... overrides them): RV_S 24, RV_K 4, RV_D 8, RV_MIN 0.25, RV_LO 1.5,
RV_HI 2.5 (the shader's ST_RV_*), RV_RETRY 1, RV_GATHER 0 (1: a gather-based traversal, measured slower), RV_STOP (ablation).

    python scripts/shadows/proj_edges_bench.py --stairs --gates --circle --grazing --time --out results.jsonl
    python scripts/shadows/proj_edges_bench.py --stairs --softness 1 --plate-h 1.2 --fovs 2.0 --sizes 1024,2048
Needs numpy and an OpenGL 4.x driver on Windows (the repo's scripts/perf/gl_test_context.py, read from --ref). Exit code 1 when a
gate or the oracle check fails.
"""
import argparse
import json
import math
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent  # scripts/shadows
ap = argparse.ArgumentParser()
ap.add_argument('--repo', default=str(HERE.parents[1]), help='the checkout: RVreal compiles its working-tree shaders; git refs are read from it')
ap.add_argument('--ref', default='origin/vkstorm-devel')
ap.add_argument('--baseline-ref', default='5a091b9559')
ap.add_argument('--stairs', action='store_true')
ap.add_argument('--gates', action='store_true')
ap.add_argument('--grazing', action='store_true', help='acne check: a tilted lit plane (P2 and RV)')
ap.add_argument('--circle', action='store_true', help='a round plate (radius --radii): the 50 %% contour against the true circle')
ap.add_argument('--radii', default='0.09,0.3')
ap.add_argument('--time', action='store_true')
ap.add_argument('--p2-regime', action='store_true', help='with --stairs: the scene must be in the P2 regime (try --softness 1 --plate-h 1.8 --sizes 2048): RVreal must equal P2 to 1e-6')
ap.add_argument('--fovs', default='2.0,2.5,3.0')
ap.add_argument('--sizes', default='256,512,1024,2048')
ap.add_argument('--angles', default='2,5,10,20,30,45')
ap.add_argument('--plate-h', type=float, default=0.75)
ap.add_argument('--softness', type=float, default=0.23)
ap.add_argument('--today-emitter', type=float, default=0.02, help="today's prim emitter (m): 2 cm, the clamp's sharpest")
ap.add_argument('--methods', default='today,P2,RV,RVreal')
ap.add_argument('--out', default=None)
ap.add_argument('--rv-off', type=int, default=0, help='--time: set the rv_off uniform (ablation)')
ap.add_argument('--defs', default='', help='extra #defines for the RV prototypes, e.g. RV_MODE=0,RV_K=2')
opts = ap.parse_args()
REPO = Path(opts.repo)


def git_show(ref, path):
    return subprocess.run(['git', '-C', str(REPO), 'show', '%s:%s' % (ref, path)], capture_output=True, check=True).stdout.decode('utf-8')


UTIL_P = 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
COMMON_P = 'indra/newview/app_settings/shaders/class1/deferred/shadowUtil.glsl'
ctx_src = git_show(opts.ref, 'scripts/perf/gl_test_context.py')
sys.argv = [sys.argv[0]]
exec(compile(ctx_src, 'gl_test_context.py', 'exec'))  # window, context, fn(), program(), use, U, I, P, C, get_string

OUT = open(opts.out, 'a') if opts.out else None


def emit(row):
    if OUT:
        OUT.write(json.dumps(row, default=float) + '\n')
        OUT.flush()


P2DIFF = []   # (case, max abs difference between RVreal and P2): under --p2-regime, every scan is in P2's regime and must agree
ORACLE = []   # (case, max abs difference between the checkout's shader (RVreal) and the prototype (RV))


def oracle_check(case, imgs):
    if 'RV' in imgs and 'RVreal' in imgs:
        ORACLE.append((case, float(np.max(np.abs(imgs['RV'] - imgs['RVreal'])))))
    if 'P2' in imgs and 'RVreal' in imgs:
        P2DIFF.append((case, float(np.max(np.abs(imgs['P2'] - imgs['RVreal'])))))


def oracle_report():
    if ORACLE:
        worst = max(d for _, d in ORACLE)
        print('\nRVreal against the prototype oracle: %d scans, max abs %.3g (gate 1e-5): %s' % (len(ORACLE), worst, 'PASS' if worst <= 1e-5 else 'FAIL'))
        emit(dict(kind='oracle', scans=len(ORACLE), max_abs=worst))
        ok = worst <= 1e-5
    else:
        ok = True
    if P2DIFF:
        w2 = max(d for _, d in P2DIFF)
        if opts.p2_regime:
            print('RVreal against P2 where the penumbra is wider than RV_HI (--p2-regime): %d scans, max abs %.3g (gate 1e-6): %s' % (len(P2DIFF), w2, 'PASS' if w2 <= 1e-6 else 'FAIL'))
            emit(dict(kind='p2_regime', scans=len(P2DIFF), max_abs=w2))
            ok &= w2 <= 1e-6
        else:
            print('RVreal against P2 (informational): max abs %.3g over %d scans' % (w2, len(P2DIFF)))
    return ok


def builds_running():
    names = []
    for exe in ('cl.exe', 'link.exe', 'MSBuild.exe'):
        r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ' + exe, '/FO', 'CSV', '/NH'], capture_output=True, text=True)
        if exe.lower() in r.stdout.lower():
            names.append(exe)
    return names


def func(text, sig):
    i = text.index(sig)
    j = text.index('\n}\n', i)
    return text[i:j + 3]


def section(text, begin, end):
    i = text.index('\n', text.index(begin)) + 1
    return text[i:text.index(end, i)]


def sources(ref):
    util = git_show(ref, UTIL_P)
    common = git_show(ref, COMMON_P)
    defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define (ST_)', l))
    g1 = section(util, '// <G1-BEGIN>', '// <G1-END>')
    common_fns = ''.join(func(common, s) for s in (
        'vec3 shadowRow(', 'vec2 shadowUVScale(', 'float shadowDepthScale(', 'float shadowTexelMetres(',
        'vec2 shadowPlaneGradient(', 'float shadowWorldBias(', 'vec3 shadowNormalOffsetPos(', 'vec3 shadowReceiverGeometry('))
    st = func(util, 'vec2 stSpotGradient(')
    sample = func(util, 'float sampleProjectorShadow(')
    return defines, common_fns + st + g1, sample


def sources_checkout():
    """the working tree's own shaders, as sources() reads them from git (RVreal: the code the viewer compiles)"""
    util = (REPO / UTIL_P).read_text(encoding='utf-8')
    common = (REPO / COMMON_P).read_text(encoding='utf-8')
    defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define (ST_)', l))
    g1 = section(util, '// <G1-BEGIN>', '// <G1-END>')
    common_fns = ''.join(func(common, s) for s in (
        'vec3 shadowRow(', 'vec2 shadowUVScale(', 'float shadowDepthScale(', 'float shadowTexelMetres(',
        'vec2 shadowPlaneGradient(', 'float shadowWorldBias(', 'vec3 shadowNormalOffsetPos(', 'vec3 shadowReceiverGeometry('))
    return defines, common_fns + func(util, 'vec2 stSpotGradient(') + g1, func(util, 'float sampleProjectorShadow(')


BASE = sources(opts.baseline_ref)
DEV = sources(opts.ref)
REAL = sources_checkout()

PREAMBLE = '''#version 400
#define SHADOW_COMMON_NO_DEPTH 1
#define SHADOW_BIAS_TEXELS 1.0
#define SHADOW_NORMAL_OFFSET_TEXELS 1.0
#define SHADOW_PLANE_BIAS_MAX_TEXELS 8.0
#define SHADOW_GRAD_MAX 10000.0
uniform float spot_shadow_offset;
uniform float spot_shadow_reach;
'''

# ------------------------------------------------------------------------------------------------ the prototypes (bench only)
RV = r'''
#ifndef RV_S
#define RV_S 24            // texels: the run traversal's reach on each side of the receiver's texel
#endif
#ifndef RV_K
#define RV_K 4             // risers a side
#endif
#if defined(RV_PRAGMA)
#pragma optionNV(unroll none)
#endif
#ifndef RV_GATHER
#define RV_GATHER 0        // 1: the traversal reads two columns a step with gathers (measured slower: code size; kept for the record)
#endif
#ifndef RV_STOP
#define RV_STOP 0          // bench ablation: 1 return after the window, 2 after the first line, 3 skip RV (P2 inside the RV shader)
#endif
#ifndef RV_RETRY
#define RV_RETRY 1         // try the other frame where the first has no line or turns steeper than 45 degrees
#endif
#ifndef RV_D
#define RV_D 8             // columns a side within which risers count
#endif
#define RV_MIN 0.25        // texels: the narrowest filter radius of the reconstructed edge (sharp, not a step)
#define RV_LO 1.5          // texels: physical radius from which the result blends towards P2's PCF ...
#define RV_HI 2.5          // ... which it is from here on (the staircase is under the penumbra's blur)
#define RVI_R 2.5          // RVINV: the PCF radius the distance is read from
#define RV_PI 3.14159265

// the blocking test of one texel of the tile for receiver c: the depth at the texel clamped into the tile (CLAMP_TO_EDGE, as
// stSpotQuadTile), the reference at the texel's own (unclamped) centre, lowered by the receiver plane (stSpotQuadTile's rule)
bool rvBlk(ivec2 q, vec3 c, vec2 g, float z_bias, float rp_max)
{
    int n = int(proj_shadow_tile.z);
    ivec2 qc = clamp(q, ivec2(0), ivec2(n - 1));
    float zs = texelFetch(proj_shadow_atlas, ivec2(proj_shadow_tile.xy) + qc, 0).r;
    vec2 d = (vec2(q) + 0.5) / float(n) - c.xy;
    return zs < c.z - z_bias + clamp(dot(g, d), -rp_max, 0.0);
}

// the share of a uniform disc (the emitter's own shape: for a straight edge this is the exact penumbra of a disc light) on the
// side x > t of the line x = t, t in units of the disc's radius
float rvSegment(float t)
{
    t = clamp(t, -1.0, 1.0);
    return (acos(t) - t * sqrt(max(1.0 - t * t, 0.0))) / RV_PI;
}

// the inverse of rvSegment (bisection; RVINV only)
float rvSegmentInv(float v)
{
    float lo = -1.0, hi = 1.0;
    for (int i = 0; i < 14; ++i)
    {
        float m = 0.5 * (lo + hi);
        if (rvSegment(m) > v) lo = m; else hi = m;
    }
    return 0.5 * (lo + hi);
}

// one texel of the edge frame (a along the edge, b across) around texel p
bool rvB(ivec2 p, bool sw, int a, int b, vec3 c, vec2 g, float z_bias, float rp_max)
{
    return rvBlk(p + (sw ? ivec2(b, a) : ivec2(a, b)), c, g, z_bias, rp_max);
}

// RV_GATHER: the blocking bits of the 2 x 2 frame texels (a, b) .. (a + 1, b + 1), bit da + 2 db, from one gather (four texel
// reads where the block leaves the tile: CLAMP_TO_EDGE as rvBlk)
uint rvQuad(ivec2 p, bool sw, int a, int b, vec3 c, vec2 g, float z_bias, float rp_max)
{
    ivec2 q = p + (sw ? ivec2(b, a) : ivec2(a, b));      // the block's lower left texel in the tile
    int n = int(g_tile.z);
    if (any(lessThan(q, ivec2(0))) || any(greaterThan(q, ivec2(n - 2))))
    {
        uint r = 0u;
        if (rvB(p, sw, a, b, c, g, z_bias, rp_max)) r |= 1u;
        if (rvB(p, sw, a + 1, b, c, g, z_bias, rp_max)) r |= 2u;
        if (rvB(p, sw, a, b + 1, c, g, z_bias, rp_max)) r |= 4u;
        if (rvB(p, sw, a + 1, b + 1, c, g, z_bias, rp_max)) r |= 8u;
        return r;
    }
    float nf = float(n);
    vec4 z = textureGather(proj_shadow_atlas, (g_tile.xy + vec2(q) + 1.0) * g_tile.w);   // (0,1) (1,1) (1,0) (0,0) of the tile
    vec2 d0 = (vec2(q) + 0.5) / nf - c.xy;
    vec4 dx = d0.x + vec4(0.0, 1.0, 1.0, 0.0) / nf;
    vec4 dy = d0.y + vec4(1.0, 1.0, 0.0, 0.0) / nf;
    bvec4 bk = lessThan(z, c.z - z_bias + clamp(g.x * dx + g.y * dy, -rp_max, 0.0));
    // frame (da, db) = tile (da, db), or tile (db, da) when transposed
    uint r = 0u;
    if (bk.w) r |= 1u;
    if (sw ? bk.x : bk.z) r |= 2u;
    if (sw ? bk.z : bk.x) r |= 4u;
    if (bk.y) r |= 8u;
    return r;
}

// The edge line of the silhouette near texel p in one frame (sw: a along v, b along u; else a along u, b along v), in that
// frame's texel coordinates relative to p. The boundary of column 0 nearest the receiver (fb), between rows j0 and j0 + 1; then
// both ways along the edge while the boundary stays a monotone staircase of one-row steps (the necessary condition of a digital
// straight segment), up to RV_S columns a side, keeping the risers within RV_D columns (at most RV_K a side). A riser is where the
// boundary moves by one row between two columns; its midpoint (the column boundary; the old and new boundary's mean height) is
// where a straight edge crosses a row-centre height, to +- half a texel along the edge.
//   risers on both sides: the least-squares line through the same number from each side (the receiver's column lies inside the
//     span they cover: an interpolation, never an extrapolation; a curve is cut by a short chord);
//   else MLAA's line over the receiver's own run (the RBSM papers' discontinuity shapes): between the run's ends at half a step
//     (Z and L; an end that is no step stays at the boundary), or two half-lines meeting at its middle (U).
// Returns 0: no single boundary in column 0 (no line here); 1: a line; 2: a line, but a side ended in a jump of two or more rows
// within two columns (the edge is steeper than 45 degrees in this frame there: the caller tries the other frame first).
int rvLine(ivec2 p, bool sw, float fb, float fa, vec3 c, vec2 g, float z_bias, float rp_max, out bool bh, out vec2 e0, out vec2 e1)
{
    bh = false;
    e0 = vec2(0.0);
    e1 = vec2(1.0, 0.0);
    int j0 = 99;
    int nb = 0;
    float best = 1.0e9;
    for (int j = -2; j <= 1; ++j)
    {
        bool lo = rvB(p, sw, 0, j, c, g, z_bias, rp_max);
        bool hi = rvB(p, sw, 0, j + 1, c, g, z_bias, rp_max);
        if (lo != hi)
        {
            ++nb;
            float dd = abs(fb - float(j + 1));
            if (dd < best) { best = dd; j0 = j; }
        }
    }
    if (nb != 1) return 0;
    bool bl = rvB(p, sw, 0, j0, c, g, z_bias, rp_max);
    bh = !bl;
    float b0 = float(j0 + 1);
    int dirc = 0;
    float ra_r[RV_K], rh_r[RV_K], ra_l[RV_K], rh_l[RV_K];
    int n_r = 0, n_l = 0;
    float kr = float(RV_S + 1), kl = -float(RV_S + 1), sr = 0.0, sl = 0.0;
    bool steep = false;
    for (int side = 0; side < 2; ++side)
    {
        int stp = side == 0 ? 1 : -1;
        int cur = j0;               // the boundary lies between rows cur and cur + 1 in the column last visited
        int nr = 0;
        bool first = true;
#if RV_GATHER
        // two columns a step: the strip of rows cur - 2 .. cur + 3 of both, three gathers issued together (independent reads,
        // one latency), then both columns from registers; a row the strip lacks (after a move in the first column) is read alone
        uint strip = 0u;
        int s_lo = 0, s_b = 0;          // the strip's first column (frame a) and first row
        for (int i = 1; i <= RV_S; ++i)
        {
            int k = stp * i;
            if (((i - 1) & 1) == 0)
            {
                s_lo = stp > 0 ? k : k - 1;
                s_b = cur - 2;
                strip = rvQuad(p, sw, s_lo, s_b, c, g, z_bias, rp_max) | (rvQuad(p, sw, s_lo, s_b + 2, c, g, z_bias, rp_max) << 4u)
                      | (rvQuad(p, sw, s_lo, s_b + 4, c, g, z_bias, rp_max) << 8u);
            }
            int da = k - s_lo;
            #define RVS(row) ((row) >= s_b && (row) < s_b + 6 ? (((strip >> uint(((row) - s_b) * 2 + da)) & 1u) != 0u) : rvB(p, sw, k, (row), c, g, z_bias, rp_max))
            bool a0 = RVS(cur);
            bool a1 = RVS(cur + 1);
            if (a0 == bl && a1 == bh) continue;
            int mv = 0;
            bool up2 = RVS(cur + 2);
            bool dn1 = RVS(cur - 1);
            if (a0 == bl && a1 == bl)
            {
                if (up2 == bh) mv = 1;
                else if (RVS(cur + 3) == bh) mv = 2;
            }
            else if (a0 == bh && a1 == bh)
            {
                if (dn1 == bl) mv = -1;
                else if (RVS(cur - 2) == bl) mv = -2;
            }
            #undef RVS
#else
        for (int i = 1; i <= RV_S; ++i)
        {
            int k = stp * i;
            bool a0 = rvB(p, sw, k, cur, c, g, z_bias, rp_max);
            bool a1 = rvB(p, sw, k, cur + 1, c, g, z_bias, rp_max);
            if (a0 == bl && a1 == bh) continue;
            // the boundary's move in this column: +-1 or +-2 rows (a slope up to 2 in this frame: near 45 degrees a curve turns
            // steeper than 1 on one side, and a move of two rows is two risers at the same column boundary)
            int mv = 0;
            bool up2 = rvB(p, sw, k, cur + 2, c, g, z_bias, rp_max);
            bool dn1 = rvB(p, sw, k, cur - 1, c, g, z_bias, rp_max);
            if (a0 == bl && a1 == bl)
            {
                if (up2 == bh) mv = 1;
                else if (rvB(p, sw, k, cur + 3, c, g, z_bias, rp_max) == bh) mv = 2;
            }
            else if (a0 == bh && a1 == bh)
            {
                if (dn1 == bl) mv = -1;
                else if (rvB(p, sw, k, cur - 2, c, g, z_bias, rp_max) == bl) mv = -2;
            }
#endif
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
            if (i > RV_D || nr >= RV_K) break;
            float ra = side == 0 ? float(k) : float(k + 1);
            for (int m = 0; m < 2; ++m)
            {
                if (m >= abs(mv) || nr >= RV_K) break;
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
        for (int i = 0; i < RV_K; ++i)
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
// (rvLine) through the receiver's neighbourhood. The frame tried first is the one whose rows the 4 x 4 texels around p flip
// between more often (an edge nearer u than v runs along u). ok = false where no single edge line holds (a feature thinner than
// the window, an end or a corner next to p): the caller uses P2's PCF there.
float rvVisibility(vec3 c, vec2 g, float z_bias, float rp_max, vec2 rw, out bool ok)
{
    ok = true;
    float n = proj_shadow_tile.z;
    vec2 t = c.xy * n;
    ivec2 p = ivec2(floor(t));
    vec2 f = t - vec2(p);
    // the 4 x 4 texels p + [-1, 2]^2: four gathers (texel reads where the window leaves the tile, CLAMP_TO_EDGE as rvBlk)
    uint bits = 0u;
    int ni = int(n);
    if (all(greaterThanEqual(p, ivec2(1))) && all(lessThanEqual(p, ivec2(ni - 3))))
    {
        for (int q = 0; q < 4; ++q)
        {
            ivec2 o = ivec2((q & 1) * 2 - 1, (q >> 1) * 2 - 1);      // the quad's lower left texel, relative to p
            vec4 z = textureGather(proj_shadow_atlas, (proj_shadow_tile.xy + vec2(p + o) + 1.0) * proj_shadow_tile.w);
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
                if (rvBlk(p + ivec2(i, j), c, g, z_bias, rp_max)) bits |= 1u << uint((j + 1) * 4 + (i + 1));
    }
    if (bits == 0u) return 1.0;
    if (bits == 0xFFFFu) return 0.0;
#if RV_STOP == 1
    return 0.5;
#endif
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
    int r = rvLine(p, sw, sw ? f.x : f.y, sw ? f.y : f.x, c, g, z_bias, rp_max, bh, e0, e1);
#if RV_STOP == 2
    return 0.5;
#endif
    if (RV_RETRY != 0 && r != 1)
    {
        int r2 = rvLine(p, !sw, sw ? f.y : f.x, sw ? f.x : f.y, c, g, z_bias, rp_max, bh2, e0b, e1b);
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
    return rvSegment(-lit);
}

// P2's percentage-closer filter at radius rf (texels per axis), the taps of stSpotFilterTile's second loop
float rvPCF(vec3 c, vec2 g, float z_bias, float rp_max, vec2 rf, bool edge)
{
    vec2 res = vec2(proj_shadow_tile.z);
    float lit = 0.0;
    for (int k = 0; k < ST_SPOT_TAPS; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuadTile(c.xy + stSpotDisc(k, ST_SPOT_TAPS) * rf / res, c, g, z_bias, rp_max, edge, ref, f);
        vec4 bw = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
        lit += (1.0 - 0.9 * (float(k) + 0.5) / float(ST_SPOT_TAPS)) * dot(vec4(greaterThanEqual(z, ref)), bw);
    }
    return lit / (0.55 * float(ST_SPOT_TAPS));
}

// stSpotFilterTile (P2) with the revectorized edge where the physical radius is small. VARIANT_RV 1: RV, 2: RVINV
uniform int rv_off;   // bench ablation: 1 = never take the RV branch at run time (its code still compiled: the cost of carrying it)
float stSpotFilterTileRV(vec3 c, vec2 g, float z_bias, float z_tex, vec2 pen, vec2 kmax)
{
    vec2 res = vec2(proj_shadow_tile.z);
    vec2 rs = clamp(pen * c.z, vec2(ST_SPOT_KERNEL_MIN), kmax);
    // P2's own cap and tile-edge flag for the search and the PCF (those pixels stay P2's, bit for bit); RV's texel reads have their
    // own cap rp_rv (at least the plane's lowering over RVI_R + 1 texels), RVINV's wider PCF its own edge flag
    float rp_max = z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(rs.x, rs.y) + 1.0));
    float rp_rv = z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(max(rs.x, rs.y), RVI_R) + 1.0));
    vec2 tc = c.xy * res - 0.5;
    float r = max(rs.x, rs.y);
    bool edge = any(lessThan(tc - r, vec2(1.0))) || any(greaterThan(tc + r, res - 2.0));
    float r2 = max(r, RVI_R);
    bool edge2 = any(lessThan(tc - r2, vec2(1.0))) || any(greaterThan(tc + r2, res - 2.0));
    float z_near = 0.0;
    float n_blk = 0.0;
    for (int k = 0; k < ST_SPOT_SEARCH; ++k)
    {
        vec4 ref;
        vec2 f;
        vec4 z = stSpotQuadTile(c.xy + stSpotDisc(k, ST_SPOT_SEARCH) * rs / res, c, g, z_bias, rp_max, edge, ref, f);
        vec4 blk = vec4(lessThan(z, ref));
        n_blk += blk.x + blk.y + blk.z + blk.w;
        vec4 bz = blk * z;
        z_near = max(z_near, max(max(bz.x, bz.y), max(bz.z, bz.w)));
    }
    if (n_blk == 0.0) return 1.0;
    if (n_blk == float(4 * ST_SPOT_SEARCH)) return 0.0;
    vec2 rphys = pen * (c.z - z_near);                         // the physical radius, unclamped
    float rmax = max(rphys.x, rphys.y);
    vec2 rf = clamp(rphys, vec2(ST_SPOT_KERNEL_MIN), kmax);    // P2's
    if (rmax >= RV_HI || rv_off != 0) return rvPCF(c, g, z_bias, rp_max, rf, edge);
#if VARIANT_RV == 1
    bool ok;
    float v = rvVisibility(c, g, z_bias, rp_rv, max(rphys, vec2(RV_MIN)), ok);
    if (!ok) return rvPCF(c, g, z_bias, rp_max, rf, edge);
#else
    float vw = rvPCF(c, g, z_bias, rp_rv, max(rf, vec2(RVI_R)), edge2);
    float dist = -rvSegmentInv(vw);                            // > 0: into the lit side, in units of the wide radius
    float v = rvSegment(-dist * RVI_R / max(rmax, RV_MIN));
#endif
    if (rmax > RV_LO) v = mix(v, rvPCF(c, g, z_bias, rp_max, rf, edge), smoothstep(RV_LO, RV_HI, rmax));
    return v;
}
'''

RECEIVER = r'''
uniform vec2 grid;
uniform vec3 p0;      // the grid's centre on the floor
uniform vec3 ax;      // the grid's extent along x (columns)
uniform vec3 ay;      // the grid's extent along y (rows)
uniform mat4 cam_inv; // --time
uniform vec3 cam_pos;
uniform vec3 recv_norm;   // --grazing: the tilted receiver's normal (zero: the floor's)
out vec4 oc;
void main()
{
    vec3 norm = dot(recv_norm, recv_norm) > 0.0 ? recv_norm : vec3(0.0, 0.0, 1.0);
#if TIMING
    vec4 q = cam_inv * vec4((gl_FragCoord.xy / grid) * 2.0 - 1.0, 1.0, 1.0);
    vec3 d = normalize(q.xyz / q.w - cam_pos);
    vec3 pos = cam_pos + d * (-cam_pos.z / d.z);
#else
    vec3 pos = p0 + ((gl_FragCoord.x / grid.x) - 0.5) * ax + ((gl_FragCoord.y / grid.y) - 0.5) * ay;
#endif
    oc = vec4(LOOKUP(pos, norm, gl_FragCoord.xy / grid));
}
'''
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'


def build(method, timing=False):
    if method == 'today':
        d, body, sample = BASE
        src = PREAMBLE + d + '\n#define TIMING %d\n#define LOOKUP sampleProjectorShadow\n' % int(timing) + body + sample
    else:
        d, body, sample = DEV
        src = PREAMBLE + d + '\n#define TIMING %d\n' % int(timing)
        if method == 'RVreal':
            d, body, sample = REAL
            src = PREAMBLE + d + '\n#define TIMING %d\n#define LOOKUP sampleProjectorShadow\n' % int(timing) + body + sample
        elif method == 'P2':
            src += '#define LOOKUP sampleProjectorShadow\n' + body + sample
        else:
            rv = sample.replace('float sampleProjectorShadow(', 'float sampleProjectorRV(', 1)
            assert 'stSpotFilterTile(c, grad' in rv
            rv = rv.replace('stSpotFilterTile(c, grad', 'stSpotFilterTileRV(c, grad', 1)
            extra = ''.join('#define %s %s\n' % tuple(d.split('=')) for d in opts.defs.split(',') if d)
            src += extra + '#define g_tile proj_shadow_tile\n#define VARIANT_RV %d\n#define LOOKUP sampleProjectorRV\n' % (1 if method == 'RV' else 2) + body + RV + rv
    return program([(0x8B31, VS), (0x8B30, src + RECEIVER)])


# ------------------------------------------------------------------------------------------------ GL helpers
glGenTextures = fn('glGenTextures', None, I, C.POINTER(U))
glBindTexture = fn('glBindTexture', None, U, U)
glTexImage2D = fn('glTexImage2D', None, U, I, I, I, I, I, U, U, P)
glTexParameteri = fn('glTexParameteri', None, U, U, I)
glActiveTexture = fn('glActiveTexture', None, U)
glGenFramebuffers = fn('glGenFramebuffers', None, I, C.POINTER(U))
glBindFramebuffer = fn('glBindFramebuffer', None, U, U)
glFramebufferTexture2D = fn('glFramebufferTexture2D', None, U, U, U, U, I)
glCheckFramebufferStatus = fn('glCheckFramebufferStatus', U, U)
glViewport = fn('glViewport', None, I, I, I, I)
glDrawArrays = fn('glDrawArrays', None, U, I, I)
glGetUniformLocation = fn('glGetUniformLocation', I, U, C.c_char_p)
glUniform1f = fn('glUniform1f', None, I, C.c_float)
glUniform1i = fn('glUniform1i', None, I, I)
glUniform2f = fn('glUniform2f', None, I, C.c_float, C.c_float)
glUniform3f = fn('glUniform3f', None, I, C.c_float, C.c_float, C.c_float)
glUniform4f = fn('glUniform4f', None, I, C.c_float, C.c_float, C.c_float, C.c_float)
glUniformMatrix4fv = fn('glUniformMatrix4fv', None, I, I, C.c_ubyte, P)
glGetTexImage = fn('glGetTexImage', None, U, I, U, U, P)
glGenVertexArrays = fn('glGenVertexArrays', None, I, C.POINTER(U))
glBindVertexArray = fn('glBindVertexArray', None, U)
glDisable = fn('glDisable', None, U)
glFinish = fn('glFinish', None)
glDeleteTextures = fn('glDeleteTextures', None, I, C.POINTER(U))
TEX = 0x0DE1


def texture(fmt, w, h, data, fmt_in):
    t = U()
    glGenTextures(1, C.byref(t))
    glBindTexture(TEX, t.value)
    glTexImage2D(TEX, 0, fmt, w, h, 0, fmt_in, 0x1406, data.ctypes.data if data is not None else None)
    for pname, value in ((0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F), (0x884C, 0)):
        glTexParameteri(TEX, pname, value)
    return t.value


def free(t):
    glDeleteTextures(1, C.byref(U(t)))


vao = U()
glGenVertexArrays(1, C.byref(vao))
glBindVertexArray(vao.value)


class Target:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.tex = texture(0x822E, w, h, None, 0x1903)
        f = U()
        glGenFramebuffers(1, C.byref(f))
        self.fbo = f.value
        glBindFramebuffer(0x8D40, self.fbo)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, self.tex, 0)
        assert glCheckFramebufferStatus(0x8D40) == 0x8CD5

    def read(self):
        buf = np.empty((self.h, self.w), np.float32)
        glBindTexture(TEX, self.tex)
        glGetTexImage(TEX, 0, 0x1903, 0x1406, buf.ctypes.data)
        return buf


# ------------------------------------------------------------------------------------------------ lamp and tiles
H_L = 2.5
LAMP = np.array([0.0, 0.0, H_L])


def lamp_matrix(fov, n_dummy=None, radius=10.0):
    """bias * perspective * inverse(pose), as spot_matrices: near at the prim's face + 1 cm (a 1 cm prim), far dist + 1.5 r."""
    th = math.tan(0.5 * fov)
    dist = 0.005 / th
    near, far = dist + 0.01, dist + 1.5 * radius
    f = 1.0 / th
    proj = np.array([[f, 0, 0, 0], [0, f, 0, 0], [0, 0, -(far + near) / (far - near), -2 * far * near / (far - near)], [0, 0, -1, 0]])
    bias = np.array([[.5, 0, 0, .5], [0, .5, 0, .5], [0, 0, .5, .5], [0, 0, 0, 1]])
    inv_pose = np.eye(4)
    inv_pose[:3, 3] = -LAMP
    return bias @ proj @ inv_pose


def plate_tile(n, m, fov, plates, boxes=()):
    """Depth tile of a downward lamp: the floor (z = 0), zero-thickness plates (h, nx, ny, c: z = h where nx x + ny y <= c and
    |x|, |y| <= 30 m), and axis-aligned boxes. Rays through the texel centres."""
    th = math.tan(0.5 * fov)
    u = ((np.arange(n) + 0.5) / n * 2 - 1) * th
    out = np.empty((n, n), np.float32)
    for r0 in range(0, n, 256):
        dx, dy = np.meshgrid(u, u[r0:r0 + 256])
        t = np.full(dx.shape, H_L)                         # the floor at t = H (d.z = -1)
        for (h, nx, ny, c, lim) in plates:
            tp = H_L - h
            x, y = dx * tp, dy * tp
            hit = (nx * x + ny * y <= c) & (np.abs(x) <= lim) & (np.abs(y) <= lim)
            t = np.where(hit & (tp < t), tp, t)
        if boxes:
            d = np.stack([dx, dy, -np.ones_like(dx)], -1)
            tb = box_hits(np.broadcast_to(LAMP, d.shape), d, boxes)
            t = np.minimum(t, tb)
        p = np.stack([dx * t, dy * t, H_L - t, np.ones_like(t)], -1)
        s = p @ m.T
        out[r0:r0 + 256] = np.clip(s[..., 2] / s[..., 3], 0.0, 1.0)
    return out


def box_hits(o, d, boxes, t_lo=1e-6, t_hi=np.inf):
    t_best = np.full(d.shape[:-1], np.inf)
    with np.errstate(divide='ignore', invalid='ignore'):
        for lo, hi in boxes:
            a, b = (lo - o) / d, (hi - o) / d
            tn = np.nanmax(np.minimum(a, b), -1)
            tf = np.nanmin(np.maximum(a, b), -1)
            ok = (tf >= tn) & (tf > t_lo) & (tn < t_hi)
            tn = np.maximum(tn, t_lo)
            t_best = np.where(ok & (tn < t_best), tn, t_best)
    return t_best


def set_lookup(prog, tex, m, n, L):
    glActiveTexture(0x84C0)
    glBindTexture(TEX, tex)
    glUniform1i(glGetUniformLocation(prog, b'proj_shadow_atlas'), 0)
    mf = np.ascontiguousarray(m.T, np.float32)
    glUniformMatrix4fv(glGetUniformLocation(prog, b'proj_shadow_matrix'), 1, 0, mf.ctypes.data)
    glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), 0.0, 0.0, float(n), 1.0 / n)
    glUniform2f(glGetUniformLocation(prog, b'proj_shadow_light_size'), L, L)
    glUniform3f(glGetUniformLocation(prog, b'proj_shadow_origin'), *[float(v) for v in LAMP])
    glUniform1f(glGetUniformLocation(prog, b'spot_shadow_offset'), 0.04)
    glUniform1f(glGetUniformLocation(prog, b'spot_shadow_reach'), 128.0)


def emitter(method):
    return opts.today_emitter if method == 'today' else 0.01 * opts.softness


def run_grid(prog, tgt, tex, m, n, L, p0, ax, ay):
    use(prog)
    glBindFramebuffer(0x8D40, tgt.fbo)
    glViewport(0, 0, tgt.w, tgt.h)
    glDisable(0x0B71)
    set_lookup(prog, tex, m, n, L)
    glUniform2f(glGetUniformLocation(prog, b'grid'), float(tgt.w), float(tgt.h))
    for name, v in ((b'p0', p0), (b'ax', ax), (b'ay', ay)):
        glUniform3f(glGetUniformLocation(prog, name), *[float(x) for x in v])
    glDrawArrays(4, 0, 3)
    glFinish()
    return tgt.read()


def seg_frac(t):
    """share of a uniform disc beyond x = t (the reference's emitter is a uniform disc)"""
    t = np.clip(t, -1.0, 1.0)
    return (np.arccos(t) - t * np.sqrt(1 - t * t)) / math.pi


# ------------------------------------------------------------------------------------------------ staircase
def stairs():
    methods = opts.methods.split(',')
    progs = {mth: build(mth) for mth in methods}
    W, Hh = 512, 96
    tgt = Target(W, Hh)
    rows = []
    for fov in [float(x) for x in opts.fovs.split(',')]:
        th = math.tan(0.5 * fov)
        m = lamp_matrix(fov)
        for n in [int(x) for x in opts.sizes.split(',')]:
            texel = 2 * th * H_L / n                 # floor texel (m): the floor is parallel to the image plane
            for ang in [float(x) for x in opts.angles.split(',')]:
                a = math.radians(ang)
                tdir = np.array([math.cos(a), math.sin(a), 0.0])       # along the edge
                nrm = np.array([-math.sin(a), math.cos(a), 0.0])       # across; the plate is on the side nrm . x <= c
                s = opts.plate_h / H_L
                foot = np.array([1.0, 0.6, 0.0])                       # the floor point the edge's shadow passes through
                c_floor = float(nrm @ foot)
                c_plate = c_floor * (1 - s)                            # the plate's edge projects through foot
                tile = plate_tile(n, m, fov, [(opts.plate_h, nrm[0], nrm[1], c_plate, 30.0)])
                tex = texture(0x8CAC, n, n, tile, 0x1902)
                span_along = max(0.3, 40 * texel)                      # at least 40 texels along the edge
                span_across = 8 * texel + 0.02                         # +-4 texels and 1 cm around the edge
                ax_v, ay_v = tdir * span_along, -nrm * span_across   # rows: + = into the shadow
                yy = ((np.arange(Hh) + 0.5) / Hh - 0.5) * span_across   # across coordinate of each row (m), + = shadow side
                imgs = {}
                for mth in methods:
                    L = emitter(mth)
                    img = run_grid(progs[mth], tgt, tex, m, n, L, foot, ax_v, ay_v)
                    imgs[mth] = img
                    # the 50 % crossing of each column; visibility falls from lit (y < 0) to shadow (y > 0)
                    x50, w = [], []
                    for col in range(W):
                        v = img[:, col]
                        def cross(level):
                            idx = np.nonzero(v <= level)[0]
                            if len(idx) == 0 or idx[0] == 0:
                                return np.nan
                            i = idx[0]
                            return yy[i - 1] + (level - v[i - 1]) * (yy[i] - yy[i - 1]) / (v[i] - v[i - 1] if v[i] != v[i - 1] else -1e-9)
                        x50.append(cross(0.5))
                        w.append(cross(0.1) - cross(0.9))
                    x50 = np.array(x50) * 1000
                    if os.environ.get('EDGES_DUMP'):
                        np.savez(os.path.join(os.environ['EDGES_DUMP'], 'c_%s_%g_%d_%g.npz' % (mth, fov, n, ang)), x50=x50, img=img, yy=yy)
                    w = np.array(w) * 1000
                    ok = np.isfinite(x50)
                    dev = x50[ok]
                    if not len(dev):
                        print('fov %.1f N %d angle %g %s: no 50 %% crossing in the scan window (a texel wider than the window), skipped' % (fov, n, ang, mth))
                        continue
                    # reference: the uniform disc emitter of diameter L (analytic); its width for this L
                    s_ = opts.plate_h / H_L
                    # floor point at signed distance y from the true line: the emitter coordinate where the edge cuts: e = -y (1 - s) / s
                    # visibility = share of the disc on the far side; width in y: the 10-90 % span
                    rr = 0.5 * L
                    tt = np.linspace(-1, 1, 2001)
                    vis = 1 - seg_frac(tt)   # visibility vs normalised y (y = tt * rr * s / (1 - s))
                    yref = tt * rr * s_ / (1 - s_)
                    ref_w = 1000 * (np.interp(0.9, vis, yref) - np.interp(0.1, vis, yref))
                    row = dict(kind='stairs', method=mth, fov=fov, N=n, angle=ang, texel_mm=1000 * texel, L=L,
                               bias_mm=float(dev.mean()), rms_mm=float(np.sqrt(np.mean(dev ** 2))),
                               rms_about_mean_mm=float(dev.std()), max_mm=float(np.max(np.abs(dev))),
                               p2p_mm=float(dev.max() - dev.min()), p2p_texels=float((dev.max() - dev.min()) / (1000 * texel)),
                               w10_90_mm=float(np.nanmean(w)), w10_90_texels=float(np.nanmean(w) / (1000 * texel)),
                               ref_w10_90_mm=float(abs(ref_w)), valid=float(ok.mean()))
                    rows.append(row)
                    emit(row)
                oracle_check('stairs fov %g N %d angle %g' % (fov, n, ang), imgs)
                free(tex)
            print('fov %.1f N %d done' % (fov, n), flush=True)
    print('\n| fov | N | texel mm | angle | method | bias mm | RMS mm | max mm | step p-p mm (texels) | 10-90 mm (texels) | reference 10-90 mm |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    for r in rows:
        print('| %.1f | %d | %.1f | %g | %s | %+.2f | %.2f | %.2f | %.2f (%.2f) | %.2f (%.2f) | %.2f |' % (
            r['fov'], r['N'], r['texel_mm'], r['angle'], r['method'], r['bias_mm'], r['rms_mm'], r['max_mm'], r['p2p_mm'],
            r['p2p_texels'], r['w10_90_mm'], r['w10_90_texels'], r['ref_w10_90_mm']))
    return rows


def stair_gates(rows):
    '''PR A's staircase gates on the checkout's shader (RVreal, else the prototype), over the tiles of 256 and up: the worst step is at most
    0.25 texel for 2 to 45 degrees and 0.50 texel below 2 degrees (the traversal's reach RV_S = 24 texels is shorter than a run of a
    shallow edge: the design's audit measured 0.41 at 1.5 degrees, the full matrix 0.49 at fov 3.0, 512, 1.5 degrees; P2: 0.9); where the
    physical width is wider than 0.35 texel the 10-90 %% width is within 5 %% of the reference (the width gate needs --softness 1
    --plate-h 1.2). Tiles under 256 are reported, not gated: their texel is 30 to 140 mm and the normal offset's drift across the
    scan (one texel times sin(theta), the same for P2) is part of the peak to peak.'''
    ok = True
    for mth in ('RVreal', 'RV'):
        rs = [r for r in rows if r['method'] == mth]
        if not rs:
            continue
        g = [r for r in rs if r['N'] >= 256]
        hi = max((r['p2p_texels'] for r in g if r['angle'] >= 2), default=0.0)
        lo = max((r['p2p_texels'] for r in g if r['angle'] < 2), default=0.0)
        small = max((r['p2p_texels'] for r in rs if r['N'] < 256), default=None)
        wid = [abs(r['w10_90_mm'] / r['ref_w10_90_mm'] - 1) for r in g if r['ref_w10_90_mm'] > 0.35 * r['texel_mm']]
        w_ok = (max(wid) <= 0.05) if wid else True
        s_ok = hi <= 0.25 and lo <= 0.50
        ok &= s_ok and w_ok
        print('%s staircase (tiles >= 256): worst step %.3f texel (2-45 deg, gate 0.25), %.3f texel (< 2 deg, gate 0.50): %s; width within 5 %% of the reference where wider than 0.35 texel: %s (worst %s); tiles < 256 (not gated): worst step %s' % (
            mth, hi, lo, 'PASS' if s_ok else 'FAIL', 'PASS' if w_ok else 'FAIL', '%.1f %%' % (100 * max(wid)) if wid else 'n/a',
            '%.3f texel' % small if small is not None else 'n/a'))
    return ok


# ------------------------------------------------------------------------------------------------ the P2 gates on the avatar
def avatar(dx=0.0):
    b = [((-0.04, -0.05, 0.00), (0.30, 0.05, 0.08)), ((-0.04, -0.05, 0.08), (0.06, 0.05, 0.85)),
         ((-0.10, -0.22, 0.85), (0.10, 0.22, 1.45)), ((-0.05, -0.05, 1.45), (0.05, 0.05, 1.52)),
         ((-0.09, -0.08, 1.52), (0.09, 0.08, 1.74)), ((0.40, -0.04, 0.75), (0.42, 0.04, 0.95))]
    return [(np.array(lo) + (dx, 0, 0), np.array(hi) + (dx, 0, 0)) for lo, hi in b]


def disc_points(L, n=1024):
    k = np.arange(n)
    r = np.sqrt((k + 0.5) / n) * 0.5 * L
    phi = k * 2.39996323
    return LAMP + np.stack([r * np.cos(phi), r * np.sin(phi), np.zeros(n)], -1)


def reference(points, L, boxes):
    e = disc_points(L)
    vis = np.empty(len(points))
    for i in range(0, len(points), 64):
        p = points[i:i + 64]
        d = e[None, :, :] - p[:, None, :]
        o = np.broadcast_to(p[:, None, :], d.shape)
        t = box_hits(o, d, boxes, t_lo=1e-6, t_hi=1.0)
        vis[i:i + 64] = np.mean(~np.isfinite(t), 1)
    return vis


def gates():
    """The P2 gates (proj_march_bench.py --edges) applied to RV: toe x50 within 2 mm of the reference, under the toe <= 0.02, toe
    RMS at most P2's + 0.005, shoulder x50 within 2 mm; on a 1.5 rad lamp, tiles 2048/1024/512."""
    methods = [m_ for m_ in opts.methods.split(',') if m_ != 'today']
    progs = {mth: build(mth) for mth in methods}
    W = 512
    tgt = Target(W, 4)
    boxes = avatar()
    fov = 1.5
    m = lamp_matrix(fov, radius=10.0)
    L = 0.01 * opts.softness
    scans = {'toe': (np.array([0.20, 0.0, 0.0]), np.array([0.45, 0.0, 0.0]), 0.285, 0.45, 0),
             'shoulder': (np.array([0.0, 0.30, 0.0]), np.array([0.0, 0.85, 0.0]), 0.30, 0.85, 1)}
    print('\n| tile | scan | method | x50 - ref mm | under toe | RMS vs ref | 10-90 mm |')
    print('|---|---|---|---|---|---|---|')
    allok = True
    for n in (2048, 1024, 512):
        tile = plate_tile(n, m, fov, [], boxes)
        tex = texture(0x8CAC, n, n, tile, 0x1902)
        texel_mm = 1000 * 2 * math.tan(0.75) * H_L / n
        res = {}
        for name, (a, b, lo, hi, axis) in scans.items():
            gimgs = {}
            s = (np.arange(W) + 0.5) / W
            pts = a[None, :] + s[:, None] * (b - a)[None, :]
            ref = reference(pts, L, boxes)
            coord = pts[:, axis]
            for mth in methods:
                img = run_grid(progs[mth], tgt, tex, m, n, L, 0.5 * (a + b), b - a, np.zeros(3))
                gimgs[mth] = img
                v = img.mean(0)
                sel = (coord >= lo) & (coord <= hi)
                vv, cc, rr = v[sel], coord[sel], ref[sel]
                def x_at(vals, level):
                    idx = np.nonzero(vals >= level)[0]
                    if len(idx) == 0 or idx[0] == 0:
                        return np.nan
                    i = idx[0]
                    return cc[i - 1] + (level - vals[i - 1]) * (cc[i] - cc[i - 1]) / (vals[i] - vals[i - 1])
                lit = float(np.interp(0.325, coord, ref)) if name == 'toe' else rr[-8:].mean()
                dark = vv[:8].mean()
                half = lambda vals, d0: x_at(vals, d0 + 0.5 * (lit - d0))
                dx = 1000 * (half(vv, dark) - half(rr, rr[:8].mean()))
                w = 1000 * (x_at(vv, dark + 0.9 * (lit - dark)) - x_at(vv, dark + 0.1 * (lit - dark)))
                rms = float(np.sqrt(np.mean((vv - rr) ** 2)))
                rd = rr[:8].mean()
                wref = 1000 * (x_at(rr, rd + 0.9 * (lit - rd)) - x_at(rr, rd + 0.1 * (lit - rd)))
                res[(name, mth)] = dict(dx=dx, dark=dark, rms=rms, w=w)
                row = dict(kind='gate', method=mth, N=n, texel_mm=texel_mm, scan=name, dx_mm=dx, under=dark, rms=rms, w10_90_mm=w, ref_w10_90_mm=wref)
                emit(row)
                print('| %d | %s | %s | %+.1f | %s | %.3f | %.1f (ref %.1f) |' % (n, name, mth, dx, '%.3f' % dark if name == 'toe' else '', rms, w, wref))
            oracle_check('gates tile %d %s' % (n, name), gimgs)
        free(tex)
        for rvm in ('RV', 'RVreal'):
            if rvm in methods and 'P2' in methods and texel_mm <= 5.0:
                t, sh, p2, p2s = res[('toe', rvm)], res[('shoulder', rvm)], res[('toe', 'P2')], res[('shoulder', 'P2')]
                # PR A's gates (audit 2): the avatar's toe and shoulder x50 within 2 mm of the reference, or no worse than P2's plus 0.1 mm
                # (the 512 shoulder is +3.4 mm for P2 itself); under the toe at most 0.02; the toe RMS at most P2's + 0.005
                ok = (abs(t['dx']) <= max(2.0, abs(p2['dx']) + 0.1) and t['dark'] <= 0.02 and t['rms'] <= p2['rms'] + 0.005
                      and abs(sh['dx']) <= max(2.0, abs(p2s['dx']) + 0.1))
                allok &= ok
                print('%s gates %d: %s' % (rvm, n, 'PASS' if ok else 'FAIL'))
    print('RV GATES (texel <= 5 mm):', 'PASS' if allok else 'FAIL')
    return allok


# ------------------------------------------------------------------------------------------------ GPU time
def look_at(eye, target, fovy, aspect):
    f = target - eye
    f = f / np.linalg.norm(f)
    s = np.cross(f, np.array([0.0, 0.0, 1.0]))
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    view = np.eye(4)
    view[0, :3], view[1, :3], view[2, :3] = s, u, -f
    view[:3, 3] = -view[:3, :3] @ eye
    t = 1.0 / math.tan(0.5 * fovy)
    n_, f_ = 0.1, 100.0
    proj = np.array([[t / aspect, 0, 0, 0], [0, t, 0, 0], [0, 0, -(f_ + n_) / (f_ - n_), -2 * f_ * n_ / (f_ - n_)], [0, 0, -1, 0]])
    return np.linalg.inv(proj @ view)


def gpu_ms(setup, draws=20, rounds=9):
    q = U()
    fn('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
    begin, end = fn('glBeginQuery', None, U, U), fn('glEndQuery', None, U)
    get64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
    samples = []
    for r in range(rounds + 1):
        setup()
        glDrawArrays(4, 0, 3)
        glFinish()
        begin(0x88BF, q.value)
        for _ in range(draws):
            glDrawArrays(4, 0, 3)
        end(0x88BF)
        v = C.c_uint64()
        get64(q.value, 0x8866, C.byref(v))
        if r:
            samples.append(v.value / 1e6 / draws)
    return statistics.median(samples), (max(samples) - min(samples)) / statistics.median(samples)


def timing():
    print('builds running at start:', builds_running() or 'none', flush=True)
    W, Hh = 2560, 1440
    tgt = Target(W, Hh)
    eye, target = np.array([2.0, -2.0, 1.7]), np.array([0.6, 0.6, 0.0])
    inv = look_at(eye, target, math.radians(55), W / Hh)
    inv_f = np.ascontiguousarray(inv.T, np.float32)
    rng = np.random.default_rng(7)
    plates = []
    for i in range(24):   # 24 plate edges (tables, shelves, a railing) at random angles and heights, around the view's floor
        a = rng.uniform(0, math.pi)
        nx, ny = -math.sin(a), math.cos(a)
        cx, cy = rng.uniform(-1.0, 2.5), rng.uniform(-1.0, 2.5)
        plates.append((rng.uniform(0.3, 1.8), nx, ny, nx * cx + ny * cy, 0.25 + 0.5 * rng.uniform()))
    # a plate here is bounded by |x|, |y| <= lim around the origin: offset each one by moving its half-plane only (bounded squares
    # would all sit at the origin), so use small plates centred at (cx, cy) through a shifted half-plane intersection instead
    methods = [m_ for m_ in opts.methods.split(',') if m_ != 'today']
    progs = {mth: build(mth, timing=True) for mth in methods}
    for fov in (1.5, 2.5):
        m = lamp_matrix(fov)
        for n in (2048, 1024, 512):
            tile = plate_tile_boxes(n, m, fov, plates)
            tex = texture(0x8CAC, n, n, tile, 0x1902)
            base = None
            for mth in methods:
                L = emitter(mth)
                def setup(prog=progs[mth], L=L):
                    use(prog)
                    glBindFramebuffer(0x8D40, tgt.fbo)
                    glViewport(0, 0, W, Hh)
                    glDisable(0x0B71)
                    set_lookup(prog, tex, m, n, L)
                    glUniform2f(glGetUniformLocation(prog, b'grid'), float(W), float(Hh))
                    glUniformMatrix4fv(glGetUniformLocation(prog, b'cam_inv'), 1, 0, inv_f.ctypes.data)
                    glUniform3f(glGetUniformLocation(prog, b'cam_pos'), *[float(v) for v in eye])
                    glUniform1i(glGetUniformLocation(prog, b'rv_off'), opts.rv_off)
                setup()
                glDrawArrays(4, 0, 3)
                glFinish()
                img = tgt.read()
                pen = float(np.mean((img > 0.0) & (img < 1.0)))
                ms, spread = gpu_ms(setup)
                base = ms if mth == 'P2' else base
                row = dict(kind='time', method=mth, fov=fov, N=n, ms=ms, spread=spread, partial_frac=pen,
                           shadow_frac=float(np.mean(img < 1.0)), vs_p2=(ms / base - 1) if base else None, builds=builds_running())
                emit(row)
                print('fov %.1f N %d %-6s %.4f ms (spread %.0f %%) %s partial %.2f %% shadowed %.1f %% builds %s' % (
                    fov, n, mth, ms, 100 * spread, ('%+.1f %% vs P2' % (100 * row['vs_p2'])) if row['vs_p2'] is not None else '',
                    100 * pen, 100 * row['shadow_frac'], row['builds'] or 'none'), flush=True)
            free(tex)
    print('GL:', get_string(0x1F01).decode(), get_string(0x1F00).decode())


def plate_tile_boxes(n, m, fov, plates):
    """the timing scene: square plates of half-size lim centred at the point of their edge line nearest (cx, cy), clipped by the
    half-plane (so one straight edge per plate at its own angle), plus the avatar"""
    th = math.tan(0.5 * fov)
    u = ((np.arange(n) + 0.5) / n * 2 - 1) * th
    out = np.empty((n, n), np.float32)
    boxes = avatar(0.6)
    for r0 in range(0, n, 256):
        dx, dy = np.meshgrid(u, u[r0:r0 + 256])
        t = np.full(dx.shape, H_L)
        for (h, nx, ny, c, lim) in plates:
            tp = H_L - h
            x, y = dx * tp, dy * tp
            px, py = c * nx, c * ny         # the edge line's point nearest the origin
            cx, cy = px - 0.5 * lim * nx, py - 0.5 * lim * ny
            hit = (nx * x + ny * y <= c) & (np.abs(x - cx) <= lim) & (np.abs(y - cy) <= lim)
            t = np.where(hit & (tp < t), tp, t)
        d = np.stack([dx, dy, -np.ones_like(dx)], -1)
        t = np.minimum(t, box_hits(np.broadcast_to(LAMP, d.shape), d, boxes))
        p = np.stack([dx * t, dy * t, H_L - t, np.ones_like(t)], -1)
        s = p @ m.T
        out[r0:r0 + 256] = np.clip(s[..., 2] / s[..., 3], 0.0, 1.0)
    return out


def circle():
    """A round zero-thickness plate (radius R at --plate-h) whose shadow is centred on the floor point (1.0, 0.6): the radial 50 %
    crossing at 720 angles against the true circle (the point projection): mean, RMS and max deviation and the peak-to-peak.
    Tests what the edge reconstruction does to a curved silhouette (a head is about 0.09 m, a table 0.3 m)."""
    methods = opts.methods.split(',')
    progs = {mth: build(mth) for mth in methods}
    G = 1024
    tgt = Target(G, G)
    s_ = opts.plate_h / H_L
    centre = np.array([1.0, 0.6, 0.0])
    print('\n| fov | N | texel mm | R mm (shadow) | method | bias mm | RMS mm | max mm | p-p mm (texels) | stair RMS mm | stair p-p mm (texels) |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    for fov in [float(x) for x in opts.fovs.split(',')]:
        th = math.tan(0.5 * fov)
        m = lamp_matrix(fov)
        for n in [int(x) for x in opts.sizes.split(',')]:
            texel = 2 * th * H_L / n
            for R in [float(x) for x in opts.radii.split(',')]:
                Rs = R / (1 - s_)                       # the shadow's radius on the floor
                pc = centre * (1 - s_)                  # the plate's centre (x, y) at its height
                u = ((np.arange(n) + 0.5) / n * 2 - 1) * th
                tile = np.empty((n, n), np.float32)
                tp = H_L - opts.plate_h
                for r0 in range(0, n, 256):
                    dx, dy = np.meshgrid(u, u[r0:r0 + 256])
                    hit = (dx * tp - pc[0]) ** 2 + (dy * tp - pc[1]) ** 2 <= R * R
                    t = np.where(hit, tp, H_L)
                    pp = np.stack([dx * t, dy * t, H_L - t, np.ones_like(t)], -1) @ m.T
                    tile[r0:r0 + 256] = np.clip(pp[..., 2] / pp[..., 3], 0, 1)
                tex = texture(0x8CAC, n, n, tile, 0x1902)
                half = Rs + max(6 * texel, 0.02)
                imgs = {}
                for mth in methods:
                    img = run_grid(progs[mth], tgt, tex, m, n, emitter(mth), centre, np.array([2 * half, 0, 0]), np.array([0, 2 * half, 0]))
                    imgs[mth] = img
                    ang = np.linspace(0, 2 * math.pi, 720, endpoint=False)
                    rr = np.linspace(max(Rs - 6 * texel, 0.0), Rs + 6 * texel, 400)
                    dev, ang_ok = [], []
                    for a in ang:
                        px = (np.cos(a) * rr / (2 * half) + 0.5) * G - 0.5
                        py = (np.sin(a) * rr / (2 * half) + 0.5) * G - 0.5
                        x0, y0 = np.clip(np.floor(px).astype(int), 0, G - 2), np.clip(np.floor(py).astype(int), 0, G - 2)
                        fx, fy = px - x0, py - y0
                        v = (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy) + img[y0 + 1, x0] * (1 - fx) * fy
                             + img[y0 + 1, x0 + 1] * fx * fy)
                        idx = np.nonzero(v >= 0.5)[0]       # dark inside, lit outside
                        if len(idx) == 0 or idx[0] == 0:
                            continue
                        i = idx[0]
                        dev.append(rr[i - 1] + (0.5 - v[i - 1]) * (rr[i] - rr[i - 1]) / (v[i] - v[i - 1]) - Rs)
                        ang_ok.append(a)
                    dev = 1000 * np.array(dev)
                    # the staircase part: the deviation less its low orders (harmonics 0..3 over the angle: the circle's size, a shift,
                    # an ellipse), which a texel-scale pattern does not have
                    ang_ok = np.array(ang_ok)
                    basis = np.stack([np.ones_like(ang_ok)] + [f(k * ang_ok) for k in (1, 2, 3) for f in (np.cos, np.sin)], 1)
                    coef, *_ = np.linalg.lstsq(basis, dev, rcond=None)
                    hp = dev - basis @ coef
                    if os.environ.get('EDGES_DUMP'):
                        np.savez(os.path.join(os.environ['EDGES_DUMP'], 'circ_%s_%g_%d_%g.npz' % (mth, fov, n, R)), dev=dev, img=img, ang=ang_ok, hp=hp)
                    row = dict(kind='circle', method=mth, fov=fov, N=n, texel_mm=1000 * texel, R=R, Rs_mm=1000 * Rs,
                               bias_mm=float(dev.mean()), rms_mm=float(np.sqrt(np.mean(dev ** 2))), max_mm=float(np.abs(dev).max()),
                               p2p_mm=float(dev.max() - dev.min()), p2p_texels=float((dev.max() - dev.min()) / (1000 * texel)), valid=len(dev) / 720,
                               stair_rms_mm=float(hp.std()), stair_p2p_mm=float(hp.max() - hp.min()), stair_p2p_texels=float((hp.max() - hp.min()) / (1000 * texel)))
                    emit(row)
                    print('| %.1f | %d | %.1f | %.0f | %s | %+.2f | %.2f | %.2f | %.2f (%.2f) | %.2f | %.2f (%.2f) |' % (fov, n, row['texel_mm'], row['Rs_mm'], mth,
                          row['bias_mm'], row['rms_mm'], row['max_mm'], row['p2p_mm'], row['p2p_texels'], row['stair_rms_mm'], row['stair_p2p_mm'],
                          row['stair_p2p_texels']), flush=True)
                oracle_check('circle fov %g N %d R %g' % (fov, n, R), imgs)
                free(tex)


def grazing():
    """Acne check (proj_march_bench.py --grazing's case): a plane through the floor point (0.6, 0, 0) tilted by phi about the y axis
    is the only surface; every receiver on it is lit, so a correct lookup reads 1. Mean and worst darkening and the share of receivers
    under 0.98, for P2 and RV, fov 1.5 lamp overhead, tiles 2048/1024/512."""
    methods = [m_ for m_ in opts.methods.split(',') if m_ in ('P2', 'RV', 'RVreal')]
    progs = {mth: build(mth) for mth in methods}
    W, Hh = 512, 8
    tgt = Target(W, Hh)
    fov = 1.5
    m = lamp_matrix(fov)
    th = math.tan(0.5 * fov)
    print('\n| tile | phi | method | mean darkening | max darkening | receivers < 0.98 |')
    print('|---|---|---|---|---|---|')
    for n in (2048, 1024, 512):
        for phi in (0, 45, 60, 75, 85):
            sp, cp = math.sin(math.radians(phi)), math.cos(math.radians(phi))
            x0 = 0.6
            u = ((np.arange(n) + 0.5) / n * 2 - 1) * th
            dx, dy = np.meshgrid(u, u)
            with np.errstate(divide='ignore', invalid='ignore'):
                t = (cp * H_L - sp * x0) / (cp - sp * dx)      # the ray (dx t, dy t, H - t) meets sin(phi) (x - x0) + cos(phi) z = 0
            hit = np.isfinite(t) & (t > 0)
            pp = np.stack([dx * t, dy * t, H_L - t, np.ones_like(t)], -1) @ m.T
            z = np.where(hit, pp[..., 2] / pp[..., 3], 1.0).astype(np.float32)
            tex = texture(0x8CAC, n, n, np.clip(z, 0, 1), 0x1902)
            hw = min(0.3, 1.0 * cp / sp) if sp > 1e-6 else 0.3
            a = np.array([x0 - hw * cp, 0.0, hw * sp])
            b = np.array([x0 + hw * cp, 0.0, -hw * sp])
            for mth in methods:
                prog = progs[mth]
                use(prog)
                glUniform3f(glGetUniformLocation(prog, b'recv_norm'), sp, 0.0, cp)
                v = run_grid(prog, tgt, tex, m, n, emitter(mth), 0.5 * (a + b), b - a, np.zeros(3)).mean(0)
                glUniform3f(glGetUniformLocation(prog, b'recv_norm'), 0.0, 0.0, 0.0)
                d = 1.0 - v
                emit(dict(kind='grazing', method=mth, N=n, phi=phi, mean=float(d.mean()), max=float(d.max()), frac=float(np.mean(v < 0.98))))
                print('| %d | %d | %s | %.4f | %.3f | %.1f %% |' % (n, phi, mth, d.mean(), d.max(), 100 * np.mean(v < 0.98)), flush=True)
            free(tex)


print('GL:', get_string(0x1F01).decode(), flush=True)
if opts.grazing:
    grazing()
if opts.circle:
    circle()
RESULT = []
if opts.stairs:
    rows_ = stairs()
    if not opts.p2_regime:
        RESULT.append(stair_gates(rows_))
if opts.gates:
    RESULT.append(gates())
if opts.time:
    timing()
RESULT.append(oracle_report())
sys.exit(0 if all(RESULT) else 1)
