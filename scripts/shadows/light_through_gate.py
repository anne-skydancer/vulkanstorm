#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Lamp shadows L3: the GPU gates of the light-through layer (offline GL, no viewer; Windows with an OpenGL 4.x driver).

The code under test is compiled from the checkout's shadowLightThroughF.glsl (the casters), shadowStochasticUtil.glsl (its G1 block
and sampleProjectorShadow), shadowUtil.glsl (the SHADOW_COMMON helpers) and class3 spotLightF.glsl, the base's from git (--base,
default b62d0fca18: lamp shadows L1 + L2, before L3).

  link      the two caster programs as the viewer composes them (LLShaderMgr::loadShaderFile's header at the file's EXTRA_CODE
            marker: the GLSL 4.20 line, the stage and G-buffer defines, the program's permutation, the indexed-texture code of
            sIndexedTextureChannels 4 for the classic one, GBufferInfo; the objects attachShaderFeatures adds: textureUtilV.glsl,
            indexedTextureV.glsl or nonindexedTextureV.glsl, srgbF.glsl): every real file compiles and the programs link.
  casters   shadowLightThroughF.glsl in both variants (classic, LIGHT_THROUGH_GLTF), drawn as LLLightThroughLayer::DrawScope draws it:
            an RGBA8 + R32F framebuffer cleared to "no caster" with a D32F depth attachment holding an opaque surface,
            glBlendFunc(GL_ZERO, GL_SRC_COLOR), glBlendEquationi(1, GL_MIN), the depth test GL_LEQUAL without writes, back faces
            culled. Six full-screen faces: two see-through ones in front of the opaque surface (their product within the 8 bits), one
            at the solid cut and one invisible, one behind the opaque surface and one facing away (all four left out); the stored depth
            is the nearest see-through face's as it is; the opaque depth is untouched.
  identity  sampleProjectorShadow of the checkout against the base's on the same depth tile (a downward lamp over a floor with plates
            at several heights, the tile inside a bigger atlas) and the same floor receivers, every channel: with proj_through 0, and
            with proj_through 1 on an empty layer (cleared to "no caster"): max abs difference 0. A lamp whose tiles hold no see-through
            caster is unchanged bit for bit; a tile cleared to no caster multiplies by exactly 1.
  spot      class3 spotLightF.glsl's pool branch (PROJECTOR_SHADOW_POOL, MULTI_SPOTLIGHT), the checkout's (a colour visibility)
            against the base's (a float), with the same stubs for everything it calls and the same visibility in every channel, over
            classic and PBR pixels, both classic_mode values: max abs difference 0, so an opaque-only lamp's light is the base's bit for
            bit, its six multiplies included (on this GPU and driver).
  twin      stThroughFilter on layers built by the CPU twin (scripts/shadows/lamp_filter_twin.py: Layer, through_filter), each with its
            depth tile: glass behind a frame, painted leading, two stacked panes, a pane hidden under the floor, a red pane's edge, a
            window flush in a tilted wall and random stacks of tinted casters at random depths over random opaque depths; the layer's
            tile in an atlas whose other texels hold a black caster and an opaque surface at the near plane (any read past the tile
            shows), random receivers over the whole tile with random receiver planes and radii from the floor to the cap: the largest
            difference to the twin per scene, weighted by the share of the filter's disc the depth does not block (the hardware's
            bilinear weights have 8 bits of sub-texel precision, the twin's are exact, and the filter divides by the open share). Gate:
            within one step of the layer's 8 bits (1/255); the unweighted largest difference is printed beside it.
  --time    GPU ms of the lookup over a 2560 x 1440 grid of floor receivers inside the lamp's frustum (timer queries, median of 9 x 20
            draws, interleaved rounds): the base, the checkout with proj_through 0, with 1 on an empty layer (a tile that met a blended
            face writing nothing: the search alone), and with 1 behind a pane over the whole tile (the search and the filter), in ms
            and per lit megapixel; the run says which builds were running. --first REF times an earlier L3 lookup beside them (its
            empty layer and its pane, the depth in its R16).

    python scripts/shadows/light_through_gate.py [--time [--first REF]] [--base REF] [--seed N]
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
ap.add_argument('--first', default=None, help='with --time: also time an earlier L3 lookup in the same rounds, its layer depth in R16 one '
                'step farther (e082aadcf6: the first L3 build)')
opts = ap.parse_args()
sys.argv = [sys.argv[0]]  # the GL context helper and the twin parse the command line themselves
CONTEXT = REPO / 'scripts/perf/gl_test_context.py'
exec(compile(CONTEXT.read_text(encoding='utf-8'), str(CONTEXT), 'exec'))  # window, context, fn(), program(), use, U, I, P, C
sys.path.insert(0, str(HERE))
import lamp_filter_twin as twin  # noqa: E402  (the CPU twin: Layer, through_filter, scenes)

