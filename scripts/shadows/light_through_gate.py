#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Lamp shadows L3: the GPU gates of the light-through layer (offline GL, no viewer; Windows with an OpenGL 4.x driver).

The code under test is compiled from the checkout's shadowLightThroughF.glsl (the casters), shadowStochasticUtil.glsl (its G1 block
and sampleProjectorShadow) and shadowUtil.glsl (the SHADOW_COMMON helpers), the base's from git (--base, default b62d0fca18: lamp
shadows L1 + L2, before L3).

  casters   shadowLightThroughF.glsl in both variants (classic, LIGHT_THROUGH_GLTF), drawn as LLLightThroughLayer::DrawScope draws it:
            an RGBA8 + R16 framebuffer cleared to "no caster", glBlendFunc(GL_ZERO, GL_SRC_COLOR), glBlendEquationi(1, GL_MIN), no
            depth test. Four full-screen faces: two see-through ones (their product within the 8 bits), one at the solid cut and one
            invisible (both left out); the stored depth is the nearest see-through face's, never nearer, at most two steps farther.
  identity  sampleProjectorShadow of the checkout against the base's on the same depth tile (a downward lamp over a floor with plates
            at several heights, the tile inside a bigger atlas) and the same floor receivers, every channel: with proj_through 0, and
            with proj_through 1 on an empty layer (cleared to "no caster"): max abs difference 0. A lamp whose tiles hold no see-through
            caster is unchanged bit for bit; a tile cleared to no caster multiplies by exactly 1.
  twin      stThroughFilter on layers built by the CPU twin (scripts/shadows/lamp_filter_twin.py: Layer, through_filter): glass behind a
            frame, two stacked panes, a red pane's edge and random stacks of tinted casters at random depths, the layer's tile in an
            atlas whose other texels hold a black caster at the near plane (so any read past the tile shows), random receivers over the
            whole tile with radii from the floor to the cap: the largest difference to the twin per scene. Gate: within one step of
            the layer's 8 bits (1/255): the hardware's bilinear weights have 8 bits of sub-texel precision, the twin's are exact.
  --time    GPU ms of the lookup over a 2560 x 1440 grid of floor receivers inside the lamp's frustum (timer queries, median of 9 x 20
            draws, interleaved rounds): the base, the checkout with proj_through 0, with 1 on an empty layer (a tile that met a blended
            face writing nothing: the search alone), and with 1 behind a pane over the whole tile (the search and the filter), in ms
            and per lit megapixel; the run says which builds were running.

    python scripts/shadows/light_through_gate.py [--time] [--base REF] [--seed N]
