# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Visual A/B harness for generated shaders: offline, fixed inputs, no viewer.

It renders the hand-written GLSL program (the reference) and its generated Slang program (the candidate) into float
targets from the same fixed inputs, reads both back, and compares them. The image gate of the Slang switchover
(decision D3): the Slang output is taken as it is, so tiny float differences are expected and accepted; the test is
a sanity check that a program renders what it did. Positions, depth and coverage are the exception and are compared
exactly (case mode "exact").

Windows, OpenGL 4.6 core through WGL (ctypes; no GLFW or PyOpenGL). Case modules live in this file; the first one is the
radiance filter (class1/interface/radianceGen{V,F}.glsl).

  python scripts/shaders/ab_harness.py --list
  python scripts/shaders/ab_harness.py --selftest                  the harness proves itself (identity, detects a change)
  python scripts/shaders/ab_harness.py radiance                    reference against generated radianceGen
Exit codes: 0 pass, 1 fail, 2 candidate not generated yet (nothing compared).
"""
import argparse
import ctypes as C
import json
import re
import struct
import sys
from ctypes import wintypes as W
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import slang_check as check  # noqa: E402
import slang_common as common  # noqa: E402

GL_VERTEX_SHADER, GL_FRAGMENT_SHADER = 0x8B31, 0x8B30
GL_COMPILE_STATUS, GL_LINK_STATUS = 0x8B81, 0x8B82
GL_UNIFORM, GL_UNIFORM_BLOCK = 0x92E1, 0x92E2
GL_ACTIVE_RESOURCES = 0x92F5
GL_NAME_LENGTH, GL_TYPE, GL_LOCATION, GL_OFFSET, GL_BLOCK_INDEX = 0x92F9, 0x92FA, 0x930E, 0x92FC, 0x92FD
GL_ARRAY_SIZE, GL_ARRAY_STRIDE, GL_MATRIX_STRIDE, GL_BUFFER_DATA_SIZE, GL_BUFFER_BINDING = 0x92FB, 0x92FE, 0x92FF, 0x9303, 0x9302
GL_ARRAY_BUFFER, GL_UNIFORM_BUFFER, GL_STATIC_DRAW = 0x8892, 0x8A11, 0x88E4
GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_COMPLETE = 0x8D40, 0x8CE0, 0x8CD5
GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE0 = 0x0DE1, 0x9009, 0x84C0
GL_RGBA, GL_RGBA32F, GL_FLOAT, GL_TRIANGLES = 0x1908, 0x8814, 0x1406, 0x0004
GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR = 0x2801, 0x2800, 0x2601, 0x2703
GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE = 0x2802, 0x2803, 0x812F
GL_SAMPLER_CUBE_MAP_ARRAY = 0x900C
SAMPLER_TYPES = {0x8B5E, 0x8B60, 0x900C, 0x8DC1, 0x8DC5, 0x8B62, 0x904D, 0x8DC8}


class GL:
    """A hidden window with an OpenGL 4.6 core context, and the few entry points the cases need."""

    def __init__(self):
        user = C.WinDLL('user32', use_last_error=True)
        gdi = C.WinDLL('gdi32', use_last_error=True)
        self.gl = C.WinDLL('opengl32.dll', use_last_error=True)
        user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int,
                                         W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p]
        user.CreateWindowExW.restype = W.HWND
        user.GetDC.argtypes = [W.HWND]
        user.GetDC.restype = W.HDC

        class PFD(C.Structure):
            _fields_ = [('size', W.WORD), ('version', W.WORD), ('flags', W.DWORD), ('pixel', C.c_ubyte),
                        ('color', C.c_ubyte), ('rest', C.c_ubyte * 18), ('layerMask', W.DWORD), ('visibleMask', W.DWORD),
                        ('damageMask', W.DWORD)]
        pfd = PFD()
        pfd.size, pfd.version, pfd.flags, pfd.color = C.sizeof(PFD), 1, 0x24, 24
        self.window = user.CreateWindowExW(0, 'STATIC', 'slang-ab', 0, 0, 0, 16, 16, None, None, None, None)
        hdc = user.GetDC(self.window)
        gdi.ChoosePixelFormat.argtypes = [W.HDC, C.POINTER(PFD)]
        gdi.SetPixelFormat.argtypes = [W.HDC, C.c_int, C.POINTER(PFD)]
        if not gdi.SetPixelFormat(hdc, gdi.ChoosePixelFormat(hdc, C.byref(pfd)), C.byref(pfd)):
            raise RuntimeError('SetPixelFormat failed')
        g = self.gl
        g.wglCreateContext.argtypes = [W.HDC]
        g.wglCreateContext.restype = C.c_void_p
        g.wglMakeCurrent.argtypes = [W.HDC, C.c_void_p]
        g.wglGetProcAddress.argtypes = [C.c_char_p]
        g.wglGetProcAddress.restype = C.c_void_p
        legacy = g.wglCreateContext(hdc)
        g.wglMakeCurrent(hdc, legacy)
        attribs_fn = C.WINFUNCTYPE(C.c_void_p, W.HDC, C.c_void_p, C.POINTER(C.c_int))(g.wglGetProcAddress(b'wglCreateContextAttribsARB'))
        attribs = (C.c_int * 9)(0x2091, 4, 0x2092, 6, 0x9126, 1, 0x2094, 0x2, 0)  # 4.6, core, forward-compatible bit off
        ctx = attribs_fn(hdc, None, attribs)
        if not ctx:
            raise RuntimeError('no OpenGL 4.6 core context')
        g.wglMakeCurrent(hdc, ctx)
        self.f = {}
        gs = self.fn('glGetString', C.c_char_p, C.c_uint)
        self.info = {'renderer': gs(0x1F01).decode(), 'version': gs(0x1F02).decode(), 'glsl': gs(0x8B8C).decode()}

    def fn(self, name, restype, *argtypes):
        if name in self.f:
            return self.f[name]
        addr = self.gl.wglGetProcAddress(name.encode())
        if addr in (None, 0, 1, 2, 3, C.c_void_p(-1).value):
            func = getattr(self.gl, name)
            func.restype, func.argtypes = restype, list(argtypes)
        else:
            func = C.WINFUNCTYPE(restype, *argtypes)(addr)
        self.f[name] = func
        return func

    # -- programs ---------------------------------------------------------------------------------------------------
    def compile(self, kind, source):
        g = self.fn
        sh = g('glCreateShader', C.c_uint, C.c_uint)(kind)
        src = C.c_char_p(source.encode())
        g('glShaderSource', None, C.c_uint, C.c_int, C.POINTER(C.c_char_p), C.c_void_p)(sh, 1, C.byref(src), None)
        g('glCompileShader', None, C.c_uint)(sh)
        ok = C.c_int()
        g('glGetShaderiv', None, C.c_uint, C.c_uint, C.POINTER(C.c_int))(sh, GL_COMPILE_STATUS, C.byref(ok))
        if not ok.value:
            log = C.create_string_buffer(16384)
            g('glGetShaderInfoLog', None, C.c_uint, C.c_int, C.c_void_p, C.c_void_p)(sh, len(log), None, log)
            raise RuntimeError('compile failed:\n' + log.value.decode(errors='replace'))
        return sh

    def program(self, vs_source, fs_source, attribs=('position',)):
        g = self.fn
        prog = g('glCreateProgram', C.c_uint)()
        for kind, text in ((GL_VERTEX_SHADER, vs_source), (GL_FRAGMENT_SHADER, fs_source)):
            sh = self.compile(kind, text)
            g('glAttachShader', None, C.c_uint, C.c_uint)(prog, sh)
            g('glDeleteShader', None, C.c_uint)(sh)
        for i, name in enumerate(attribs):  # a layout(location) in the source wins; the hand-written GLSL binds by name
            g('glBindAttribLocation', None, C.c_uint, C.c_uint, C.c_char_p)(prog, i, name.encode())
        g('glLinkProgram', None, C.c_uint)(prog)
        ok = C.c_int()
        g('glGetProgramiv', None, C.c_uint, C.c_uint, C.POINTER(C.c_int))(prog, GL_LINK_STATUS, C.byref(ok))
        if not ok.value:
            log = C.create_string_buffer(16384)
            g('glGetProgramInfoLog', None, C.c_uint, C.c_int, C.c_void_p, C.c_void_p)(prog, len(log), None, log)
            raise RuntimeError('link failed:\n' + log.value.decode(errors='replace'))
        return prog

    def _resources(self, prog, iface, props):
        g = self.fn
        n = C.c_int()
        g('glGetProgramInterfaceiv', None, C.c_uint, C.c_uint, C.c_uint, C.POINTER(C.c_int))(prog, iface, GL_ACTIVE_RESOURCES, C.byref(n))
        out = []
        arr = (C.c_uint * len(props))(*props)
        for i in range(n.value):
            vals = (C.c_int * len(props))()
            g('glGetProgramResourceiv', None, C.c_uint, C.c_uint, C.c_uint, C.c_int, C.c_void_p, C.c_int, C.c_void_p, C.c_void_p)(
                prog, iface, i, len(props), arr, len(props), None, vals)
            name = C.create_string_buffer(vals[0] + 1)
            g('glGetProgramResourceName', None, C.c_uint, C.c_uint, C.c_uint, C.c_int, C.c_void_p, C.c_void_p)(
                prog, iface, i, vals[0] + 1, None, name)
            out.append((name.value.decode(), list(vals)))
        return out

    def uniforms(self, prog):
        props = [GL_NAME_LENGTH, GL_TYPE, GL_LOCATION, GL_OFFSET, GL_BLOCK_INDEX, GL_ARRAY_SIZE, GL_ARRAY_STRIDE, GL_MATRIX_STRIDE]
        keys = ['len', 'type', 'location', 'offset', 'block', 'array_size', 'array_stride', 'matrix_stride']
        return [dict(zip(keys, v), name=n) for n, v in self._resources(prog, GL_UNIFORM, props)]

    def blocks(self, prog):
        props = [GL_NAME_LENGTH, GL_BUFFER_DATA_SIZE, GL_BUFFER_BINDING]
        return [{'name': n, 'size': v[1], 'binding': v[2]} for n, v in self._resources(prog, GL_UNIFORM_BLOCK, props)]

    def set_uniforms(self, prog, values):
        """Set uniforms by name, in the default block or in a uniform block (std140, offsets from introspection).
        A value is a float, an int, a list of floats (vecN, or a column-major matrix) or a list of ints. Unknown names are
        skipped (the optimiser drops unused uniforms); the names that were set are returned."""
        g = self.fn
        g('glUseProgram', None, C.c_uint)(prog)
        uni = self.uniforms(prog)
        blocks = self.blocks(prog)
        buffers = {}
        done = set()
        for u in uni:
            base = u['name'].split('.')[-1]
            base = re.sub(r'\[0\]$', '', base)
            key = base if base in values else (base[:-2] if base.endswith('_0') and base[:-2] in values else None)
            if key is None or u['type'] in SAMPLER_TYPES:
                continue
            v = values[key]
            vals = list(v) if isinstance(v, (list, tuple)) else [v]
            is_int = all(isinstance(x, int) and not isinstance(x, bool) for x in vals) and u['type'] in (0x1404, 0x8B53, 0x8B54, 0x8B55)
            data = struct.pack(f'{len(vals)}{"i" if is_int else "f"}', *vals)
            if u['block'] < 0:
                n = len(vals)
                if u['type'] == 0x8B5C:  # mat4
                    g('glUniformMatrix4fv', None, C.c_int, C.c_int, C.c_ubyte, C.c_void_p)(u['location'], 1, 0, data)
                elif is_int:
                    g({1: 'glUniform1iv', 2: 'glUniform2iv', 3: 'glUniform3iv', 4: 'glUniform4iv'}[n], None, C.c_int, C.c_int, C.c_void_p)(u['location'], 1, data)
                else:
                    g({1: 'glUniform1fv', 2: 'glUniform2fv', 3: 'glUniform3fv', 4: 'glUniform4fv'}[n], None, C.c_int, C.c_int, C.c_void_p)(u['location'], 1, data)
            else:
                buf = buffers.setdefault(u['block'], bytearray(blocks[u['block']]['size']))
                if u['type'] == 0x8B5C:  # a column-major mat4 in a std140 block: 4 columns, matrix_stride apart
                    for col in range(4):
                        at = u['offset'] + col * u['matrix_stride']
                        buf[at:at + 16] = data[col * 16:(col + 1) * 16]
                else:
                    buf[u['offset']:u['offset'] + len(data)] = data
            done.add(key)
        for index, buf in buffers.items():
            ubo = C.c_uint()
            g('glGenBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(ubo))
            g('glBindBuffer', None, C.c_uint, C.c_uint)(GL_UNIFORM_BUFFER, ubo.value)
            raw = bytes(buf)
            g('glBufferData', None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)(GL_UNIFORM_BUFFER, len(raw), raw, GL_STATIC_DRAW)
            g('glBindBufferBase', None, C.c_uint, C.c_uint, C.c_uint)(GL_UNIFORM_BUFFER, blocks[index]['binding'], ubo.value)
        return done

    def sampler_units(self, prog, names, assign):
        """Texture unit per sampler name. A hand-written program has loose samplers: with assign=True they get units 0, 1,
        ... in the order given. A generated one has layout(binding = N): its unit is read back, never set."""
        g = self.fn
        g('glUseProgram', None, C.c_uint)(prog)
        units = {}
        for u in self.uniforms(prog):
            if u['type'] not in SAMPLER_TYPES or u['block'] >= 0:
                continue
            name = re.sub(r'\[0\]$', '', u['name'])
            key = name if name in names else (name[:-2] if name.endswith('_0') and name[:-2] in names else None)
            if key is None:
                continue
            if assign:
                g('glUniform1i', None, C.c_int, C.c_int)(u['location'], names.index(key))
                units[key] = names.index(key)
            else:
                unit = C.c_int()
                g('glGetUniformiv', None, C.c_uint, C.c_int, C.POINTER(C.c_int))(prog, u['location'], C.byref(unit))
                units[key] = unit.value
        return units


class Target:
    def __init__(self, gl, w, h):
        g = gl.fn
        self.gl, self.w, self.h = gl, w, h
        tex = C.c_uint()
        g('glGenTextures', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(tex))
        g('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_2D, tex.value)
        g('glTexImage2D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
            GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, None)
        fbo = C.c_uint()
        g('glGenFramebuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(fbo))
        g('glBindFramebuffer', None, C.c_uint, C.c_uint)(GL_FRAMEBUFFER, fbo.value)
        g('glFramebufferTexture2D', None, C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_int)(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.value, 0)
        if g('glCheckFramebufferStatus', C.c_uint, C.c_uint)(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE:
            raise RuntimeError('float framebuffer incomplete')
        self.fbo = fbo.value

    def draw_triangle(self, prog, vertices):
        g = self.gl.fn
        g('glBindFramebuffer', None, C.c_uint, C.c_uint)(GL_FRAMEBUFFER, self.fbo)
        g('glViewport', None, C.c_int, C.c_int, C.c_int, C.c_int)(0, 0, self.w, self.h)
        vao = C.c_uint()
        g('glGenVertexArrays', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vao))
        g('glBindVertexArray', None, C.c_uint)(vao.value)
        vbo = C.c_uint()
        g('glGenBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vbo))
        g('glBindBuffer', None, C.c_uint, C.c_uint)(GL_ARRAY_BUFFER, vbo.value)
        data = np.asarray(vertices, dtype='<f4').tobytes()
        g('glBufferData', None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)(GL_ARRAY_BUFFER, len(data), data, GL_STATIC_DRAW)
        g('glEnableVertexAttribArray', None, C.c_uint)(0)
        g('glVertexAttribPointer', None, C.c_uint, C.c_int, C.c_uint, C.c_ubyte, C.c_int, C.c_void_p)(0, 3, GL_FLOAT, 0, 12, None)
        g('glUseProgram', None, C.c_uint)(prog)
        g('glClearColor', None, C.c_float, C.c_float, C.c_float, C.c_float)(-1.0, -1.0, -1.0, -1.0)
        g('glClear', None, C.c_uint)(0x4000)
        g('glDrawArrays', None, C.c_uint, C.c_int, C.c_int)(GL_TRIANGLES, 0, len(vertices))
        g('glFinish', None)()
        out = (C.c_float * (self.w * self.h * 4))()
        g('glReadPixels', None, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
            0, 0, self.w, self.h, GL_RGBA, GL_FLOAT, out)
        g('glDeleteBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vbo))
        g('glDeleteVertexArrays', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vao))
        return np.frombuffer(out, dtype='<f4').reshape(self.h, self.w, 4).copy()


# -- comparison -----------------------------------------------------------------------------------------------------
def ulp_distance(a, b):
    """Distance in representable float32 steps (0 for equal values, including -0.0 against 0.0)."""
    ia = a.view(np.int32).astype(np.int64)
    ib = b.view(np.int32).astype(np.int64)
    ia = np.where(ia < 0, np.int64(-2**31) - ia, ia)
    ib = np.where(ib < 0, np.int64(-2**31) - ib, ib)
    return np.abs(ia - ib)


def compare(ref, cand, mode='tolerance', rel_tol=1e-3, abs_tol=1e-5):
    """Compare two float32 images. mode 'exact': every bit must agree (positions, depth, coverage). mode 'tolerance':
    np.isclose-style with the ULP statistics reported, because Slang reshapes expressions (decision D3)."""
    ref, cand = np.ascontiguousarray(ref, '<f4'), np.ascontiguousarray(cand, '<f4')
    same_nan = np.isnan(ref) & np.isnan(cand)
    ulp = np.where(same_nan, 0, ulp_distance(ref, cand))
    differing = ulp > 0
    absdiff = np.where(same_nan, 0.0, np.abs(ref - cand))
    ok_tol = np.isclose(ref, cand, rtol=rel_tol, atol=abs_tol, equal_nan=True)
    result = {
        'texels': int(ref.shape[0] * ref.shape[1]),
        'values_differing': int(differing.sum()),
        'max_ulp': int(ulp.max()) if ulp.size else 0,
        'max_abs': float(np.nanmax(absdiff)) if absdiff.size else 0.0,
        'outside_tolerance': int((~ok_tol).sum()),
        'mode': mode,
    }
    result['pass'] = result['values_differing'] == 0 if mode == 'exact' else result['outside_tolerance'] == 0
    return result


# -- the radiance filter ----------------------------------------------------------------------------------------------
CUBE_SIZE, LAYERS = 32, 2


def fixed_probe_cube():
    """The fixed input: a procedural HDR environment, the same on every run (no random state)."""
    levels = int(np.log2(CUBE_SIZE)) + 1
    ii, jj = np.meshgrid(np.arange(CUBE_SIZE, dtype='f4'), np.arange(CUBE_SIZE, dtype='f4'), indexing='ij')
    data = np.zeros((LAYERS * 6, CUBE_SIZE, CUBE_SIZE, 4), dtype='<f4')
    for layer in range(LAYERS):
        for face in range(6):
            u, v = (jj + 0.5) / CUBE_SIZE, (ii + 0.5) / CUBE_SIZE
            sun = np.exp(-80.0 * ((u - 0.3 - 0.1 * face) ** 2 + (v - 0.6) ** 2)) * (6.0 + 2.0 * layer)
            sky = 0.2 + 0.8 * v
            data[layer * 6 + face] = np.stack([sky * 0.5 + sun, sky * 0.7 + 0.8 * sun, sky + 0.6 * sun,
                                               np.full_like(u, 1.0)], axis=-1)
    return data, levels


def upload_cube(gl, data, levels):
    g = gl.fn
    tex = C.c_uint()
    g('glGenTextures', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(tex))
    g('glActiveTexture', None, C.c_uint)(GL_TEXTURE0)
    g('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_CUBE_MAP_ARRAY, tex.value)
    g('glTexImage3D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
        GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_RGBA32F, CUBE_SIZE, CUBE_SIZE, LAYERS * 6, 0, GL_RGBA, GL_FLOAT, data.tobytes())
    g('glGenerateMipmap', None, C.c_uint)(GL_TEXTURE_CUBE_MAP_ARRAY)
    for p, v in ((GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR), (GL_TEXTURE_MAG_FILTER, GL_LINEAR)):
        g('glTexParameteri', None, C.c_uint, C.c_uint, C.c_int)(GL_TEXTURE_CUBE_MAP_ARRAY, p, v)
    return tex.value


def read_legacy(path_in_class1, defines, stage):
    """The hand-written file as the loader assembles it today: #version 420, the extra code, the file."""
    text = (common.LEGACY_SHADERS / 'class1' / path_in_class1).read_text(encoding='utf-8')
    extra = [f'#define {"FRAGMENT_SHADER" if stage == "fragment" else "VERTEX_SHADER"} 1']
    extra += [f'#define {k} {v}' for k, v in defines.items()]
    extra.append('struct GBufferInfo { vec4 albedo; vec4 specular; vec3 normal; vec4 emissive; float gbufferFlag; float envIntensity; };')
    block = '\n'.join(extra) + '\n'
    if '[EXTRA_CODE_HERE]' in text:
        text = re.sub(r'(?m)^.*\[EXTRA_CODE_HERE\].*$', lambda m: block, text, count=1)
        return '#version 420\n' + text
    return '#version 420\n' + block + text