SHADERS = 'indra/newview/app_settings/shaders'
UTIL_P = SHADERS + '/class1/deferred/shadowStochasticUtil.glsl'
COMMON_P = SHADERS + '/class1/deferred/shadowUtil.glsl'
SPOT_P = SHADERS + '/class3/deferred/spotLightF.glsl'


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
# the receivers of the twin comparison, from two textures: tile u, v, depth, pen (texels per unit of depth); the receiver plane's
# gradient (x, y) and one texel of depth
TWIN = r'''
uniform sampler2D receivers;
uniform sampler2D planes;
uniform sampler2D twin_depth;
uniform vec4 layer_tile;
uniform float z_bias;
out vec4 oc;
void main()
{
    vec4 r = texelFetch(receivers, ivec2(gl_FragCoord.xy), 0);
    vec4 g = texelFetch(planes, ivec2(gl_FragCoord.xy), 0);
    oc = vec4(stThroughFilter(proj_through_atlas, proj_through_depth, twin_depth, layer_tile, r.xyz, g.xy, z_bias, g.z, vec2(r.w),
                              vec2(ST_SPOT_KERNEL_TEXELS_MAX)), 1.0);
}
'''
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'


def build(src, body):
    defines, fns, sample = src
    return program([(0x8B31, VS), (0x8B30, PREAMBLE + defines + '\n' + fns + sample + FLOOR % body)])


prog_base = build(BASE, 'vec4(vec3(sampleProjectorShadow(pos, vec3(0.0, 0.0, 1.0), gl_FragCoord.xy / grid)), 1.0)')
prog_real = build(REAL, 'vec4(sampleProjectorShadow(pos, vec3(0.0, 0.0, 1.0), gl_FragCoord.xy / grid), 1.0)')
prog_twin = program([(0x8B31, VS), (0x8B30, PREAMBLE + REAL[0] + '\n' + section(REAL_UTIL, '// <G1-BEGIN>', '// <G1-END>') + TWIN)])
prog_first = build(sources(git_show(opts.first, UTIL_P), git_show(opts.first, COMMON_P)),
                   'vec4(sampleProjectorShadow(pos, vec3(0.0, 0.0, 1.0), gl_FragCoord.xy / grid), 1.0)') if opts.first else None

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
glEnable = fn('glEnable', None, U)
glDisable = fn('glDisable', None, U)
glFinish = fn('glFinish', None)
glGetError = fn('glGetError', U)
glDeleteTextures = fn('glDeleteTextures', None, I, C.POINTER(U))
glDeleteFramebuffers = fn('glDeleteFramebuffers', None, I, C.POINTER(U))
TEX = 0x0DE1
GL_FLOAT, GL_RED, GL_RGBA, GL_DEPTH = 0x1406, 0x1903, 0x1908, 0x1902
RGBA8, R32F, RGBA32F, D32F = 0x8058, 0x822E, 0x8814, 0x8CAC
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


def read_tex(tex, w, h, fmt, channels):
    buf = np.empty((h, w, channels), np.float32) if channels > 1 else np.empty((h, w), np.float32)
    glBindTexture(TEX, tex)
    glGetTexImage(TEX, 0, fmt, GL_FLOAT, buf.ctypes.data)
    return buf


class Target:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.tex = texture(RGBA32F, w, h, None, GL_RGBA)
        f = U()
        glGenFramebuffers(1, C.byref(f))
        self.fbo = f.value
        glBindFramebuffer(0x8D40, self.fbo)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, self.tex, 0)
        assert glCheckFramebufferStatus(0x8D40) == 0x8CD5

    def read(self):
        return read_tex(self.tex, self.w, self.h, GL_RGBA, 4)

    def free(self):
        free(self.tex)
        glDeleteFramebuffers(1, C.byref(U(self.fbo)))


vao = U()
glGenVertexArrays(1, C.byref(vao))
glBindVertexArray(vao.value)
glDisable(0x0B71)  # depth test


# ---- the programs as the viewer composes them (LLShaderMgr::loadShaderFile, attachShaderFeatures)
GBUFFER_DEFINES = '''#define GBUFFER_FLAG_SKIP_ATMOS   0.0
#define GBUFFER_FLAG_HAS_ATMOS    0.34
#define GBUFFER_FLAG_HAS_PBR      0.67
#define GBUFFER_FLAG_HAS_HDRI      1.0
#define GET_GBUFFER_FLAG(data, flag)    (abs(data-flag)< 0.1)
'''
GBUFFER_INFO = 'struct GBufferInfo { vec4 albedo; vec4 specular; vec3 normal; vec4 emissive; float gbufferFlag; float envIntensity; };\n'