Exit code 1 when a gate fails. Needs numpy.
"""
import argparse
import math
import re
import statistics
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--base', default='b62d0fca18', help='git ref of the lookup before L3')
ap.add_argument('--time', action='store_true')
ap.add_argument('--seed', type=int, default=1)
opts = ap.parse_args()
sys.argv = [sys.argv[0]]  # the GL context helper and the twin parse the command line themselves
CONTEXT = REPO / 'scripts/perf/gl_test_context.py'
exec(compile(CONTEXT.read_text(encoding='utf-8'), str(CONTEXT), 'exec'))  # window, context, fn(), program(), use, U, I, P, C
sys.path.insert(0, str(HERE))
import lamp_filter_twin as twin  # noqa: E402  (the CPU twin: Layer, through_filter, scenes)

UTIL_P = 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
COMMON_P = 'indra/newview/app_settings/shaders/class1/deferred/shadowUtil.glsl'


def git_show(ref, path):
    return subprocess.run(['git', '-C', str(REPO), 'show', '%s:%s' % (ref, path)], capture_output=True, check=True).stdout.decode('utf-8')


def func(text, sig):
    i = text.index(sig)
    return text[i:text.index('\n}\n', i) + 3]


def section(text, begin, end):
    i = text.index('\n', text.index(begin)) + 1
    return text[i:text.index(end, i)]


def sources(util, common):
    defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define ST_', l))
    common_fns = ''.join(func(common, s) for s in (
        'vec3 shadowRow(', 'vec2 shadowUVScale(', 'float shadowDepthScale(', 'float shadowTexelMetres(',
        'vec2 shadowPlaneGradient(', 'float shadowWorldBias(', 'vec3 shadowNormalOffsetPos(', 'vec3 shadowReceiverGeometry('))
    g1 = section(util, '// <G1-BEGIN>', '// <G1-END>')
    sample = func(util, 'vec3 sampleProjectorShadow(' if 'vec3 sampleProjectorShadow(' in util else 'float sampleProjectorShadow(')
    return defines, common_fns + func(util, 'vec2 stSpotGradient(') + g1, sample


BASE = sources(git_show(opts.base, UTIL_P), git_show(opts.base, COMMON_P))
REAL_UTIL = (REPO / UTIL_P).read_text(encoding='utf-8')
REAL = sources(REAL_UTIL, (REPO / COMMON_P).read_text(encoding='utf-8'))
assert 'stThroughFilter' in REAL[1], 'the checkout has no light-through lookup'

PREAMBLE = '''#version 400
#define SHADOW_COMMON_NO_DEPTH 1
#define SHADOW_BIAS_TEXELS 1.0
#define SHADOW_NORMAL_OFFSET_TEXELS 1.0
#define SHADOW_PLANE_BIAS_MAX_TEXELS 8.0
#define SHADOW_GRAD_MAX 10000.0
uniform float spot_shadow_offset;
uniform float spot_shadow_reach;
'''
# the floor receivers of the identity and the timing: a grid over the floor inside the frustum, normal up
FLOOR = r'''
uniform vec2 grid;
uniform vec3 p0;
uniform vec3 ax;
uniform vec3 ay;
out vec4 oc;
void main()
{
    vec3 pos = p0 + ((gl_FragCoord.x / grid.x) - 0.5) * ax + ((gl_FragCoord.y / grid.y) - 0.5) * ay;
    oc = %s;
}
'''
# the receivers of the twin comparison, from a texture: tile u, v, depth, pen (texels per unit of depth)
TWIN = r'''
uniform sampler2D receivers;
uniform vec4 layer_tile;
uniform float z_bias;
out vec4 oc;
void main()
{
    vec4 r = texelFetch(receivers, ivec2(gl_FragCoord.xy), 0);
    oc = vec4(stThroughFilter(proj_through_atlas, proj_through_depth, layer_tile, r.xyz, vec2(r.w), vec2(ST_SPOT_KERNEL_TEXELS_MAX), z_bias), 1.0);
}
'''
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'


def build(src, body):
    defines, fns, sample = src
    return program([(0x8B31, VS), (0x8B30, PREAMBLE + defines + '\n' + fns + sample + FLOOR % body)])


prog_base = build(BASE, 'vec4(vec3(sampleProjectorShadow(pos, vec3(0.0, 0.0, 1.0), gl_FragCoord.xy / grid)), 1.0)')
prog_real = build(REAL, 'vec4(sampleProjectorShadow(pos, vec3(0.0, 0.0, 1.0), gl_FragCoord.xy / grid), 1.0)')
prog_twin = program([(0x8B31, VS), (0x8B30, PREAMBLE + REAL[0] + '\n' + section(REAL_UTIL, '// <G1-BEGIN>', '// <G1-END>') + TWIN)])

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
glGetError = fn('glGetError', U)
glDeleteTextures = fn('glDeleteTextures', None, I, C.POINTER(U))
TEX = 0x0DE1
GL_FLOAT, GL_RED, GL_RGBA = 0x1406, 0x1903, 0x1908
NEAREST, LINEAR, CLAMP = 0x2600, 0x2601, 0x812F


def texture(fmt, w, h, data, fmt_in, filt=NEAREST):
    t = U()
    glGenTextures(1, C.byref(t))
    glBindTexture(TEX, t.value)
    arr = None if data is None else np.ascontiguousarray(data, np.float32)
    glTexImage2D(TEX, 0, fmt, w, h, 0, fmt_in, GL_FLOAT, arr.ctypes.data if arr is not None else None)
    for pname, value in ((0x2801, filt), (0x2800, filt), (0x2802, CLAMP), (0x2803, CLAMP), (0x884C, 0), (0x813D, 0)):
        glTexParameteri(TEX, pname, value)  # filters, CLAMP_TO_EDGE, compare mode NONE, MAX_LEVEL 0
    return t.value


def free(t):
    glDeleteTextures(1, C.byref(U(t)))


class Target:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.tex = texture(0x8814, w, h, None, GL_RGBA)  # RGBA32F
        f = U()
        glGenFramebuffers(1, C.byref(f))
        self.fbo = f.value
        glBindFramebuffer(0x8D40, self.fbo)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, self.tex, 0)
        assert glCheckFramebufferStatus(0x8D40) == 0x8CD5

    def read(self):
        buf = np.empty((self.h, self.w, 4), np.float32)
        glBindTexture(TEX, self.tex)
        glGetTexImage(TEX, 0, GL_RGBA, GL_FLOAT, buf.ctypes.data)
        return buf


vao = U()
glGenVertexArrays(1, C.byref(vao))
glBindVertexArray(vao.value)
glDisable(0x0B71)  # depth test

# ---- the lamp: 2.5 m above the floor, looking down, fov 1.5 rad, a 5 cm prim (near plane at its face + 1 cm), radius 6 m
H_L, FOV, RADIUS = 2.5, 1.5, 6.0


def lamp_matrix():
    th = math.tan(0.5 * FOV)
    dist = 0.025 / th
    near, far = dist + 0.01, dist + 1.5 * RADIUS
    f = 1.0 / th
    proj = np.array([[f, 0, 0, 0], [0, f, 0, 0], [0, 0, -(far + near) / (far - near), -2 * far * near / (far - near)], [0, 0, -1, 0]])
    bias = np.array([[.5, 0, 0, .5], [0, .5, 0, .5], [0, 0, .5, .5], [0, 0, 0, 1]])
    inv_pose = np.eye(4)
    inv_pose[2, 3] = -H_L
    return bias @ proj @ inv_pose


M = lamp_matrix()


def depth_of(x, y, h):
    p = np.stack([x, y, h, np.ones_like(x)], -1) @ M.T
    return p[..., 2] / p[..., 3]


def tile_rays(n):
    th = math.tan(0.5 * FOV)
    u = ((np.arange(n) + 0.5) / n * 2 - 1) * th
    return np.meshgrid(u, u)


def plates_tile(n, rng):
    """the depth tile: the floor and 12 rectangular plates at heights 0.3 .. 2 m, rays through the texel centres"""
    dx, dy = tile_rays(n)
    t = np.full(dx.shape, H_L)
    for _ in range(12):
        h = rng.uniform(0.3, 2.0)
        x0, y0 = rng.uniform(-2, 1.5, 2)
        w, d = rng.uniform(0.1, 1.0, 2)
        tp = H_L - h
        x, y = dx * tp, dy * tp
        hit = (x > x0) & (x < x0 + w) & (y > y0) & (y < y0 + d)
        t = np.where(hit & (tp < t), tp, t)
    return np.clip(depth_of(dx * t, dy * t, H_L - t), 0, 1).astype(np.float32)


def set_lookup(prog, atlas_tex, ox, oy, n, atlas, through, tau_tex, zt_tex, softness=0.23):
    use(prog)
    glActiveTexture(0x84C0)
    glBindTexture(TEX, atlas_tex)
    glUniform1i(glGetUniformLocation(prog, b'proj_shadow_atlas'), 0)
    glActiveTexture(0x84C1)
    glBindTexture(TEX, tau_tex)
    glUniform1i(glGetUniformLocation(prog, b'proj_through_atlas'), 1)
    glActiveTexture(0x84C2)
    glBindTexture(TEX, zt_tex)
    glUniform1i(glGetUniformLocation(prog, b'proj_through_depth'), 2)
    glUniform1i(glGetUniformLocation(prog, b'proj_through'), through)
    mf = np.ascontiguousarray(M.T, np.float32)
    glUniformMatrix4fv(glGetUniformLocation(prog, b'proj_shadow_matrix'), 1, 0, mf.ctypes.data)
    glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), float(ox), float(oy), float(n), 1.0 / atlas)
    glUniform4f(glGetUniformLocation(prog, b'proj_shadow_grid'), 1.0, 1.0, 0.0, 0.0)
    L = 0.045 * softness
    glUniform2f(glGetUniformLocation(prog, b'proj_shadow_light_size'), L, L)
    glUniform3f(glGetUniformLocation(prog, b'proj_shadow_origin'), 0.0, 0.0, H_L)
    glUniform1f(glGetUniformLocation(prog, b'spot_shadow_reach'), 128.0)


def floor_grid(prog, w, h):
    half = H_L * math.tan(0.5 * FOV) * 0.98   # the floor's patch inside the frustum
    glUniform2f(glGetUniformLocation(prog, b'grid'), float(w), float(h))
    glUniform3f(glGetUniformLocation(prog, b'p0'), 0.0, 0.0, 0.0)
    glUniform3f(glGetUniformLocation(prog, b'ax'), 2 * half, 0.0, 0.0)
    glUniform3f(glGetUniformLocation(prog, b'ay'), 0.0, 2 * half * h / w, 0.0)


def layer_atlas(atlas, ox, oy, n, tau=None, zt=None):
    """the light-through layer: the tile's rect holds tau (n x n x 3) and zt (n x n), every other texel a black caster at the near
    plane (tau 0, depth 0): a read past the tile shows"""
    t = np.zeros((atlas, atlas, 4), np.float32)
    z = np.zeros((atlas, atlas), np.float32)
    t[oy:oy + n, ox:ox + n, :3] = 1.0 if tau is None else tau
    t[oy:oy + n, ox:ox + n, 3] = 1.0
    z[oy:oy + n, ox:ox + n] = 1.0 if zt is None else zt
    return texture(0x8058, atlas, atlas, t, GL_RGBA, LINEAR), texture(0x822A, atlas, atlas, z, GL_RED)  # RGBA8, R16


def identity(rng):
    failures = 0
    atlas, n, ox, oy = 2048, 1024, 512, 1024
    depth = np.zeros((atlas, atlas), np.float32)          # blockers at the near plane everywhere outside the tile
    depth[oy:oy + n, ox:ox + n] = plates_tile(n, rng)
    atlas_tex = texture(0x8CAC, atlas, atlas, depth, 0x1902)  # D32F
    empty_tau, empty_zt = layer_atlas(atlas, ox, oy, n)
    w = h = 1024
    tgt = Target(w, h)
    print('identity: sampleProjectorShadow of the checkout against %s, %d x %d floor receivers, tile %d at (%d, %d) of %d' % (
        opts.base, w, h, n, ox, oy, atlas))
    outs = {}
    for label, prog, through in (('base', prog_base, 0), ('proj_through 0', prog_real, 0), ('proj_through 1, empty layer', prog_real, 1)):
        for softness in (0.23, 1.0):
            set_lookup(prog, atlas_tex, ox, oy, n, atlas, through, empty_tau, empty_zt, softness)
            floor_grid(prog, w, h)
            glBindFramebuffer(0x8D40, tgt.fbo)
            glViewport(0, 0, w, h)
            glDrawArrays(4, 0, 3)
            glFinish()
            outs[(label, softness)] = tgt.read()[..., :3]
    for softness in (0.23, 1.0):
        ref = outs[('base', softness)]
        partial = float(np.mean((ref[..., 0] > 0) & (ref[..., 0] < 1)))
        for label in ('proj_through 0', 'proj_through 1, empty layer'):
            d = np.abs(outs[(label, softness)] - ref)
            ok = bool(d.max() == 0.0)
            failures += not ok
            print('  softness %.2f, %-28s max abs diff %.3g, %d differing of %d (%.0f %% of the receivers in a penumbra)  %s' % (
                softness, label, d.max(), np.count_nonzero(d), d.size, 100 * partial, 'PASS' if ok else 'FAIL'))
    err = glGetError()
    failures += err != 0
    for t in (atlas_tex, empty_tau, empty_zt, tgt.tex):
        free(t)
    return failures


def twin_scenes(rng):
    """layers of the CPU twin: (name, Layer)"""
    n = 96
    t = twin.WINDOW_M / n
    c = (np.arange(n) + 0.5) * t
    x, y = np.meshgrid(c, c)
    out = []
    opaque, alpha, _ = twin.scene_glass(x, y)
    glass = twin.Layer(n)
    glass.draw(np.ones(opaque.shape, bool), alpha, (0.0, 0.0, 0.0), np.full(opaque.shape, twin.Z_WINDOW))
    out.append(('glass 0.5 behind a frame', glass))
    u = (np.arange(n) + 0.5) / n
    X, Y = np.meshgrid(u, u)
    stack = twin.Layer(n)
    stack.draw(X < 2 / 3, 0.5, (0.5, 0.5, 0.5), np.full(X.shape, twin.map_depth(2.5)))
    stack.draw(X > 1 / 3, 0.6, (1.0, 0.0, 0.0), np.full(X.shape, twin.Z_WINDOW))
    out.append(('two stacked panes', stack))
    edge = twin.Layer(n)
    edge.draw(((X - 0.503) * -0.17 + (Y - 0.497) * 0.985) < 0, 0.6, (1.0, 0.0, 0.0), np.full(X.shape, twin.Z_WINDOW))
    out.append(('a red pane\'s edge', edge))
    rand = twin.Layer(n)
    for _ in range(40):
        x0, y0 = rng.integers(0, n, 2)
        w, h = rng.integers(2, n // 3, 2)
        cover = np.zeros((n, n), bool)
        cover[y0:y0 + h, x0:x0 + w] = True
        rand.draw(cover, rng.uniform(0.0, 1.0), rng.uniform(0.0, 1.0, 3), np.full((n, n), rng.uniform(0.95, 0.99)))
    out.append(('40 random tinted casters', rand))
    return out


def twin_check(rng):
    failures = 0
    atlas, ox, oy = 256, 96, 64
    m = 192
    tgt = Target(m, m)
    print('twin: stThroughFilter against the CPU twin, %d x %d random receivers per scene, the tile at (%d, %d) of a %d atlas' % (m, m, ox, oy, atlas))
    for name, layer in twin_scenes(rng):
        n = layer.zt.shape[0]
        tau_tex, zt_tex = layer_atlas(atlas, ox, oy, n, layer.tau, layer.zt)
        cx = rng.uniform(-0.02, 1.02, (m, m))  # past the edges too: the tile's clamp
        cy = rng.uniform(-0.02, 1.02, (m, m))
        cz = rng.uniform(0.95, 0.999, (m, m))
        pen = np.exp(rng.uniform(np.log(30.0), np.log(3000.0), (m, m)))
        z_bias = 2.0e-5
        rec = texture(0x8814, m, m, np.stack([cx, cy, cz, pen], -1), GL_RGBA)
        use(prog_twin)
        glActiveTexture(0x84C0)
        glBindTexture(TEX, rec)
        glUniform1i(glGetUniformLocation(prog_twin, b'receivers'), 0)
        glActiveTexture(0x84C1)
        glBindTexture(TEX, tau_tex)
        glUniform1i(glGetUniformLocation(prog_twin, b'proj_through_atlas'), 1)
        glActiveTexture(0x84C2)
        glBindTexture(TEX, zt_tex)
        glUniform1i(glGetUniformLocation(prog_twin, b'proj_through_depth'), 2)
        glUniform4f(glGetUniformLocation(prog_twin, b'layer_tile'), float(ox), float(oy), float(n), 1.0 / atlas)
        glUniform1f(glGetUniformLocation(prog_twin, b'z_bias'), z_bias)
        glBindFramebuffer(0x8D40, tgt.fbo)
        glViewport(0, 0, m, m)
        glDrawArrays(4, 0, 3)
        glFinish()
        gpu = tgt.read()[..., :3].astype(np.float64)
        f32 = lambda a: a.astype(np.float32).astype(np.float64)  # the receivers as the GPU reads them
        cpu = twin.through_filter(layer, f32(cx), f32(cy), f32(cz), f32(pen), float(np.float32(z_bias)))
        d = np.abs(gpu - cpu)
        tinted = float(np.mean(np.any(cpu < 0.999, axis=-1)))
        ok = bool(d.max() <= 1.0 / 255.0) and glGetError() == 0
        failures += not ok
        print('  %-26s max abs diff %.5f, 99.9 %% %.5f, mean %.2e (%.0f %% of the receivers tinted)  %s' % (
            name, d.max(), np.percentile(d, 99.9), d.mean(), 100 * tinted, 'PASS' if ok else 'FAIL'))
        for t in (tau_tex, zt_tex, rec):
            free(t)
    free(tgt.tex)
    return failures


CASTER_VS = '''#version 400
uniform float z;
uniform vec4 colour;
out vec4 vertex_color;
out vec2 vary_texcoord0;
void main()
{
    vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1);
    gl_Position = vec4(p, z * 2.0 - 1.0, 1.0);
    vertex_color = colour;
    vary_texcoord0 = p * 0.5 + 0.5;
}'''
SRGB = 'vec3 srgb_to_linear(vec3 cs) { return pow(cs, vec3(2.2)); }\n'   # a stand-in: the colours below are 0 or 1 in sRGB, or a factor


def casters():
    """the caster pass as the viewer draws it (LLLightThroughLayer::DrawScope's state) on a 16 x 16 layer"""
    failures = 0
    text = (REPO / 'indra/newview/app_settings/shaders/class1/deferred/shadowLightThroughF.glsl').read_text(encoding='utf-8')
    body = text.replace('/*[EXTRA_CODE_HERE]*/', '').replace('vec3 srgb_to_linear(vec3 cs);', '')
    variants = (('classic', '#version 400\nuniform sampler2D tex0;\nvec4 diffuseLookup(vec2 t) { return texture(tex0, t); }\n' + SRGB, b'tex0'),
                ('glTF', '#version 400\n#define LIGHT_THROUGH_GLTF 1\n' + SRGB, b'diffuseMap'))
    glDrawBuffers = fn('glDrawBuffers', None, I, P)
    glEnable = fn('glEnable', None, U)
    glBlendFunc = fn('glBlendFunc', None, U, U)
    glBlendEquationi = fn('glBlendEquationi', None, U, U)
    glClearTexImage = fn('glClearTexImage', None, U, I, U, U, P)
    n = 16
    white = texture(0x8058, n, n, np.ones((n, n, 4), np.float32), GL_RGBA)
    # (z, linear colour as the shader reads it with a white texture, alpha): two see-through faces, one at the solid cut, one invisible
    faces = [(0.7, (1.0, 0.0, 1.0), 0.5), (0.4, (0.0, 1.0, 1.0), 0.3), (0.2, (1.0, 1.0, 1.0), float(np.float32(0.98))), (0.1, (0.0, 0.0, 0.0), 0.002)]
    expect = np.ones(3)
    for _, c, a in faces[:2]:
        expect = expect * twin.tau_of(a, np.array(c))
    print('casters: shadowLightThroughF.glsl into an RGBA8 + R16 layer, multiply and MIN blending, no depth test')
    for name, head, sampler in variants:
        prog = program([(0x8B31, CASTER_VS), (0x8B30, head + body)])
        tau = texture(0x8058, n, n, None, GL_RGBA)
        zt = texture(0x822A, n, n, None, GL_RED)
        f = U()
        glGenFramebuffers(1, C.byref(f))
        glBindFramebuffer(0x8D40, f.value)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, tau, 0)
        glFramebufferTexture2D(0x8D40, 0x8CE1, TEX, zt, 0)
        glDrawBuffers(2, (U * 2)(0x8CE0, 0x8CE1))
        complete = glCheckFramebufferStatus(0x8D40) == 0x8CD5
        glClearTexImage(tau, 0, GL_RGBA, 0x1401, (C.c_ubyte * 4)(255, 255, 255, 255))
        glClearTexImage(zt, 0, GL_RED, 0x1403, (C.c_ushort * 1)(0xFFFF))
        glViewport(0, 0, n, n)
        glEnable(0x0BE2)                 # GL_BLEND
        glBlendFunc(0, 0x0300)           # GL_ZERO, GL_SRC_COLOR
        glBlendEquationi(1, 0x8007)      # GL_MIN
        use(prog)
        glActiveTexture(0x84C0)
        glBindTexture(TEX, white)
        glUniform1i(glGetUniformLocation(prog, sampler), 0)
        for z, c, a in faces:
            glUniform1f(glGetUniformLocation(prog, b'z'), z)
            # the classic shader takes the sRGB face colour to linear, the glTF one the texture: (0, 1) are the same in both
            glUniform4f(glGetUniformLocation(prog, b'colour'), c[0], c[1], c[2], a)
            glDrawArrays(4, 0, 3)
        glFinish()
        glBlendEquationi(1, 0x8006)      # GL_FUNC_ADD
        glDisable(0x0BE2)
        out_tau = np.empty((n, n, 4), np.float32)
        glBindTexture(TEX, tau)
        glGetTexImage(TEX, 0, GL_RGBA, GL_FLOAT, out_tau.ctypes.data)
        out_z = np.empty((n, n), np.float32)
        glBindTexture(TEX, zt)
        glGetTexImage(TEX, 0, GL_RED, GL_FLOAT, out_z.ctypes.data)
        got, depth = out_tau[..., :3], out_z
        err = float(np.abs(got - expect).max())
        near = float((0.4 - depth.min()) * 65535)
        far = float((depth.max() - 0.4) * 65535)
        ok = complete and err <= 2.0 / 255.0 and near <= 1e-3 and far <= 2.0 + 1e-3 and glGetError() == 0
        failures += not ok
        print('  %-7s tau %s against the product %s (largest error %.4f); depth %.3f to %.3f steps behind the nearest caster  %s' % (
            name, np.round(got[n // 2, n // 2], 4), np.round(expect, 4), err, -near, far, 'PASS' if ok else 'FAIL'))
        for t in (tau, zt):
            free(t)
    free(white)
    return failures


def builds_running():
    names = []
    for exe in ('cl.exe', 'link.exe'):
        r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ' + exe, '/FO', 'CSV', '/NH'], capture_output=True, text=True)
        if exe.lower() in r.stdout.lower():
            names.append(exe)
    return names


def timing(rng):
    q = U()
    fn('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
    begin, end = fn('glBeginQuery', None, U, U), fn('glEndQuery', None, U)
    get64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
    atlas, n, ox, oy = 4096, 1024, 1024, 2048
    depth = np.zeros((atlas, atlas), np.float32)
    depth[oy:oy + n, ox:ox + n] = plates_tile(n, rng)
    atlas_tex = texture(0x8CAC, atlas, atlas, depth, 0x1902)
    empty = layer_atlas(atlas, ox, oy, n)
    # a pane over the whole tile at 2.2 m (in front of every plate below it and of the floor): every lit receiver reads the filter
    pane_z = np.full((n, n), float(np.clip(depth_of(np.zeros(1), np.zeros(1), np.full(1, 2.2))[0], 0, 1)))
    pane_layer = twin.Layer(n)
    pane_layer.draw(np.ones((n, n), bool), 0.5, (0.9, 0.6, 0.3), pane_z)
    pane = layer_atlas(atlas, ox, oy, n, pane_layer.tau, pane_layer.zt)
    W, H = 2560, 1440
    tgt = Target(W, H)
    cases = [('base (L1 + L2)', prog_base, 0, empty), ('proj_through 0', prog_real, 0, empty),
             ('proj_through 1, empty layer', prog_real, 1, empty), ('proj_through 1, behind a pane', prog_real, 1, pane)]
    samples = {c[0]: [] for c in cases}
    running = builds_running()
    for r in range(10):
        for label, prog, through, (tau_tex, zt_tex) in cases:  # interleaved rounds
            set_lookup(prog, atlas_tex, ox, oy, n, atlas, through, tau_tex, zt_tex)
            floor_grid(prog, W, H)
            glBindFramebuffer(0x8D40, tgt.fbo)
            glViewport(0, 0, W, H)
            glDrawArrays(4, 0, 3)
            glFinish()
            begin(0x88BF, q.value)
            for _ in range(20):
                glDrawArrays(4, 0, 3)
            end(0x88BF)
            v = C.c_uint64()
            get64(q.value, 0x8866, C.byref(v))
            if r:
                samples[label].append(v.value / 1e6 / 20)
    mp = W * H / 1e6
    base = statistics.median(samples[cases[0][0]])
    print('time: GPU ms of the lookup, %d x %d floor receivers (every one lit by the lamp; tile %d), median of 9 x 20 draws, interleaved%s' % (
        W, H, n, '; builds running: ' + ', '.join(running) if running else '; no build running'))
    for label, *_ in cases:
        ms = statistics.median(samples[label])
        print('  %-30s %.3f ms (%.4f ms per lit megapixel), %+.1f %% of the base; spread %.3f..%.3f' % (
            label, ms, ms / mp, 100 * (ms / base - 1), min(samples[label]), max(samples[label])))
    for t in (atlas_tex, *empty, *pane, tgt.tex):
        free(t)


def main():
    rng = np.random.default_rng(opts.seed)
    failures = casters() + identity(rng) + twin_check(rng)
    if opts.time:
        timing(rng)
    print('L3 gate', 'PASS' if failures == 0 else 'FAIL (%d)' % failures)
    return 0 if failures == 0 else 1


sys.exit(main())