# the two smallest drawn faces: a triangle covering the target, directions on the +Z, +X and -Y faces
FACE_MATRICES = {
    '+Z': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
    '+X': [0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1],
    '-Y': [1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1],
}
TRIANGLE = [(-1, -1, 0.5), (3, -1, 0.5), (-1, 3, 0.5)]


def run_radiance(gl, reference, candidate, size=32, mips=(0, 1, 3, 5)):
    """Draw the radiance filter for several faces, mip levels and source layers with both programs; compare each."""
    data, levels = fixed_probe_cube()
    cube = upload_cube(gl, data, levels)
    target = Target(gl, size, size)
    results = []
    units = {'ref': gl.sampler_units(reference, ['reflectionProbes'], assign=True),
             'cand': gl.sampler_units(candidate, ['reflectionProbes'], assign=False)}
    for face, matrix in FACE_MATRICES.items():
        for mip in mips:
            for layer in range(LAYERS):
                values = {'modelview_matrix': matrix, 'sourceIdx': layer, 'mipLevel': float(mip), 'u_width': size,
                          'max_probe_lod': float(levels - 1), 'probe_strength': 0.75}
                images = {}
                for name, prog in (('ref', reference), ('cand', candidate)):
                    gl.set_uniforms(prog, values)
                    gl.fn('glActiveTexture', None, C.c_uint)(GL_TEXTURE0 + units[name].get('reflectionProbes', 0))
                    gl.fn('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_CUBE_MAP_ARRAY, cube)
                    images[name] = target.draw_triangle(prog, TRIANGLE)
                r = compare(images['ref'], images['cand'])
                r.update(case=f'face {face} mip {mip} layer {layer}')
                r['nonblack'] = int((images['ref'][..., :3] > 0).sum())
                results.append(r)
    return results


def radiance_reference(gl, defines=None, tweak=None):
    defines = defines or {'PROBE_FILTER_SAMPLES': '32'}
    vs = read_legacy('interface/radianceGenV.glsl', defines, 'vertex')
    fs = read_legacy('interface/radianceGenF.glsl', defines, 'fragment')
    if tweak:
        fs = tweak(fs)
    return gl.program(vs, fs)


def radiance_candidate(gl, generated, program_id):
    manifest = check.load_manifest(generated)
    prog = next((p for p in manifest['programs'] if p['id'] == program_id), None)
    if prog is None:
        return None
    texts = {}
    for stage in ('vertex', 'fragment'):
        text = (Path(generated) / prog['stages'][stage]['file']).read_text(encoding='utf-8')
        texts[stage] = check.assemble_source(stage, text, {})
    return gl.program(texts['vertex'], texts['fragment'])


def summarize(results):
    return {
        'cases': len(results),
        'failed': [r['case'] for r in results if not r['pass']],
        'max_ulp': max(r['max_ulp'] for r in results),
        'max_abs': max(r['max_abs'] for r in results),
        'values_differing': sum(r['values_differing'] for r in results),
        'nonblack_min': min(r['nonblack'] for r in results),
    }


FIXTURES = common.ROOT / 'scripts/shaders/fixtures/src'
TINT_VS = """#version 420
uniform mat4 modelview_projection_matrix;
in vec3 position; in vec2 texcoord0; in vec4 diffuse_color;
out vec2 vary_texcoord0; out vec4 vertex_color;
void main() { gl_Position = modelview_projection_matrix * vec4(position, 1.0);
  vary_texcoord0 = texcoord0; vertex_color = diffuse_color; }
"""
TINT_FS = """#version 420
uniform vec4 tint; uniform sampler2D diffuseMap;
in vec2 vary_texcoord0; in vec4 vertex_color; out vec4 frag_color;
void main() { frag_color = texture(diffuseMap, vary_texcoord0) * vertex_color * tint * 2.0; }
"""


def selftest_block_path(gl):
    """The generated-program path of the harness (uniforms packed into a std140 block from introspection, a sampler with
    layout(binding)) against a loose-uniform program, on the fixture program `tint` (a mat4, a vec4 and a texture)."""
    import slang_gen
    import tempfile
    g = gl.fn
    with tempfile.TemporaryDirectory() as tmp:
        slang_gen.generate(tmp, FIXTURES)
        cand = radiance_candidate_for(gl, tmp, 'tint__DOUBLE1')
    ref = gl.program(TINT_VS, TINT_FS, attribs=('position', 'x', 'texcoord0', 'x', 'x', 'x', 'diffuse_color'))
    target = Target(gl, 8, 8)  # before the texture: it binds its own colour texture on the active unit
    tex = C.c_uint()
    g('glGenTextures', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(tex))
    texels = np.linspace(0.05, 0.95, 4 * 4 * 4, dtype='<f4').reshape(4, 4, 4)
    for unit in (0, 1):
        g('glActiveTexture', None, C.c_uint)(GL_TEXTURE0 + unit)
        g('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_2D, tex.value)
        if unit == 0:
            g('glTexImage2D', None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
                GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, texels.tobytes())
            for p, v in ((GL_TEXTURE_MIN_FILTER, GL_LINEAR), (GL_TEXTURE_MAG_FILTER, GL_LINEAR),
                         (GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE), (GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)):
                g('glTexParameteri', None, C.c_uint, C.c_uint, C.c_int)(GL_TEXTURE_2D, p, v)
    gl.sampler_units(ref, ['diffuseMap'], assign=True)
    print('block path: candidate sampler units', gl.sampler_units(cand, ['diffuseMap'], assign=False))
    values = {'modelview_projection_matrix': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'tint': [0.9, 0.8, 0.7, 1.0]}
    images = {}
    for name, prog in (('ref', ref), ('cand', cand)):
        done = gl.set_uniforms(prog, values)
        assert done == set(values), (name, done)
        g('glVertexAttrib4f', None, C.c_uint, C.c_float, C.c_float, C.c_float, C.c_float)(2, 0.3, 0.6, 0.0, 1.0)
        g('glVertexAttrib4f', None, C.c_uint, C.c_float, C.c_float, C.c_float, C.c_float)(6, 0.5, 1.0, 0.25, 1.0)
        images[name] = target.draw_triangle(prog, TRIANGLE)
    r = compare(images['ref'], images['cand'])
    r['nonblack'] = int((images['ref'][..., :3] > 0).sum())
    print('block path:', {k: r[k] for k in ('values_differing', 'max_ulp', 'outside_tolerance', 'nonblack')})
    return r['pass'] and r['nonblack'] > 0


def radiance_candidate_for(gl, generated, program_id):
    return radiance_candidate(gl, generated, program_id)


def selftest(gl):
    """The harness proves itself: the reference against itself passes with zero differing values, and against a
    program with one changed constant it fails, so a real difference cannot slip through."""
    ref = radiance_reference(gl)
    same = summarize(run_radiance(gl, ref, radiance_reference(gl)))
    changed = summarize(run_radiance(gl, ref, radiance_reference(gl, tweak=lambda fs: fs.replace('+ 0.0001;', '+ 0.01;', 1))))
    exact_ref = run_radiance(gl, ref, radiance_reference(gl), size=8, mips=(2,))
    block_ok = selftest_block_path(gl) if common.slangc_path(common.read_pin()) else True
    ok = (not same['failed'] and same['values_differing'] == 0 and same['nonblack_min'] > 0
          and changed['failed'] and changed['values_differing'] > 0 and block_ok)
    print(json.dumps({'identity': same, 'one_constant_changed': changed, 'exact_mode_identity_pass': all(r['pass'] for r in exact_ref)}, indent=1))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('case', nargs='?', choices=['radiance'])
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--generated', default=str(common.GENERATED))
    ap.add_argument('--program', default='radianceGen__PROBE_FILTER_SAMPLES32', help='generated program id of the candidate')
    ap.add_argument('--json', help='write the per-case results here')
    args = ap.parse_args()
    if args.list:
        print('radiance: class1/interface/radianceGen{V,F}.glsl against the generated program (tolerance mode)')
        return 0
    gl = GL()
    print(f'GL: {gl.info["renderer"]} | {gl.info["version"]} | GLSL {gl.info["glsl"]}')
    if args.selftest:
        ok = selftest(gl)
        print('selftest: ' + ('pass' if ok else 'FAIL'))
        return 0 if ok else 1
    if args.case == 'radiance':
        cand = radiance_candidate(gl, args.generated, args.program)
        if cand is None:
            print(f'candidate {args.program} is not generated yet; nothing compared')
            return 2
        results = run_radiance(gl, radiance_reference(gl), cand)
        print(json.dumps(summarize(results), indent=1))
        if args.json:
            Path(args.json).write_text(json.dumps(results, indent=1), encoding='utf-8')
        return 0 if not [r for r in results if not r['pass']] else 1
    ap.print_help()
    return 0


if __name__ == '__main__':
    sys.exit(main())
