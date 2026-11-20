# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Visual A/B cases for every converted program: the hand-written GLSL (reference) against the generated Slang program.

`python scripts/shaders/ab_harness.py programs [--only TEXT]` draws each case on a grid mesh or fullscreen triangle, with
both programs, into float targets (colour attachments and, where checked, depth), reads both back and compares
them. Colour uses the decision D3 tolerance and depth uses exact comparison where required.

The inputs are deterministic:
  * the uniforms are filled from the reference program's own active uniforms (introspection), by name and type: a view,
    projection and model-view-projection matrix, a normal matrix, a texture matrix with scale and offset, and a stable
    pseudo-random value per name; a case lists overrides for the few that need a particular value (an integer mode);
  * the samplers are filled by type (2D, cube, cube array) with smooth procedural data whose alpha straddles the
    alpha-test values, with overrides for inputs such as luminance mip levels and G-buffer flags;
  * a uniform the reference has and the candidate does not take is a failed case unless the case lists it as optional
    (the optimiser dropped it); a case where the reference draws nothing proves nothing and fails.
The mirror and reflection clip is run on and off for the G-buffer programs.
"""
import ctypes as C
import re
import zlib
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

import ab_harness as H
import slang_check as check
import slang_common as common
import slang_gen as gen

SIZE = 64
SCREEN_TRIANGLE = np.array([(-1, 1, 0), (-1, -3, 0), (3, 1, 0)], dtype='f4')
GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_DEPTH_ATTACHMENT = 0x8CAC, 0x1902, 0x8D00
GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8513, 0x8515
GL_DEPTH_TEST, GL_ALWAYS, GL_REPEAT = 0x0B71, 0x0207, 0x2901
GL_NEAREST = 0x2600
GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT = 0x8236, 0x8D94, 0x1405
GL_TEXTURE_2D_MULTISAMPLE, GL_MULTISAMPLE = 0x9100, 0x809D
GL_SAMPLE_SHADING, GL_SAMPLE_ALPHA_TO_COVERAGE = 0x8C36, 0x809E
GL_READ_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER = 0x8CA8, 0x8CA9
GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT = 0x4000, 0x0100
SAMPLER2D, SAMPLERCUBE, SAMPLERCUBEARRAY = 0x8B5E, 0x8B60, 0x900C
GL_FLOAT_T, VEC2, VEC3, VEC4, INT_T, IVEC2, IVEC3, IVEC4, MAT3, MAT4 = 0x1406, 0x8B50, 0x8B51, 0x8B52, 0x1404, 0x8B53, 0x8B54, 0x8B55, 0x8B5B, 0x8B5C
COMPONENTS = {GL_FLOAT_T: 1, VEC2: 2, VEC3: 3, VEC4: 4, INT_T: 1, IVEC2: 2, IVEC3: 3, IVEC4: 4}
GBUFFER_DEFS = ('#define GBUFFER_FLAG_SKIP_ATMOS 0.0\n#define GBUFFER_FLAG_HAS_ATMOS 0.34\n#define GBUFFER_FLAG_HAS_PBR 0.67\n'
                '#define GBUFFER_FLAG_HAS_HDRI 1.0\n#define GET_GBUFFER_FLAG(data, flag) (abs(data-flag)< 0.1)\n')


@dataclass
class Case:
    name: str
    vs: str                      # legacy vertex file under class1/
    fs: str                      # legacy fragment file under class1/
    gen: str                     # generated program id
    defines: dict = field(default_factory=dict)
    outputs: int = 1             # colour attachments compared
    depth: bool = False          # compare the depth target exactly
    skin: bool = False           # append objectSkinV and feed weight4 and a palette
    uniforms: dict = field(default_factory=dict)
    optional: tuple = ()         # uniforms the candidate may legitimately not take
    clip: bool = False           # also run with the mirror clip on (G-buffer programs)
    depth_info: bool = False     # report (never fail) how far the generated depth is from the hand-written one: the generated vertex
                                 # stage declares `invariant gl_Position`, the hand-written one does not
    flat_ok: tuple = ()          # colour targets the program legitimately writes as one constant (colour writes off, zero specular)
    variants: tuple = ()         # extra override dicts: the case is drawn once per variant (a cube face, a source layer)
    fragment_helpers: tuple = ()  # additional legacy fragment shader objects, as attached by the viewer
    vertex_helpers: tuple = ()    # additional legacy vertex shader objects
    fullscreen: bool = False
    size: int = SIZE
    texture_levels: dict = field(default_factory=dict)  # RGBA arrays per sampler, from level zero up
    point_textures: tuple = ()
    repeat_textures: tuple = ()
    hooks: dict = field(default_factory=dict)  # candidate loader defines for this A/B
    indexed_texture_channels: int = 0
    texcoord1_s_scale: float = 1.0
    geometry: str = ''           # optional legacy geometry shader under class1/
    required_colours: tuple = ()  # RGBA values that must appear in attachment zero in both paths
    samples: int = 1             # four samples compare resolved color and exact per-sample depth
    coverage: bool = False      # compare depth-derived coverage exactly; requires a nontrivial footprint
    mesh_builder: Callable | None = None


def _hash01(name, salt=0):
    return (zlib.crc32(f'{name}/{salt}'.encode()) & 0xFFFF) / 65535.0


def _matrices():
    view = np.array([[0.95, 0.0, 0.31, 0.1], [0.0, 1.0, 0.0, -0.05], [-0.31, 0.0, 0.95, -3.0], [0, 0, 0, 1]], dtype='f4')
    proj = H._perspective(60.0, 1.0, 0.1, 100.0)
    normal = np.linalg.inv(view[:3, :3]).T.astype('f4')
    tex = np.array([[0.9, 0.0, 0.0, 0.05], [0.0, 0.85, 0.0, 0.1], [0, 0, 1, 0], [0, 0, 0, 1]], dtype='f4')
    return view, proj, normal, tex


def fill_uniforms(gl, ref, overrides, clip):
    """Values for every active non-sampler uniform of the reference, by name and type, plus the case's overrides."""
    view, proj, normal, tex = _matrices()
    col = lambda m: [float(x) for x in np.asarray(m, 'f4').T.reshape(-1)]  # column-major, as GL takes it
    fixed = {
        'modelview_matrix': col(view), 'projection_matrix': col(proj), 'modelview_projection_matrix': col(proj @ view),
        'normal_matrix': col(normal), 'texture_matrix0': col(tex),
        'minimum_alpha': 0.4, 'mirror_flag': 1.0 if clip else 0.0, 'clipPlane': [0.0, 0.0, 1.0, 3.0],
        'screen_res': [float(SIZE), float(SIZE)], 'color': [0.8, 0.5, 0.3, 0.7],
    }
    values = {}
    for u in gl.uniforms(ref):
        if u['type'] in H.SAMPLER_TYPES:
            continue
        name = re.sub(r'\[0\]$', '', u['name'].split('.')[-1])
        if name in fixed:
            values[name] = fixed[name]
        elif u['type'] == MAT4:
            values[name] = col(np.eye(4, dtype='f4') * 0.9 + 0.02)
        elif u['type'] == MAT3:
            values[name] = [float(x) for x in (np.eye(3, dtype='f4') * 0.9 + 0.03).T.reshape(-1)]
        elif u['type'] in (INT_T, IVEC2, IVEC3, IVEC4):
            values[name] = [1] * COMPONENTS[u['type']] if COMPONENTS[u['type']] > 1 else 1
        elif u['type'] in COMPONENTS:
            n = COMPONENTS[u['type']]
            vals = [0.25 + 0.6 * _hash01(name, k) for k in range(n)]
            values[name] = vals if n > 1 else vals[0]
        else:
            values[name] = None   # a type this driver does not fill: the case must override it
    values.update(overrides)
    return {k: v for k, v in values.items() if v is not None}


def palette_values(rng, joints=6):
    palette = []
    for _ in range(joints):
        rot = np.eye(3, dtype='f4') + rng.normal(0, 0.15, (3, 3)).astype('f4')
        trans = rng.normal(0, 0.25, 3).astype('f4')
        palette += list(rot.reshape(-1)) + list(trans)
    palette += [0.0] * (12 * (110 - joints))
    return H.Mat3x4Array(palette)


def grid_mesh(rng, cells=10, joints=6, indexed_texture_channels=0, texcoord1_s_scale=1.0):
    """A wavy grid (two triangles a cell) covering the viewport, with every attribute the viewer's programs read."""
    xs = np.linspace(-1, 1, cells + 1, dtype='f4')
    pos, nrm, uv0, uv1, col, tan, wgt = [], [], [], [], [], [], []
    for i in range(cells):
        for j in range(cells):
            for ci, cj in [(i, j), (i + 1, j), (i, j + 1), (i + 1, j), (i + 1, j + 1), (i, j + 1)]:
                x, y = xs[ci], xs[cj]
                pos.append((x, y, 0.3 * np.sin(3 * x) * np.cos(2 * y)))
                n = np.array([0.4 * np.cos(3 * x), 0.3 * np.sin(2 * y), 1.0], 'f4')
                nrm.append(n / np.linalg.norm(n))
                uv0.append((ci / cells, cj / cells))
                uv1.append((texcoord1_s_scale * cj / cells, ci / cells))
                col.append(rng.uniform(0.2, 1.0, 4))
                tan.append((1.0, 0.1 * x, 0.0, 1.0))
                wgt.append(rng.integers(0, joints, size=4) + rng.uniform(0.05, 0.95, size=4))
    arrays = {0: pos, 1: nrm, 2: uv0, 3: uv1, 6: col, 7: col, 8: tan, 10: wgt}
    attributes = {slot: np.array(values, 'f4') for slot, values in arrays.items()}
    if indexed_texture_channels > 1:
        # Every triangle in a cell has one flat index. The first out-of-range index exercises the fallback color.
        row, column = np.indices((cells, cells), dtype=np.int32)
        indices = (row + 2 * column) % (indexed_texture_channels + 1)
        attributes[13] = np.repeat(indices, 6).reshape(-1, 1)
    return attributes, len(pos)