def indexed_lookup(channels, nvidia=True):
    """the indexed-texture code loadShaderFile puts in a fragment shader (NVIDIA's if-chain, else the switch)"""
    code = '#define HAS_DIFFUSE_LOOKUP\n' + ''.join('uniform sampler2D tex%d;\n' % i for i in range(channels))
    if channels > 1:
        code += 'flat in int vary_texture_index;\n'
    code += 'vec4 diffuseLookup(vec2 texcoord)\n{\n'
    if channels == 1:
        return code + 'return texture(tex0, texcoord);\n}\n'
    if nvidia:
        code += ''.join('\t%sif (vary_texture_index == %d) { return texture(tex%d, texcoord); }\n' % ('else ' if i else '', i, i) for i in range(channels))
        return code + '\treturn vec4(1,0,1,1);\n}\n'
    code += '\tvec4 ret = vec4(1,0,1,1);\n\tswitch (vary_texture_index)\n\t{\n'
    code += ''.join('\t\tcase %d: return texture(tex%d, texcoord);\n' % (i, i) for i in range(channels))
    return code + '\t}\n\treturn ret;\n}\n'


def viewer_text(path, fragment, defines=None, channels=0, nvidia=True, text=None):
    """a shader file as loadShaderFile hands it to the driver: #version 420 (a GLSL 4.20+ device), then its extra code at the file's
    EXTRA_CODE marker (or at the top without one)"""
    text = text if text is not None else (REPO / SHADERS / 'class1' / path).read_text(encoding='utf-8')
    extra = '#define FRAGMENT_SHADER 1\n' if fragment else '#define VERTEX_SHADER 1\n'
    extra += GBUFFER_DEFINES
    for k, v in (defines or {}).items():
        extra += '#define %s %s\n' % (k, v)
    if channels > 0 and fragment:
        extra += indexed_lookup(channels, nvidia)
    extra += GBUFFER_INFO
    lines = text.replace('\r\n', '\n').split('\n')
    for i, line in enumerate(lines):
        if '[EXTRA_CODE_HERE]' in line:
            lines[i] = extra.rstrip('\n')
            return '#version 420\n' + '\n'.join(lines)
    return '#version 420\n' + extra + text


def link_check():
    """the caster programs of llviewershadermgr.cpp as the viewer builds them: compile and link"""
    failures = 0
    vendor = get_string(0x1F00).decode()
    nvidia = 'NVIDIA' in vendor
    print('link: the light-through caster programs as the viewer composes them (%s)' % vendor)
    cases = (
        ('Deferred Shadow Light Through Shader', 'deferred/shadowAlphaMaskV.glsl', 'objects/indexedTextureV.glsl', {}, 4),
        ('Deferred GLTF Shadow Light Through Shader', 'deferred/pbrShadowAlphaMaskV.glsl', 'objects/nonindexedTextureV.glsl', {'LIGHT_THROUGH_GLTF': '1'}, 0),
    )
    for name, vs, tex_obj, defines, channels in cases:
        stages = [(0x8B31, viewer_text(vs, False, defines)),
                  (0x8B31, viewer_text('deferred/textureUtilV.glsl', False)),
                  (0x8B31, viewer_text(tex_obj, False)),
                  (0x8B30, viewer_text('deferred/shadowLightThroughF.glsl', True, defines, channels, nvidia)),
                  (0x8B30, viewer_text('environment/srgbF.glsl', True))]
        try:
            prog = program(stages)
            ok = prog != 0 and glGetError() == 0
            detail = 'compiles and links'
        except AssertionError as e:
            ok = False
            detail = 'FAILED: %s' % str(e).strip()[:400]
        failures += not ok
        print('  %-44s %s  %s' % (name, detail, 'PASS' if ok else 'FAIL'))
    return failures


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


def layer_atlas(atlas, ox, oy, n, tau=None, zt=None, zt_format=None):
    """the light-through layer: the tile's rect holds tau (n x n x 3) and zt (n x n), every other texel a black caster at the near
    plane (tau 0, depth 0): a read past the tile shows. zt_format: R32F, or an earlier build's R16"""
    t = np.zeros((atlas, atlas, 4), np.float32)
    z = np.zeros((atlas, atlas), np.float32)
    t[oy:oy + n, ox:ox + n, :3] = 1.0 if tau is None else tau
    t[oy:oy + n, ox:ox + n, 3] = 1.0
    z[oy:oy + n, ox:ox + n] = 1.0 if zt is None else zt
    return texture(RGBA8, atlas, atlas, t, GL_RGBA, LINEAR), texture(zt_format or R32F, atlas, atlas, z, GL_RED)


def identity(rng):
    failures = 0
    atlas, n, ox, oy = 2048, 1024, 512, 1024
    depth = np.zeros((atlas, atlas), np.float32)          # blockers at the near plane everywhere outside the tile
    depth[oy:oy + n, ox:ox + n] = plates_tile(n, rng)
    atlas_tex = texture(D32F, atlas, atlas, depth, GL_DEPTH)
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
    for t in (atlas_tex, empty_tau, empty_zt):
        free(t)
    tgt.free()
    return failures


