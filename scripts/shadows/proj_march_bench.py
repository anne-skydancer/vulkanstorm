#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Projector shadows: today's PCSS lookup, the P2 fixes, the P1 inset and a sun-style march, against an analytic area light.

Offline study bench (no viewer). It compiles the shader code under test from a checkout (--repo, default the checkout this
script lives in): from shadowStochasticUtil.glsl the G1 block, stSpotGradient, sampleProjectorShadow, stIGN, stNoise,
stReference and stMarch (the sun's march, unchanged), and from shadowUtil.glsl the SHADOW_COMMON helpers
(shadowNormalOffsetPos and friends; shadowReceiverGeometry in its SHADOW_COMMON_NO_DEPTH form, so the receiver normal is
the given one). Three prototypes are written here, never in the viewer:

  P2     sampleProjectorP2: the normal offset is one texel along the geometric normal scaled by sin(theta) (the sun's
         shadowNormalOffsetPos) instead of 4 cm, and the filter radius comes from the blocker nearest the receiver
         (stSpotFilterTileNear: the G1 filter with z_near = max over blocking texels instead of the mean).
  MARCH  sampleProjectorMarch: 8 rays from the receiver to points of the emitter disc (a Vogel spiral rotated per
         pixel and frame by stNoise, as stSunRays), each marched by the sun's stMarch through the tile; the reach is
         the sun's footprint rule (the ray's lateral travel at the receiver at most SHADOW_FOOTPRINT_M) and the near
         plane; steps = clamp(ceil(texels travelled), 2, steps_max).
  inset  P1: the same lookups on a symmetric crop of the lamp frustum that holds only the avatar (N_inset), so that
         min(static, inset) = inset on the floor of this scene (the static tile holds only the floor).

Scene: projection centre 2.5 m above the floor looking down (fov 1.5 rad), an avatar-like caster: a foot on the floor
(toe at x = 0.30 m), shin, torso (shoulders at 1.45 m, y = +-0.22), neck, head, and a flat hand at 0.75-0.95 m. The
reference is the visibility of a disc emitter of diameter L at the projection centre (the emitter PCSS's formula assumes),
ray cast in numpy against the boxes (1024 points of the disc, exact geometry, not a one-layer map).

Modes:
  --edges     edge widths (10-90 %), umbra/penumbra lengths and RMS error against the reference, for emitters 3/6/15 cm and
              tiles 2048/1024/512 (+ inset 1024 and 512); the march converged (64 frames) and its grain after 1, 4, 8 frames
              and in the EMA(1/8) steady state.
  --time      GPU ms of a 2560x1440 view of the floor, every pixel a floor receiver inside the lamp frustum (timer queries,
              median of 9 x 20 draws) for each method, tile and emitter; reported per lit megapixel per lamp.
  --grazing   acne check: a lit plane tilted 0-88 degrees to the lamp, scanned with its own normal (a correct lookup reads 1.0)
  --temporal  a walking avatar (1.4 m/s at 60 fps): the march with the sun's temporal resolve (EMA 1/8, 3x3 min/max clamp,
              static camera so no reprojection), against the instantaneous reference: lag and ghost of the toe and shoulder edges.

    python scripts/shadows/proj_march_bench.py --edges [--out edges.jsonl]
    python scripts/shadows/proj_march_bench.py --time [--out time.jsonl]
    python scripts/shadows/proj_march_bench.py --temporal [--out temporal.jsonl]
Needs numpy and an OpenGL 4.x driver on Windows (scripts/perf/gl_test_context.py). Plain python, not -I (numpy is in the user site).

