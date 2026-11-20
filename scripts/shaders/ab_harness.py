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
  python scripts/shaders/ab_harness.py manifest                    programs.json against the driver (offsets, block, units, inputs)
  python scripts/shaders/ab_harness.py programs [--only TEXT | --family NAME]    converted programs (ab_cases.py)
  --expect-renderer TEXT verifies the OpenGL renderer. --report PATH records the GPU and qualification results.
Exit codes: 0 pass, 1 fail, 2 candidate not generated yet (nothing compared).
"""
import argparse
import ctypes as C
import hashlib
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

GL_VERTEX_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER = 0x8B31, 0x8DD9, 0x8B30, 0x91B9
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


class Mat3x4Array:
    """A uniform array of GLSL mat3x4 (3 columns of 4 rows): `rows` is each matrix as 4 rows of 3 values, row-major (what
    glUniformMatrix3x4fv takes with transpose set, and what the viewer's palette upload sends)."""

    def __init__(self, rows):
        self.rows = [float(x) for x in rows]
        self.count = len(self.rows) // 12


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
        create_context = g.wglGetProcAddress(b'wglCreateContextAttribsARB')
        if not create_context:
            raise RuntimeError('WGL cannot create an OpenGL 4.6 core context')
        attribs_fn = C.WINFUNCTYPE(C.c_void_p, W.HDC, C.c_void_p, C.POINTER(C.c_int))(create_context)
        attribs = (C.c_int * 9)(0x2091, 4, 0x2092, 6, 0x9126, 1, 0x2094, 0x2, 0)  # 4.6, core, forward-compatible bit set
        ctx = attribs_fn(hdc, None, attribs)
        if not ctx:
            raise RuntimeError('no OpenGL 4.6 core context')
        g.wglMakeCurrent(hdc, ctx)
        self.f = {}
        self.ubos = {}
        gs = self.fn('glGetString', C.c_char_p, C.c_uint)
        self.info = {'vendor': gs(0x1F00).decode(), 'renderer': gs(0x1F01).decode(),
                     'version': gs(0x1F02).decode(), 'glsl': gs(0x8B8C).decode()}

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

    def program(self, vs_source, fs_source, attribs=('position',), fragment_helpers=(), geometry_source=None):
        stages = [(GL_VERTEX_SHADER, vs_source)]
        if geometry_source is not None:
            stages.append((GL_GEOMETRY_SHADER, geometry_source))
        stages.append((GL_FRAGMENT_SHADER, fs_source))
        stages.extend((GL_FRAGMENT_SHADER, text) for text in fragment_helpers)
        return self.link(stages, attribs)

    def link(self, stages, attribs=()):
        g = self.fn
        prog = g('glCreateProgram', C.c_uint)()
        for kind, text in stages:
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
            if isinstance(v, Mat3x4Array):  # GLSL mat3x4 array: v.rows holds 4 rows of 3 values per matrix (row-major)
                if u['block'] < 0:
                    data = struct.pack(f'{len(v.rows)}f', *v.rows)
                    g('glUniformMatrix3x4fv', None, C.c_int, C.c_int, C.c_ubyte, C.c_void_p)(u['location'], v.count, 1, data)
                else:
                    buf = buffers.setdefault(u['block'], bytearray(blocks[u['block']]['size']))
                    for m in range(v.count):
                        for c in range(3):  # std140, column_major: a column is a vec4, 16 bytes
                            column = [v.rows[m * 12 + r * 3 + c] for r in range(4)]
                            at = u['offset'] + m * u['array_stride'] + c * u['matrix_stride']
                            buf[at:at + 16] = struct.pack('4f', *column)
                done.add(key)
                continue
            vals = list(v) if isinstance(v, (list, tuple)) else [v]
            is_int = all(isinstance(x, int) and not isinstance(x, bool) for x in vals) and u['type'] in (0x1404, 0x8B53, 0x8B54, 0x8B55)
            data = struct.pack(f'{len(vals)}{"i" if is_int else "f"}', *vals)
            if u['block'] < 0:
                n = len(vals)
                if u['type'] == 0x8B5C:  # mat4
                    g('glUniformMatrix4fv', None, C.c_int, C.c_int, C.c_ubyte, C.c_void_p)(u['location'], 1, 0, data)
                elif u['type'] == 0x8B5B:  # mat3
                    g('glUniformMatrix3fv', None, C.c_int, C.c_int, C.c_ubyte, C.c_void_p)(u['location'], 1, 0, data)
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
                elif u['type'] == 0x8B5B:  # a column-major mat3: 3 columns of 3 floats, matrix_stride apart
                    for col in range(3):
                        at = u['offset'] + col * u['matrix_stride']
                        buf[at:at + 12] = data[col * 12:(col + 1) * 12]
                else:
                    buf[u['offset']:u['offset'] + len(data)] = data
            done.add(key)
        old = self.ubos.pop(prog, [])  # the buffers this program got on the last call are finished with
        if old:
            g('glDeleteBuffers', None, C.c_int, C.POINTER(C.c_uint))(len(old), (C.c_uint * len(old))(*old))
        for index, buf in buffers.items():
            ubo = C.c_uint()
            g('glGenBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(ubo))
            g('glBindBuffer', None, C.c_uint, C.c_uint)(GL_UNIFORM_BUFFER, ubo.value)
            raw = bytes(buf)
            g('glBufferData', None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)(GL_UNIFORM_BUFFER, len(raw), raw, GL_STATIC_DRAW)
            g('glBindBufferBase', None, C.c_uint, C.c_uint, C.c_uint)(GL_UNIFORM_BUFFER, blocks[index]['binding'], ubo.value)
            self.ubos.setdefault(prog, []).append(ubo.value)
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


def _draw_mesh(self, prog, attribs, count):
    """Draw `count` vertices as triangles; attribs maps slot to an (count, n) float32 array. Returns the RGBA32F image."""
    g = self.gl.fn
    g('glBindFramebuffer', None, C.c_uint, C.c_uint)(GL_FRAMEBUFFER, self.fbo)
    g('glViewport', None, C.c_int, C.c_int, C.c_int, C.c_int)(0, 0, self.w, self.h)
    vao = C.c_uint()
    g('glGenVertexArrays', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vao))
    g('glBindVertexArray', None, C.c_uint)(vao.value)
    vbos = []
    for slot, array in attribs.items():
        vbo = C.c_uint()
        g('glGenBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vbo))
        g('glBindBuffer', None, C.c_uint, C.c_uint)(GL_ARRAY_BUFFER, vbo.value)
        data = np.ascontiguousarray(array, dtype='<f4').tobytes()
        g('glBufferData', None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)(GL_ARRAY_BUFFER, len(data), data, GL_STATIC_DRAW)
        g('glEnableVertexAttribArray', None, C.c_uint)(slot)
        g('glVertexAttribPointer', None, C.c_uint, C.c_int, C.c_uint, C.c_ubyte, C.c_int, C.c_void_p)(
            slot, array.shape[1], GL_FLOAT, 0, 4 * array.shape[1], None)
        vbos.append(vbo)
    g('glUseProgram', None, C.c_uint)(prog)
    g('glClearColor', None, C.c_float, C.c_float, C.c_float, C.c_float)(-1.0, -1.0, -1.0, -1.0)
    g('glClear', None, C.c_uint)(0x4000)
    g('glDrawArrays', None, C.c_uint, C.c_int, C.c_int)(GL_TRIANGLES, 0, count)
    g('glFinish', None)()
    out = (C.c_float * (self.w * self.h * 4))()
    g('glReadPixels', None, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)(
        0, 0, self.w, self.h, GL_RGBA, GL_FLOAT, out)
    for vbo in vbos:
        g('glDeleteBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vbo))
    g('glDeleteVertexArrays', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vao))
    return np.frombuffer(out, dtype='<f4').reshape(self.h, self.w, 4).copy()


Target.draw_mesh = _draw_mesh


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


def run_radiance(gl, reference, candidate, size=32, mips=(0, 1, 3, 5), mode='tolerance', optional=('u_width',)):  # u_width is declared in radianceGenF.glsl but never read
    """Draw the radiance filter for several faces, mip levels and source layers with both programs; compare each.
    `optional` names the uniforms the candidate may legitimately not have (the optimiser dropped them); any other name
    that the candidate does not take is a failed case, so a renamed uniform cannot pass as two identical black images."""
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
                    done = gl.set_uniforms(prog, values)
                    if name == 'cand':
                        unset = sorted(set(values) - done - set(optional))
                    gl.fn('glActiveTexture', None, C.c_uint)(GL_TEXTURE0 + units[name].get('reflectionProbes', 0))
                    gl.fn('glBindTexture', None, C.c_uint, C.c_uint)(GL_TEXTURE_CUBE_MAP_ARRAY, cube)
                    images[name] = target.draw_triangle(prog, TRIANGLE)
                r = compare(images['ref'], images['cand'], mode=mode)
                r.update(case=f'face {face} mip {mip} layer {layer}')
                r['nonblack'] = int((images['ref'][..., :3] > 0).sum())
                r['unset_uniforms'] = unset
                if unset:
                    r['pass'] = False
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


# -- the object skin and the shared position module (bump, rigged and static) -----------------------------------------
# The reference is class1/objects/bumpV.glsl with the hand-written class1/avatar/objectSkinV.glsl in the same unit; the
# candidate is the generated bump program. Both fragment programs are replaced by one that writes the eye position
# (vary_position) and the texture coordinates, so the image shows what the vertex stage computed, per pixel.
SKIN_REF_FS = """#version 420
in vec2 vary_texcoord0; in vec2 vary_texcoord1; in vec3 vary_position; out vec4 frag_color;
void main() { frag_color = vec4(vary_position, vary_texcoord0.x + 2.0 * vary_texcoord1.y); }
"""
SKIN_CAND_FS = """#version 460
layout(location = 0) in vec2 vary_texcoord0; layout(location = 1) in vec2 vary_texcoord1; layout(location = 2) in vec3 vary_position;
layout(location = 0) out vec4 frag_color;
void main() { frag_color = vec4(vary_position, vary_texcoord0.x + 2.0 * vary_texcoord1.y); }
"""
ATTRIB_SLOTS = ('position', 'normal', 'texcoord0', 'texcoord1', 'texcoord2', 'texcoord3', 'diffuse_color', 'emissive',
                'tangent', 'weight', 'weight4', 'clothing', 'joint', 'texture_index')


def skin_reference(gl, rigged):
    defines = {'MAX_JOINTS_PER_MESH_OBJECT': '110'}
    if rigged:
        defines['HAS_SKIN'] = '1'
    vs = read_legacy('objects/bumpV.glsl', defines, 'vertex')
    if rigged:
        skin = (common.LEGACY_SHADERS / 'class1/avatar/objectSkinV.glsl').read_text(encoding='utf-8')
        vs += chr(10) + skin
    return gl.program(vs, SKIN_REF_FS, attribs=ATTRIB_SLOTS)


def skin_candidate(gl, generated, program_id):
    manifest = check.load_manifest(generated)
    prog = next((p for p in manifest['programs'] if p['id'] == program_id), None)
    if prog is None:
        return None
    text = (Path(generated) / prog['stages']['vertex']['file']).read_text(encoding='utf-8')
    return gl.program(check.assemble_source('vertex', text, {}), SKIN_CAND_FS)


def _perspective(fov_deg, aspect, near, far):
    f = 1.0 / np.tan(np.radians(fov_deg) / 2.0)
    m = np.zeros((4, 4), dtype='f4')
    m[0, 0], m[1, 1] = f / aspect, f
    m[2, 2], m[2, 3] = (far + near) / (near - far), 2 * far * near / (near - far)
    m[3, 2] = -1.0
    return m


def skin_mesh(rng, joints=6, cells=12):
    """A wavy grid of triangles with four joints per vertex (index in the integer part, weight in the fraction)."""
    xs = np.linspace(-1, 1, cells + 1, dtype='f4')
    pos, uv0, uv1, weights = [], [], [], []
    for i in range(cells):
        for j in range(cells):
            for ci, cj in [(i, j), (i + 1, j), (i, j + 1), (i + 1, j), (i + 1, j + 1), (i, j + 1)]:
                pos.append((xs[ci], xs[cj], 0.3 * np.sin(3 * xs[ci]) * np.cos(2 * xs[cj])))
                uv0.append((ci / cells, cj / cells))
                uv1.append((cj / cells, ci / cells))
                weights.append(rng.integers(0, joints, size=4) + rng.uniform(0.05, 0.95, size=4))
    return np.array(pos, 'f4'), np.array(uv0, 'f4'), np.array(uv1, 'f4'), np.array(weights, 'f4')


def run_skin(gl, rigged, candidate, size=96, frames=4, mode='tolerance'):
    """Draw the rigged (or static) bump program with both the hand-written and the generated programs, over several
    palettes and view angles, and compare the images of the eye position."""
    rng = np.random.default_rng(11)
    reference = skin_reference(gl, rigged)
    target = Target(gl, size, size)
    pos, uv0, uv1, weights = skin_mesh(rng)
    attribs = {0: pos, 2: uv0, 3: uv1}
    if rigged:
        attribs[10] = weights
    results = []
    for frame in range(frames):
        palette = []
        for _ in range(6):
            rot = np.eye(3, dtype='f4') + rng.normal(0, 0.15, (3, 3)).astype('f4')
            trans = rng.normal(0, 0.25, 3).astype('f4')
            palette += list(rot.reshape(-1)) + list(trans)  # 4 rows of 3: the 3x3 part, then the translation row
        palette += [0.0] * (12 * (110 - 6))
        angle = 0.35 * frame - 0.4
        c, s = np.cos(angle), np.sin(angle)
        view = np.array([[c, 0, s, 0.1], [0, 1, 0, -0.05], [-s, 0, c, -3.0], [0, 0, 0, 1]], dtype='f4')
        proj = _perspective(60.0, 1.0, 0.1, 100.0)
        values = {'modelview_matrix': list(view.T.reshape(-1)), 'projection_matrix': list(proj.T.reshape(-1)),
                  'modelview_projection_matrix': list((proj @ view).T.reshape(-1)),
                  'texture_matrix0': list(np.eye(4, dtype='f4').reshape(-1)), 'matrixPalette': Mat3x4Array(palette)}
        images = {}
        unset = []
        for name, prog in (('ref', reference), ('cand', candidate)):
            done = gl.set_uniforms(prog, values)
            images[name] = target.draw_mesh(prog, attribs, len(pos))
            if name == 'cand':
                needed = {'modelview_matrix', 'texture_matrix0'}
                needed |= {'projection_matrix', 'matrixPalette'} if rigged else {'modelview_projection_matrix'}
                unset = sorted(needed - done)
        r = compare(images['ref'], images['cand'], mode=mode)
        r.update(case=f'{"rigged" if rigged else "static"} frame {frame}')
        r['nonblack'] = int((images['ref'][..., 3] >= 0).sum())  # drawn pixels (the clear value is -1)
        r['unset_uniforms'] = unset
        if unset:
            r['pass'] = False
        results.append(r)
    return results


def summarize(results):
    return {
        'cases': len(results),
        'failed': [r['case'] for r in results if not r['pass']],
        'max_ulp': max(r['max_ulp'] for r in results),
        'max_abs': max(r['max_abs'] for r in results),
        'values_differing': sum(r['values_differing'] for r in results),
        'nonblack_min': min(r['nonblack'] for r in results),
        'unset_uniforms': sorted({n for r in results for n in r.get('unset_uniforms', [])}),
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
    # not symmetric and not the identity, so a row-major / column-major mix-up shows
    values = {'modelview_projection_matrix': [0.8, 0.1, 0, 0, 0.2, 0.9, 0, 0, 0, 0, 1, 0, 0.1, -0.15, 0, 1], 'tint': [0.9, 0.8, 0.7, 1.0]}
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


def selftest_manifest(gl):
    """The manifest check against the driver catches a wrong offset, a wrong sampler unit and a wrong input location."""
    import shutil
    import tempfile
    sys.modules.setdefault('ab_harness', sys.modules[__name__])
    import ab_cases
    with tempfile.TemporaryDirectory() as tmp:
        copy = Path(tmp) / 'generated'
        shutil.copytree(common.GENERATED, copy)
        if ab_cases.check_manifest_against_driver(gl, copy):
            return False
        mf = json.loads((copy / 'programs.json').read_text(encoding='utf-8'))
        prog = next(p for p in mf['programs'] if p['id'] == 'twotexturecompare')
        prog['stages']['vertex']['interface']['uniforms'][0]['offset'] += 16
        prog['stages']['fragment']['interface']['textures'][0]['binding'] += 1
        prog['stages']['vertex']['interface']['inputs'][1]['location'] += 5
        (copy / 'programs.json').write_text(json.dumps(mf), encoding='utf-8')
        errors = ab_cases.check_manifest_against_driver(gl, copy)
        wanted = ('GL_UNIFORM_OFFSET', 'sampler', 'vertex input locations')
        print('manifest self-test: the wrong offset, unit and input are reported:', len(errors), 'problem(s)')
        return all(any(w in e for e in errors) for w in wanted)


def selftest(gl):
    """The harness proves itself: the reference against itself passes with zero differing values, and against a
    program with one changed constant it fails, so a real difference cannot slip through."""
    ref = radiance_reference(gl)
    same = summarize(run_radiance(gl, ref, radiance_reference(gl)))
    changed = summarize(run_radiance(gl, ref, radiance_reference(gl, tweak=lambda fs: fs.replace('+ 0.0001;', '+ 0.01;', 1))))
    exact = summarize(run_radiance(gl, ref, radiance_reference(gl), size=8, mips=(2,), mode='exact'))
    base = np.linspace(0.1, 1.0, 64, dtype='<f4').reshape(8, 8)
    bumped = np.nextafter(base, np.float32(2.0))  # every value one ULP up
    one_ulp_fails_exact = (not compare(base, bumped, mode='exact')['pass']) and compare(base, bumped)['pass']
    # a candidate that does not take a uniform the case sets (here probe_strength is hard-coded) fails, never passes
    lacking = summarize(run_radiance(gl, ref, radiance_reference(gl, tweak=lambda fs: fs.replace('frag_color.a *= probe_strength;', 'frag_color.a *= 0.75;')),
                                     size=8, mips=(2,)))
    unset_caught = lacking['unset_uniforms'] == ['probe_strength'] and bool(lacking['failed'])
    block_ok = selftest_block_path(gl) if common.slangc_path(common.read_pin()) else True
    manifest_ok = selftest_manifest(gl)
    ok = (manifest_ok and not same['failed'] and same['values_differing'] == 0 and same['nonblack_min'] > 0
          and changed['failed'] and changed['values_differing'] > 0 and block_ok
          and not exact['failed'] and exact['values_differing'] == 0 and one_ulp_fails_exact and unset_caught)
    print(json.dumps({'identity': same, 'one_constant_changed': changed, 'exact_mode_identity': exact, 'one_ulp_fails_exact_passes_tolerance': one_ulp_fails_exact, 'unset_uniform_caught': unset_caught}, indent=1))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('case', nargs='?', choices=['radiance', 'skin', 'programs', 'manifest'])
    action = ap.add_mutually_exclusive_group()
    action.add_argument('--list', action='store_true')
    action.add_argument('--selftest', action='store_true')
    ap.add_argument('--generated', default=str(common.GENERATED))
    ap.add_argument('--program', default='radianceGen__PROBE_FILTER_SAMPLES32', help='generated program id of the candidate')
    ap.add_argument('--optional', nargs='*', default=[], help='uniform names the candidate may legitimately lack')
    ap.add_argument('--json', help='write the per-case results here')
    subset = ap.add_mutually_exclusive_group()
    subset.add_argument('--only', help='programs: run only the cases whose name contains this text')
    subset.add_argument('--family', help='programs: require A/B coverage of every generated permutation in this family')
    ap.add_argument('--strict-depth', action='store_true', help='programs: enforce exact agreement on advisory depth comparisons')
    ap.add_argument('--expect-renderer', help='fail unless the OpenGL renderer contains this text (case-insensitive)')
    ap.add_argument('--report', help='write GPU identity, selection and results to this JSON file')
    args = ap.parse_args()
    if args.case is not None and (args.list or args.selftest):
        ap.error('--list and --selftest cannot be combined with a command')
    if args.case != 'programs' and (args.only is not None or args.family is not None or args.strict_depth):
        ap.error('--only, --family and --strict-depth require programs')
    if args.only == '' or args.family == '' or args.expect_renderer == '':
        ap.error('--only, --family and --expect-renderer require non-empty text')
    if args.json and args.report and Path(args.json).resolve() == Path(args.report).resolve():
        ap.error('--json and --report must name different files')
    if args.list:
        print('radiance: class1/interface/radianceGen{V,F}.glsl against the generated program (tolerance mode)')
        print('skin: class1/objects/bumpV.glsl with class1/avatar/objectSkinV.glsl (rigged) and without (static) against the '
              'generated bump programs: the eye position per pixel (tolerance mode)')
        print('programs: visual comparisons, with optional --family or --only selection')
        print('manifest: link the generated programs and verify their interfaces on the driver')
        return 0
    gpu = None
    selection = {'command': 'selftest' if args.selftest else args.case, 'generated': str(Path(args.generated).resolve()),
                 'family': args.family, 'only': args.only, 'strict_depth': args.strict_depth,
                 'expected_renderer': args.expect_renderer}
    if args.case == 'radiance':
        selection['program'] = args.program

    def finish(status, results=(), errors=()):
        if args.report:
            manifest = Path(args.generated) / 'programs.json'
            try:
                manifest_sha256 = hashlib.sha256(manifest.read_bytes()).hexdigest()
            except OSError:
                manifest_sha256 = None
            report = {'gpu': gpu, 'selection': selection, 'manifest_sha256': manifest_sha256,
                      'status': status, 'results': results, 'errors': errors}
            Path(args.report).write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
        if args.json and args.case in ('radiance', 'programs'):
            Path(args.json).write_text(json.dumps(results, indent=1), encoding='utf-8')
        return status

    def fail(message, status=1, results=()):
        print(message)
        return finish(status, results, [message])

    try:
        gl = GL()
    except RuntimeError as e:
        return fail(str(e))
    gpu = gl.info
    print(f'GL: {gl.info["renderer"]} | {gl.info["version"]} | GLSL {gl.info["glsl"]}')
    if args.expect_renderer and args.expect_renderer.casefold() not in gpu['renderer'].casefold():
        return fail(f'expected OpenGL renderer containing {args.expect_renderer!r}, got {gpu["renderer"]!r}')
    if args.selftest:
        ok = selftest(gl)
        print('selftest: ' + ('pass' if ok else 'FAIL'))
        return finish(0 if ok else 1)
    if args.case == 'radiance':
        cand = radiance_candidate(gl, args.generated, args.program)
        if cand is None:
            return fail(f'candidate {args.program} is not generated yet; nothing compared', status=2)
        samples = re.search(r'SAMPLES(\d+)$', args.program)  # the 4-sample hero filter is its own program
        reference = radiance_reference(gl, {'PROBE_FILTER_SAMPLES': samples.group(1)} if samples else None)
        results = run_radiance(gl, reference, cand, optional=('u_width', *args.optional))
        summary = summarize(results)
        print(json.dumps(summary, indent=1))
        if summary['nonblack_min'] == 0:
            return fail('FAIL: the reference drew nothing in at least one case', results=results)
        return finish(1 if summary['failed'] else 0, results)
    if args.case == 'manifest':
        sys.modules.setdefault('ab_harness', sys.modules[__name__])
        import ab_cases
        results = []
        try:
            errors = ab_cases.check_manifest_against_driver(gl, args.generated, results=results)
        except (check.CheckError, OSError, ValueError) as e:
            return fail(str(e), results=results)
        for e in errors:
            print('  ' + e)
        print('manifest: ' + ('ok' if not errors else f'{len(errors)} problem(s)'))
        return finish(1 if errors else 0, results, errors)
    if args.case == 'programs':
        sys.modules.setdefault('ab_harness', sys.modules[__name__])  # the cases import this module: one copy of its classes
        import ab_cases
        try:
            status, results, errors = ab_cases.run_programs(gl, args.generated, only=args.only, family=args.family,
                                                          strict_depth=args.strict_depth)
        except (check.CheckError, OSError, ValueError) as e:
            return fail(str(e))
        return finish(status, results, errors)
    if args.case == 'skin':
        results = []
        for rigged, program_id in ((True, 'bump__HAS_SKIN1'), (False, 'bump__HAS_SKIN0')):
            try:
                cand = skin_candidate(gl, args.generated, program_id)
                summary = summarize(run_skin(gl, rigged, cand)) if cand is not None else None
            except (RuntimeError, check.CheckError, OSError, ValueError) as e:
                return fail(f'{program_id}: {e}', results=results)
            if cand is None:
                return fail(f'candidate {program_id} is not generated yet', status=2, results=results)
            print(program_id, json.dumps(summary, indent=1))
            results.append({'program': program_id, 'pass': not summary['failed'] and summary['nonblack_min'] > 0, **summary})
        return finish(int(any(not r['pass'] for r in results)), results)
    ap.print_help()
    return 0


if __name__ == '__main__':
    sys.exit(main())