# ---- class3 spotLightF.glsl's pool branch: the checkout's colour visibility against the base's float, the same stubs
SPOT_STUBS = r'''
float st_h(vec2 p, float k) { return fract(sin(dot(p, vec2(12.9898, 78.233)) + k) * 43758.5453); }
vec2 getScreenCoord(vec4 clip) { return gl_FragCoord.xy / 512.0; }
vec4 getPosition(vec2 tc) { return vec4(st_h(tc, 1.0) * 4.0 - 2.0, st_h(tc, 2.0) * 4.0 - 2.0, -1.0 - st_h(tc, 3.0) * 8.0, 1.0); }
bool clipProjectedLightVars(vec3 c, vec3 pos, out float dist, out float l_dist, out vec3 lv, out vec4 proj_tc)
{
    lv = c - pos;
    dist = length(lv);
    l_dist = dist * 0.5;
    proj_tc = vec4(st_h(pos.xy, 4.0) * 1.1 - 0.05, st_h(pos.xy, 5.0) * 1.1 - 0.05, st_h(pos.xy, 6.0) + 0.01, 1.0);
    return st_h(pos.xy, 7.0) < 0.05;
}
float calcLegacyDistanceAttenuation(float distance, float falloff) { return max(1.0 - distance / 12.0, 0.0) * falloff; }
GBufferInfo getGBuffer(vec2 tc)
{
    GBufferInfo g;
    g.albedo = vec4(st_h(tc, 8.0), st_h(tc, 9.0), st_h(tc, 10.0), 1.0);
    g.specular = vec4(st_h(tc, 11.0), st_h(tc, 12.0), st_h(tc, 13.0), st_h(tc, 14.0) < 0.3 ? 0.0 : st_h(tc, 14.5));
    g.normal = normalize(vec3(st_h(tc, 15.0) - 0.5, st_h(tc, 16.0) - 0.5, st_h(tc, 17.0) - 0.2));
    g.emissive = vec4(0.0);
    g.gbufferFlag = st_h(tc, 18.0) < 0.5 ? GBUFFER_FLAG_HAS_PBR : GBUFFER_FLAG_HAS_ATMOS;
    g.envIntensity = st_h(tc, 19.0) < 0.5 ? 0.0 : st_h(tc, 20.0);
    return g;
}
void calcHalfVectors(vec3 lv, vec3 n, vec3 v, out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist)
{
    l = normalize(lv);
    h = normalize(l + v);
    nh = dot(n, h);
    nl = dot(n, l);
    nv = dot(n, v);
    vh = dot(v, h);
    lightDist = length(lv);
}
vec3 getProjectedLightDiffuseColor(float d, vec2 uv) { return vec3(uv, d * 0.1); }
vec3 getProjectedLightAmbiance(float amb_da, float att, float lit, float nl, float noise, vec2 uv) { return vec3(amb_da * att, uv.x * lit + 0.1, nl * noise); }
vec3 srgb_to_linear(vec3 cs) { return cs * cs; }
vec4 texture2DLodSpecular(vec2 tc, float lod) { return vec4(tc, lod * 0.1, 1.0); }
void pbrPunctual(vec3 diffuseColor, vec3 specularColor, float r, float m, vec3 n, vec3 v, vec3 l, out float nl, out vec3 diff, out vec3 spec)
{
    nl = dot(n, l);
    diff = diffuseColor * max(nl, 0.0) * 0.31830988;
    spec = specularColor * (r * 0.7 + m * 0.3 + 0.01);
}
float st_shadow(vec2 ps) { float s = st_h(ps, 21.0); return s < 0.2 ? 0.0 : (s > 0.8 ? 1.0 : st_h(ps, 22.0)); } // umbra, lit and penumbra values
'''
SPOT_VS = '#version 420\nout vec4 vary_fragcoord;\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); vary_fragcoord = gl_Position; }'


