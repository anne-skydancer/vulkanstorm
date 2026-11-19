#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""G1-P of the point lights' shadow faces (lamp shadows L4): the shader's twins of llpointshadowpool.h and the lookup on a known scene.

Offline, no viewer, Windows with an OpenGL 4 driver (scripts/perf/gl_test_context.py). The code under test is compiled from
shadowStochasticUtil.glsl itself (its G1 and P1 blocks, stPointGradient, stPointReceiverNormal, samplePointShadowNg and
samplePointShadow) with the method-neutral helpers of shadowUtil.glsl (SHADOW_COMMON_NO_DEPTH for (c) and (d): the shading normal is the
receiver plane; the timing also runs the viewer's variant, which rebuilds the plane from the depth buffer).

  (a) pick: stPointPick, stPointFace and stPointCoord against the C++ rules' twin (llpointpool::pickFace and faceTexCoord, here in
      numpy) on 10^6 directions, ties and signed zeros included: the face exactly, uv within 16 ulp of 1 (1.9e-6) and depth within 4 ulp
      of 1 (4.8e-7) of the double-precision twin;
  (b) rects: stPointFaceTile against llpointpool::unpackFaceRect for every face rect of the 4096 atlas (exact);
  (c) wall: a lamp whose six faces are ray-cast here (a 1 m x 1 m wall 1 m from the light on +X, nothing else), looked up by the real
      samplePointShadow at every face size: behind the wall's middle 0, in front of it and beside it 1, a penumbra across its shadow's
      edge that widens with the distance behind the wall (the receivers' visibility against the analytic disc emitter is reported);
      and no receiver reads past its face's tile (each tile has a 64-texel border of blockers at the near plane, so a read past a face's
      edge shows as a shadow on the open receivers);
  (d) camera: the same receivers from four camera poses (view-space positions, point_view_to_agent and the lamp's centre transformed)
      give the same visibility to within the float rounding of the view transform: no screen, frame or view term;
  --time: the lookup's GPU cost, ms per 2560 x 1440 pass with every pixel a receiver in a lamp's light (a third of them behind the
      wall, in its umbra and penumbra; neighbouring pixels on neighbouring points, as a frame reads the faces), per face size: the cost
      per lit megapixel per lamp, for the shading-normal variant and for the viewer's (the receiver plane from four position reads, as
      getPosition reads the depth buffer); and a batch of 16 lights (multiPointLightF.glsl's form: the plane once, one call site), with
      the compile and link time of that program against the same batch with two call sites.

    python scripts/shadows/point_faces_g1.py [--sizes 128,256,512,1024] [--grid 512] [--time]

Exit code 0 on pass, 1 on a failure. Needs numpy.
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
COMMON = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowUtil.glsl'
CONTEXT = ROOT / 'scripts/perf/gl_test_context.py'

ap = argparse.ArgumentParser()
ap.add_argument('--sizes', default='128,256,512,1024')
ap.add_argument('--grid', type=int, default=512)
ap.add_argument('--seed', type=int, default=1)
ap.add_argument('--time', action='store_true')
opts = ap.parse_args()
sys.argv = [sys.argv[0]]
exec(compile(CONTEXT.read_text(), str(CONTEXT), 'exec'))  # window, context, fn(), program()

util = UTIL.read_text(encoding='utf-8')
common = COMMON.read_text(encoding='utf-8')


def section(text, begin, end):
    i = text.index(chr(10), text.index(begin)) + 1
    return text[i:text.index(end, i)]


def func(text, sig):
    i = text.index(sig)
    return text[i:text.index('\n}\n', i) + 3]


GUARD = float(re.search(r'#define ST_POINT_GUARD ([0-9.]+)', util).group(1))
NEAR = float(re.search(r'#define ST_POINT_NEAR ([0-9.]+)', util).group(1))
defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define ST_', l))
g1 = section(util, '// <G1-BEGIN>', '// <G1-END>')
p1 = section(util, '// <P1-BEGIN>', '// <P1-END>')
lookup = ''.join(func(util, sig) for sig in ('vec2 stPointGradient(', 'vec3 stPointReceiverNormal(', 'float samplePointShadowNg(',
                                               'float samplePointShadow('))
helpers = ''.join(func(common, s) for s in ('vec3 shadowNormalOffsetPos(', 'vec3 shadowReceiverGeometry('))
# the viewer's variant (the receiver plane from the depth buffer): shadowUtil.glsl's depth block, with getPosition defined by the test
depth_block = section(common, '#if !defined(SHADOW_COMMON_NO_DEPTH)', '#endif // !SHADOW_COMMON_NO_DEPTH')

PREAMBLE = '''#version 400
#define SHADOW_COMMON_NO_DEPTH 1
#define SHADOW_BIAS_TEXELS 1.0
#define SHADOW_NORMAL_OFFSET_TEXELS 1.0
#define SHADOW_PLANE_BIAS_MAX_TEXELS 8.0
#define SHADOW_GRAD_MAX 10000.0
''' + defines + '''
uniform float spot_shadow_reach;
'''
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'

PICK_FS = PREAMBLE + p1 + '''
uniform sampler2D dirs;
uniform float tan_half;
uniform float far_m;
out vec4 oc;
void main()
{
    vec3 d = texelFetch(dirs, ivec2(gl_FragCoord.xy), 0).xyz;
    int f = stPointPick(d);
    float c0 = far_m / (far_m - ST_POINT_NEAR);
    vec3 c = stPointCoord(stPointFace(d, f), tan_half, c0, c0 * ST_POINT_NEAR);
    oc = vec4(float(f), c);
}
'''
RECT_FS = PREAMBLE + p1 + '''
uniform sampler2D rects;
out vec4 oc;
void main()
{
    float p = texelFetch(rects, ivec2(gl_FragCoord.xy), 0).r;
    oc = stPointFaceTile(vec4(p), vec4(p), 0);
}
'''
LOOKUP_FS = PREAMBLE + g1 + p1 + helpers + lookup + '''
uniform sampler2D recv_pos;  // agent space (xyz), w > 0: a receiver
uniform sampler2D recv_norm; // agent space
uniform mat4 view;           // agent -> view (a rigid transform)
uniform vec4 lamp_agent;     // the light's centre (agent), far
uniform vec4 faces_a;
uniform vec4 faces_b;
out vec4 oc;
void main()
{
    vec4 rp = texelFetch(recv_pos, ivec2(gl_FragCoord.xy), 0);
    vec3 rn = texelFetch(recv_norm, ivec2(gl_FragCoord.xy), 0).xyz;
    vec3 pos = (view * vec4(rp.xyz, 1.0)).xyz;
    vec3 n = mat3(view) * rn;
    vec4 lamp = vec4((view * vec4(lamp_agent.xyz, 1.0)).xyz, lamp_agent.w);
    oc = vec4(rp.w > 0.0 ? samplePointShadow(lamp, faces_a, faces_b, pos, n, gl_FragCoord.xy / 512.0) : -1.0);
}
'''

# the viewer's variant of the lookup: the receiver plane from four neighbour positions (getPosition reads the depth buffer in the viewer;
# here the receivers' position texture, one fetch each), for the timing
DEPTH_PREAMBLE = PREAMBLE.replace('#define SHADOW_COMMON_NO_DEPTH 1\n', '') + '''
uniform vec2 screen_res;
uniform sampler2D recv_pos;  // agent space (xyz), w > 0: a receiver
uniform mat4 view;           // agent -> view (a rigid transform)
'''
GET_POSITION = '''
vec4 getPosition(vec2 pos_screen)
{
    return vec4((view * vec4(texture(recv_pos, pos_screen).xyz, 1.0)).xyz, 1.0);
}
'''
LOOKUP_DEPTH_FS = DEPTH_PREAMBLE + g1 + p1 + depth_block + helpers + lookup + '''
uniform sampler2D recv_norm;
uniform vec4 lamp_agent;
uniform vec4 faces_a;
uniform vec4 faces_b;
out vec4 oc;
void main()
{
    vec2 tc = gl_FragCoord.xy / screen_res;
    vec4 rp = texelFetch(recv_pos, ivec2(gl_FragCoord.xy), 0);
    vec3 rn = texelFetch(recv_norm, ivec2(gl_FragCoord.xy), 0).xyz;
    vec3 pos = (view * vec4(rp.xyz, 1.0)).xyz;
    vec3 n = mat3(view) * rn;
    vec4 lamp = vec4((view * vec4(lamp_agent.xyz, 1.0)).xyz, lamp_agent.w);
    oc = vec4(rp.w > 0.0 ? samplePointShadow(lamp, faces_a, faces_b, pos, n, tc) : -1.0);
}
''' + GET_POSITION

BATCH_LOOP = '''
        for (int i = 0; i < LIGHT_COUNT; ++i)
        {
            vis[i] = INIT;
            vec4 lamp = vec4((view * vec4(lamps_agent[i].xyz, 1.0)).xyz, lamps_agent[i].w);
            vec3 plv = lamp.xyz - pos;
            if (length(plv) / lamp.w <= 1.0 && dot(n, plv) > 0.0)
            {
                if (!have)
                {
                    ng = stPointReceiverNormal(tc, pos, n);
                    have = true;
                }
                vis[i] = samplePointShadowNg(lamp, faces_a, faces_b, pos, ng);
            }
        }
'''


def batch_fs(sites, nonce):
    """multiPointLightF.glsl's form for 16 lights (the plane once, then each light's visibility); sites = 2 repeats the loop in two
    branches, as the batch did before (one call site per shading branch). `nonce` keeps the driver's shader cache out of the timing."""
    if sites == 1:
        body = BATCH_LOOP.replace('INIT', '1.0')
    else:
        body = ('    if (branch_f < 0.5)\n    {' + BATCH_LOOP.replace('INIT', '1.0') + '    }\n    else\n    {' +
                BATCH_LOOP.replace('INIT', '1.0 + 0.0 * branch_f') + '    }\n')
    return DEPTH_PREAMBLE + '#define LIGHT_COUNT 16\n// nonce %d\n' % nonce + g1 + p1 + depth_block + helpers + lookup + '''
uniform sampler2D recv_norm;
uniform vec4 lamps_agent[LIGHT_COUNT];
uniform vec4 faces_a;
uniform vec4 faces_b;
uniform float branch_f;
out vec4 oc;
void main()
{
    vec2 tc = gl_FragCoord.xy / screen_res;
    vec4 rp = texelFetch(recv_pos, ivec2(gl_FragCoord.xy), 0);
    vec3 rn = texelFetch(recv_norm, ivec2(gl_FragCoord.xy), 0).xyz;
    vec3 pos = (view * vec4(rp.xyz, 1.0)).xyz;
    vec3 n = mat3(view) * rn;
    float vis[LIGHT_COUNT];
    vec3 ng = vec3(0.0);
    bool have = false;
''' + body + '''
    float sum = 0.0;
    for (int i = 0; i < LIGHT_COUNT; ++i)
    {
        sum += vis[i];
    }
    oc = vec4(sum / float(LIGHT_COUNT));
}
''' + GET_POSITION


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
glUniform4f = fn('glUniform4f', None, I, C.c_float, C.c_float, C.c_float, C.c_float)
glUniformMatrix4fv = fn('glUniformMatrix4fv', None, I, I, U, P)
glUniformMatrix3fv = fn('glUniformMatrix3fv', None, I, I, U, P)
glUniform4fv = fn('glUniform4fv', None, I, I, P)
glGetTexImage = fn('glGetTexImage', None, U, I, U, U, P)
glGenVertexArrays = fn('glGenVertexArrays', None, I, C.POINTER(U))
glBindVertexArray = fn('glBindVertexArray', None, U)
glDisable = fn('glDisable', None, U)
glFinish = fn('glFinish', None)
glGetError = fn('glGetError', U)
glDeleteTextures = fn('glDeleteTextures', None, I, C.POINTER(U))

TEX = 0x0DE1
GL_DEPTH32F, GL_DEPTH, GL_FLOAT = 0x8CAC, 0x1902, 0x1406
GL_RGBA32F, GL_RGBA, GL_R32F, GL_RED = 0x8814, 0x1908, 0x822E, 0x1903


def texture(fmt, w, h, data, fmt_in):
    t = U()
    glGenTextures(1, C.byref(t))
    glBindTexture(TEX, t.value)
    glTexImage2D(TEX, 0, fmt, w, h, 0, fmt_in, GL_FLOAT, data.ctypes.data if data is not None else None)
    for pname, value in ((0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F), (0x884C, 0)):
        glTexParameteri(TEX, pname, value)  # NEAREST, CLAMP_TO_EDGE, compare mode NONE
    return t.value


def target(w, h):
    tex = texture(GL_RGBA32F, w, h, None, GL_RGBA)
    fb = U()
    glGenFramebuffers(1, C.byref(fb))
    glBindFramebuffer(0x8D40, fb.value)
    glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, tex, 0)
    assert glCheckFramebufferStatus(0x8D40) == 0x8CD5
    return tex, fb.value


