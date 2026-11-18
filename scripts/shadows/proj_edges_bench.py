#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Projector shadow edges: the bench of the lamp lookup's edges, acne and cost (offline GL, no viewer).

The shader sources of the baselines come from git (--repo and the refs below), the checkout's own shader from its working tree:
  today    the release lookup before P2 (--baseline-ref, default 0991e142f4: mean-blocker PCSS, 4 cm normal offset, prim emitter)
  P2       the devel lookup of #527 (--ref, default 475124a620: nearest blocker, 1-texel normal offset and floor, 4 cm cap)
  release  the lookup this branch replaces (--release-ref, default 3dcac6a5b5: #552, P2 plus the edge reconstruction, a 1 cm x
           softness lamp)
  L1       the checkout's shadowStochasticUtil.glsl as the viewer compiles it (its G1 block and sampleProjectorShadow): lamp shadows L1,
           the search with the receiver's own texels, the filter at the physical radius between 1.5 and 12 texels, no edge
           reconstruction, no 4 cm cap.
Every method but today and release is run with the same lamp, --lamp-base (default: the header's kProjectorLamp, 4.5 cm) times --softness (0.23): the lookups are compared
on one physical lamp; release keeps the 1 cm x softness it ships with (--release-emitter-base), today its prim emitter.

Scene for the staircase (--stairs): a projector at H = 2.5 m looking straight down (fov --fovs), a zero-thickness plate at height
--plate-h whose straight edge makes an angle with the tile's u axis (--angles); the receiver is the floor. The true 50 % contour is the
point projection of the edge (exact for a disc emitter centred on the projection centre); the reference of the whole profile is the
disc emitter's coverage, analytic. Per scan: the 50 % contour of the lookup across the edge at 512 positions along it; its deviation
from the true line (mean = bias, RMS and max about the true line, peak-to-peak = step amplitude, all in mm), and the 10-90 % width (mm),
mean over the positions. Gate (L1, tiles of 256 and up, 2 degrees and up): the 10-90 % width within 20 % of the analytic one wherever
the physical radius lies between the filter's floor and its cap (--softness 1 --plate-h 1.2 puts most scans there); the steps are
reported, not gated (a soft edge of at least 1.5 texels hides them).

Scene for the avatar gates (--gates): the avatar boxes of scripts/shadows/proj_march_bench.py (toe and shoulder edges) under a
1.5 rad lamp, tiles 2048/1024/512, the lamp above. Gates for L1 (the lamp-shadow study's L1 row), on tiles whose texel is at most 5 mm:
toe and shoulder x50 within 2 mm of the reference or no worse than P2's + 0.1 mm, the value under the toe at most 0.02, the shoulder's
10-90 % width within 20 % of the reference. The toe's RMS is reported: a contact is narrower than the 1.5-texel floor, which draws it
wider than P2's 1-texel floor did.

--time: GPU ms of a 2560x1440 view of a floor under a wide lamp with 24 plates at various angles and heights (the edge pixels a real
room has more of than one avatar), plus the avatar (timer queries, median of 9 x 20); the run says which builds were running.
--circle: round plates (a head, 0.09 m; a table, 0.3 m): the 50 % contour against the true circle, and its staircase part (the
deviation less its harmonics 0..3); reported, not gated.
--grazing: a tilted lit plane, the acne check (at 85 degrees this plane faces away from the lamp: dark is right).

    python scripts/shadows/proj_edges_bench.py --stairs --gates --circle --grazing --time --out results.jsonl
    python scripts/shadows/proj_edges_bench.py --stairs --softness 1 --plate-h 1.2 --fovs 2.0 --sizes 1024,2048    (the width gate)
Needs numpy and an OpenGL 4.x driver on Windows (the repo's scripts/perf/gl_test_context.py, read from --ref). Exit code 1 when a
gate fails; --time, --circle and --grazing are reported, not gated (GPU timing spreads about 7 % run to run, more while a build runs).
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
ap.add_argument('--repo', default=str(HERE.parents[1]), help='the checkout: L1 compiles its working-tree shaders; git refs are read from it')
ap.add_argument('--ref', default='475124a620', help='git ref of the devel util of #527 (P2); pinned so P2 stays P2')
ap.add_argument('--baseline-ref', default='0991e142f4')
ap.add_argument('--release-ref', default='3dcac6a5b5', help='git ref of the lookup L1 replaces (release with #552: the edge reconstruction)')
ap.add_argument('--lamp-base', type=float, default=None,
                help='m: the lamp base for every method but today and release (default: the one L1 ships, llprojectorpool::kProjectorLamp, '
                     'read from the llprojectorshadowpool.h of --repo)')
ap.add_argument('--release-emitter-base', type=float, default=0.01, help='m: the lamp base the release lookup ships with')
ap.add_argument('--stairs', action='store_true')
ap.add_argument('--gates', action='store_true')
ap.add_argument('--grazing', action='store_true', help='acne check: a tilted lit plane (P2, release and L1)')
ap.add_argument('--circle', action='store_true', help='a round plate (radius --radii): the 50 %% contour against the true circle')
ap.add_argument('--radii', default='0.09,0.3')
ap.add_argument('--time', action='store_true')
ap.add_argument('--fovs', default='2.0,2.5,3.0')
ap.add_argument('--sizes', default='256,512,1024,2048')
ap.add_argument('--angles', default='0.5,1,1.5,2,5,10,20,30,45')
ap.add_argument('--plate-h', type=float, default=0.75)
ap.add_argument('--softness', type=float, default=0.23)
ap.add_argument('--today-emitter', type=float, default=0.02, help="today's prim emitter (m): 2 cm, the clamp's sharpest")
ap.add_argument('--methods', default='today,P2,release,L1')
ap.add_argument('--out', default=None)
opts = ap.parse_args()
REPO = Path(opts.repo)
if opts.lamp_base is None:  # the header's value, as lamp_filter_twin.py reads it: the bench cannot drift from what ships
    opts.lamp_base = float(re.search(r'constexpr LampFilter kProjectorLamp\{\s*([0-9.]+)f',
                                     (REPO / 'indra/newview/llprojectorshadowpool.h').read_text(encoding='utf-8')).group(1))


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
    """the working tree's own shaders, as sources() reads them from git (L1: the code the viewer compiles)"""
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
RELEASE = sources(opts.release_ref)
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
    d, body, sample = {'today': BASE, 'P2': DEV, 'release': RELEASE, 'L1': REAL}[method]
    src = PREAMBLE + d + '\n#define TIMING %d\n#define LOOKUP sampleProjectorShadow\n' % int(timing) + body + sample
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
    if method == 'today':
        return opts.today_emitter
    return (opts.release_emitter_base if method == 'release' else opts.lamp_base) * opts.softness


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
    '''L1's staircase gate on the checkout's shader, over the tiles of 256 and up at 2 degrees and more: the 10-90 % width within 20 % of
    the analytic one wherever the physical radius lies between the filter's floor (1.5 texels: a 10-90 % width of about 2 texels) and its
    cap (12 texels). The filter's weights draw an edge about 16 % narrower than a uniform disc of the same radius. The steps (peak to peak
    of the 50 % contour) are reported, not gated: L1 draws no edge narrower than its floor, which hides them.'''
    rs = [r for r in rows if r['method'] == 'L1']
    if not rs:
        return True
    g = [r for r in rs if r['N'] >= 256 and r['angle'] >= 2]
    wid = [abs(r['w10_90_mm'] / r['ref_w10_90_mm'] - 1) for r in g if 2.2 * r['texel_mm'] < r['ref_w10_90_mm'] < 16.0 * r['texel_mm']]
    w_ok = (max(wid) <= 0.20) if wid else True
    if not wid:
        print('L1: the width gate is NOT EXERCISED (no scan with a physical width between the floor and the cap: --softness 1 --plate-h 1.2 --fovs 2.0 --sizes 1024,2048)')
    steps = [r['p2p_texels'] for r in g]
    print('L1 staircase (tiles >= 256, 2 degrees and up): width within 20 %% of the reference between the floor and the cap: %s (worst %s, %d scans); steps (reported) up to %s texel' % (
        'PASS' if w_ok else 'FAIL', '%.1f %%' % (100 * max(wid)) if wid else 'n/a', len(wid), '%.2f' % max(steps) if steps else 'n/a'))
    return w_ok


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
    """L1's gates: toe and shoulder x50 within 2 mm of the reference (or no worse than P2's + 0.1 mm), under the toe <= 0.02, the
    shoulder's 10-90 % width within 20 % of the reference; on a 1.5 rad lamp, tiles 2048/1024/512; every method on the same lamp."""
    methods = [m_ for m_ in opts.methods.split(',') if m_ != 'today']
    progs = {mth: build(mth) for mth in methods}
    W = 512
    tgt = Target(W, 4)
    boxes = avatar()
    fov = 1.5
    m = lamp_matrix(fov, radius=10.0)
    L = opts.lamp_base * opts.softness
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
                res[(name, mth)] = dict(dx=dx, dark=dark, rms=rms, w=w, wref=wref)
                row = dict(kind='gate', method=mth, N=n, texel_mm=texel_mm, scan=name, dx_mm=dx, under=dark, rms=rms, w10_90_mm=w, ref_w10_90_mm=wref)
                emit(row)
                print('| %d | %s | %s | %+.1f | %s | %.3f | %.1f (ref %.1f) |' % (n, name, mth, dx, '%.3f' % dark if name == 'toe' else '', rms, w, wref))
        free(tex)
        if 'L1' in methods and 'P2' in methods and texel_mm <= 5.0:
            t, sh, p2, p2s = res[('toe', 'L1')], res[('shoulder', 'L1')], res[('toe', 'P2')], res[('shoulder', 'P2')]
            # the avatar's toe and shoulder x50 within 2 mm of the reference, or no worse than P2's plus 0.1 mm (the 512 shoulder is
            # +3.4 mm for P2 itself); under the toe at most 0.02; the shoulder's width within 20 % of physics
            ok = (abs(t['dx']) <= max(2.0, abs(p2['dx']) + 0.1) and t['dark'] <= 0.02 and abs(sh['dx']) <= max(2.0, abs(p2s['dx']) + 0.1)
                  and abs(sh['w'] / sh['wref'] - 1.0) <= 0.20)
            allok &= ok
            print('L1 gates %d: %s (shoulder width %.0f %% of the reference; toe RMS %.3f, P2 %.3f, reported)' % (
                n, 'PASS' if ok else 'FAIL', 100 * sh['w'] / sh['wref'], t['rms'], p2['rms']))
    print('L1 GATES (texel <= 5 mm):', 'PASS' if allok else 'FAIL')
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
    # 24 plates, each a square with one straight edge at a random angle, offset and height (see plate_tile_boxes)
    methods = [m_ for m_ in opts.methods.split(',') if m_ != 'today']
    methods.sort(key=lambda m_: m_ != 'release')  # the release lookup first: the others are timed against it
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
                setup()
                glDrawArrays(4, 0, 3)
                glFinish()
                img = tgt.read()
                pen = float(np.mean((img > 0.0) & (img < 1.0)))
                ms, spread = gpu_ms(setup)
                base = ms if mth == 'release' else base
                row = dict(kind='time', method=mth, fov=fov, N=n, ms=ms, spread=spread, partial_frac=pen,
                           shadow_frac=float(np.mean(img < 1.0)), vs_release=(ms / base - 1) if base else None, builds=builds_running())
                emit(row)
                print('fov %.1f N %d %-7s %.4f ms (spread %.0f %%) %s partial %.2f %% shadowed %.1f %% builds %s' % (
                    fov, n, mth, ms, 100 * spread, ('%+.1f %% vs release' % (100 * row['vs_release'])) if row['vs_release'] is not None else '',
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
    How the lookups draw a curved silhouette (a head is about 0.09 m, a table 0.3 m)."""
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
                free(tex)


def grazing():
    """Acne check (proj_march_bench.py --grazing's case): a plane through the floor point (0.6, 0, 0) tilted by phi about the y axis
    is the only surface; every receiver on it is lit, so a correct lookup reads 1. Mean and worst darkening and the share of receivers
    under 0.98, for P2, release and L1, fov 1.5 lamp overhead, tiles 2048/1024/512."""
    methods = [m_ for m_ in opts.methods.split(',') if m_ in ('P2', 'release', 'L1')]
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
    RESULT.append(stair_gates(stairs()))
if opts.gates:
    RESULT.append(gates())
if opts.time:
    timing()
sys.exit(0 if all(RESULT) else 1)