def spot_identity():
    """the six multiplies of the pool branch on a colour visibility against a float one: bit for bit"""
    failures = 0
    defines = {'PROJECTOR_SHADOW_POOL': '1', 'MULTI_SPOTLIGHT': '1'}
    variants = {}
    for label, text, stub in (('base', git_show(opts.base, SPOT_P), 'float sampleProjectorShadow(vec3 pos, vec3 norm, vec2 ps) { return st_shadow(ps); }\n'),
                              ('checkout', (REPO / SPOT_P).read_text(encoding='utf-8'),
                               'vec3 sampleProjectorShadow(vec3 pos, vec3 norm, vec2 ps) { return vec3(st_shadow(ps)); }\n')):
        fs = viewer_text(None, True, defines, text=text)
        stubs = viewer_text(None, True, defines, text='/*[EXTRA_CODE_HERE]*/\n' + SPOT_STUBS + stub)
        variants[label] = program([(0x8B31, SPOT_VS), (0x8B30, fs), (0x8B30, stubs)])
    w = h = 512
    tgt = Target(w, h)
    rng = np.random.default_rng(5)
    lf = texture(RGBA32F, 64, 64, rng.uniform(0.0, 1.0, (64, 64, 4)), GL_RGBA, LINEAR)
    outs = {}
    for label, prog in variants.items():
        for classic in (0, 1):
            use(prog)
            glActiveTexture(0x84C0)
            glBindTexture(TEX, lf)
            glUniform1i(glGetUniformLocation(prog, b'lightFunc'), 0)
            for unit, sampler in ((1, b'environmentMap'), (2, b'lightMap')):   # unused here; never on lightFunc's unit
                glUniform1i(glGetUniformLocation(prog, sampler), unit)
            glUniform3f(glGetUniformLocation(prog, b'center'), 0.3, -0.2, -3.0)
            glUniform3f(glGetUniformLocation(prog, b'proj_origin'), 0.5, 0.4, -2.0)
            glUniform3f(glGetUniformLocation(prog, b'color'), 1.0, 0.8, 0.6)
            glUniform3f(glGetUniformLocation(prog, b'proj_p'), 0.0, 0.0, -4.0)
            glUniform3f(glGetUniformLocation(prog, b'proj_n'), 0.0, 0.3, 1.0)
            glUniform1f(glGetUniformLocation(prog, b'falloff'), 1.0)
            glUniform1f(glGetUniformLocation(prog, b'proj_ambiance'), 0.4)
            glUniform1f(glGetUniformLocation(prog, b'proj_lod'), 6.0)
            glUniform1i(glGetUniformLocation(prog, b'proj_shadow_idx'), 0)
            glUniform1i(glGetUniformLocation(prog, b'classic_mode'), classic)
            m = np.eye(4, dtype=np.float32) * 0.25
            m[3, 3] = 1.0
            m[3, 0] = m[3, 1] = 0.5
            glUniformMatrix4fv(glGetUniformLocation(prog, b'proj_mat'), 1, 0, np.ascontiguousarray(m).ctypes.data)
            glBindFramebuffer(0x8D40, tgt.fbo)
            glViewport(0, 0, w, h)
            glDrawArrays(4, 0, 3)
            glFinish()
            outs[(label, classic)] = tgt.read()
    print('spot: class3 spotLightF.glsl, pool branch, the checkout (colour visibility) against %s (float), %d x %d pixels' % (opts.base, w, h))
    for classic in (0, 1):
        a, b = outs[('base', classic)], outs[('checkout', classic)]
        d = np.abs(a.astype(np.float64) - b.astype(np.float64))
        lit = float(np.mean(np.any(a[..., :3] > 0, axis=-1)))
        same = bool(np.array_equal(a.view(np.uint32), b.view(np.uint32)))
        failures += not same
        print('  classic_mode %d: max abs diff %.3g, %d differing words (%.0f %% of the pixels lit)  %s' % (
            classic, d.max(), int(np.count_nonzero(a.view(np.uint32) != b.view(np.uint32))), 100 * lit, 'PASS' if same else 'FAIL'))
    failures += glGetError() != 0
    free(lf)
    tgt.free()
    return failures