Projector shadow sharpness P2 (branch codex/shadow-sharpness-p2): on a checkout of that branch the working checkout's
sampleProjectorShadow IS P2. On a later checkout 'P2 real' is the CURRENT lookup: since lamp shadows L1 the search with the receiver's own
texels and the filter at the physical radius between 1.5 and 12 texels (no edge reconstruction, no 4 cm cap), so its difference to the
prototype and the P2 gates here are no longer the P2 claims; L1's gates live in scripts/shadows/proj_edges_bench.py and
scripts/shadows/lamp_filter_twin.py.
'Today's lookup' (PCSS today), the tiles' G1 filter of the prototypes and the prototype P2 are compiled from the BASELINE util
(--baseline-ref, default 0991e142f4, the release tip before P2, whose util equals devel's 5a091b9559; the march and the inset variants also use it). The bench adds the method 'P2 real': the working checkout's shadowStochasticUtil.glsl compiled as it is
(its own G1 block, sampleProjectorShadow, proj_shadow_origin), which must match the prototype. --edges prints the real shader's
difference to the prototype and the P2 gates (toe x50 within 2 mm of the reference, the value under the toe at most 0.02, the
toe RMS no worse than today's, shoulder x50 within 2 mm); --time gives the lookup cost against today's.
"""
import argparse
import json
import math
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--repo', default=str(HERE.parents[1]))
ap.add_argument('--baseline-ref', default='0991e142f4', help="git ref of the util before P2 (today's lookup and the prototype)")
ap.add_argument('--no-march', action='store_true', help='skip the march (the slow part of --edges and --time)')
ap.add_argument('--edges', action='store_true')
ap.add_argument('--time', action='store_true')
ap.add_argument('--temporal', action='store_true')
ap.add_argument('--out', default=None)
ap.add_argument('--lamp-h', type=float, default=2.5)
ap.add_argument('--fov', type=float, default=1.5)
ap.add_argument('--rows', type=int, default=32, help='--edges: noise rows per scan position')
ap.add_argument('--frames', type=int, default=64)
ap.add_argument('--emitters', default='0.03,0.06,0.15')
ap.add_argument('--sizes', default='2048,1024,512')
ap.add_argument('--grazing', action='store_true', help='acne check: a lit plane tilted 60-88 degrees to the lamp, scanned with its own normal (should read 1.0 everywhere)')
opts = ap.parse_args()
REPO = Path(opts.repo)
UTIL = REPO / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
COMMON = REPO / 'indra/newview/app_settings/shaders/class1/deferred/shadowUtil.glsl'
CONTEXT = REPO / 'scripts/perf/gl_test_context.py'
sys.argv = [sys.argv[0]]
exec(compile(CONTEXT.read_text(), str(CONTEXT), 'exec'))  # window, context, fn(), program(), use, U, I, P, C

OUT = open(opts.out, 'a') if opts.out else None


def emit(row):
    if OUT:
        OUT.write(json.dumps(row) + '\n')
        OUT.flush()


def builds_running():
    names = []
    for exe in ('cl.exe', 'link.exe', 'MSBuild.exe'):
        r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ' + exe, '/FO', 'CSV', '/NH'], capture_output=True, text=True)
        if exe.lower() in r.stdout.lower():
            names.append(exe)
    return names


# ---------------------------------------------------------------- shader sources
real_util = UTIL.read_text(encoding='utf-8')
util = subprocess.run(['git', '-C', str(REPO), 'show', '%s:%s' % (opts.baseline_ref, UTIL.relative_to(REPO).as_posix())],
                      capture_output=True, check=True).stdout.decode('utf-8')   # the baseline: today's lookup, the prototype's G1
common = COMMON.read_text(encoding='utf-8')


def func(text, sig):
    i = text.index(sig)
    j = text.index('\n}\n', i)
    return text[i:j + 3]


def section(text, begin, end):
    i = text.index('\n', text.index(begin)) + 1
    return text[i:text.index(end, i)]


defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define (ST_)', l))
g1 = section(util, '// <G1-BEGIN>', '// <G1-END>')
# P2a: the filter radius from the blocker nearest the receiver (largest blocking map depth) instead of the mean
near = g1[g1.index('float stSpotFilterTile('):]
near = near.replace('float stSpotFilterTile(', 'float stSpotFilterTileNear(', 1)
near = near.replace('    float n_blk = 0.0;\n', '    float n_blk = 0.0;\n    float z_near = 0.0;\n', 1)
near = near.replace('        n_blk += blk.x + blk.y + blk.z + blk.w;\n',
                    '        n_blk += blk.x + blk.y + blk.z + blk.w;\n'
                    '        vec4 bz = blk * z;\n        z_near = max(z_near, max(max(bz.x, bz.y), max(bz.z, bz.w)));\n', 1)
assert '(c.z - z_sum / n_blk)' in near
near = near.replace('(c.z - z_sum / n_blk)', '(c.z - z_near)', 1)

common_fns = ''.join(func(common, s) for s in (
    'vec3 shadowRow(', 'vec2 shadowUVScale(', 'float shadowDepthScale(', 'float shadowTexelMetres(',
    'vec2 shadowPlaneGradient(', 'float shadowWorldBias(', 'vec3 shadowNormalOffsetPos(', 'vec3 shadowReceiverGeometry('))
st_fns = ''.join(func(util, s) for s in (
    'float stIGN(', 'vec2 stNoise()', 'vec2 stSpotGradient(', 'float stReference(', 'float stMarch('))
sample_today = func(util, 'float sampleProjectorShadow(')

PREAMBLE = '''#version 400
#define SHADOW_COMMON_NO_DEPTH 1
#define SHADOW_BIAS_TEXELS 1.0
#define SHADOW_NORMAL_OFFSET_TEXELS 1.0
#define SHADOW_PLANE_BIAS_MAX_TEXELS 8.0
#define SHADOW_GRAD_MAX 10000.0
#define SHADOW_FOOTPRINT_M 0.25
''' + defines + '''
uniform int shadow_st_frame;
uniform float spot_shadow_offset;
uniform float spot_shadow_reach;
uniform vec3 proj_light_pos;   // the projection centre (view space): the emitter disc's centre
uniform int march_steps_max;
'''

PROTO = r'''
// P2 (prototype): 1 texel normal offset along the geometric normal (the sun's rule), nearest-blocker filter radius
float sampleProjectorP2(vec3 pos, vec3 norm, vec2 pos_screen)
{
    mat4 m = proj_shadow_matrix;
    vec4 sr = m * vec4(pos, 1.0);
    if (sr.w <= 0.0) return 1.0;
    StSpot sp = stSpotSetup(m);
    vec2 res = vec2(proj_shadow_tile.z);
    float texel_m = sr.w / min(sp.k.x * res.x, sp.k.y * res.y);
    float sin_theta;
    vec3 ng = shadowReceiverGeometry(pos_screen, pos, norm, normalize(proj_light_pos - pos), sin_theta);
    pos = shadowNormalOffsetPos(pos, ng, sin_theta, texel_m);
    vec4 s0 = m * vec4(pos, 1.0);
    if (s0.w <= 0.0 || any(greaterThan(abs(s0.xyz / s0.w - 0.5), vec3(0.5)))) return 1.0;
    vec3 c = s0.xyz / s0.w;
    float z_tex = sp.c1 / (min(sp.k.x * res.x, sp.k.y * res.y) * s0.w);
    vec2 pen = (0.5 / sp.c1) * proj_shadow_light_size * sp.k * res;
    vec2 kmax = clamp(ST_SPOT_RADIUS_MAX_M * sp.k * res / s0.w, vec2(ST_SPOT_KERNEL_MIN), vec2(ST_SPOT_KERNEL_TEXELS_MAX));
    vec2 grad = stSpotGradient(m, pos, ng, c);
    return stSpotFilterTileNear(c, grad, SHADOW_BIAS_TEXELS * z_tex, z_tex, pen, kmax);
}

// MARCH (prototype): the sun's formula on the projector's tile. 8 rays to the emitter disc (diameters
// proj_shadow_light_size along the tile's u and v axes, centred on the projection centre), Vogel spiral rotated by
// stNoise; each ray marched by stMarch (unchanged sun code) in homogeneous map coordinates, s(t) = s0 + t ds, t = 1 at the
// emitter. Reach: the edge ray's lateral travel at most SHADOW_FOOTPRINT_M at the receiver, and never past the near plane.
float sampleProjectorMarch(vec3 pos, vec3 norm, vec2 pos_screen)
{
    mat4 m = proj_shadow_matrix;
    vec4 sr = m * vec4(pos, 1.0);
    if (sr.w <= 0.0) return 1.0;
    StSpot sp = stSpotSetup(m);
    vec2 res = vec2(proj_shadow_tile.z);
    float kn = min(sp.k.x * res.x, sp.k.y * res.y);
    float texel_m = sr.w / kn;
    float sin_theta;
    vec3 ng = shadowReceiverGeometry(pos_screen, pos, norm, normalize(proj_light_pos - pos), sin_theta);
    vec3 p = shadowNormalOffsetPos(pos, ng, sin_theta, texel_m);
    vec4 s0 = m * vec4(p, 1.0);
    if (s0.w <= 0.0 || any(greaterThan(abs(s0.xyz / s0.w - 0.5), vec3(0.5)))) return 1.0;
    vec3 c = s0.xyz / s0.w;
    float w = s0.w;
    float z_tex = sp.c1 / (kn * w);
    vec2 grad = stSpotGradient(m, p, ng, c);
    // the tile's u and v axes in view space (symmetric frustum: row0 - 0.5 row3 = k * u axis)
    vec3 r0 = vec3(m[0][0], m[1][0], m[2][0]);
    vec3 r1 = vec3(m[0][1], m[1][1], m[2][1]);
    vec3 r3 = vec3(m[0][3], m[1][3], m[2][3]);
    vec3 ua = normalize(r0 - 0.5 * r3);
    vec3 va = normalize(r1 - 0.5 * r3);
    vec2 h = 0.5 * proj_shadow_light_size;
    vec2 nz = stNoise();
    float theta = 6.28318531 * nz.x;
    vec4 ds[ST_RAYS];
    for (int k = 0; k < ST_RAYS; ++k)
    {
        float r = sqrt((float(k) + 0.5) / float(ST_RAYS));
        float phi = float(k) * ST_GOLDEN + theta;
        vec3 e = proj_light_pos + (h.x * r * cos(phi)) * ua + (h.y * r * sin(phi)) * va;
        ds[k] = m * vec4(e - p, 0.0);
    }
    // lateral travel of the edge ray at the receiver, in texels, at parameter t: pen_t * t / (1 - t)
    float pen_t = max(h.x * sp.k.x, h.y * sp.k.y) * res.x / w; // = (0.5 L k N / c1) * c1 / w
    float a = SHADOW_FOOTPRINT_M * kn / w / max(pen_t, 1.0e-6);
    float t_cap = a / (1.0 + a);
    float t_near = 1.0 - (sp.c1 / sp.c0) / w;     // the near plane, w (1 - t) = c1 / c0
    float t_max = clamp(min(t_cap, 0.999 * t_near), 1.0e-4, 0.999);
    float travel = pen_t * t_max / (1.0 - t_max);
    int steps = clamp(int(ceil(travel)), 2, march_steps_max);
    return stMarch(proj_shadow_atlas, res, s0, grad, SHADOW_BIAS_TEXELS * z_tex, SHADOW_PLANE_BIAS_MAX_TEXELS * z_tex, ds, t_max, steps, nz.y);
}
'''

# stMarch reads ST_STEPS_MAX only through its `steps` argument; the prototype passes march_steps_max
RECEIVER = r'''
uniform vec2 grid;
uniform vec3 scan_a;
uniform vec3 scan_b;
uniform mat4 cam_inv;     // --time: clip -> view (world) of the camera
uniform vec3 cam_pos;
uniform vec3 recv_norm;   // the receiver's normal (--grazing: a tilted plane; otherwise the floor's)
out vec4 oc;
float VIS(vec3 pos, vec3 norm, vec2 ps)
{
#if VARIANT == 0
    return sampleProjectorShadow(pos, norm, ps);
#elif VARIANT == 1
    return sampleProjectorP2(pos, norm, ps);
#elif VARIANT == 3
    return sampleProjectorShadow(pos, norm, ps);
#else
    return sampleProjectorMarch(pos, norm, ps);
#endif
}
void main()
{
    vec2 id = floor(gl_FragCoord.xy);
    vec3 norm = recv_norm;
#if TIMING
    vec4 q = cam_inv * vec4((gl_FragCoord.xy / grid) * 2.0 - 1.0, 1.0, 1.0);
    vec3 d = normalize(q.xyz / q.w - cam_pos);
    vec3 pos = cam_pos + d * (-cam_pos.z / d.z);
#else
    vec3 pos = mix(scan_a, scan_b, (id.x + 0.5) / grid.x);
#endif
    float v = VIS(pos, norm, gl_FragCoord.xy / grid);
#if INSET
    // P1: min(static, inset); the inset lookup is a second call with the inset's matrix and tile (uniform block swap)
    v = min(v, VIS2(pos, norm, gl_FragCoord.xy / grid));
#endif
    oc = vec4(v);
}
'''
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'


def inset_variant(src, variant):
    """A second copy of the lookup reading the inset's own uniforms and sampler (proj_inset_*): the real P1 passes the inset
    tile as its own uniforms. Only for --time (the cost of two lookups per pixel)."""
    body = g1 + near + PROTO + sample_today
    for a, b in (('proj_shadow_atlas', 'proj_inset_atlas'), ('proj_shadow_matrix', 'proj_inset_matrix'),
                 ('proj_shadow_tile', 'proj_inset_tile'), ('proj_shadow_light_size', 'proj_inset_light_size')):
        body = body.replace(a, b)
    body = re.sub(r'\b(stSpotSetup|stSpotDisc|stSpotQuadTile|stSpotFilterTileNear|stSpotFilterTile|sampleProjectorShadow|'
                  r'sampleProjectorP2|sampleProjectorMarch|StSpot)\b', r'\1_I', body)
    name = {0: 'sampleProjectorShadow_I', 1: 'sampleProjectorP2_I', 2: 'sampleProjectorMarch_I'}[variant]
    return body + 'float VIS2(vec3 pos, vec3 norm, vec2 ps) { return %s(pos, norm, ps); }\n' % name


real_defines = '\n'.join(l for l in real_util.splitlines() if re.match(r'#define (ST_)', l))
real_g1 = section(real_util, '// <G1-BEGIN>', '// <G1-END>')
real_st_fns = ''.join(func(real_util, s) for s in (
    'float stIGN(', 'vec2 stNoise()', 'vec2 stSpotGradient(', 'float stReference(', 'float stMarch('))
real_sample = func(real_util, 'float sampleProjectorShadow(')


def build(variant, timing=False, inset=False, inset_variant_id=None):
    if variant == 3:
        # P2 real: the working checkout's own code, nothing of the baseline's or the prototypes'
        head = (PREAMBLE.replace(defines, real_defines) + '#define VARIANT 3\n#define TIMING %d\n#define INSET 0\n' % int(timing))
        src = head + common_fns + real_st_fns + real_g1 + real_sample
        return program([(0x8B31, VS), (0x8B30, src + RECEIVER)])
    head = PREAMBLE + '#define VARIANT %d\n#define TIMING %d\n#define INSET %d\n' % (variant, int(timing), int(inset))
    src = head + common_fns + st_fns + g1 + near + PROTO + sample_today
    if inset:
        src += inset_variant(src, variant if inset_variant_id is None else inset_variant_id)
    return program([(0x8B31, VS), (0x8B30, src + RECEIVER)])


# ---------------------------------------------------------------- GL helpers
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
glUniform1i = fn('glUniform1i', None, I, I)
glUniform1f = fn('glUniform1f', None, I, C.c_float)
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

# ---------------------------------------------------------------- scene
H_L = opts.lamp_h
LAMP = np.array([0.0, 0.0, H_L])
RECV_NORM = (0.0, 0.0, 1.0)
FOV = opts.fov


def avatar(dx=0.0):
    b = [((-0.04, -0.05, 0.00), (0.30, 0.05, 0.08)),   # foot, toe at x = 0.30
         ((-0.04, -0.05, 0.08), (0.06, 0.05, 0.85)),   # shin
         ((-0.10, -0.22, 0.85), (0.10, 0.22, 1.45)),   # torso, shoulders at 1.45 m, y = +-0.22
         ((-0.05, -0.05, 1.45), (0.05, 0.05, 1.52)),   # neck
         ((-0.09, -0.08, 1.52), (0.09, 0.08, 1.74)),   # head
         ((0.40, -0.04, 0.75), (0.42, 0.04, 0.95))]    # a flat hand, 2 cm thick, 8 cm wide, 20 cm tall
    return [(np.array(lo) + (dx, 0, 0), np.array(hi) + (dx, 0, 0)) for lo, hi in b]


SCANS = {  # name: (a, b, window lo, window hi) along the scan coordinate s in [0, 1]
    'toe': (np.array([0.20, 0.0, 0.0]), np.array([0.45, 0.0, 0.0]), 0.285, 0.45),
    'shoulder': (np.array([0.0, 0.30, 0.0]), np.array([0.0, 0.85, 0.0]), 0.30, 0.85),
    'hand': (np.array([0.62, -0.02, 0.0]), np.array([0.62, 0.28, 0.0]), -0.02, 0.28),
}
SCAN_COORD = {'toe': 0, 'shoulder': 1, 'hand': 1}
LIT_AT = {'toe': 0.325}  # the toe contact: rise from 0 under the toe to the reference's value 1.5-2.5 cm past the toe tip


def lamp_matrix(L, tan_half, near=None, far=None):
    """bias * perspective * inverse(pose): the lamp looks down -z with u = +x, v = +y (pose rotation = identity)."""
    dist = 0.5 * L / math.tan(0.5 * FOV)
    near = dist + 0.01 if near is None else near
    far = dist + 15.0 if far is None else far
    f = 1.0 / tan_half
    proj = np.array([[f, 0, 0, 0], [0, f, 0, 0], [0, 0, -(far + near) / (far - near), -2 * far * near / (far - near)], [0, 0, -1, 0]])
    bias = np.array([[.5, 0, 0, .5], [0, .5, 0, .5], [0, 0, .5, .5], [0, 0, 0, 1]])
    inv_pose = np.eye(4)
    inv_pose[:3, 3] = -LAMP
    return bias @ proj @ inv_pose


def box_hits(o, d, boxes, t_lo=1e-6, t_hi=np.inf):
    """first hit parameter t of rays o + t d against axis-aligned boxes (np.inf for none). o, d: (..., 3)."""
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


def depth_tile(n, m, tan_half, boxes, floor=True):
    u = ((np.arange(n) + 0.5) / n * 2 - 1) * tan_half
    out = np.empty((n, n), np.float32)
    for r0 in range(0, n, 128):
        dx, dy = np.meshgrid(u, u[r0:r0 + 128])
        d = np.stack([dx, dy, -np.ones_like(dx)], -1)
        o = np.broadcast_to(LAMP, d.shape)
        t = box_hits(o, d, boxes)
        if floor:
            t = np.minimum(t, H_L)  # the floor z = 0 at t = H_L (d.z = -1)
        hit = np.isfinite(t)
        p = o + d * np.where(hit, t, 1.0)[..., None]
        s = np.concatenate([p, np.ones(p.shape[:-1] + (1,))], -1) @ m.T
        z = np.where(hit, s[..., 2] / s[..., 3], 1.0)
        out[r0:r0 + 128] = np.clip(z, 0.0, 1.0)
    return out


def disc_points(L, n=1024):
    k = np.arange(n)
    r = np.sqrt((k + 0.5) / n) * 0.5 * L
    phi = k * 2.39996323
    return LAMP + np.stack([r * np.cos(phi), r * np.sin(phi), np.zeros(n)], -1)


def reference(points, L, boxes):
    """visibility of a disc emitter of diameter L at the projection centre, exact geometry."""
    e = disc_points(L)
    vis = np.empty(len(points))
    for i in range(0, len(points), 64):
        p = points[i:i + 64]
        d = e[None, :, :] - p[:, None, :]
        o = np.broadcast_to(p[:, None, :], d.shape)
        t = box_hits(o, d, boxes, t_lo=1e-6, t_hi=1.0)
        vis[i:i + 64] = np.mean(~np.isfinite(t), 1)
    return vis


# ---------------------------------------------------------------- run a scan on the GPU
W_SCAN = 512


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


def set_lookup(prog, prefix, tex, unit, m, n, L):
    glActiveTexture(0x84C0 + unit)
    glBindTexture(TEX, tex)
    glUniform1i(glGetUniformLocation(prog, (prefix + 'atlas').encode()), unit)
    mf = np.ascontiguousarray(m.T, np.float32)
    glUniformMatrix4fv(glGetUniformLocation(prog, (prefix + 'matrix').encode()), 1, 0, mf.ctypes.data)
    glUniform4f(glGetUniformLocation(prog, (prefix + 'tile').encode()), 0.0, 0.0, float(n), 1.0 / n)
    glUniform2f(glGetUniformLocation(prog, (prefix + 'light_size').encode()), L, L)


def set_common(prog, frame, steps_max, w, h):
    glUniform1i(glGetUniformLocation(prog, b'shadow_st_frame'), frame)
    glUniform1f(glGetUniformLocation(prog, b'spot_shadow_offset'), 0.04)   # RenderSpotShadowOffset
    glUniform1f(glGetUniformLocation(prog, b'spot_shadow_reach'), 128.0)
    glUniform3f(glGetUniformLocation(prog, b'proj_light_pos'), *[float(v) for v in LAMP])
    glUniform3f(glGetUniformLocation(prog, b'proj_shadow_origin'), *[float(v) for v in LAMP])
    glUniform3f(glGetUniformLocation(prog, b'recv_norm'), *[float(v) for v in RECV_NORM])
    glUniform1i(glGetUniformLocation(prog, b'march_steps_max'), steps_max)
    glUniform2f(glGetUniformLocation(prog, b'grid'), float(w), float(h))


def scan(prog, tgt, tex, m, n, L, a, b, frame=0, steps_max=8):
    use(prog)
    glBindFramebuffer(0x8D40, tgt.fbo)
    glViewport(0, 0, tgt.w, tgt.h)
    glDisable(0x0B71)
    set_lookup(prog, 'proj_shadow_', tex, 0, m, n, L)
    set_common(prog, frame, steps_max, tgt.w, tgt.h)
    glUniform3f(glGetUniformLocation(prog, b'scan_a'), *[float(v) for v in a])
    glUniform3f(glGetUniformLocation(prog, b'scan_b'), *[float(v) for v in b])
    glDrawArrays(4, 0, 3)
    glFinish()
    return tgt.read()


def edge_metrics(s, v, lo, hi, lit=None):
    """10-90 % width from the dark end of the window to its lit end (first crossings), umbra and penumbra lengths.
    lit: the lit level to rise to (the toe: the reference's value just past the toe's own penumbra, since the head's
    penumbra lies over the toe under an overhead lamp); default the mean of the window's last 8 samples."""
    sel = (s >= lo) & (s <= hi)
    ss, vv = s[sel], v[sel]
    v_dark, v_lit = vv[:8].mean(), (vv[-8:].mean() if lit is None else lit)
    if ss[0] > ss[-1]:
        ss, vv = ss[::-1], vv[::-1]
    def cross(level):
        thr = v_dark + level * (v_lit - v_dark)
        idx = np.nonzero(vv >= thr)[0] if v_lit > v_dark else np.nonzero(vv <= thr)[0]
        if len(idx) == 0 or idx[0] == 0:
            return float('nan')
        i = idx[0]
        x0, x1, y0, y1 = ss[i - 1], ss[i], vv[i - 1], vv[i]
        return float(x0 + (thr - y0) * (x1 - x0) / (y1 - y0 if y1 != y0 else 1e-9))
    step = abs(ss[1] - ss[0])
    return {'w10_90_mm': 1000 * (cross(0.9) - cross(0.1)), 'x50_mm': 1000 * cross(0.5),
            'umbra_mm': 1000 * step * int(np.sum(vv <= 0.02)), 'penumbra_mm': 1000 * step * int(np.sum((vv > 0.02) & (vv < 0.98))),
            'v_dark': float(v_dark), 'v_lit': float(v_lit)}


def edges():
    gl_info = get_string(0x1F01).decode()
    progs = {v: build(v) for v in (0, 1, 2, 3)}
    rows = opts.rows
    tgt = Target(W_SCAN, rows)
    boxes = avatar()
    tan_full = math.tan(0.5 * FOV)
    tan_inset = 0.75 / H_L      # P1: a symmetric crop holding the avatar and its shadow (+-0.75 m on the floor)
    results = []
    for L in [float(x) for x in opts.emitters.split(',')]:
        ref = {}
        lit_level = {}
        for name, (a, b, lo, hi) in SCANS.items():
            s = np.linspace(0, 1, W_SCAN, endpoint=False) + 0.5 / W_SCAN
            pts = a[None, :] + s[:, None] * (b - a)[None, :]
            ref[name] = (pts[:, SCAN_COORD[name]], reference(pts, L, boxes))
            lit_level[name] = float(np.interp(LIT_AT[name], ref[name][0], ref[name][1])) if name in LIT_AT else None
            r = edge_metrics(ref[name][0], ref[name][1], lo, hi, lit_level[name])
            row = dict(kind='edge', method='reference', L=L, tile='-', scan=name, **r)
            results.append(row)
            emit(row)
        tiles = [('static', int(n), tan_full) for n in opts.sizes.split(',')] + [('inset', 1024, tan_inset), ('inset', 512, tan_inset)]
        for kind, n, th in tiles:
            m = lamp_matrix(L, th)
            z = depth_tile(n, m, th, boxes, floor=(kind == 'static'))
            tex = texture(0x8CAC, n, n, z, 0x1902)
            label = '%s %d' % (kind, n)
            texel_mm = 1000 * 2 * th * H_L / n
            for name, (a, b, lo, hi) in SCANS.items():
                coord, rv = ref[name]
                scanned = {}
                for vid, mname in ((0, 'PCSS today'), (1, 'P2'), (3, 'P2 real')):
                    v = scan(progs[vid], tgt, tex, m, n, L, a, b).mean(0)
                    scanned[vid] = v
                    r = edge_metrics(coord, v, lo, hi, lit_level[name])
                    sel = (coord >= min(lo, hi)) & (coord <= max(lo, hi))
                    row = dict(kind='edge', method=mname, L=L, tile=label, texel_mm=texel_mm, scan=name,
                               rms_vs_ref=float(np.sqrt(np.mean((v[sel] - rv[sel]) ** 2))), **r)
                    if vid == 3:
                        row['max_diff_vs_proto'] = float(np.max(np.abs(v - scanned[1])))   # the real shader against the bench's prototype
                    results.append(row)
                    emit(row)
                for steps_max in (() if opts.no_march else (8, 16)):
                    frames = np.stack([scan(progs[2], tgt, tex, m, n, L, a, b, frame=f, steps_max=steps_max) for f in range(1, opts.frames + 1)])
                    conv = frames.mean((0, 1))
                    r = edge_metrics(coord, conv, lo, hi, lit_level[name])
                    sel = (coord >= min(lo, hi)) & (coord <= max(lo, hi))
                    pen = sel & (conv > 0.03) & (conv < 0.97)
                    grain = {}
                    for k in (1, 4, 8):
                        acc = frames[:k].mean(0)  # rows: independent pixels of the same receiver
                        grain['grain_%d' % k] = float(np.sqrt(np.mean((acc[:, pen] - conv[pen]) ** 2))) if pen.any() else 0.0
                    ema = frames[0].copy()
                    for f in range(1, frames.shape[0]):
                        ema += (frames[f] - ema) / 8.0
                    grain['grain_ema'] = float(np.sqrt(np.mean((ema[:, pen] - conv[pen]) ** 2))) if pen.any() else 0.0
                    row = dict(kind='edge', method='MARCH s%d' % steps_max, L=L, tile=label, texel_mm=texel_mm, scan=name,
                               rms_vs_ref=float(np.sqrt(np.mean((conv[sel] - rv[sel]) ** 2))), **r, **grain)
                    results.append(row)
                    emit(row)
            free(tex)
            print('L=%.2f %s done' % (L, label), flush=True)
    # table
    print('\nGL:', gl_info)
    print('| L | tile | scan | method | 10-90 % mm | x50 mm | umbra mm | penumbra mm | RMS vs ref | value under the toe | real - proto max | grain 1 / 4 / 8 / EMA |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|')
    for r in results:
        g = ' / '.join('%.3f' % r[k] for k in ('grain_1', 'grain_4', 'grain_8', 'grain_ema')) if 'grain_1' in r else ''
        print('| %g | %s | %s | %s | %.1f | %.1f | %.0f | %.0f | %s | %s | %s | %s |' % (r['L'], r['tile'], r['scan'], r['method'], r['w10_90_mm'], r['x50_mm'],
              r['umbra_mm'], r['penumbra_mm'], '%.3f' % r['rms_vs_ref'] if 'rms_vs_ref' in r else '-',
              '%.3f' % r['v_dark'] if r['scan'] == 'toe' and r['method'] != 'reference' else '', '%.4f' % r['max_diff_vs_proto'] if 'max_diff_vs_proto' in r else '', g))
    # the P2 gates, per tile: toe x50 within 2 mm of the reference, the value under the toe at most 0.02, the toe RMS no worse than
    # today's, shoulder x50 within 2 mm of the reference
    print('\nP2 gates (P2 real against the reference and against today):')
    refs = {(r['L'], r['scan']): r for r in results if r['method'] == 'reference'}
    ok_all = True
    for r in results:
        if r['method'] != 'P2 real' or r['scan'] not in ('toe', 'shoulder'):
            continue
        ref_r = refs[(r['L'], r['scan'])]
        today = next(t for t in results if t['method'] == 'PCSS today' and (t['L'], t['tile'], t['scan']) == (r['L'], r['tile'], r['scan']))
        dx = r['x50_mm'] - ref_r['x50_mm']
        ok = abs(dx) <= 2.0
        txt = '%s %-12s x50 %+.1f mm of the reference (%s)' % (r['scan'], r['tile'], dx, 'PASS' if ok else 'FAIL')
        if r['scan'] == 'toe':
            ok_dark = r['v_dark'] <= 0.02
            ok_rms = r['rms_vs_ref'] <= today['rms_vs_ref']
            ok = ok and ok_dark and ok_rms
            txt += '; under the toe %.3f (%s); RMS %.3f vs today %.3f (%s)' % (r['v_dark'], 'PASS' if ok_dark else 'FAIL', r['rms_vs_ref'], today['rms_vs_ref'], 'PASS' if ok_rms else 'FAIL')
        # a tile whose texel is wider than 5 mm at the floor (static 512: 9.1 mm) is resolution-limited: reported, not gated (the
        # study's P1 inset is the answer there); the real shader equals the prototype on it all the same (real - proto max)
        gated = r['texel_mm'] <= 5.0
        ok_all = ok_all and (ok or not gated)
        print('  L=%g %s%s' % (r['L'], txt, '' if gated else '  [texel %.1f mm: resolution-limited, not gated]' % r['texel_mm']))
    print('P2 GATES (tiles with texels of 5 mm or less)', 'PASS' if ok_all else 'FAIL')


# ---------------------------------------------------------------- GPU time
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


def look_at(eye, target, fovy, aspect):
    f = target - eye
    f = f / np.linalg.norm(f)
    up = np.array([0.0, 0.0, 1.0])
    s = np.cross(f, up)
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    view = np.eye(4)
    view[0, :3], view[1, :3], view[2, :3] = s, u, -f
    view[:3, 3] = -view[:3, :3] @ eye
    t = 1.0 / math.tan(0.5 * fovy)
    n_, f_ = 0.1, 100.0
    proj = np.array([[t / aspect, 0, 0, 0], [0, t, 0, 0], [0, 0, -(f_ + n_) / (f_ - n_), -2 * f_ * n_ / (f_ - n_)], [0, 0, -1, 0]])
    return np.linalg.inv(proj @ view)


def timing():
    busy = builds_running()
    print('builds running at start:', busy or 'none', flush=True)
    W, H = 2560, 1440
    tgt = Target(W, H)
    eye, target = np.array([1.3, -1.3, 1.7]), np.array([0.15, 0.15, 0.0])
    inv = look_at(eye, target, math.radians(55), W / H)
    inv_f = np.ascontiguousarray(inv.T, np.float32)
    # coverage: every pixel's floor point inside the lamp frustum?
    yy, xx = np.mgrid[0:H:8, 0:W:8]
    ndc = np.stack([(xx + 0.5) / W * 2 - 1, (yy + 0.5) / H * 2 - 1, np.ones_like(xx, float), np.ones_like(xx, float)], -1)
    q = ndc @ inv.T
    d = q[..., :3] / q[..., 3:] - eye
    p = eye + d * (-eye[2] / d[..., 2])[..., None]
    lim = H_L * math.tan(0.5 * FOV)
    cover = float(np.mean((d[..., 2] < 0) & (np.abs(p[..., 0]) < lim) & (np.abs(p[..., 1]) < lim)))
    print('lamp frustum coverage of the view: %.1f %%' % (100 * cover), flush=True)
    boxes = avatar()
    tan_full = math.tan(0.5 * FOV)
    tan_inset = 0.75 / H_L
    progs = {('PCSS today', False): build(0, timing=True), ('P2', False): build(1, timing=True), ('P2 real', False): build(3, timing=True)}
    if not opts.no_march:
        progs.update({('MARCH', False): build(2, timing=True),
                      ('P1 inset + P2', True): build(0, timing=True, inset=True, inset_variant_id=1),
                      ('P1 inset + MARCH', True): build(0, timing=True, inset=True, inset_variant_id=2)})
    rows = []
    for L in [float(x) for x in opts.emitters.split(',')]:
        mi = lamp_matrix(L, tan_inset)
        zi = depth_tile(1024, mi, tan_inset, boxes, floor=False)
        tex_i = texture(0x8CAC, 1024, 1024, zi, 0x1902)
        for n in [int(x) for x in opts.sizes.split(',')]:
            m = lamp_matrix(L, tan_full)
            z = depth_tile(n, m, tan_full, boxes)
            tex = texture(0x8CAC, n, n, z, 0x1902)
            for (name, inset), prog in progs.items():
                for steps_max in ((8, 16) if 'MARCH' in name else (8,)):
                    def setup(prog=prog, inset=inset, steps_max=steps_max):
                        use(prog)
                        glBindFramebuffer(0x8D40, tgt.fbo)
                        glViewport(0, 0, W, H)
                        glDisable(0x0B71)
                        set_lookup(prog, 'proj_shadow_', tex, 0, m, n, L)
                        if inset:
                            set_lookup(prog, 'proj_inset_', tex_i, 1, mi, 1024, L)
                        set_common(prog, 3, steps_max, W, H)
                        glUniformMatrix4fv(glGetUniformLocation(prog, b'cam_inv'), 1, 0, inv_f.ctypes.data)
                        glUniform3f(glGetUniformLocation(prog, b'cam_pos'), *[float(v) for v in eye])
                    setup()
                    glDrawArrays(4, 0, 3)
                    glFinish()
                    img = tgt.read()
                    pen = float(np.mean((img > 0.0) & (img < 1.0)))
                    shadowed = float(np.mean(img < 1.0))
                    ms, spread = gpu_ms(setup)
                    label = name + (' s%d' % steps_max if 'MARCH' in name else '')
                    row = dict(kind='time', method=label, L=L, N=n, ms=ms, spread=spread, ms_per_lit_mpx=ms / (W * H * cover / 1e6),
                               penumbra_frac=pen, shadow_frac=shadowed, coverage=cover, builds=builds_running())
                    rows.append(row)
                    emit(row)
                    print('L=%.2f N=%d %-22s %.3f ms (spread %.0f %%) %.4f ms/lit Mpx, penumbra %.1f %%, shadowed %.1f %%, builds %s' % (
                        L, n, label, ms, 100 * spread, row['ms_per_lit_mpx'], 100 * pen, 100 * shadowed, row['builds'] or 'none'), flush=True)
            free(tex)
        free(tex_i)
    print('\nGL:', get_string(0x1F01).decode(), get_string(0x1F00).decode())


# ---------------------------------------------------------------- temporal: a walking avatar under the march + the sun's resolve
def temporal():
    """Static camera: the history pixel = the same receiver. Per frame the avatar moves +x by v / 60; the tile is redrawn
    every frame (C1); the march runs with this frame's seed; the resolve is the sun's: clamp the history to the 3 x 3
    min/max of this frame (rows = screen neighbours, columns = positions along the scan), then blend 1/8 of this frame.
    Measured over frames 12-23 against the instantaneous reference: the leading edge's lag (the toe/head front, 50 %
    crossing), the ghost behind the trailing edge (newly lit floor still dark in the history), and the RMS error of the
    whole profile; PCSS today and P2 (no history) for comparison."""
    prog, p2, pcss = build(2), build(1), build(0)
    rows_n, W = 16, 1024
    tgt = Target(W, rows_n)
    tan_full = math.tan(0.5 * FOV)
    a, b = np.array([-0.5, 0.0, 0.0]), np.array([2.1, 0.0, 0.0])
    s = (np.arange(W) + 0.5) / W
    pts = a[None, :] + s[:, None] * (b - a)[None, :]
    xs = pts[:, 0]

    def front(v, x_from, level):
        sel = np.nonzero((xs > x_from) & (v >= level))[0]
        return float(xs[sel[0]]) if len(sel) else float('nan')

    for L in (0.06, 0.15):
        n = 1024
        m = lamp_matrix(L, tan_full)
        for v_ms, label, x0 in ((1.4, 'walk 1.4 m/s', 0.0), (4.0, 'run 4 m/s', -0.8)):
            dx = v_ms / 60.0
            hist = None
            acc = {k: [] for k in ('lag', 'ghost_amp', 'ghost_len', 'rms_m', 'rms_p2', 'rms_pc', 'grain')}
            for f in range(24):
                boxes = avatar(x0 + dx * f)
                z = depth_tile(n, m, tan_full, boxes)
                tex = texture(0x8CAC, n, n, z, 0x1902)
                cur = scan(prog, tgt, tex, m, n, L, a, b, frame=f + 1)
                if f >= 12:
                    vp2 = scan(p2, tgt, tex, m, n, L, a, b).mean(0)
                    vpc = scan(pcss, tgt, tex, m, n, L, a, b).mean(0)
                free(tex)
                if hist is None:
                    hist = cur.copy()
                else:
                    pad = np.pad(cur, 1, mode='edge')
                    st = np.stack([pad[i:i + rows_n, j:j + W] for i in range(3) for j in range(3)])
                    hist = np.clip(hist, st.min(0), st.max(0))
                    hist = hist + (cur - hist) / 8.0
                if f < 12:
                    continue
                ref = reference(pts, L, boxes)
                out = hist.mean(0)
                # the shadow's front (largest x below 0.5) and back (smallest x below 0.5): reference against output
                def ends(v):
                    d = np.nonzero(v < 0.5)[0]
                    return (float(xs[d[0]]), float(xs[d[-1]])) if len(d) else (float('nan'), float('nan'))
                (rb, rf), (ob, of) = ends(ref), ends(out)
                acc['lag'].append(1000 * (rf - of))              # > 0: the output's front trails the reference's
                acc['ghost_amp'].append(1000 * (rb - ob))        # > 0: the output's shadow starts behind the reference's (a ghost)
                gsel = xs < rb
                acc['ghost_len'].append(1000 * float(np.sum((ref[gsel] - out[gsel]) > 0.05)) * float(xs[1] - xs[0]))
                if f == 23:
                    np.savez('temporal_L%g_%s.npz' % (L, label.split()[0]), xs=xs, ref=ref, out=out, p2=vp2, pcss=vpc)
                acc['rms_m'].append(float(np.sqrt(np.mean((out - ref) ** 2))))
                acc['rms_p2'].append(float(np.sqrt(np.mean((vp2 - ref) ** 2))))
                acc['rms_pc'].append(float(np.sqrt(np.mean((vpc - ref) ** 2))))
                pen = (ref > 0.03) & (ref < 0.97)
                acc['grain'].append(float(np.sqrt(np.mean((hist[:, pen] - hist[:, pen].mean(0)) ** 2))))
            row = dict(kind='temporal', L=L, N=n, motion=label, mm_per_frame=1000 * dx,
                       lead_lag_mm=statistics.mean(acc['lag']), lead_lag_frames=statistics.mean(acc['lag']) / (1000 * dx),
                       trail_ghost_mm=statistics.mean(acc['ghost_amp']), trail_ghost_len_mm=statistics.mean(acc['ghost_len']),
                       rms_march_resolved=statistics.mean(acc['rms_m']), rms_p2=statistics.mean(acc['rms_p2']),
                       rms_pcss_today=statistics.mean(acc['rms_pc']), grain_rows=statistics.mean(acc['grain']))
            emit(row)
            print(json.dumps(row), flush=True)


def grazing():
    """Acne on a curved or grazing receiver: a single plane through the origin tilted by phi to the horizontal (normal
    (sin phi, 0, cos phi)) is the only caster, so every receiver on it is lit and a correct lookup reads 1.0. The lamp is overhead,
    so phi is the angle between the receiver's normal and the light. Reports the mean and the worst darkening (1 - v) of the
    scan x in [-0.3, 0.3] for today's lookup and the real P2, per tile size and phi; the receiver plane and normal are exact."""
    global RECV_NORM
    progs = {'PCSS today': build(0), 'P2 real': build(3)}
    tgt = Target(W_SCAN, 8)
    tan_full = math.tan(0.5 * FOV)
    print('| L | tile | phi | method | mean darkening | max darkening | receivers darker than 0.98 |')
    print('|---|---|---|---|---|---|---|')
    for L in [float(x) for x in opts.emitters.split(',')]:
        m = lamp_matrix(L, tan_full)
        for n in [int(x) for x in opts.sizes.split(',')]:
            for phi in (0, 45, 60, 75, 85, 88):
                sp, cp = math.sin(math.radians(phi)), math.cos(math.radians(phi))
                u = ((np.arange(n) + 0.5) / n * 2 - 1) * tan_full
                dx, dy = np.meshgrid(u, u)
                with np.errstate(divide='ignore', invalid='ignore'):
                    t = cp * H_L / (cp - sp * dx)          # the ray (dx t, dy t, H - t) meets sin(phi) x + cos(phi) z = 0
                hit = np.isfinite(t) & (t > 0)
                pts = np.stack([dx * t, dy * t, H_L - t], -1)
                sc = np.concatenate([pts, np.ones(pts.shape[:-1] + (1,))], -1) @ m.T
                z = np.where(hit, sc[..., 2] / sc[..., 3], 1.0).astype(np.float32)
                tex = texture(0x8CAC, n, n, np.clip(z, 0.0, 1.0), 0x1902)
                RECV_NORM = (sp, 0.0, cp)
                hw = min(0.3, 1.0 * cp / sp) if sp > 1e-6 else 0.3     # the scan stays within 1 m of the floor's height
                a = np.array([-hw, 0.0, hw * sp / cp])
                b = np.array([hw, 0.0, -hw * sp / cp])
                for name, prog in progs.items():
                    v = scan(prog, tgt, tex, m, n, L, a, b).mean(0)
                    d = 1.0 - v
                    print('| %g | %d | %d | %s | %.4f | %.3f | %.1f %% |' % (L, n, phi, name, d.mean(), d.max(), 100 * np.mean(v < 0.98)), flush=True)
                free(tex)
    RECV_NORM = (0.0, 0.0, 1.0)


if opts.grazing:
    grazing()
if opts.edges:
    edges()
if opts.time:
    timing()
if opts.temporal:
    temporal()