def draw(prog, fb, w, h, textures, uniforms):
    glBindFramebuffer(0x8D40, fb)
    glViewport(0, 0, w, h)
    glDisable(0x0B71)
    use(prog)
    for unit, (name, tex) in enumerate(textures):
        glActiveTexture(0x84C0 + unit)
        glBindTexture(TEX, tex)
        glUniform1i(glGetUniformLocation(prog, name.encode()), unit)
    for name, value in uniforms.items():
        loc = glGetUniformLocation(prog, name.encode())
        v = np.asarray(value, np.float32)
        if v.size == 1:
            glUniform1f(loc, float(v))
        elif v.size == 2:
            glUniform2f(loc, *map(float, v))
        elif v.size == 4:
            glUniform4f(loc, *map(float, v))
        elif v.size == 9:
            glUniformMatrix3fv(loc, 1, 0, np.ascontiguousarray(v.reshape(3, 3).T).ctypes.data)
        elif v.size == 16:
            glUniformMatrix4fv(loc, 1, 0, np.ascontiguousarray(v.reshape(4, 4).T).ctypes.data)  # row-major numpy -> column-major GL
        elif v.size > 16 and v.size % 4 == 0:
            glUniform4fv(loc, v.size // 4, np.ascontiguousarray(v).ctypes.data)  # a vec4 array
    glDrawArrays(4, 0, 3)
    glFinish()


def read(tex, w, h, channels=4):
    buf = np.empty((h, w, channels), np.float32)
    glBindTexture(TEX, tex)
    glGetTexImage(TEX, 0, GL_RGBA if channels == 4 else GL_RED, GL_FLOAT, buf.ctypes.data)
    return buf


# ---- the C++ rules' twin (llpointshadowpool.h), float32 as the viewer computes the faces
AXES = np.array([  # fwd, right, up per face (+X -X +Y -Y +Z -Z)
    [[1, 0, 0], [0, -1, 0], [0, 0, 1]], [[-1, 0, 0], [0, 1, 0], [0, 0, 1]],
    [[0, 1, 0], [1, 0, 0], [0, 0, 1]], [[0, -1, 0], [-1, 0, 0], [0, 0, 1]],
    [[0, 0, 1], [-1, 0, 0], [0, 1, 0]], [[0, 0, -1], [1, 0, 0], [0, 1, 0]]], np.float32)


def pick_face(d):
    a = np.abs(d)
    x = (a[..., 0] >= a[..., 1]) & (a[..., 0] >= a[..., 2])
    y = ~x & (a[..., 1] >= a[..., 2])
    face = np.where(x, np.where(d[..., 0] >= 0, 0, 1), np.where(y, np.where(d[..., 1] >= 0, 2, 3), np.where(d[..., 2] >= 0, 4, 5)))
    ax = AXES[face]
    xyw = np.stack([(ax[..., 1, :] * d).sum(-1), (ax[..., 2, :] * d).sum(-1), (ax[..., 0, :] * d).sum(-1)], -1)
    return face, xyw


def tan_half(n):
    return (0.5 * n) / (0.5 * n - GUARD)


def face_depth(far):
    c0 = far / (far - NEAR)
    return c0, c0 * NEAR


def check_pick(failures):
    rng = np.random.default_rng(opts.seed)
    g = 1000
    d = rng.uniform(-1, 1, (g, g, 3)).astype(np.float32)
    k = np.arange(g * g).reshape(g, g)
    tie = (k % 8) == 0
    a_idx, b_idx = k % 3, (k // 3) % 3
    rows = np.arange(g)[:, None].repeat(g, 1)
    cols = np.arange(g)[None, :].repeat(g, 0)
    src = np.abs(d[rows, cols, a_idx]) * np.where((k & 8) > 0, -1.0, 1.0).astype(np.float32)
    d[rows[tie], cols[tie], b_idx[tie]] = src[tie]
    zero = (k % 64) == 1
    d[rows[zero], cols[zero], (k % 3)[zero]] = np.where((k & 64)[zero] > 0, -0.0, 0.0)
    dirs = texture(GL_RGBA32F, g, g, np.dstack([d, np.zeros((g, g, 1), np.float32)]).copy(), GL_RGBA)
    out, fb = target(g, g)
    prog = program([(0x8B31, VS), (0x8B30, PICK_FS)])
    worst_uv = worst_z = 0.0
    for n in (128, 256, 512, 1024):
        t = np.float32(tan_half(n))
        far = np.float32(9.0)
        draw(prog, fb, g, g, [('dirs', dirs)], {'tan_half': t, 'far_m': far})
        r = read(out, g, g)
        face, xyw = pick_face(d)
        bad = int(np.count_nonzero(r[..., 0] != face))
        c0, c1 = face_depth(np.float64(far))
        w = xyw[..., 2].astype(np.float64)
        ok = np.abs(w) > 1e-3
        uv = 0.5 + 0.5 * xyw[..., :2].astype(np.float64) / (w[..., None] * float(t))
        z = c0 - c1 / w
        du = np.abs(r[..., 1:3] - uv)[ok].max()
        dz = np.abs(r[..., 3] - z)[ok & (w > 0.05)].max()
        worst_uv, worst_z = max(worst_uv, du), max(worst_z, dz)
        good = bad == 0 and du <= 4 * 2.0 ** -23 * 4 and dz <= 4 * 2.0 ** -23
        failures += 0 if good else 1
        print('  (a) N=%-4d face pick: %d of %d differ; uv max abs %.3g, depth max abs %.3g  %s' % (n, bad, g * g, du, dz, 'PASS' if good else 'FAIL'))
    glDeleteTextures(1, C.byref(U(dirs)))
    return failures


def check_rects(failures):
    packed, want = [], []
    for n in (128, 256, 512, 1024):
        for y in range(0, 4096, n):
            for x in range(0, 4096, n):
                code = int(np.log2(n // 64))
                packed.append(x // 64 + 64 * (y // 64) + 4096 * code)
                want.append((x, y, n))
    m = len(packed)
    w = 512
    h = (m + w - 1) // w
    data = np.zeros((h, w), np.float32)
    data.flat[:m] = np.array(packed, np.float32)
    tex = texture(GL_R32F, w, h, data, GL_RED)
    out, fb = target(w, h)
    prog = program([(0x8B31, VS), (0x8B30, RECT_FS)])
    draw(prog, fb, w, h, [('rects', tex)], {'point_shadow_params': (0.01, 1.0 / 4096)})
    r = read(out, w, h).reshape(-1, 4)[:m]
    bad = int(np.count_nonzero(np.any(r[:, :3] != np.array(want, np.float32), axis=1)))
    ok = bad == 0 and bool(np.all(r[:, 3] == np.float32(1.0 / 4096)))
    print('  (b) %d face rects of the 4096 atlas unpacked: %d differ  %s' % (m, bad, 'PASS' if ok else 'FAIL'))
    glDeleteTextures(1, C.byref(U(tex)))
    return failures + (0 if ok else 1)


# ---- (c) and (d): one lamp at the origin of agent space, a wall 1 m x 1 m at x = 1 (y, z in [-0.5, 0.5]), its faces ray-cast here
LAMP = np.array([0.0, 0.0, 0.0], np.float64)
FAR = 6.0  # radius 4 m


def wall_hit_w(dirs_face, face):
    """the axial distance (fwd . p) where the rays of a face (direction per texel, agent axes, unnormalised) hit the wall, inf where not"""
    dx = dirs_face[..., 0]
    with np.errstate(divide='ignore', invalid='ignore'):
        s = 1.0 / dx  # the ray parameter at x = 1
        p = dirs_face * s[..., None]
        hit = (dx > 0) & (np.abs(p[..., 1]) <= 0.5) & (np.abs(p[..., 2]) <= 0.5)
    fwd = AXES[face][0].astype(np.float64)
    return np.where(hit, (p * fwd).sum(-1), np.inf)


def face_tile(n, far):
    """the six faces' depth pictures (n x n each), as the GL half draws them: texel centres through the face's projection"""
    t = tan_half(n)
    c0, c1 = face_depth(far)
    idx = (np.arange(n) + 0.5) / n
    u, v = np.meshgrid(idx, idx)  # rows: v
    tiles = []
    for f in range(6):
        fwd, right, up = (AXES[f][i].astype(np.float64) for i in range(3))
        x = (u - 0.5) * 2.0 * t
        y = (v - 0.5) * 2.0 * t
        d = fwd + x[..., None] * right + y[..., None] * up  # w = 1
        w = wall_hit_w(d, f)
        z = np.where(np.isfinite(w) & (w > NEAR) & (w < far), c0 - c1 / w, 1.0)
        tiles.append(z.astype(np.float32))
    return tiles


def receivers(grid):
    """receivers on three planes behind, in front of and beside the wall, facing the lamp (agent space)"""
    rng = np.random.default_rng(opts.seed + 5)
    pos = np.zeros((grid, grid, 4), np.float32)
    nrm = np.zeros((grid, grid, 4), np.float32)
    kind = np.zeros((grid, grid), np.int32)
    third = grid // 3
    # behind: the plane x = 2 (y, z within the wall's shadow and around its edge: the shadow spans |y|, |z| <= 1 there)
    yz = rng.uniform(-1.4, 1.4, (third, grid, 2))
    pos[:third, :, 0] = 2.0
    pos[:third, :, 1:3] = yz
    nrm[:third, :, 0] = -1.0
    kind[:third] = 1
    # in front: x = 0.6
    yz = rng.uniform(-0.6, 0.6, (third, grid, 2))
    pos[third:2 * third, :, 0] = 0.6
    pos[third:2 * third, :, 1:3] = yz
    nrm[third:2 * third, :, 0] = -1.0
    kind[third:2 * third] = 2
    # the opposite side (-X): open
    yz = rng.uniform(-2, 2, (grid - 2 * third, grid, 2))
    pos[2 * third:, :, 0] = -2.0
    pos[2 * third:, :, 1:3] = yz
    nrm[2 * third:, :, 0] = 1.0
    kind[2 * third:] = 3
    pos[..., 3] = 1.0
    return pos, nrm, kind


def view_matrix(yaw, pitch, eye):
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]])
    rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    r = rx @ rz
    m = np.eye(4)
    m[:3, :3] = r
    m[:3, 3] = -r @ np.asarray(eye, np.float64)
    return m.astype(np.float32)


def analytic_visibility(p, light_m):
    """the fraction of a disc emitter of diameter light_m at the lamp (facing the receiver) that the wall leaves visible, by sampling"""
    rng = np.random.default_rng(3)
    k = 256
    r = np.sqrt((np.arange(k) + 0.5) / k)
    phi = np.arange(k) * 2.39996323
    vis = np.empty(p.shape[0])
    for i, q in enumerate(p):
        dvec = q / np.linalg.norm(q)
        t1 = np.cross(dvec, [0, 0, 1.0]) if abs(dvec[2]) < 0.9 else np.cross(dvec, [1.0, 0, 0])
        t1 /= np.linalg.norm(t1)
        t2 = np.cross(dvec, t1)
        e = (0.5 * light_m * r * np.cos(phi))[:, None] * t1 + (0.5 * light_m * r * np.sin(phi))[:, None] * t2
        ray = q[None, :] - e
        s = (1.0 - e[:, 0]) / ray[:, 0]
        y = e[:, 1] + s * ray[:, 1]
        z = e[:, 2] + s * ray[:, 2]
        blocked = (s > 0) & (s < 1) & (np.abs(y) <= 0.5) & (np.abs(z) <= 0.5)
        vis[i] = 1.0 - blocked.mean()
    return vis


def check_wall(failures, timing):
    A = 4096
    grid = opts.grid
    pos, nrm, kind = receivers(grid)
    pos_tex = texture(GL_RGBA32F, grid, grid, pos, GL_RGBA)
    nrm_tex = texture(GL_RGBA32F, grid, grid, nrm, GL_RGBA)
    out, fb = target(grid, grid)
    prog = program([(0x8B31, VS), (0x8B30, LOOKUP_FS)])
    light = 0.045 * 0.23  # the point lamp's emitter at the default softness (m)
    for n in [int(s) for s in opts.sizes.split(',')]:
        tiles = face_tile(n, FAR)
        atlas = np.zeros((A, A), np.float32)  # blockers at the near plane everywhere outside the faces' tiles: a read past a tile shows
        rects = []
        for f in range(6):
            # each tile with a 64-texel border of blockers (rects stay multiples of 64, as packFaceRect wants): a read past any edge of a
            # face lands in the border, not in a neighbouring face's tile
            x, y = 64 + (f % 3) * (n + 64), 64 + (f // 3) * (n + 64)
            atlas[y:y + n, x:x + n] = tiles[f]
            rects.append(x // 64 + 64 * (y // 64) + 4096 * int(np.log2(n // 64)))
        atlas_tex = texture(GL_DEPTH32F, A, A, atlas, GL_DEPTH)
        uniforms = {'lamp_agent': (0.0, 0.0, 0.0, FAR), 'faces_a': rects[:4], 'faces_b': rects[4:] + [0, 0],
                    'point_shadow_params': (light, 1.0 / A), 'spot_shadow_reach': 128.0}
        results = []
        for cam in ((0.3, 0.1, (-3.0, 1.0, 1.5)), (2.1, -0.4, (5.0, -3.0, 2.0)), (-1.2, 0.7, (0.5, 4.0, -2.0)), (3.0, 0.0, (-6.0, -6.0, 0.0))):
            view = view_matrix(*cam)
            u = dict(uniforms)
            u['view'] = view
            u['point_view_to_agent'] = view[:3, :3].T.copy()
            draw(prog, fb, grid, grid, [('point_shadow_atlas', atlas_tex), ('recv_pos', pos_tex), ('recv_norm', nrm_tex)], u)
            results.append(read(out, grid, grid)[..., 0].copy())
        r = results[0]
        behind_core = (kind == 1) & (np.abs(pos[..., 1]) < 0.85) & (np.abs(pos[..., 2]) < 0.85)
        behind_open = (kind == 1) & ((np.abs(pos[..., 1]) > 1.15) | (np.abs(pos[..., 2]) > 1.15))
        front = kind == 2
        opposite = kind == 3
        penumbra = (kind == 1) & ~behind_core & ~behind_open
        core = float(r[behind_core].max())
        lit = float(min(r[behind_open].min(), r[front].min(), r[opposite].min()))
        cam_diff = max(float(np.abs(x - r).max()) for x in results[1:])
        # the penumbra against the analytic disc emitter (the filter's floor of 1.5 texels widens it on coarse faces: reported, not gated)
        sel = np.argwhere(penumbra)[:2000]
        vis = analytic_visibility(pos[sel[:, 0], sel[:, 1], :3].astype(np.float64), light)
        got = r[sel[:, 0], sel[:, 1]]
        ok = core == 0.0 and lit == 1.0 and cam_diff <= 2e-3 and glGetError() == 0
        failures += 0 if ok else 1
        print('  (c) N=%-4d behind the wall max %.3g (want 0), open max abs from 1: %.3g; penumbra vs analytic disc: mean abs %.3f, max %.3f' % (
            n, core, 1.0 - lit, float(np.abs(got - vis).mean()), float(np.abs(got - vis).max())))
        print('  (d) N=%-4d four camera poses: max abs difference %.3g (%d of %d receivers differ)  %s' % (
            n, cam_diff, int(sum(np.count_nonzero(x != r) for x in results[1:])), 3 * r.size, 'PASS' if ok else 'FAIL'))
        if timing:
            time_lookup(prog, atlas_tex, uniforms, n)
        glDeleteTextures(1, C.byref(U(atlas_tex)))
    return failures


def coherent_receivers(W, H):
    """receivers as a view would see them: neighbouring pixels on neighbouring points of three planes (behind the wall, the wall's
    front, the open side), so the faces' texels are read the way a frame reads them"""
    pos = np.zeros((H, W, 4), np.float32)
    nrm = np.zeros((H, W, 4), np.float32)
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    uu, vv = np.meshgrid(u, v)
    third = H // 3
    # behind the wall (x = 2): its shadow (|y|, |z| <= 1) and around it
    pos[:third, :, 0] = 2.0
    pos[:third, :, 1] = (uu[:third] - 0.5) * 2.8
    pos[:third, :, 2] = (vv[:third] * 3 - 0.5) * 2.8
    nrm[:third, :, 0] = -1.0
    # in front of it (x = 0.6)
    pos[third:2 * third, :, 0] = 0.6
    pos[third:2 * third, :, 1] = (uu[third:2 * third] - 0.5) * 1.2
    pos[third:2 * third, :, 2] = (vv[third:2 * third] * 3 - 1.5) * 1.2
    nrm[third:2 * third, :, 0] = -1.0
    # the open side (x = -2)
    pos[2 * third:, :, 0] = -2.0
    pos[2 * third:, :, 1] = (uu[2 * third:] - 0.5) * 4.0
    pos[2 * third:, :, 2] = (vv[2 * third:] * 3 - 2.5) * 4.0
    nrm[2 * third:, :, 0] = 1.0
    pos[..., 3] = 1.0
    return pos, nrm


def gpu_ms(prog, textures, uniforms, W, H, rounds=10, draws=20):
    """the median GPU ms of one full W x H pass of `prog` (a time-elapsed query over `draws` passes, the first round a warm-up)"""
    import statistics
    out, fb = target(W, H)
    q = U()
    fn('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
    begin, end = fn('glBeginQuery', None, U, U), fn('glEndQuery', None, U)
    get64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
    samples = []
    for rnd in range(rounds):
        draw(prog, fb, W, H, textures, uniforms)
        begin(0x88BF, q.value)
        for _ in range(draws):
            glDrawArrays(4, 0, 3)
        end(0x88BF)
        v = C.c_uint64()
        get64(q.value, 0x8866, C.byref(v))
        if rnd:
            samples.append(v.value / 1e6 / draws)
    glDeleteTextures(1, C.byref(U(out)))
    return statistics.median(samples)


def time_lookup(prog, atlas_tex, uniforms, n):
    """GPU ms of one full 2560 x 1440 pass of the lookup, every pixel a receiver in the light (a third behind the wall), the receivers
    coherent across the screen as in a frame: the shading-normal variant (prog), the viewer's (the plane from four position reads), and a
    batch of 16 lights at every pixel in multiPointLightF.glsl's form (an upper bound: in a frame a pixel is inside a few lamps' light)"""
    import time
    W, H = 2560, 1440
    big_pos, big_nrm = coherent_receivers(W, H)
    pt = texture(GL_RGBA32F, W, H, big_pos, GL_RGBA)
    nt = texture(GL_RGBA32F, W, H, big_nrm, GL_RGBA)
    u = dict(uniforms)
    view = view_matrix(0.3, 0.1, (-3.0, 1.0, 1.5))
    u['view'] = view
    u['point_view_to_agent'] = view[:3, :3].T.copy()
    textures = [('point_shadow_atlas', atlas_tex), ('recv_pos', pt), ('recv_norm', nt)]
    mpx = W * H / 1e6
    plain = gpu_ms(prog, textures, u, W, H)
    ud = dict(u)
    ud['screen_res'] = (float(W), float(H))
    depth_prog = program([(0x8B31, VS), (0x8B30, LOOKUP_DEPTH_FS)])
    depth = gpu_ms(depth_prog, textures, ud, W, H)
    print('  --time N=%-4d lookup: %.3f ms per 2560x1440 pass (%.4f ms per lit megapixel per lamp); with the receiver plane from the '
          'depth buffer (the viewer\'s variant) %.3f ms (%.4f)' % (n, plain, plain / mpx, depth, depth / mpx))
    ub = dict(ud)
    ub['lamps_agent'] = np.tile(np.array(uniforms['lamp_agent'], np.float32), 16)
    ub['branch_f'] = 0.0
    nonce = time.time_ns()  # a source the driver's shader cache has not seen: the compile is timed, not a cache hit
    t0 = time.perf_counter()
    one = program([(0x8B31, VS), (0x8B30, batch_fs(1, nonce))])
    link_one = (time.perf_counter() - t0) * 1000.0
    t0 = time.perf_counter()
    program([(0x8B31, VS), (0x8B30, batch_fs(2, nonce + 1))])
    link_two = (time.perf_counter() - t0) * 1000.0
    batch = gpu_ms(one, textures, ub, W, H, rounds=6, draws=4)
    print('  --time N=%-4d a batch of 16 lights at every pixel (multiPointLightF.glsl\'s form): %.3f ms per pass, %.4f ms per lit megapixel '
          'per lamp; compile and link %.0f ms with one call site, %.0f ms with two' % (n, batch, batch / mpx / 16, link_one, link_two))
    glDeleteTextures(1, C.byref(U(pt)))
    glDeleteTextures(1, C.byref(U(nt)))


def main():
    vao = U()
    glGenVertexArrays(1, C.byref(vao))
    glBindVertexArray(vao.value)
    failures = 0
    print('G1-P: the point lights\' faces (shadowStochasticUtil.glsl P1 block and samplePointShadow)')
    failures = check_pick(failures)
    failures = check_rects(failures)
    failures = check_wall(failures, opts.time)
    print('G1-P', 'PASS' if failures == 0 else 'FAIL (%d cases)' % failures)
    return 0 if failures == 0 else 1


sys.exit(main())