def twin_scenes(rng):
    """layers of the CPU twin with their depth tiles: (name, Layer, depth, plane gradient scale)"""
    n = 96
    t = twin.WINDOW_M / n
    c = (np.arange(n) + 0.5) * t
    x, y = np.meshgrid(c, c)
    out = []
    opaque, alpha, _ = twin.scene_glass(x, y)
    depth = np.where(opaque, twin.Z_FRAME, twin.Z_FLOOR)
    glass = twin.Layer(n)
    glass.draw(np.ones(opaque.shape, bool), alpha, (0.0, 0.0, 0.0), twin.Z_WINDOW, depth)
    out.append(('glass 0.5 behind a frame', glass, depth, 0.0))
    leading_depth = np.where(opaque, twin.Z_WINDOW, twin.Z_FLOOR)
    leading = twin.Layer(n)
    leading.draw(~opaque, alpha, (0.9, 0.3, 0.1), twin.Z_WINDOW, leading_depth)
    out.append(('painted leading', leading, leading_depth, 0.0))
    u = (np.arange(n) + 0.5) / n
    X, Y = np.meshgrid(u, u)
    floor = np.full((n, n), twin.Z_FLOOR)
    stack = twin.Layer(n)
    stack.draw(X < 2 / 3, 0.5, (0.5, 0.5, 0.5), twin.map_depth(2.5), floor)
    stack.draw(X > 1 / 3, 0.6, (1.0, 0.0, 0.0), twin.Z_WINDOW, floor)
    out.append(('two stacked panes', stack, floor, 0.0))
    hidden = twin.Layer(n)
    hidden.draw(X < 2 / 3, 0.5, (0.5, 0.5, 0.5), twin.map_depth(2.5), floor)
    hidden.draw(X > 1 / 3, 0.6, (1.0, 0.0, 0.0), twin.map_depth(5.0), floor)
    out.append(('a pane hidden under the floor', hidden, floor, 0.0))
    edge = twin.Layer(n)
    edge.draw(((X - 0.503) * -0.17 + (Y - 0.497) * 0.985) < 0, 0.6, (1.0, 0.0, 0.0), twin.Z_WINDOW, floor)
    out.append(('a red pane\'s edge', edge, floor, 0.0))
    wall, gz, _ = twin.tilted_wall(n, t, 60, -1.0)
    win_depth = np.where(X > 0.5, twin.map_depth(8.0), wall)
    flush = twin.Layer(n)
    flush.draw(X > 0.5, 0.5, (0.2, 0.6, 0.9), wall, win_depth)
    out.append(('a window flush in a wall at 60 deg', flush, win_depth, gz))
    rand_depth = np.full((n, n), 0.999)
    for _ in range(12):
        x0, y0 = rng.integers(0, n, 2)
        w, h = rng.integers(2, n // 4, 2)
        rand_depth[y0:y0 + h, x0:x0 + w] = np.minimum(rand_depth[y0:y0 + h, x0:x0 + w], rng.uniform(0.95, 0.995))
    rand = twin.Layer(n)
    for _ in range(40):
        x0, y0 = rng.integers(0, n, 2)
        w, h = rng.integers(2, n // 3, 2)
        cover = np.zeros((n, n), bool)
        cover[y0:y0 + h, x0:x0 + w] = True
        rand.draw(cover, rng.uniform(0.0, 1.0), rng.uniform(0.0, 1.0, 3), rng.uniform(0.95, 0.99), rand_depth)
    out.append(('40 random tinted casters', rand, rand_depth, 0.05))
    return out


def twin_check(rng):
    failures = 0
    atlas, ox, oy = 256, 96, 64
    m = 192
    tgt = Target(m, m)
    print('twin: stThroughFilter against the CPU twin, %d x %d random receivers per scene, the tile at (%d, %d) of a %d atlas' % (m, m, ox, oy, atlas))
    for name, layer, depth, g_scale in twin_scenes(rng):
        n = layer.zt.shape[0]
        tau_tex, zt_tex = layer_atlas(atlas, ox, oy, n, layer.tau, layer.zt)
        d_atlas = np.zeros((atlas, atlas), np.float32)    # an opaque surface at the near plane outside the tile: a read past it shows
        d_atlas[oy:oy + n, ox:ox + n] = depth
        depth_tex = texture(D32F, atlas, atlas, d_atlas, GL_DEPTH)
        cx = rng.uniform(-0.02, 1.02, (m, m))  # past the edges too: the tile's clamp
        cy = rng.uniform(-0.02, 1.02, (m, m))
        cz = rng.uniform(0.95, 0.999, (m, m))
        pen = np.exp(rng.uniform(np.log(30.0), np.log(3000.0), (m, m)))
        z_tex = np.exp(rng.uniform(np.log(1e-5), np.log(4e-4), (m, m)))
        if g_scale == 0.0:
            gx = gy = np.zeros((m, m))
        elif name.startswith('a window flush'):
            # receivers on the wall's plane (the wall and the glass in it), with the wall's own gradient
            cz = twin.Z_FLOOR + g_scale * (np.clip(cx, 0.0, 1.0) - 0.5)
            gx, gy = np.full((m, m), g_scale), np.zeros((m, m))
        else:
            gx, gy = rng.uniform(-g_scale, g_scale, (m, m)), rng.uniform(-g_scale, g_scale, (m, m))
        z_bias = 2.0e-5
        rec = texture(RGBA32F, m, m, np.stack([cx, cy, cz, pen], -1), GL_RGBA)
        pla = texture(RGBA32F, m, m, np.stack([gx, gy, z_tex, np.zeros((m, m))], -1), GL_RGBA)
        use(prog_twin)
        for unit, (tex, uname) in enumerate(((rec, b'receivers'), (tau_tex, b'proj_through_atlas'), (zt_tex, b'proj_through_depth'),
                                             (depth_tex, b'twin_depth'), (pla, b'planes'))):
            glActiveTexture(0x84C0 + unit)
            glBindTexture(TEX, tex)
            glUniform1i(glGetUniformLocation(prog_twin, uname), unit)
        glUniform4f(glGetUniformLocation(prog_twin, b'layer_tile'), float(ox), float(oy), float(n), 1.0 / atlas)
        glUniform1f(glGetUniformLocation(prog_twin, b'z_bias'), z_bias)
        glBindFramebuffer(0x8D40, tgt.fbo)
        glViewport(0, 0, m, m)
        glDrawArrays(4, 0, 3)
        glFinish()
        gpu = tgt.read()[..., :3].astype(np.float64)
        f32 = lambda a: a.astype(np.float32).astype(np.float64)  # the receivers as the GPU reads them
        args = (layer, depth, f32(cx), f32(cy), f32(cz), f32(pen), float(np.float32(z_bias)), f32(z_tex), (f32(gx), f32(gy)))
        cpu, open_share = twin.through_filter(*args, want_open=True)
        # a receiver on the edge of a decision (a caster or an opaque texel within float rounding of its reference: the GPU's single
        # precision and fused operations may take the other side) is left out of the gate when shifting every reference by 2e-6 either
        # way moves the twin's value by more than the gate
        lo = twin.through_filter(*args, eps=-2e-6)
        hi = twin.through_filter(*args, eps=2e-6)
        edge = np.any(np.abs(lo - hi) * open_share[..., None] > 0.5 / 255.0, axis=-1)
        d = np.abs(gpu - cpu)
        weighted = d * open_share[..., None]
        tinted = float(np.mean(np.any(cpu < 0.999, axis=-1)))
        gated = weighted[~edge]
        ok = bool(gated.max() <= 1.0 / 255.0) and glGetError() == 0
        failures += not ok
        print('  %-36s weighted max %.5f, max %.5f, 99.9 %% %.5f (%.0f %% of the receivers tinted; %d on the edge of a decision left out, their weighted max %.5f)  %s' % (
            name, gated.max(), d.max(), np.percentile(d, 99.9), 100 * tinted, int(edge.sum()), float(weighted[edge].max()) if edge.any() else 0.0,
            'PASS' if ok else 'FAIL'))
        for tex in (tau_tex, zt_tex, depth_tex, rec, pla):
            free(tex)
    tgt.free()
    return failures


CASTER_VS = '''#version 400
uniform float z;
uniform vec4 colour;
uniform int flip;
out vec4 vertex_color;
out vec2 vary_texcoord0;
void main()
{
    vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1);
    p = flip != 0 ? p.yx : p;   // the same triangle wound the other way: a face facing away
    gl_Position = vec4(p, z * 2.0 - 1.0, 1.0);
    vertex_color = colour;
    vary_texcoord0 = p * 0.5 + 0.5;
}'''
SRGB = 'vec3 srgb_to_linear(vec3 cs) { return pow(cs, vec3(2.2)); }\n'   # a stand-in: the colours below are 0 or 1 in sRGB, or a factor


def casters():
    """the caster pass as the viewer draws it (LLLightThroughLayer::DrawScope's state) on a 16 x 16 layer over an opaque surface"""
    failures = 0
    text = (REPO / SHADERS / 'class1/deferred/shadowLightThroughF.glsl').read_text(encoding='utf-8')
    body = text.replace('/*[EXTRA_CODE_HERE]*/', '').replace('vec3 srgb_to_linear(vec3 cs);', '')
    variants = (('classic', '#version 400\nuniform sampler2D tex0;\nvec4 diffuseLookup(vec2 t) { return texture(tex0, t); }\n' + SRGB, b'tex0'),
                ('glTF', '#version 400\n#define LIGHT_THROUGH_GLTF 1\n' + SRGB, b'diffuseMap'))
    glDrawBuffers = fn('glDrawBuffers', None, I, P)
    glBlendFunc = fn('glBlendFunc', None, U, U)
    glBlendEquationi = fn('glBlendEquationi', None, U, U)
    glClearTexImage = fn('glClearTexImage', None, U, I, U, U, P)
    glDepthFunc = fn('glDepthFunc', None, U)
    glDepthMask = fn('glDepthMask', None, C.c_ubyte)
    glCullFace = fn('glCullFace', None, U)
    n = 16
    opaque_z = 0.6
    white = texture(RGBA8, n, n, np.ones((n, n, 4), np.float32), GL_RGBA)
    # (z, linear colour as the shader reads it with a white texture, alpha, facing away): two see-through faces in front of the opaque
    # surface, one at the solid cut, one invisible, one behind the opaque surface, one facing away from the lamp
    faces = [(0.5, (1.0, 0.0, 1.0), 0.5, 0), (0.4, (0.0, 1.0, 1.0), 0.3, 0), (0.2, (1.0, 1.0, 1.0), float(np.float32(0.98)), 0),
             (0.1, (0.0, 0.0, 0.0), 0.002, 0), (0.7, (0.0, 0.0, 0.0), 0.5, 0), (0.3, (0.0, 0.0, 0.0), 0.5, 1)]
    expect = np.ones(3)
    for _, c, a, _ in faces[:2]:
        expect = expect * twin.tau_of(a, np.array(c))
    print('casters: shadowLightThroughF.glsl into an RGBA8 + R32F layer over a D32F opaque surface at %.1f, multiply and MIN blending, the depth test, back faces culled' % opaque_z)
    for name, head, sampler in variants:
        prog = program([(0x8B31, CASTER_VS), (0x8B30, head + body)])
        tau = texture(RGBA8, n, n, None, GL_RGBA)
        zt = texture(R32F, n, n, None, GL_RED)
        dep = texture(D32F, n, n, None, GL_DEPTH)
        f = U()
        glGenFramebuffers(1, C.byref(f))
        glBindFramebuffer(0x8D40, f.value)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, tau, 0)
        glFramebufferTexture2D(0x8D40, 0x8CE1, TEX, zt, 0)
        glFramebufferTexture2D(0x8D40, 0x8D00, TEX, dep, 0)   # GL_DEPTH_ATTACHMENT
        glDrawBuffers(2, (U * 2)(0x8CE0, 0x8CE1))
        complete = glCheckFramebufferStatus(0x8D40) == 0x8CD5
        glClearTexImage(tau, 0, GL_RGBA, 0x1401, (C.c_ubyte * 4)(255, 255, 255, 255))
        glClearTexImage(zt, 0, GL_RED, GL_FLOAT, (C.c_float * 1)(1.0))
        glClearTexImage(dep, 0, GL_DEPTH, GL_FLOAT, (C.c_float * 1)(opaque_z))
        glViewport(0, 0, n, n)
        glEnable(0x0BE2)                 # GL_BLEND
        glBlendFunc(0, 0x0300)           # GL_ZERO, GL_SRC_COLOR
        glBlendEquationi(1, 0x8007)      # GL_MIN
        glEnable(0x0B71)                 # GL_DEPTH_TEST
        glDepthFunc(0x0203)              # GL_LEQUAL
        glDepthMask(0)
        glEnable(0x0B44)                 # GL_CULL_FACE
        glCullFace(0x0405)               # GL_BACK
        use(prog)
        glActiveTexture(0x84C0)
        glBindTexture(TEX, white)
        glUniform1i(glGetUniformLocation(prog, sampler), 0)
        for z, c, a, away in faces:
            glUniform1f(glGetUniformLocation(prog, b'z'), z)
            glUniform1i(glGetUniformLocation(prog, b'flip'), away)
            # the classic shader takes the sRGB face colour to linear, the glTF one the texture: (0, 1) are the same in both
            glUniform4f(glGetUniformLocation(prog, b'colour'), c[0], c[1], c[2], a)
            glDrawArrays(4, 0, 3)
        glFinish()
        glBlendEquationi(1, 0x8006)      # GL_FUNC_ADD
        glDisable(0x0BE2)
        glDisable(0x0B71)
        glDepthMask(1)
        glDisable(0x0B44)
        got = read_tex(tau, n, n, GL_RGBA, 4)[..., :3]
        depth = read_tex(zt, n, n, GL_RED, 1)
        surface = read_tex(dep, n, n, GL_DEPTH, 1)
        err = float(np.abs(got - expect).max())
        dz = float(np.abs(depth - np.float32(0.4)).max())
        kept = bool(np.all(surface == np.float32(opaque_z)))
        ok = complete and err <= 2.0 / 255.0 and dz <= 1e-6 and kept and glGetError() == 0
        failures += not ok
        print('  %-7s tau %s against the product %s (largest error %.4f); depth %.7f (the nearest, 0.4: off by %.2g); opaque depth untouched: %s  %s' % (
            name, np.round(got[n // 2, n // 2], 4), np.round(expect, 4), err, float(depth[n // 2, n // 2]), dz, kept, 'PASS' if ok else 'FAIL'))
        glBindFramebuffer(0x8D40, 0)
        glDeleteFramebuffers(1, C.byref(f))
        for t in (tau, zt, dep):
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
    plates = plates_tile(n, rng)
    depth[oy:oy + n, ox:ox + n] = plates
    atlas_tex = texture(D32F, atlas, atlas, depth, GL_DEPTH)
    empty = layer_atlas(atlas, ox, oy, n)
    # a pane over the whole tile at 2.2 m (in front of every plate below it and of the floor): every lit receiver reads the filter
    pane_z = np.full((n, n), float(np.clip(depth_of(np.zeros(1), np.zeros(1), np.full(1, 2.2))[0], 0, 1)))
    pane_layer = twin.Layer(n)
    pane_layer.draw(np.ones((n, n), bool), 0.5, (0.9, 0.6, 0.3), pane_z, plates.astype(np.float64))
    pane = layer_atlas(atlas, ox, oy, n, pane_layer.tau, pane_layer.zt)
    W, H = 2560, 1440
    tgt = Target(W, H)
    cases = [('base (L1 + L2)', prog_base, 0, empty), ('proj_through 0', prog_real, 0, empty),
             ('proj_through 1, empty layer', prog_real, 1, empty), ('proj_through 1, behind a pane', prog_real, 1, pane)]
    extra = []
    if prog_first:
        r16 = 0x822A
        extra = [layer_atlas(atlas, ox, oy, n, zt_format=r16),
                 layer_atlas(atlas, ox, oy, n, pane_layer.tau, np.minimum(pane_layer.zt + 1.0 / 65535.0, 1.0), zt_format=r16)]
        cases += [('%s, empty layer' % opts.first, prog_first, 1, extra[0]), ('%s, behind a pane' % opts.first, prog_first, 1, extra[1])]
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
    for t in (atlas_tex, *empty, *pane, *[tex for pair in extra for tex in pair]):
        free(t)
    tgt.free()


def main():
    rng = np.random.default_rng(opts.seed)
    failures = link_check() + casters() + identity(rng) + spot_identity() + twin_check(rng)
    if opts.time:
        timing(rng)
    print('L3 gate', 'PASS' if failures == 0 else 'FAIL (%d)' % failures)
    return 0 if failures == 0 else 1


sys.exit(main())