def texture_for(gl, kind, name, levels=(), point=False, repeat=False):
    """A smooth procedural texture, deterministic per sampler name; alpha runs 0.1..1 so alpha tests discard some."""
    g = gl.fn
    if kind == SAMPLERCUBEARRAY:   # the environment the probe filters run on (two layers, mips)
        data, levels = H.fixed_probe_cube()
        return H.upload_cube(gl, data, levels), H.GL_TEXTURE_CUBE_MAP_ARRAY
    tex = C.c_uint()
    g('glGenTextures', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(tex))
    g('glActiveTexture', None, C.c_uint)(H.GL_TEXTURE0 + 15)
    a, b, p = 2 + 6 * _hash01(name, 1), 2 + 6 * _hash01(name, 2), 6.28 * _hash01(name, 3)

    def face(n, salt):
        u, v = np.meshgrid((np.arange(n, dtype='f4') + 0.5) / n, (np.arange(n, dtype='f4') + 0.5) / n)
        img = np.stack([0.5 + 0.45 * np.sin(a * u + b * v + p + salt), 0.5 + 0.45 * np.cos(b * u - a * v + p + 2 * salt),
                        0.5 + 0.45 * np.sin((a + b) * (u - v) + p), 0.1 + 0.9 * (0.5 + 0.5 * np.sin(a * 2 * u + b * 2 * v + p + salt))], -1)
        return img.astype('<f4')

    if kind == H.GL_UNSIGNED_INT_SAMPLER_2D:
        if len(levels) != 1:
            raise RuntimeError(f'unsigned sampler {name} requires a single integer input image')
        data = np.ascontiguousarray(levels[0], dtype='<u4')
        height, width = data.shape
        target = H.GL_TEXTURE_2D
        g('glBindTexture', None, C.c_uint, C.c_uint)(target, tex.value)
        g('glTexImage2D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
            target, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, data.ctypes.data)
        point = True
    elif kind == SAMPLER2D:
        g('glBindTexture', None, C.c_uint, C.c_uint)(H.GL_TEXTURE_2D, tex.value)
        for level, data in enumerate(levels or (face(SIZE, 0),)):
            height, width = data.shape[:2]
            g('glTexImage2D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
                H.GL_TEXTURE_2D, level, H.GL_RGBA32F, width, height, 0, H.GL_RGBA, H.GL_FLOAT, data.tobytes())
        target = H.GL_TEXTURE_2D
    elif kind == SAMPLERCUBE:
        g('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_CUBE_MAP, tex.value)
        for f in range(6):
            g('glTexImage2D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, H.GL_RGBA32F, 32, 32, 0, H.GL_RGBA, H.GL_FLOAT, face(32, f).tobytes())
        target = GL_TEXTURE_CUBE_MAP
    else:
        raise RuntimeError(f'sampler type {kind:#x} of {name} has no procedural texture')
    mag = GL_NEAREST if point else H.GL_LINEAR
    min_filter = H.GL_LINEAR_MIPMAP_LINEAR if len(levels) > 1 else mag
    wrap = GL_REPEAT if repeat else H.GL_CLAMP_TO_EDGE
    for p_, v in ((H.GL_TEXTURE_MIN_FILTER, min_filter), (H.GL_TEXTURE_MAG_FILTER, mag),
                  (H.GL_TEXTURE_WRAP_S, wrap), (H.GL_TEXTURE_WRAP_T, wrap)):
        g('glTexParameteri', None, C.c_uint, C.c_uint, C.c_int)(target, p_, v)
    return tex.value, target


class MRT:
    """`n` RGBA32F colour attachments and a 32 bit float depth, cleared to 1."""

    def __init__(self, gl, w, h, n, samples=1):
        if samples not in (1, 4, 8):
            raise ValueError('MRT supports single-sample, 4× and 8× MSAA cases')
        self.gl, self.w, self.h, self.n, self.samples = gl, w, h, n, samples
        self.framebuffers, self.textures = [], []
        self.depth_reader = None
        try:
            self.fbo = self._new_framebuffer()
            for i in range(n):
                self._attach(H.GL_COLOR_ATTACHMENT0 + i, H.GL_RGBA32F, H.GL_RGBA, samples)
            self.depth = self._attach(GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, samples)
            self._complete_framebuffer(n)
            if samples > 1:
                self.resolve_fbo = self._new_framebuffer()
                self._attach(H.GL_COLOR_ATTACHMENT0, H.GL_RGBA32F, H.GL_RGBA, 1)
                self._complete_framebuffer(1)
                self.depth_reader = gl.program('''#version 460 core
void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
''', '''#version 460 core
layout(binding = 0) uniform sampler2DMS depth_samples;
uniform int first_sample;
layout(location = 0) out vec4 depth_by_sample;
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    for (int sample_idx = 0; sample_idx < 4; ++sample_idx)
        depth_by_sample[sample_idx] = texelFetch(depth_samples, pixel, first_sample + sample_idx).r;
}
''', attribs=())
        except Exception:
            self.close()
            raise

    def _new_framebuffer(self):
        g = self.gl.fn
        fbo = C.c_uint()
        g('glGenFramebuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(fbo))
        self.framebuffers.append(fbo.value)
        g('glBindFramebuffer', None, C.c_uint, C.c_uint)(H.GL_FRAMEBUFFER, fbo.value)
        return fbo.value

    def _attach(self, attachment, internal_format, pixel_format, samples):
        g = self.gl.fn
        tex = C.c_uint()
        g('glGenTextures', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(tex))
        self.textures.append(tex.value)
        target = H.GL_TEXTURE_2D if samples == 1 else GL_TEXTURE_2D_MULTISAMPLE
        g('glBindTexture', None, C.c_uint, C.c_uint)(target, tex.value)
        if samples == 1:
            g('glTexImage2D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
                target, 0, internal_format, self.w, self.h, 0, pixel_format, H.GL_FLOAT, None)
        else:
            g('glTexImage2DMultisample', None, C.c_uint, C.c_int, C.c_uint, C.c_int, C.c_int, C.c_ubyte)(
                target, samples, internal_format, self.w, self.h, 1)
        g('glFramebufferTexture2D', None, C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_int)(
            H.GL_FRAMEBUFFER, attachment, target, tex.value, 0)
        return tex.value

    def _complete_framebuffer(self, n):
        g = self.gl.fn
        bufs = (C.c_uint * n)(*[H.GL_COLOR_ATTACHMENT0 + i for i in range(n)])
        g('glDrawBuffers', None, C.c_int, C.c_void_p)(n, bufs)
        if g('glCheckFramebufferStatus', C.c_uint, C.c_uint)(H.GL_FRAMEBUFFER) != H.GL_FRAMEBUFFER_COMPLETE:
            raise RuntimeError(f'MRT framebuffer incomplete for {self.samples} sample(s)')

    def close(self):
        g = self.gl.fn
        if self.depth_reader is not None:
            g('glDeleteProgram', None, C.c_uint)(self.depth_reader)
        for delete, ids in (('glDeleteFramebuffers', self.framebuffers), ('glDeleteTextures', self.textures)):
            if ids:
                g(delete, None, C.c_int, C.POINTER(C.c_uint))(len(ids), (C.c_uint * len(ids))(*ids))

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def _read_pixels(self, pixel_format, channels):
        out = (C.c_float * (self.w * self.h * channels))()
        self.gl.fn('glReadPixels', None, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
            0, 0, self.w, self.h, pixel_format, H.GL_FLOAT, out)
        shape = (self.h, self.w, channels) if channels > 1 else (self.h, self.w)
        return np.frombuffer(out, dtype='<f4').reshape(shape).copy()

    def _read(self):
        g = self.gl.fn
        bind_framebuffer = g('glBindFramebuffer', None, C.c_uint, C.c_uint)
        read_buffer = g('glReadBuffer', None, C.c_uint)
        images = []
        if self.samples == 1:
            for i in range(self.n):
                read_buffer(H.GL_COLOR_ATTACHMENT0 + i)
                images.append(self._read_pixels(H.GL_RGBA, 4))
            return images, self._read_pixels(GL_DEPTH_COMPONENT, 1)

        bind_framebuffer(GL_DRAW_FRAMEBUFFER, self.resolve_fbo)
        blit = g('glBlitFramebuffer', None, *([C.c_int] * 8), C.c_uint, C.c_uint)
        for i in range(self.n):
            bind_framebuffer(GL_READ_FRAMEBUFFER, self.fbo)
            read_buffer(H.GL_COLOR_ATTACHMENT0 + i)
            blit(0, 0, self.w, self.h, 0, 0, self.w, self.h, GL_COLOR_BUFFER_BIT, GL_NEAREST)
            bind_framebuffer(GL_READ_FRAMEBUFFER, self.resolve_fbo)
            read_buffer(H.GL_COLOR_ATTACHMENT0)
            images.append(self._read_pixels(H.GL_RGBA, 4))

        # A depth blit would pick one sample. Pack all sample depths into RGBA instead.
        g('glDisable', None, C.c_uint)(GL_DEPTH_TEST)
        g('glUseProgram', None, C.c_uint)(self.depth_reader)
        g('glActiveTexture', None, C.c_uint)(H.GL_TEXTURE0)
        g('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_2D_MULTISAMPLE, self.depth)
        loc = g('glGetUniformLocation', C.c_int, C.c_uint, C.c_char_p)(self.depth_reader, b'first_sample')
        depths = []
        for first in range(0, self.samples, 4):
            g('glUniform1i', None, C.c_int, C.c_int)(loc, first)
            g('glDrawArrays', None, C.c_uint, C.c_int, C.c_int)(H.GL_TRIANGLES, 0, 3)
            depths.append(self._read_pixels(H.GL_RGBA, 4))
        return images, np.concatenate(depths, axis=-1)

    def draw(self, prog, attribs, count, bind_textures):
        g = self.gl.fn
        g('glBindFramebuffer', None, C.c_uint, C.c_uint)(H.GL_FRAMEBUFFER, self.fbo)
        g('glViewport', None, C.c_int, C.c_int, C.c_int, C.c_int)(0, 0, self.w, self.h)
        g('glEnable', None, C.c_uint)(GL_DEPTH_TEST)
        g('glDepthFunc', None, C.c_uint)(GL_ALWAYS)
        g('glDepthMask', None, C.c_ubyte)(1)
        vao = C.c_uint()
        g('glGenVertexArrays', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vao))
        g('glBindVertexArray', None, C.c_uint)(vao.value)
        vbos = []
        for slot, array in attribs.items():
            vbo = C.c_uint()
            g('glGenBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vbo))
            g('glBindBuffer', None, C.c_uint, C.c_uint)(H.GL_ARRAY_BUFFER, vbo.value)
            is_integer = array.dtype == np.int32
            data = np.ascontiguousarray(array, dtype='<i4' if is_integer else '<f4').tobytes()
            g('glBufferData', None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)(H.GL_ARRAY_BUFFER, len(data), data, H.GL_STATIC_DRAW)
            g('glEnableVertexAttribArray', None, C.c_uint)(slot)
            components = array.shape[1]
            if is_integer:
                g('glVertexAttribIPointer', None, C.c_uint, C.c_int, C.c_uint, C.c_int, C.c_void_p)(
                    slot, components, INT_T, 0, None)
            else:
                g('glVertexAttribPointer', None, C.c_uint, C.c_int, C.c_uint, C.c_ubyte, C.c_int, C.c_void_p)(
                    slot, components, H.GL_FLOAT, 0, 0, None)
            vbos.append(vbo)
        g('glUseProgram', None, C.c_uint)(prog)
        bind_textures()
        # Depth identifies covered MSAA samples. A zero clear keeps the colour resolve
        # proportional to the written values, without a negative bias at triangle edges.
        clear = 0.0 if self.samples > 1 else -1.0
        g('glClearColor', None, C.c_float, C.c_float, C.c_float, C.c_float)(clear, clear, clear, clear)
        g('glClearDepth', None, C.c_double)(1.0)
        g('glClear', None, C.c_uint)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        restore = []
        if self.samples > 1:
            is_enabled = g('glIsEnabled', C.c_ubyte, C.c_uint)
            for capability, enabled in ((GL_MULTISAMPLE, True), (GL_SAMPLE_SHADING, False),
                                        (GL_SAMPLE_ALPHA_TO_COVERAGE, False)):
                if bool(is_enabled(capability)) != enabled:
                    g('glEnable' if enabled else 'glDisable', None, C.c_uint)(capability)
                    restore.append((capability, not enabled))
        try:
            g('glDrawArrays', None, C.c_uint, C.c_int, C.c_int)(H.GL_TRIANGLES, 0, count)
            g('glFinish', None)()
            return self._read()
        finally:
            for capability, enabled in restore:
                g('glEnable' if enabled else 'glDisable', None, C.c_uint)(capability)
            for vbo in vbos:
                g('glDeleteBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vbo))
            g('glDeleteVertexArrays', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vao))
            g('glDisable', None, C.c_uint)(GL_DEPTH_TEST)


def legacy_indexed_texture_code(channels):
    """The NVIDIA path in LLShaderMgr::loadShaderFile builds one named sampler per texture."""
    lines = [f'uniform sampler2D tex{index};' for index in range(channels)]
    if channels == 1:
        lines.append('vec4 diffuseLookup(vec2 texcoord) { return texture(tex0, texcoord); }')
    else:
        lines += ['flat in int vary_texture_index;', 'vec4 diffuseLookup(vec2 texcoord)', '{']
        for index in range(channels):
            prefix = 'else ' if index else ''
            lines.append(f'    {prefix}if (vary_texture_index == {index}) {{ return texture(tex{index}, texcoord); }}')
        lines += ['    return vec4(1,0,1,1);', '}']
    return '\n'.join(lines) + '\n'


def legacy_source(path, defines, stage, case):
    text = H.read_legacy(path, defines, stage)
    helpers = ''
    if stage == 'fragment':
        extra = GBUFFER_DEFS
        if case.indexed_texture_channels:
            extra += legacy_indexed_texture_code(case.indexed_texture_channels)
        text = text.replace('#version 420\n', '#version 420\n' + extra, 1)
        if 'deferred/globalF.glsl' not in case.fragment_helpers and ('mirrorClip' in text or 'encodeNormal' in text):
            helpers = '\n' + (common.LEGACY_SHADERS / 'class1/deferred/globalF.glsl').read_text(encoding='utf-8')
    else:
        if case.skin:
            helpers += '\n' + (common.LEGACY_SHADERS / 'class1/avatar/objectSkinV.glsl').read_text(encoding='utf-8')
        if 'passTextureIndex()' in text:
            index_helper = 'indexedTextureV.glsl' if case.indexed_texture_channels > 1 else 'nonindexedTextureV.glsl'
            helpers += '\n' + (common.LEGACY_SHADERS / 'class1/objects' / index_helper).read_text(encoding='utf-8')
    return text + helpers


def build_reference(gl, case):
    defines = dict(case.defines)
    if case.skin:
        defines.update({'HAS_SKIN': '1', 'MAX_JOINTS_PER_MESH_OBJECT': '110'})
    vs = legacy_source(case.vs, defines, 'vertex', case)
    fs = legacy_source(case.fs, defines, 'fragment', case)
    helpers = tuple(H.read_legacy(path, defines, 'fragment') for path in case.fragment_helpers)
    vertex_helpers = tuple(H.read_legacy(path, defines, 'vertex') for path in case.vertex_helpers)
    geometry = H.read_legacy(case.geometry, defines, 'geometry') if case.geometry else None
    return gl.program(vs, fs, attribs=H.ATTRIB_SLOTS, fragment_helpers=helpers,
                      geometry_source=geometry, vertex_helpers=vertex_helpers)


def build_candidate(gl, generated, entry, hook_values):
    stage_types = {'vertex': H.GL_VERTEX_SHADER, 'geometry': H.GL_GEOMETRY_SHADER,
                   'fragment': H.GL_FRAGMENT_SHADER, 'compute': H.GL_COMPUTE_SHADER}
    declared_hooks = {hook for info in entry['stages'].values() for hook in info.get('hooks', ())}
    unknown_hooks = sorted(hook_values.keys() - declared_hooks)
    if unknown_hooks:
        raise RuntimeError(f'{entry["id"]} does not declare hooks: {", ".join(unknown_hooks)}')
    stages = []
    for stage, info in entry['stages'].items():
        text = (Path(generated) / info['file']).read_text(encoding='utf-8')
        stages.append((stage_types[stage], check.assemble_source(stage, text, hook_values)))
    return gl.link(stages)


def run_case(gl, case, generated, entry, strict_depth=False, rng_seed=7):
    if 'compute' in entry['stages']:
        raise RuntimeError('a compute program needs a dispatch A/B fixture')
    cand = build_candidate(gl, generated, entry, case.hooks)
    ref = build_reference(gl, case)
    samplers = [(re.sub(r'\[0\]$', '', u['name']), u['type']) for u in gl.uniforms(ref) if u['type'] in H.SAMPLER_TYPES]
    names = [n for n, _ in samplers]
    textures = {n: texture_for(gl, t, n, case.texture_levels.get(n, ()),
                               point=n in case.point_textures, repeat=n in case.repeat_textures)
                for n, t in samplers}
    results = []
    runs = [(clip, i, v) for clip in ([False, True] if case.clip else [False]) for i, v in enumerate(case.variants or ({},))]
    for clip, variant_index, variant in runs:
        rng = np.random.default_rng(rng_seed)
        if case.mesh_builder is not None:
            attribs, count = case.mesh_builder(rng)
        elif case.fullscreen:
            attribs, count = {0: SCREEN_TRIANGLE}, len(SCREEN_TRIANGLE)
        else:
            attribs, count = grid_mesh(rng, indexed_texture_channels=case.indexed_texture_channels,
                                       texcoord1_s_scale=case.texcoord1_s_scale)
        values = fill_uniforms(gl, ref, {**case.uniforms, **variant}, clip)
        if case.skin:
            values['matrixPalette'] = palette_values(np.random.default_rng(rng_seed + 1))
        units = {'ref': gl.sampler_units(ref, names, assign=True), 'cand': gl.sampler_units(cand, names, assign=False)}
        images = {}
        unset = []
        with MRT(gl, case.size, case.size, max(case.outputs, 1), case.samples) as target:
            for label, prog in (('ref', ref), ('cand', cand)):
                done = gl.set_uniforms(prog, values)
                if label == 'cand':
                    unset = sorted(set(values) - done - set(case.optional))

                def bind(units=units[label]):
                    for n, unit in units.items():
                        if n in textures:
                            tex, tgt = textures[n]
                            gl.fn('glActiveTexture', None, C.c_uint)(H.GL_TEXTURE0 + unit)
                            gl.fn('glBindTexture', None, C.c_uint, C.c_uint)(tgt, tex)
                images[label] = target.draw(prog, attribs, count, bind)
        suffix = (' mirror clip on' if clip else '') + (f' variant {variant_index}' if case.variants else '')
        multisample = case.samples > 1
        if multisample:
            suffix += f' ({case.samples}× MSAA)'
        # These fixtures keep their geometry inside the far plane, so depth 1 is the clear value.
        ref_coverage = images['ref'][1] < 1.0
        for i in range(case.outputs):
            reference_colour = images['ref'][0][i]
            r = H.compare(reference_colour, images['cand'][0][i])
            if multisample:
                drawn_pixels = ref_coverage.any(axis=-1)
            elif case.coverage:
                # A written target can have negative alpha, as in the legacy terrain specular buffer.
                drawn_pixels = ref_coverage & np.any(reference_colour != -1.0, axis=-1)
            else:
                drawn_pixels = reference_colour[..., 3] >= 0
            drawn = reference_colour[drawn_pixels][:, :3]
            r.update(case=f'{case.name} colour {i}{suffix}', nonblack=int(len(drawn)), unset_uniforms=unset,
                     flat=bool(len(drawn) and float(np.ptp(drawn)) == 0.0))
            if unset or r['nonblack'] == 0:
                r['pass'] = False
            # a constant image proves nothing about its channel: only the listed ones (and the emissive target, which no
            # program of these writes anything into) may be flat
            if r['flat'] and i not in case.flat_ok and not (i == 3 and case.outputs == 4):
                r['pass'] = False
            if i == 0 and case.required_colours:
                counts = [dict(rgba=rgba, ref=int(np.all(images['ref'][0][i] == rgba, axis=-1).sum()),
                               cand=int(np.all(images['cand'][0][i] == rgba, axis=-1).sum()))
                          for rgba in case.required_colours]
                r['colour_counts'] = counts
                r['pass'] &= all(count['ref'] > 0 and count['cand'] > 0 for count in counts)
            results.append(r)
        if case.depth or case.depth_info or multisample:
            advisory = case.depth_info and not (case.depth or strict_depth or multisample)
            label = 'depth (info)' if advisory else 'depth'
            if multisample:
                label = 'per-sample depth'
            r = H.compare(images['ref'][1], images['cand'][1], mode='exact')
            r.update(case=f'{case.name} {label}{suffix}', nonblack=int(ref_coverage.sum()),
                     unset_uniforms=unset, info=advisory)
            r['pass'] = advisory or (r['pass'] and r['nonblack'] > 0 and (case.outputs or not unset))
            results.append(r)
        if case.coverage:
            cand_coverage = images['cand'][1] < 1.0
            r = H.compare(ref_coverage, cand_coverage, mode='exact')
            covered = int(ref_coverage.sum())
            label = 'per-sample coverage' if multisample else 'coverage'
            r.update(case=f'{case.name} {label}{suffix}', nonblack=covered, unset_uniforms=unset)
            r['pass'] &= not unset and 0 < covered < ref_coverage.size
            if multisample:
                samples_per_pixel = ref_coverage.sum(axis=-1)
                partial = int(np.count_nonzero((samples_per_pixel > 0) & (samples_per_pixel < case.samples)))
                r['partial_pixels'] = partial
                r['pass'] &= partial > 0
            results.append(r)
    return results


def deferred_post_cases():
    vertex = 'deferred/postDeferredNoTCV.glsl'
    luminance = 'deferred/luminanceF.glsl'
    exposure = 'deferred/exposureF.glsl'
    gamma = 'deferred/postDeferredGammaCorrect.glsl'
    tonemap = 'deferred/postDeferredTonemap.glsl'
    srgb = 'environment/srgbF.glsl'
    tonemap_util = 'deferred/tonemapUtilF.glsl'

    # Sample both sky flags and the object flags with the same point filtering as the viewer.
    normals = np.zeros((SIZE, SIZE, 4), dtype='<f4')
    normals[..., 3] = np.repeat([0.0, 0.34, 0.67, 1.0], SIZE // 4)
    yield Case('post-luminance', vertex, luminance, 'luminance', fullscreen=True, depth=True,
               uniforms={'diffuse_luminance_scale': 0.375}, texture_levels={'normalMap': (normals,)},
               point_textures=('diffuseRect', 'normalMap'))

    # Each mip differs so sampling level zero or a neighbouring level changes the exposure result.
    luminance_mips = tuple(np.full((256 >> level, 256 >> level, 4), 0.25 - level / 64, dtype='<f4')
                           for level in range(9))
    exposure_texel = (np.full((1, 1, 4), 0.75, dtype='<f4'),)
    exposure_variants = tuple({'dt': dt, 'dynamic_exposure_params': [coefficient, 0.5, 2.0, 0.1]}
                              for dt, coefficient in ((1 / 60, 0.175), (0.75, 0.08)))
    for history in (False, True):
        yield Case('post-exposure-history' if history else 'post-exposure', vertex, exposure,
                   f'exposure__USE_LAST_EXPOSURE{int(history)}', defines={'USE_LAST_EXPOSURE': '1'} if history else {},
                   uniforms={'dynamic_exposure_params2': [1.0, 0.5, 2.0, 2.0]} if history else {},
                   fullscreen=True, size=1, flat_ok=(0,), depth=True, variants=exposure_variants,
                   texture_levels={'emissiveRect': luminance_mips, 'exposureMap': exposure_texel})

    u, v = np.meshgrid(np.linspace(0, 1, SIZE, dtype='f4'), np.linspace(0, 1, SIZE, dtype='f4'))
    hdr = np.stack((0.001 + 3 * u**2, 0.001 + 2 * (1 - u) * v**2, 0.001 + 4 * v**2, 0.2 + 0.7 * u), axis=-1)
    display_input = {'diffuseRect': (hdr.astype('<f4'),), 'exposureMap': exposure_texel}
    display = dict(fullscreen=True, depth=True, texture_levels=display_input, point_textures=('diffuseRect',))
    legacy_gamma = {'gamma': 0.65}
    yield Case('post-gamma-srgb', vertex, gamma, 'postGammaCorrect__LEGACY_GAMMA0', fragment_helpers=(srgb,), **display)
    yield Case('post-gamma-legacy', vertex, gamma, 'postGammaCorrect__LEGACY_GAMMA1',
               defines={'LEGACY_GAMMA': '1'}, uniforms=legacy_gamma, fragment_helpers=(srgb,), **display)

    tonemap_variants = tuple({'tonemap_type': operator, 'tonemap_mix': mix, 'exposure': 1.4}
                             for operator in (0, 1) for mix in (0.65, 1.0))
    for gamma_mode, label in enumerate(('linear', 'srgb', 'legacy')):
        for no_post in (0, 1):
            defines = {'NO_POST': '1'} if no_post else {}
            if gamma_mode:
                defines['GAMMA_CORRECT'] = '1'
            if gamma_mode == 2:
                defines['LEGACY_GAMMA'] = '1'
            suffix = '-no-post' if no_post else ''
            yield Case(f'post-tonemap-{label}{suffix}', vertex, tonemap,
                       f'postTonemap__GAMMA_MODE{gamma_mode}__NO_POST{no_post}', defines=defines,
                       uniforms=legacy_gamma if gamma_mode == 2 else {}, fragment_helpers=(srgb, tonemap_util),
                       variants=() if no_post else tonemap_variants, **display)


def depth_of_field_cases():
    vertex = 'deferred/postDeferredNoTCV.glsl'
    hdr_clamp = ('deferred/deferredUtil.glsl',)
    axis = np.linspace(0.0, 1.0, SIZE, dtype='f4')
    u, v = np.meshgrid(axis, axis)
    hdr = np.stack((12.5 * u - 0.125, 0.15 + 2.0 * v**2, 0.05 + 4.0 * u * v, 0.15 + 0.7 * v), axis=-1)

    # A perspective depth buffer with a near ramp, an in-focus band and a far ramp.
    projection = H._perspective(55.0, 1.0, 0.1, 100.0)
    distance = np.where(u < 0.4, 0.5 + 3.5 * u / 0.4, 4.0 + 72.0 * np.maximum(u - 0.6, 0.0) / 0.4)
    ndc_z = (-distance * projection[2, 2] + projection[2, 3]) / distance
    depth = np.repeat((ndc_z * 0.5 + 0.5)[..., None], 4, axis=-1).astype('<f4')
    inv_proj = np.linalg.inv(projection).T.reshape(-1).tolist()
    # The LLRenderTarget bind overload defaults to bilinear for these color and depth inputs.
    scene = {'diffuseRect': (hdr,), 'depthMap': (depth,)}
    yield Case('dof-circle-of-confusion', vertex, 'deferred/cofF.glsl', 'dofCoF', fullscreen=True, depth=True,
               uniforms={'inv_proj': inv_proj, 'focal_distance': -4.0, 'blur_constant': 0.0006,
                         'tan_pixel_angle': 0.001, 'magnification': 0.025, 'max_cof': 8.0},
               texture_levels=scene)

    # The blur pass samples packed CoF values below, within and above the focus band.
    packed = hdr.copy()
    packed[..., 3] = 0.05 + 0.9 * u
    yield Case('dof-half-resolution-blur', vertex, 'deferred/postDeferredF.glsl', 'dofBlur',
               fullscreen=True, depth=True, size=SIZE // 2, fragment_helpers=hdr_clamp,
               uniforms={'max_cof': 7.25}, texture_levels={'diffuseRect': (packed,)})

    # Distinct targets expose both the transition taps and the clamping at the blurred subrectangle's edges.
    blurred = packed[::-1, ::-1].copy()
    combine = {'diffuseRect': (blurred,), 'lightMap': (packed,)}
    variants = tuple({'res_scale': scale, 'dof_width': (int(SIZE * scale) - 1) / SIZE,
                      'dof_height': (int(SIZE * scale) - 1) / SIZE} for scale in (0.5, 0.6))
    yield Case('dof-combine', vertex, 'deferred/dofCombineF.glsl', 'dofCombine', fullscreen=True, depth=True,
               uniforms={'max_cof': 7.25}, variants=variants, texture_levels=combine)

    for noise in (0, 1):
        yield Case('dof-bypass-noise' if noise else 'dof-bypass', vertex, 'deferred/postDeferredNoDoFF.glsl',
                   f'dofBypass__HAS_NOISE{noise}', fullscreen=True, depth=True,
                   defines={'HAS_NOISE': '1'} if noise else {}, fragment_helpers=hdr_clamp,
                   texture_levels=scene)


def render_diagnostics_cases():
    vertex = 'deferred/postDeferredNoTCV.glsl'
    fragment = 'deferred/postDeferredVisualizeBuffers.glsl'
    yield Case('buffer-visualization-mip0', vertex, fragment, 'bufferVisual',
               fullscreen=True, depth=True, uniforms={'mipLevel': 0.0})

    # The luminance target is 256x256. Distinct red values detect selection of any other mip.
    mips = []
    for level in range(9):
        size = 256 >> level
        pixels = np.zeros((size, size, 4), dtype='<f4')
        pixels[..., 0] = (level + 1) / 16
        pixels[..., 3] = 1.0
        mips.append(pixels)
    yield Case('buffer-visualization-mip8', vertex, fragment, 'bufferVisual', fullscreen=True, depth=True,
               uniforms={'mipLevel': 8.0}, texture_levels={'diffuseRect': tuple(mips)}, flat_ok=(0,))

    # Parcel ownership is a nearest-filtered grid of RGBA colors with exact, sharp edges.
    row, column = np.indices((SIZE, SIZE))
    parcel_colors = np.array([[0, 0, 0, 0], [255, 0, 0, 128], [0, 220, 110, 180],
                              [0, 100, 255, 215], [235, 210, 15, 255]], dtype='<f4') / 255
    parcels = parcel_colors[(row // 8 + 2 * (column // 8)) % len(parcel_colors)]
    scale = 257 / 256
    tex_matrix = np.diag([scale, scale, 1.0, 1.0]).reshape(-1).tolist()
    for emissive in (0, 1):
        yield Case(f'parcel-highlight-emissive{emissive}', 'interface/highlightV.glsl', 'deferred/highlightF.glsl',
                   f'parcelHighlight__HAS_EMISSIVE{emissive}', defines={'HAS_EMISSIVE': '1'} if emissive else {},
                   outputs=3 + emissive, depth=True, flat_ok=(1, 2, 3), point_textures=('diffuseMap',),
                   uniforms={'color': [1.0] * 4, 'texture_matrix0': tex_matrix}, texture_levels={'diffuseMap': (parcels,)})


def deferred_diffuse_cases():
    vertex = 'deferred/diffuseV.glsl'
    for emissive in (0, 1):
        defines = {'HAS_EMISSIVE': '1'} if emissive else {}
        for skin in (0, 1):
            for mask in (0, 1):
                fragment = 'diffuseAlphaMaskIndexedF.glsl' if mask else 'diffuseIndexedF.glsl'
                yield Case(f'deferred-diffuse-indexed-e{emissive}-s{skin}-m{mask}', vertex, f'deferred/{fragment}',
                           f'ddiffuseIndexed__HAS_EMISSIVE{emissive}__HAS_SKIN{skin}__MASK{mask}',
                           defines=defines, outputs=3 + emissive, depth=True, skin=bool(skin), clip=True,
                           indexed_texture_channels=4, flat_ok=(1,) if mask else ())
        for no_color in (0, 1):
            vertex_file = 'deferred/diffuseNoColorV.glsl' if no_color else vertex
            fragment = 'diffuseAlphaMaskNoColorF.glsl' if no_color else 'diffuseAlphaMaskF.glsl'
            yield Case(f'deferred-diffuse-single-e{emissive}-nc{no_color}', vertex_file, f'deferred/{fragment}',
                       f'ddiffuseSingle__HAS_EMISSIVE{emissive}__INDEXED0__NO_COLOR{no_color}',
                       defines=defines, outputs=3 + emissive, depth=True, clip=not no_color, flat_ok=(1,))


def terrain_alpha_ramp():
    # The viewer's alpha-only texture descends in S, with a noisy transition in T.
    axis = (np.arange(256, dtype='f4') + 0.5) / 256
    s, t = np.meshgrid(axis, axis)
    noisy_ramp = 1 - s + 0.2 * np.sin(np.pi * s) * np.sin(2 * np.pi * (3 * t + s))
    mips = []
    for size in (256, 128, 64, 32, 16, 8, 4, 2, 1):
        rgba = np.zeros((size, size, 4), dtype='<f4')
        alpha = np.rint(np.clip(noisy_ramp, 0, 1) * 255) / 255
        rgba[..., 3] = alpha
        mips.append(rgba)
        if size > 1:
            half = size // 2
            noisy_ramp = alpha.reshape(half, 2, half, 2).mean(axis=(1, 3))
    return tuple(mips)


def terrain_bake_cases():
    texture = {'alpha_ramp': terrain_alpha_ramp()}

    # Draw an orthographic terrain patch and cover all three composition transitions in texcoord1.s.
    mvp = [0.96, 0, 0, 0, 0, 0.91, 0, 0, 0, 0, 0.45, 0, 0.03, -0.02, 0, 1]
    terrain_defaults = {'TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT': '3',
                        'TERRAIN_TRIPLANAR_BLEND_FACTOR': '8.0', 'TERRAIN_PBR_DETAIL': '0'}
    for bit_depth in range(1, 9):
        precision = {'TERRAIN_PAINT_PRECISION': str((1 << bit_depth) - 1)}
        yield Case(f'terrain-bake-rgb{bit_depth}', 'interface/pbrTerrainBakeV.glsl', 'interface/pbrTerrainBakeF.glsl',
                   'terrainBake', defines={**terrain_defaults, **precision}, hooks=precision, depth=True,
                   uniforms={'modelview_projection_matrix': mvp}, texture_levels=texture,
                   texcoord1_s_scale=3.0, fragment_helpers=('deferred/pbrterrainUtilF.glsl',))


def rlv_sphere_cases():
    # Vary the depth at subpixel offsets so point and linear filtering exercise different reconstructions.
    s, t = np.meshgrid(np.linspace(0, 1, 47, dtype='f4'), np.linspace(0, 1, 29, dtype='f4'))
    projection = H._perspective(55.0, 1.0, 0.1, 100.0)
    distance = 2.5 + 0.8 * s + 0.35 * np.sin(3 * np.pi * t) + 0.55 * (s > 0.66)
    ndc_z = (-distance * projection[2, 2] + projection[2, 3]) / distance
    depth = np.repeat((ndc_z * 0.5 + 0.5)[..., None], 4, axis=-1).astype('<f4')
    scene = {'depthMap': (depth,)}
    inv_proj = np.linalg.inv(projection).T.reshape(-1).tolist()
    base = {
        'inv_proj': inv_proj,
        'rlvEffectParam1': [0.15, -0.1, -3.0, 1.0],
        'rlvEffectParam2': [0.2, 0.42, 0.85, 1.45],
        'rlvEffectParam3': [2.0, 1.0],  # C++ uploads the nonzero min and max mask bits as floats.
    }
    extend_variants = tuple({'rlvEffectParam3': [near, far]} for near, far in
                            ((0.0, 0.0), (2.0, 0.0), (0.0, 1.0), (2.0, 1.0)))
    directions = ([1.0, 0.0], [0.0, 1.0])
    fixed_blur_variants = tuple({'rlvEffectParam5': direction} for direction in directions)
    variable_blur_variants = tuple({'rlvEffectParam5': direction, 'rlvEffectParam4': [kernel, 0.0, 0.0, 0.0]}
                                   for kernel in (11.0, 10000.0) for direction in directions)
    modes = (
        ('blend', {'rlvEffectParam4': [0.93, 0.12, 0.52, 1.0]}, extend_variants),
        ('fixed-blur', {'rlvEffectParam2': [0.4, 0.42, 2.8, 1.45]}, fixed_blur_variants),
        ('variable-blur', {'rlvEffectParam2': [8.0, 0.42, 26.0, 1.45], 'rlvEffectParam3': [0.0, 0.0]},
         variable_blur_variants),
        ('chromatic-aberration', {'rlvEffectParam4': [-2.0, 0.6, 1.6, -0.2]}, ()),
        ('pixelate', {'rlvEffectParam4': [5.0, 7.0, 0.0, 0.0]}, ()),
    )
    for filter_name, point_textures in (('point', ('depthMap',)), ('linear', ())):
        for mode, (name, overrides, variants) in enumerate(modes):
            yield Case(f'rlv-sphere-{name}-{filter_name}', 'deferred/rlvV.glsl', 'deferred/rlvF.glsl',
                       'rlvSphere', fullscreen=True, depth=True, variants=variants,
                       uniforms={**base, **overrides, 'rlvEffectMode': mode},
                       fragment_helpers=('deferred/deferredUtil.glsl',), texture_levels=scene,
                       point_textures=point_textures)


def normal_debug_cases():
    normal = (1.0, 1.0, 0.0, 1.0)
    tangent = (0.0, 1.0, 1.0, 1.0)
    for tangents in (0, 1):
        for skin in (0, 1):
            yield Case(f'normal-debug-tangent{tangents}-skin{skin}',
                       'interface/normaldebugV.glsl', 'interface/normaldebugF.glsl',
                       f'normaldebug__HAS_ATTRIBUTE_TANGENT{tangents}__HAS_SKIN{skin}',
                       defines={'HAS_ATTRIBUTE_TANGENT': '1'} if tangents else {},
                       depth=True, skin=bool(skin), flat_ok=(0,), geometry='interface/normaldebugG.glsl',
                       variants=({'debug_normal_draw_length': 0.1}, {'debug_normal_draw_length': 0.35}),
                       required_colours=(normal, tangent) if tangents else (normal,))


def material_mesh(rng):
    attributes, count = grid_mesh(rng)
    attributes[0][:, :2] += (0.013, -0.021)  # Move triangle edges away from whole-pixel alignment.
    uv = attributes[2]
    attributes[4] = np.column_stack((1.0 - 0.85 * uv[:, 0], 0.13 + 0.72 * uv[:, 1]))
    # A cell's tangent sign is flat, and both signs occur across the material.
    attributes[8][:, 3] = np.where((np.arange(count) // 6) % 2, -1.0, 1.0)
    return attributes, count


def material_gbuffer_cases():
    axis = (np.arange(SIZE, dtype='f4') + 0.5) / SIZE
    s, t = np.meshgrid(axis, axis)
    cutoff = 128 / 255
    # Include bands just below and just inside the 1/512 bias, with diagonal boundaries for MSAA.
    bands = np.floor(8 * (s + 0.19 * t)).astype(np.int32) % 4
    alpha = np.take(np.array([cutoff - 3 / 512, cutoff - 0.75 / 512, cutoff, 0.92], dtype='f4'), bands)
    albedo = np.stack((0.17 + 0.72 * s, 0.23 + 0.63 * t, 0.12 + 0.66 * s * t, alpha), axis=-1)
    normals = np.stack((0.52 + 0.22 * np.sin(2 * np.pi * s), 0.49 + 0.25 * np.cos(2 * np.pi * t),
                        0.72 + 0.23 * t, 0.30 + 0.62 * s), axis=-1)
    specular = np.stack((0.1 + 0.78 * s, 0.18 + 0.67 * t, 0.90 - 0.68 * s, 0.16 + 0.75 * s * t), axis=-1)
    textures = {'diffuseMap': (albedo,), 'bumpMap': (normals,), 'specularMap': (specular,)}

    def material_case(emissive, normal, skin, spec, mode, samples=1):
        defines = {'DIFFUSE_ALPHA_MODE': str(mode)}
        for flag, active in (('HAS_EMISSIVE', emissive), ('HAS_NORMAL_MAP', normal), ('HAS_SPECULAR_MAP', spec)):
            if active:
                defines[flag] = '1'
        uniforms = {
            'emissive_brightness': 0.57 if mode == 3 else 0.22,
            'env_intensity': 0.63 if spec else 0.28,
            'specular_color': [0.78, 0.28, 0.48, 0.74] if normal else [0.23, 0.69, 0.37, 0.41],
        }
        if mode == 2:
            uniforms['minimum_alpha'] = cutoff
        name = f'material-gbuffer-e{emissive}-n{normal}-s{skin}-p{spec}-m{mode}'
        if samples > 1:
            name += f'-{samples}x-msaa'
        return Case(name, 'deferred/materialV.glsl', '../class3/deferred/materialF.glsl',
                    f'materialGbuffer__HAS_EMISSIVE{emissive}__HAS_NORMAL_MAP{normal}__HAS_SKIN{skin}'
                    f'__HAS_SPECULAR_MAP{spec}__MODE{mode}',
                    defines=defines, outputs=3 + emissive, depth=True, coverage=True,
                    skin=bool(skin), clip=True, uniforms=uniforms, texture_levels=textures,
                    samples=samples, mesh_builder=material_mesh)

    for emissive in (0, 1):
        for normal in (0, 1):
            for skin in (0, 1):
                for spec in (0, 1):
                    for mode in (0, 2, 3):
                        yield material_case(emissive, normal, skin, spec, mode)
    for skin in (0, 1):
        yield material_case(skin, 1, skin, 1, 2, samples=4)


def pbr_mesh(rng):
    attributes, count = grid_mesh(rng)
    attributes[0][:, :2] += (0.014, -0.017)
    # Independently vary winding and tangent handedness by cell without changing the surface.
    cell = np.arange(count) // 6
    attributes[8][:, 3] = np.where((cell // 2) % 2, -1.0, 1.0)
    attributes[7][:, 3] = 0.18 + 0.72 * attributes[2][:, 1]
    back_faces = (np.arange(count // 3) // 2) % 2 == 1
    for array in attributes.values():
        triangles = array.reshape(-1, 3, array.shape[1])
        triangles[back_faces] = triangles[back_faces][:, [0, 2, 1]]
    return attributes, count


def pbr_opaque_glow_cases():
    axis = (np.arange(SIZE, dtype='f4') + 0.5) / SIZE
    s, t = np.meshgrid(axis, axis)
    albedo = np.stack((0.008 + 0.87 * s**2, 0.017 + 0.69 * t, 0.02 + 0.74 * s * t,
                       0.17 + 0.77 * (0.5 + 0.5 * np.sin(2 * np.pi * (1.4 * s + 0.6 * t)))), axis=-1)
    normals = np.stack((0.31 + 0.12 * np.sin(3 * np.pi * s), 0.58 + 0.14 * np.cos(2 * np.pi * t),
                        0.72 + 0.23 * t, np.ones_like(s)), axis=-1)
    orm = np.stack((0.18 + 0.72 * s * t, 0.13 + 0.76 * t, 0.85 - 0.68 * s, np.ones_like(s)), axis=-1)
    glow = np.stack((0.013 + 0.80 * t**2, 0.03 + 0.72 * s, 0.04 + 0.75 * (1 - s) * t, np.ones_like(s)), axis=-1)
    textures = {'diffuseMap': (albedo,), 'bumpMap': (normals,), 'specularMap': (orm,), 'emissiveMap': (glow,)}

    base_transform = H.Vec4Array(((0.92, 0.85, 0.17, 0.0), (0.07, 0.08, 0.0, 0.0)))
    normal_transform = H.Vec4Array(((-0.79, 0.64, -0.29, 0.0), (0.81, 0.11, 0.0, 0.0)))
    orm_transform = H.Vec4Array(((0.61, 0.91, 0.43, 0.0), (0.08, 0.21, 0.0, 0.0)))
    emissive_transform = H.Vec4Array(((0.64, 0.72, -0.39, 0.0), (0.30, 0.12, 0.0, 0.0)))
    # Column-major texture animation with rotation, nonuniform scale and offset.
    angle = 0.27
    animated = [0.82 * np.cos(angle), 0.82 * np.sin(angle), 0, 0,
                -0.74 * np.sin(angle), 0.74 * np.cos(angle), 0, 0,
                0, 0, 1, 0,
                0.11, 0.08, 0, 1]
    identity = np.eye(4, dtype='f4').reshape(-1).tolist()
    variants = ({'minimum_alpha': 0.0}, {'minimum_alpha': 0.36},
                {'minimum_alpha': 0.36, 'texture_matrix0': identity})
    base = {'texture_base_color_transform': base_transform, 'texture_matrix0': animated}
    emission = {'texture_emissive_transform': emissive_transform, 'emissiveColor': [0.74, 1.15, 0.42]}
    feature_helpers = dict(vertex_helpers=('deferred/textureUtilV.glsl',),
                           fragment_helpers=('deferred/globalF.glsl', 'environment/srgbF.glsl'))
    for emissive in (0, 1):
        for skin in (0, 1):
            uniforms = {**base, 'texture_normal_transform': normal_transform,
                        'texture_metallic_roughness_transform': orm_transform,
                        'metallicFactor': 0.54, 'roughnessFactor': 0.68}
            if emissive:
                uniforms.update(emission)
            yield Case(f'pbr-opaque-e{emissive}-s{skin}', 'deferred/pbropaqueV.glsl', 'deferred/pbropaqueF.glsl',
                       f'pbrOpaque__HAS_EMISSIVE{emissive}__HAS_SKIN{skin}',
                       defines={'HAS_EMISSIVE': '1'} if emissive else {}, outputs=3 + emissive,
                       depth=True, coverage=True, skin=bool(skin), clip=True, variants=variants,
                       uniforms=uniforms, texture_levels=textures, mesh_builder=pbr_mesh, **feature_helpers)
    yield Case('pbr-hud', 'deferred/pbropaqueV.glsl', 'deferred/pbropaqueF.glsl', 'pbrHud__IS_HUD1',
               defines={'IS_HUD': '1'}, depth=True, coverage=True, variants=variants,
               uniforms={**base, **emission}, texture_levels=textures, mesh_builder=pbr_mesh, **feature_helpers)
    for skin in (0, 1):
        yield Case(f'pbr-glow-s{skin}', 'deferred/pbrglowV.glsl', 'deferred/pbrglowF.glsl',
                   f'pbrGlow__HAS_SKIN{skin}', depth=True, coverage=True, skin=bool(skin), variants=variants,
                   uniforms={**base, **emission}, texture_levels=textures, mesh_builder=pbr_mesh, **feature_helpers)


def legacy_terrain_cases():
    # Match the viewer's repeated detail textures and clamped, noisy alpha ramp.
    texture = {'alpha_ramp': terrain_alpha_ramp()}
    detail_maps = tuple(f'detail_{index}' for index in range(4))
    # The object planes have two distinct region offsets and generate UVs outside [0, 1].
    planes = tuple({'object_plane_s': [scale, 0, 0, offset_s],
                    'object_plane_t': [0, scale, 0, offset_t]}
                   for scale, offset_s, offset_t in ((0.79, 0.23, 0.31), (1.25, 0.37, 0.16)))
    for emissive in (0, 1):
        yield Case(f'legacy-terrain-e{emissive}', 'deferred/terrainV.glsl', 'deferred/terrainF.glsl',
                   f'terrainLegacy__HAS_EMISSIVE{emissive}', defines={'HAS_EMISSIVE': '1'} if emissive else {},
                   outputs=3 + emissive, depth=True, coverage=True, clip=True, flat_ok=(1, 3),
                   variants=planes, texture_levels=texture, texcoord1_s_scale=3.0, repeat_textures=detail_maps)


def fullbright_emissive_mesh(rng):
    attributes, count = grid_mesh(rng, indexed_texture_channels=4)
    attributes[0][:, :2] += (0.014, -0.017)
    s, t = attributes[2].T
    # The threshold uses sampled alpha, while the output uses sampled alpha times vertex alpha.
    attributes[6][:, 3] = 0.08 + 0.79 * t
    attributes[7][:] = np.column_stack((0.48 * t, 0.33 + 0.21 * s, 0.73 * t, 0.88 - 0.72 * s))
    return attributes, count


FULLBRIGHT_VERTEX_HELPERS = ('windlight/atmosphericsVarsV.glsl', 'windlight/atmosphericsHelpersV.glsl',
                             'environment/srgbF.glsl', 'windlight/atmosphericsFuncs.glsl',
                             'windlight/atmosphericsV.glsl')
# The vertex helpers assign atmospheric varyings that these fragments do not read.
UNUSED_FULLBRIGHT_ATMOSPHERE = ('max_y', 'moon_glow_factor', 'primary_light_dir')


def fullbright_textures():
    axis = (np.arange(SIZE, dtype='f4') + 0.5) / SIZE
    s, t = np.meshgrid(axis, axis)
    # Broad alpha bands cross the legacy cutoffs even with bilinear filtering.
    alpha = np.array([0, 0.003, 0.07, 0.23, 0.65, 0.92], dtype='f4')
    textures = {}
    for index in range(4):
        bands = np.floor(12 * (s + 0.29 * t) + index).astype(np.int32) % len(alpha)
        image = np.stack((0.005 + (0.86 - 0.16 * index) * s**2,
                          0.016 + (0.57 + 0.09 * index) * t,
                          0.023 + 0.66 * (1 - s) * t + 0.04 * index,
                          alpha[bands]), axis=-1)
        textures[f'tex{index}'] = (image,)
    return textures


def fullbright_emissive_cases():
    feature_inputs = dict(
        indexed_texture_channels=4, texture_levels=fullbright_textures(), depth=True, coverage=True,
        mesh_builder=fullbright_emissive_mesh, vertex_helpers=FULLBRIGHT_VERTEX_HELPERS)
    plane = [0.75, -0.25, 0.30, 0.92]
    mirrors = ({}, {'mirror_flag': 1.0, 'clipPlane': plane},
               {'mirror_flag': 1.0, 'clipPlane': [-component for component in plane]})

    def fullbright_case(hud, skin, mask, samples):
        defines = {'HAS_DIFFUSE_LOOKUP': '1'}
        if hud:
            defines['IS_HUD'] = '1'
        if mask:
            defines['HAS_ALPHA_MASK'] = '1'
        if hud:
            name, program = f'fullbright-hud-m{mask}', f'fullbrightHud__IS_HUD1__MASK{mask}'
        else:
            name, program = f'fullbright-world-s{skin}-m{mask}', f'fullbrightSurface__HAS_SKIN{skin}__MASK{mask}'
        # Include the alpha floor, impostor, and DoF cutoffs along with a zero cutoff.
        draws = mirrors if samples == 1 else mirrors[:2]
        if mask:
            cuts = (0.0, 0.004, 0.10, 0.33) if samples == 1 else (0.33,)
            draws = tuple({**mirror, 'minimum_alpha': cutoff} for mirror in draws for cutoff in cuts)
        return Case(name, 'deferred/fullbrightV.glsl', 'deferred/fullbrightF.glsl', program,
                    defines=defines, skin=bool(skin), samples=samples, variants=draws,
                    optional=UNUSED_FULLBRIGHT_ATMOSPHERE,
                    fragment_helpers=('environment/srgbF.glsl',), **feature_inputs)

    for samples in (1, 4):
        for mask in (0, 1):
            yield fullbright_case(True, 0, mask, samples)
            for skin in (0, 1):
                yield fullbright_case(False, skin, mask, samples)
        for skin in (0, 1):
            optional = UNUSED_FULLBRIGHT_ATMOSPHERE if skin else (*UNUSED_FULLBRIGHT_ATMOSPHERE, 'modelview_matrix')
            yield Case(f'fullbright-emissive-s{skin}', 'deferred/emissiveV.glsl', 'deferred/emissiveF.glsl',
                       f'fullbrightEmissive__HAS_SKIN{skin}__IS_EMISSIVE1', skin=bool(skin), samples=samples,
                       optional=optional, **feature_inputs)


def hud_water_mesh(rng):
    attributes, count = fullbright_emissive_mesh(rng)
    x = attributes[0][:, 0]
    # The middle band is exactly on the water plane, with geometry on both sides.
    attributes[0][:, 2] = np.sign(x) * np.maximum(np.abs(x) - 0.24, 0) * 0.38
    return attributes, count


def hud_material_cases():
    indexed_inputs = dict(indexed_texture_channels=4, texture_levels=fullbright_textures(),
                          vertex_helpers=FULLBRIGHT_VERTEX_HELPERS, depth=True, coverage=True,
                          optional=UNUSED_FULLBRIGHT_ATMOSPHERE)
    hud_indexed = {'IS_HUD': '1', 'HAS_DIFFUSE_LOOKUP': '1'}
    mirror_plane = [0.75, -0.25, 0.30, 0.92]
    shiny_draws = ({}, {'mirror_flag': 1.0, 'clipPlane': mirror_plane},
                   {'mirror_flag': 1.0, 'clipPlane': [-component for component in mirror_plane]})
    identity = np.eye(4, dtype='f4').reshape(-1).tolist()
    # Keep the same projection for each water sign. Identity modelview puts the grid across eye Z=0.
    water = {'modelview_matrix': identity, 'waterPlane': [0, 0, -1, 0], 'minimum_alpha': 0.004}
    water_draws = ({'waterSign': 1.0}, {'waterSign': -1.0}, {'waterSign': 1.0, 'minimum_alpha': 0.0},
                   {'waterSign': 1.0, 'mirror_flag': 1.0, 'clipPlane': [0.25, 0.91, 0.05, 0.01]})

    axis = (np.arange(SIZE, dtype='f4') + 0.5) / SIZE
    s, t = np.meshgrid(axis, axis)
    bands = np.floor(8 * (s + 0.19 * t)).astype(np.int32) % 4
    base_alpha = np.take(np.array([0.0, 0.003, 0.24, 0.76], dtype='f4'), bands)
    base = np.stack((0.005 + 0.85 * s**2, 0.016 + 0.74 * t, 0.023 + 0.69 * s * t, base_alpha), axis=-1)
    emissive = np.stack((0.017 + 0.92 * t**2, 0.021 + 0.79 * s**2, 0.012 + 0.88 * s * t,
                         0.14 + 0.71 * s), axis=-1)
    pbr_params = {
        'texture_base_color_transform': H.Vec4Array(((0.83, 0.71, 0.37, 0), (0.08, 0.24, 0, 0))),
        'texture_emissive_transform': H.Vec4Array(((-0.76, 0.92, -0.21, 0), (0.81, 0.02, 0, 0))),
        'emissiveColor': [1.75, 0.58, 1.1],
    }
    # Column-major texture animation has its own rotation, scale and offset.
    angle = 0.27
    animated = [0.82 * np.cos(angle), 0.82 * np.sin(angle), 0, 0,
                -0.74 * np.sin(angle), 0.74 * np.cos(angle), 0, 0,
                0, 0, 1, 0, 0.11, 0.08, 0, 1]
    for samples in (1, 4):
        yield Case('hud-fullbright-shiny', 'deferred/fullbrightShinyV.glsl', '../class3/deferred/fullbrightShinyF.glsl',
                   'fullbrightShinyHud__IS_HUD1__IS_SHINY1', defines=hud_indexed, samples=samples,
                   variants=shiny_draws, mesh_builder=fullbright_emissive_mesh, **indexed_inputs)
        yield Case('hud-fullbright-mask-blend', 'deferred/fullbrightV.glsl', 'deferred/fullbrightF.glsl',
                   'fullbrightMaskBlendHud__IS_ALPHA1__IS_HUD1__MASK1', samples=samples,
                   defines={**hud_indexed, 'IS_ALPHA': '1', 'HAS_ALPHA_MASK': '1'},
                   fragment_helpers=('deferred/deferredUtil.glsl',),
                   uniforms=water, variants=water_draws, mesh_builder=hud_water_mesh, **indexed_inputs)
        yield Case('hud-pbr-blend', 'deferred/pbralphaV.glsl', '../class2/deferred/pbralphaF.glsl',
                   'pbrHudBlend__IS_ALPHA1__IS_HUD1', defines={'IS_HUD': '1'}, samples=samples,
                   vertex_helpers=('deferred/textureUtilV.glsl',), fragment_helpers=('environment/srgbF.glsl',),
                   uniforms=pbr_params, variants=({'texture_matrix0': animated}, {'texture_matrix0': identity}),
                   texture_levels={'diffuseMap': (base,), 'emissiveMap': (emissive,)},
                   depth=True, coverage=True, mesh_builder=pbr_mesh)


def deferred_avatar_mesh(rng):
    attributes, count = grid_mesh(rng)
    uv = attributes[2]
    s, t = uv.T
    # The viewer supplies one float in slot 9, with joint index and interpolation packed in it.
    weight = 0.15 + s * (12.0 + 1.65 * t)
    weight = np.where(s > 0.95, 15.25, np.where(s >= 0.85, 14.625, weight))
    clothing = np.column_stack((0.10 * (2 * s - 1), 0.06 + 0.10 * t, 0.12 * (1 - t),
                                np.where(t > 0.22, 0.35 + 0.60 * s, 0.0)))
    return {0: attributes[0], 1: attributes[1], 2: uv, 9: weight[:, None], 11: clothing}, count


def deferred_avatar_cases():
    # LLViewerJointMesh::uploadJointMatrices packs all 15 joint rows for each axis together.
    joints = np.empty((15, 3, 4), dtype='f4')
    for joint in range(15):
        angle = 0.29 * np.sin(0.71 * joint)
        sine, cosine = np.sin(angle), np.cos(angle)
        joints[joint] = ((cosine, 0, sine, 0.13 * np.cos(0.51 * joint)),
                         (0, 1, 0, 0.12 * np.sin(0.84 * joint)),
                         (-sine, 0, cosine, -2.85 + 0.24 * np.sin(1.07 * joint)))
    palette = H.Vec4Array(joints.transpose(1, 0, 2).reshape(45, 4))
    uniforms = {'matrixPalette': palette, 'minimum_alpha': 0.20,
                'clipPlane': [0.79, 0.18, 0.28, 0.72]}
    zero = [0.0] * 4
    no_wind = {'gWindDir': zero, 'gSinWaveParams': zero, 'gGravity': zero}
    cloth = {'gWindDir': [0.33, -0.21, 0.69, 0.37], 'gGravity': [0.07, -0.33, -0.60, 0.0]}
    wind = tuple({**cloth, 'gSinWaveParams': [6.5, 6.5, 6.5, phase]} for phase in (-4.67, -2.15))
    textures = {'diffuseMap': fullbright_textures()['tex0']}
    for emissive in (0, 1):
        for cloth_enabled in (0, 1):
            # Both literal viewer defines enter the legacy #ifdef; cloth off leaves the uniforms at zero.
            defines = {'AVATAR_CLOTH': str(cloth_enabled)}
            if emissive:
                defines['HAS_EMISSIVE'] = '1'
            yield Case(f'deferred-avatar-e{emissive}-cloth{cloth_enabled}',
                       'deferred/avatarV.glsl', 'deferred/avatarF.glsl', f'avatarDeferred__HAS_EMISSIVE{emissive}',
                       defines=defines, outputs=3 + emissive, depth=True, coverage=True, clip=True,
                       flat_ok=(1,), uniforms=uniforms, variants=wind if cloth_enabled else (no_wind,),
                       vertex_helpers=('avatar/avatarSkinV.glsl',), mesh_builder=deferred_avatar_mesh,
                       texture_levels=textures)


def pbr_terrain_mesh(rng):
    attributes, count = grid_mesh(rng, texcoord1_s_scale=3.0)
    # A region starts at positive XY in the paint map. Place the grid at the region's center.
    attributes[0][:, :2] += 1.0
    directions = np.array([(0.93, 0.18, 0.36), (-0.92, 0.20, 0.37), (0.19, 0.91, 0.37),
                           (0.22, -0.90, 0.38), (0.11, 0.25, 0.96), (0.66, 0.61, 0.47)], dtype='f4')
    directions /= np.linalg.norm(directions, axis=1, keepdims=True)
    cell = np.arange(count) // 6
    attributes[1] = directions[cell % len(directions)]
    tangent = np.cross([0, 1, 0], attributes[1])
    tangent /= np.linalg.norm(tangent, axis=1, keepdims=True)
    attributes[8] = np.column_stack((tangent, np.where(cell % 4 < 2, -1.0, 1.0)))
    # Exercise front/back-facing normals independently of each swatch's tangent handedness.
    back_faces = (np.arange(count // 3) // 2) % 3 == 1
    selected = {slot: attributes[slot] for slot in (0, 1, 3, 8)}
    for attribute in selected.values():
        triangles = attribute.reshape(-1, 3, attribute.shape[1])
        triangles[back_faces] = triangles[back_faces][:, [0, 2, 1]]
    return selected, count


def pbr_terrain_textures():
    axis = (np.arange(SIZE, dtype='f4') + 0.5) / SIZE
    s, t = np.meshgrid(axis, axis)
    x = np.clip((s - 0.35) / 0.3, 0, 1)
    y = np.clip((t - 0.35) / 0.3, 0, 1)
    # Four swatches with broad single-material areas, transitions, and slightly overfull weights.
    extra = np.where((s > 0.49) & (s < 0.56) & (t > 0.48) & (t < 0.62), 0.18, 0).astype('f4')
    paint_map = np.stack((x * (1 - y) + extra, (1 - x) * y + extra, x * y + extra, np.ones_like(s)), axis=-1)
    textures = {'paint_map': (paint_map,), 'alpha_ramp': terrain_alpha_ramp()}
    for index in range(4):
        phase = 0.73 * index
        wave = np.sin(2 * np.pi * (s + 0.61 * t) + phase)
        ripple = np.cos(2 * np.pi * (0.37 * s - 1.13 * t) - phase)
        maps = {
            'base_color': (0.20 + 0.15 * index + 0.13 * s, 0.45 + 0.24 * ripple - 0.07 * index,
                           0.16 + 0.29 * t + 0.08 * index, 0.59 + 0.37 * wave),
            'normal': (0.50 + 0.15 * wave, 0.50 + 0.17 * ripple, 0.79 + 0.15 * s, np.ones_like(s)),
            'metallic_roughness': (0.58 + 0.31 * ripple, 0.16 + 0.65 * t, 0.13 + 0.71 * s, np.ones_like(s)),
            'emissive': (0.021 + 0.71 * s**2, 0.038 + 0.56 * t**2, 0.024 + 0.14 * index + 0.33 * s * t,
                         np.ones_like(s)),
        }
        for name, channels in maps.items():
            textures[f'detail_{index}_{name}'] = (np.stack(channels, axis=-1),)
    return textures


def pbr_terrain_cases():
    textures = pbr_terrain_textures()
    repeats = tuple(name for name in textures if name.startswith('detail_'))
    view, projection, _, _ = _matrices()
    view[:3, 3] -= view[:3, 0] + view[:3, 1]
    # The viewer's PackTight upload contains four records of scale X/Y, rotation, offset X/Y.
    transforms = np.array([(1.26, 0.82, 0.21, 0.11, 0.22), (-0.96, 1.19, -0.38, 0.47, -0.31),
                           (1.41, -0.63, 0.43, -0.37, 0.25), (-0.86, -1.31, -0.24, 0.32, 0.51)], dtype='f4')
    shared = {'modelview_matrix': view.T.ravel().tolist(),
              'modelview_projection_matrix': (projection @ view).T.ravel().tolist(),
              'clipPlane': [0.79, 0.18, 0.28, 0.84],
              'terrain_texture_transforms': H.Vec4Array(transforms.reshape(5, 4)),
              'baseColorFactors': H.Vec4Array(((0.86, 0.61, 0.35, 0.87), (0.45, 0.83, 0.62, 0.78),
                                               (0.64, 0.51, 0.91, 0.69), (0.75, 0.96, 0.71, 0.92))),
              'minimum_alphas': [0.26, 0.37, 0.03, 0.29]}
    for emissive in (0, 1):
        for paint in (0, 1):
            for detail in range(-4, 1):
                uniforms = dict(shared)
                if paint == 1:
                    uniforms['region_scale'] = 2.0
                if detail >= -3:
                    uniforms.update(metallicFactors=[0.34, 0.81, 0.23, 0.64], roughnessFactors=[0.88, 0.62, 0.95, 0.42])
                if detail == 0 and emissive:
                    uniforms['emissiveColors'] = H.Vec3Array(((0.16, 0.92, 0.47), (1.10, 0.69, 0.28),
                                                               (0.57, 0.17, 0.77), (0.31, 1.18, 0.62)))
                for mapping in (1, 3):
                    defines = {'TERRAIN_PAINT_TYPE': str(paint), 'TERRAIN_PBR_DETAIL': str(detail),
                               'TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT': str(mapping)}
                    if emissive:
                        defines['HAS_EMISSIVE'] = '1'
                    name = f'pbr-terrain-e{emissive}-p{paint}-d{detail}-m{mapping}'
                    program = (f'pbrTerrain__HAS_EMISSIVE{emissive}__TERRAIN_PAINT_TYPE{paint}'
                               f'__TERRAIN_PBR_DETAIL{detail}__TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT{mapping}')
                    factors = ('2.25', '8.00') if mapping == 3 and (emissive, detail) in ((0, -4), (1, 0)) else ('8.00',)
                    for factor in factors:
                        hooks = {'TERRAIN_TRIPLANAR_BLEND_FACTOR': factor} if mapping == 3 else {}
                        suffix = '-blend-2.25' if factor != '8.00' else ''
                        yield Case(name + suffix, 'deferred/pbrterrainV.glsl', 'deferred/pbrterrainF.glsl', program,
                                   defines={**defines, **hooks}, hooks=hooks, outputs=3 + emissive,
                                   depth=True, coverage=True, clip=True, uniforms=uniforms,
                                   vertex_helpers=('deferred/textureUtilV.glsl',),
                                   fragment_helpers=('deferred/pbrterrainUtilF.glsl', 'environment/srgbF.glsl'),
                                   texture_levels=textures, repeat_textures=repeats, mesh_builder=pbr_terrain_mesh)


def transparency_coverage_cases():
    # The real table dimensions and valid r-of-eight rows, including the two
    # rows used as blue-noise ranks. Rotate each column's subset deterministically.
    columns = np.arange(2048, dtype=np.uint32)
    rotation = columns & 7
    rows = [(1 << count) - 1 for count in range(9)]
    table = np.vstack([((row << rotation) | (row >> (8 - rotation))) & 255 for row in rows] +
                      [columns, columns + 2048])
    texture_levels = {'stoch_masks': (table,)}
    water = {'waterPlane': [0.0, 0.0, 0.0, 1.0], 'waterSign': 1.0}
    coverage = {'st_samples': 8, 'st_samples_hw': 8, 'st_seed': 1051, 'st_frame': 3,
                'st_mode': 2, 'st_min_alpha': 0.0, **water}
    modes = ({'st_mode': 1}, {},
             {'st_frame': 9, 'st_seed': -1487, 'waterPlane': [0.82, 0.18, 0.0, -0.12], 'waterSign': -1.0},
             {'st_mode': 3, 'st_min_alpha': 0.38})
    shader_objects = ('deferred/deferredUtil.glsl', 'environment/srgbF.glsl')
    shading_objects = ('windlight/atmosphericsFuncs.glsl', 'windlight/atmosphericsF.glsl',
                       'windlight/atmosphericsVarsF.glsl', 'environment/waterFogF.glsl',
                       '../class2/deferred/reflectionProbeF.glsl')
    masks = ('deferred/alphaStochasticF.glsl', 'deferred/stochasticSampleF.glsl')
    atmos_vertex = ('windlight/atmosphericsVarsV.glsl', 'windlight/atmosphericsHelpersV.glsl',
                    'environment/srgbF.glsl', 'windlight/atmosphericsFuncs.glsl', 'windlight/atmosphericsV.glsl')
    unused_atmosphere = ('max_y', 'moon_glow_factor', 'primary_light_dir')
    unused_material_lighting = ('cloud_shadow_params', 'emissive_brightness', 'env_intensity', 'env_mat',
                                'light_attenuation', 'light_diffuse', 'light_direction', 'light_position',
                                'normal_matrix', 'specular_color')
    transform = H.Vec4Array([(0.86, 1.10, 0.24, 0.0), (0.10, -0.06, 0.0, 0.0)])

    for skin in (0, 1):
        for mask_texture in (0, 1):
            fullbright = bool(mask_texture)
            name = 'fullbright' if fullbright else 'indexed'
            vertex = 'deferred/fullbrightV.glsl' if fullbright else 'deferred/alphaV.glsl'
            fragment = 'deferred/fullbrightF.glsl' if fullbright else '../class2/deferred/alphaF.glsl'
            defines = {'HAS_ALPHA_MASK': '1', 'ALPHA_STOCHASTIC': '1'}
            defines.update({'IS_ALPHA': '1', 'HAS_DIFFUSE_LOOKUP': '1'} if fullbright else
                           {'USE_INDEXED_TEX': '1', 'USE_VERTEX_COLOR': '1'})
            yield Case(f'transparency-{name}-s{skin}', vertex, fragment,
                       f'alphaIndexedCoverage__HAS_SKIN{skin}__MASK_TEXTURE{mask_texture}',
                       defines=defines, skin=bool(skin), indexed_texture_channels=4, samples=8,
                       coverage=True, clip=True, flat_ok=(0,), uniforms={**coverage, 'minimum_alpha': 0.17},
                       variants=modes, texture_levels=texture_levels,
                       fragment_helpers=(*shader_objects, *shading_objects, *masks),
                       vertex_helpers=atmos_vertex if fullbright else (), optional=unused_atmosphere)

        yield Case(f'transparency-pbr-s{skin}', 'deferred/pbralphaV.glsl', '../class2/deferred/pbralphaF.glsl',
                   f'alphaPBRCoverage__HAS_SKIN{skin}__PBR1', defines={'ALPHA_STOCHASTIC': '1'},
                   skin=bool(skin), samples=8, coverage=True, clip=True, flat_ok=(0,),
                   uniforms={**coverage, 'texture_base_color_transform': transform}, variants=modes,
                   texture_levels=texture_levels, vertex_helpers=('deferred/textureUtilV.glsl',),
                   fragment_helpers=(*shader_objects, *shading_objects, *masks))

        for mapped in (0, 1):
            defines = {'DIFFUSE_ALPHA_MODE': '1', 'ALPHA_STOCHASTIC': '1'}
            if mapped:
                defines.update(HAS_NORMAL_MAP='1', HAS_SPECULAR_MAP='1')
            material_modes = (*modes[:3],
                              {**modes[3], 'env_intensity': 0.8, 'specular_color': [1.0, 0.7, 0.6, 0.55]})
            yield Case(f'transparency-material-s{skin}-maps{mapped}', 'deferred/materialV.glsl',
                       '../class3/deferred/materialF.glsl', f'alphaMaterialCoverage__HAS_SKIN{skin}__MATERIAL1',
                       defines=defines, skin=bool(skin), samples=8, coverage=True, clip=True, flat_ok=(0,),
                       uniforms={**coverage, 'env_intensity': 0.0, 'specular_color': [1.0, 0.7, 0.6, 0.0]},
                       variants=material_modes, texture_levels=texture_levels,
                       optional=unused_material_lighting,
                       fragment_helpers=(*shader_objects, *shading_objects, 'windlight/gammaF.glsl', *masks))

        yield Case(f'transparency-impostor-s{skin}', 'deferred/alphaV.glsl', '../class2/deferred/alphaF.glsl',
                   f'alphaImpostor__HAS_SKIN{skin}__IS_IMPOSTOR1', skin=bool(skin), indexed_texture_channels=4,
                   defines={'FOR_IMPOSTOR': '1', 'USE_INDEXED_TEX': '1', 'USE_VERTEX_COLOR': '1', 'HAS_ALPHA_MASK': '1'},
                   samples=4, coverage=True, clip=True, uniforms={**water, 'minimum_alpha': 0.17},
                   variants=({'classic_mode': 0}, {'classic_mode': 1}), fragment_helpers=shader_objects)


def cases():
    I, O, D = 'interface/', 'objects/', 'deferred/'
    c = []
    add = lambda *a, **k: c.append(Case(*a, **k))
    add('ui', I + 'uiV.glsl', I + 'uiF.glsl', 'ui')
    add('alphamask', I + 'alphamaskV.glsl', I + 'alphamaskF.glsl', 'alphamask')
    add('benchmark', I + 'benchmarkV.glsl', I + 'benchmarkF.glsl', 'benchmark')
    add('clip', I + 'clipV.glsl', I + 'clipF.glsl', 'clip', uniforms={'clip_plane': [0.0, 0.0, 1.0, 3.0]})
    add('copy', I + 'copyV.glsl', I + 'copyF.glsl', 'copy__COPY_DEPTH0')
    add('copy-depth', I + 'copyV.glsl', I + 'copyF.glsl', 'copy__COPY_DEPTH1', defines={'COPY_DEPTH': '1'}, depth=True)
    add('glowcombine', I + 'glowcombineV.glsl', I + 'glowcombineF.glsl', 'glowcombine')
    add('glowcombineFXAA', I + 'glowcombineFXAAV.glsl', I + 'glowcombineFXAAF.glsl', 'glowcombineFXAA')
    add('gaussian', I + 'splattexturerectV.glsl', I + 'gaussianF.glsl', 'gaussian', uniforms={'direction': [1.0, 0.0], 'resScale': 0.01})
    add('gaussian-vertical', I + 'splattexturerectV.glsl', I + 'gaussianF.glsl', 'gaussian', uniforms={'direction': [0.0, 1.0], 'resScale': 0.02})
    add('reflectionmip', I + 'splattexturerectV.glsl', I + 'reflectionmipF.glsl', 'reflectionmip')
    add('solidcolor', I + 'solidcolorV.glsl', I + 'solidcolorF.glsl', 'solidcolor')
    add('onetexturefilter', I + 'onetexturefilterV.glsl', I + 'onetexturefilterF.glsl', 'onetexturefilter')
    add('twotexturecompare', I + 'twotexturecompareV.glsl', I + 'twotexturecompareF.glsl', 'twotexturecompare')
    add('occlusion', I + 'occlusionV.glsl', I + 'occlusionF.glsl', 'occlusion__CUBE0', outputs=3, flat_ok=(0, 1))
    add('occlusion-cube', I + 'occlusionCubeV.glsl', I + 'occlusionF.glsl', 'occlusion__CUBE1', outputs=3, flat_ok=(0, 1))
    add('occlusion-skinned', I + 'occlusionSkinnedV.glsl', I + 'occlusionF.glsl', 'occlusionSkinned', outputs=3, skin=True, flat_ok=(0, 1))
    add('highlight-diffuse', I + 'highlightV.glsl', I + 'highlightF.glsl', 'highlightdiffuse__HAS_SKIN0')
    add('highlight-diffuse-rigged', I + 'highlightV.glsl', I + 'highlightF.glsl', 'highlightdiffuse__HAS_SKIN1', skin=True)
    add('highlight-normals', I + 'highlightNormV.glsl', I + 'highlightF.glsl', 'highlight__UVSET1')
    add('highlight-specular', I + 'highlightSpecV.glsl', I + 'highlightF.glsl', 'highlight__UVSET2')
    add('pathfinding', I + 'pathfindingV.glsl', I + 'pathfindingF.glsl', 'pathfinding__NORMALS1')
    add('pathfinding-no-normals', I + 'pathfindingNoNormalV.glsl', I + 'pathfindingF.glsl', 'pathfinding__NORMALS0')
    add('debug', I + 'debugV.glsl', I + 'debugF.glsl', 'debug__HAS_SKIN0')
    add('debug-rigged', I + 'debugV.glsl', I + 'debugF.glsl', 'debug__HAS_SKIN1', skin=True)
    add('drawcolor', O + 'simpleNoAtmosV.glsl', O + 'simpleColorF.glsl', 'drawcolor')
    add('impostor', O + 'impostorV.glsl', O + 'impostorF.glsl', 'impostor')
    add('preview', O + 'previewV.glsl', O + 'previewF.glsl', 'preview__HAS_SKIN0')
    add('preview-rigged', O + 'previewV.glsl', O + 'previewF.glsl', 'preview__HAS_SKIN1', skin=True)
    add('physics-preview', O + 'previewPhysicsV.glsl', O + 'previewPhysicsF.glsl', 'previewPhysics')
    faces = tuple({'modelview_matrix': m, 'sourceIdx': layer, 'max_probe_lod': 5.0} for m in H.FACE_MATRICES.values() for layer in (0, 1))
    add('irradiance', I + 'irradianceGenV.glsl', '../class2/interface/irradianceGenF.glsl', 'irradianceGen', variants=faces)
    # this stage: effects, post, quiet G-buffer programs
    add('glow', 'effects/glowV.glsl', 'effects/glowF.glsl', 'glow')
    add('glowextract', 'effects/glowExtractV.glsl', 'effects/glowExtractF.glsl', 'glowextract__HAS_NOISE0')
    add('glowextract-noise', 'effects/glowExtractV.glsl', 'effects/glowExtractF.glsl', 'glowextract__HAS_NOISE1', defines={'HAS_NOISE': '1'})
    add('vignette', 'post/exoPostBaseV.glsl', 'post/exoVignetteF.glsl', 'vignette', uniforms={'vignette': [0.8, 1.2, 1.5]})
    add('snapshotframe', 'post/exoPostBaseV.glsl', 'post/snapshotFrameF.glsl', 'snapshotframe',
        uniforms={'frame_rect': [0.25, 0.25, 0.75, 0.75], 'border_color': [1.0, 0.2, 0.1], 'border_thickness': 3.0})
    add('normgen', D + 'normgenV.glsl', D + 'normgenF.glsl', 'normgen', uniforms={'bump_code': 1, 'stepX': 0.015, 'stepY': 0.015, 'norm_scale': 0.5})
    add('normgen-darkness', D + 'normgenV.glsl', D + 'normgenF.glsl', 'normgen', uniforms={'bump_code': 2, 'stepX': 0.015, 'stepY': 0.015, 'norm_scale': 0.5})
    add('genbrdflut', D + 'genbrdflutV.glsl', D + 'genbrdflutF.glsl', 'genbrdflut')
    for emissive in (0, 1):
        e = {'HAS_EMISSIVE': '1'} if emissive else {}
        n = 4 if emissive else 3
        tag = '-emissive' if emissive else ''
        add('deferred-bump' + tag, D + 'bumpV.glsl', D + 'bumpF.glsl', f'dbump__HAS_EMISSIVE{emissive}__HAS_SKIN0', defines=e, outputs=n, clip=True, depth_info=True)
        add('deferred-bump-rigged' + tag, D + 'bumpV.glsl', D + 'bumpF.glsl', f'dbump__HAS_EMISSIVE{emissive}__HAS_SKIN1', defines=e, outputs=n, skin=True, clip=True, depth_info=True)
        add('deferred-impostor' + tag, D + 'impostorV.glsl', D + 'impostorF.glsl', f'dimpostor__HAS_EMISSIVE{emissive}', defines=e, outputs=n)
        add('deferred-tree' + tag, D + 'treeV.glsl', D + 'treeF.glsl', f'dtree__HAS_EMISSIVE{emissive}', defines=e, outputs=n, clip=True, flat_ok=(1, 3))
        add('avatar-eyes' + tag, D + 'avatarEyesV.glsl', D + 'diffuseF.glsl', f'avatareyes__HAS_EMISSIVE{emissive}', defines=e, outputs=n, clip=True)
    c.extend(deferred_post_cases())
    c.extend(depth_of_field_cases())
    c.extend(render_diagnostics_cases())
    c.extend(deferred_diffuse_cases())
    c.extend(terrain_bake_cases())
    c.extend(rlv_sphere_cases())
    c.extend(normal_debug_cases())
    c.extend(material_gbuffer_cases())
    c.extend(pbr_opaque_glow_cases())
    c.extend(legacy_terrain_cases())
    c.extend(fullbright_emissive_cases())
    c.extend(hud_material_cases())
    c.extend(deferred_avatar_cases())
    c.extend(pbr_terrain_cases())
    c.extend(transparency_coverage_cases())
    return c


def select_cases(available, programs, only=None, tasks=None, family=None):
    """A family run must exercise every advertised and generated permutation of that family."""
    selected = [case for case in available if only is None or only in case.name]
    if family is not None:
        expected = {task['id']: task['program'] for task in tasks if task['family'] == family}
        if not expected:
            raise RuntimeError(f'no registered Slang programs in family {family!r}')
        names = set(expected.values())
        missing = sorted(pid for pid, name in expected.items() if pid not in programs or programs[pid]['program'] != name)
        stale = sorted(pid for pid, entry in programs.items() if entry['program'] in names and pid not in expected)
        if missing or stale:
            raise RuntimeError(f'family {family!r} manifest differs from its registration: missing {missing}, stale {stale}')
        selected = [case for case in selected if case.gen in expected]
        uncovered = sorted(expected.keys() - {case.gen for case in selected})
        if uncovered:
            raise RuntimeError(f'family {family!r} lacks A/B cases for: {", ".join(uncovered)}')
    if not selected:
        raise RuntimeError(f'no A/B cases match {only!r}' if only is not None else 'no A/B cases registered')
    return selected


def run_programs(gl, generated, only=None, family=None, strict_depth=False):
    errors = []
    everything = []
    programs = {entry['id']: entry for entry in check.load_manifest(generated)['programs']}
    try:
        tasks = gen.load_program_list(common.SLANG_SRC)['tasks'] if family is not None else None
        selected = select_cases(cases(), programs, only=only, tasks=tasks, family=family)
    except (gen.GenError, RuntimeError) as e:
        print(f'programs: ERROR {e}')
        return 1, everything, [str(e)]
    for case in selected:
        try:
            entry = programs.get(case.gen)
            if entry is None:
                raise RuntimeError('candidate is not generated')
            results = run_case(gl, case, generated, entry, strict_depth=strict_depth)
            if not results:
                raise RuntimeError('case produced no comparisons')
        except RuntimeError as e:
            print(f'{case.name}: ERROR {e}')
            errors.append(f'{case.name} ({case.gen}): {e}')
            continue
        for r in results:
            r['program'] = case.gen
            if case.hooks:
                r['hooks'] = case.hooks
            everything.append(r)
            flag = 'pass' if r['pass'] else 'FAIL'
            print(f'{r["case"]:46s} {flag}  drawn={r["nonblack"]:5d}  differing={r["values_differing"]:6d}  max_ulp={r["max_ulp"]:8d}  '
                  f'max_abs={r["max_abs"]:.3g}  outside_tol={r["outside_tolerance"]}' + ('  (flat image)' if r.get('flat') else '') + (f'  UNSET {r["unset_uniforms"]}' if r['unset_uniforms'] else ''))
    failed = sum(not r['pass'] for r in everything)
    print(f'{len(everything)} comparison(s), {failed} failed, {len(errors)} case error(s)')
    return int(bool(failed or errors)), everything, errors


# -- the manifest against the driver ----------------------------------------------------------------------------------
GL_PROGRAM_INPUT = 0x92E3


def check_manifest_against_driver(gl, generated, results=None):
    """Link every generated program on the real driver and compare what the driver says with programs.json, which the
    viewer trusts instead of asking: the block's name and binding, every member's byte offset (GL_UNIFORM_OFFSET) and
    the set of members, each sampler's unit, and the vertex input locations. Returns the list of problems."""
    errors = []
    manifest = check.load_manifest(generated)
    header = (common.ROOT / 'indra/llrender/llgeneratedshader.h').read_text(encoding='utf-8')
    block_name = re.search(r'kBlockName\s*=\s*"(\w+)"', header).group(1)
    linked = 0
    for entry in manifest['programs']:
        pid = entry['id']
        errors_before = len(errors)
        if results is not None:
            results.append({'program': pid, 'linked': False, 'pass': False})
        try:
            prog = build_candidate(gl, generated, entry, {})
        except RuntimeError as e:
            errors.append(f'{pid}: does not link on the driver: {e}')
            continue
        linked += 1
        if results is not None:
            results[-1]['linked'] = True
        members, textures, inputs, per_stage = {}, {}, set(), []
        for stage, info in entry['stages'].items():
            iface = info['interface']
            for m in iface['uniforms']:
                members[m['name']] = m
                per_stage.append((stage, m))
            for t in iface['textures']:
                textures[t['name']] = t['binding']
            if stage == 'vertex':
                inputs = {i['location'] for i in iface['inputs']}
        blocks = gl.blocks(prog)
        if members:
            if [b['name'] for b in blocks] != [block_name]:
                errors.append(f'{pid}: the driver has blocks {[b["name"] for b in blocks]}, the loader asks for {block_name}')
                continue
            stage = per_stage[0][0]
            text = (Path(generated) / entry['stages'][stage]['file']).read_text(encoding='utf-8')
            declared = check.block_of(text)
            if declared and declared[0] != blocks[0]['binding']:
                errors.append(f'{pid}: the driver binds the block at {blocks[0]["binding"]}, the source says {declared[0]}')
        elif blocks:
            errors.append(f'{pid}: the driver has a block, the manifest lists no uniform')
        gl_members = {}
        for u in gl.uniforms(prog):
            if u['block'] >= 0:
                base = re.sub(r'\[0\]$', '', u['name'].split('.')[-1])
                gl_members[base] = u
        for stage, m in per_stage:   # every stage lists the members it uses; the loader merges them, so each must be right
            u = gl_members.get(m['name'])
            if u is None:
                errors.append(f'{pid}.{stage}: manifest member {m["name"]} is not in the driver block')
            elif u['offset'] != m['offset']:
                errors.append(f'{pid}.{stage}: {m["name"]} at byte {m["offset"]} in the manifest, {u["offset"]} by GL_UNIFORM_OFFSET')
        for name in sorted(set(gl_members) - set(members)):
            errors.append(f'{pid}: the driver block has {name}, the manifest does not (the viewer could never set it)')
        for u in gl.uniforms(prog):
            if u['type'] in H.SAMPLER_TYPES and u['block'] < 0:
                name = re.sub(r'\[0\]$', '', u['name'])
                unit = C.c_int()
                gl.fn('glUseProgram', None, C.c_uint)(prog)
                gl.fn('glGetUniformiv', None, C.c_uint, C.c_int, C.POINTER(C.c_int))(prog, u['location'], C.byref(unit))
                if textures.get(name) != unit.value:
                    errors.append(f'{pid}: sampler {name} is on unit {unit.value} by the driver, {textures.get(name)} in the manifest')
        if 'vertex' in entry['stages']:
            gl_inputs = {v[2] for _, v in gl._resources(prog, GL_PROGRAM_INPUT, [H.GL_NAME_LENGTH, H.GL_TYPE, H.GL_LOCATION])}
            if gl_inputs != inputs:
                errors.append(f'{pid}: vertex input locations {sorted(gl_inputs)} by the driver, {sorted(inputs)} in the manifest')
        if results is not None:
            results[-1]['pass'] = len(errors) == errors_before
    if not manifest['programs']:
        errors.append('no generated programs in manifest')
    print(f'manifest against the driver: {linked} of {len(manifest["programs"])} programs linked')
    return errors
