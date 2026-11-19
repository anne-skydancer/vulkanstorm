# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The `Atmosphere` uniform block (stage AT2-1) for the GPU harnesses: atmosphereCommonF.glsl reads everything from it, as the viewer
fills it (LLAtmosphericLight::fill_atmosphere_block, llatmospherebase.h).

    pack(a, camera_km=0.0) -> bytes   the 512-byte std140 block for an atmosphere dict (the keys of gen_atmosphere_scatter_vectors.py's
                                      atmospheres(); gen_transmittance_vectors.py's carry no scattering, which is then 0)
    cut_altitudes(a) -> [km]          LLAtmosphere::cut_altitudes_km, ported (the GLSL takes the C++'s cuts; it no longer searches them)
    Block(prog)                       a uniform buffer bound at the block's binding for `prog`; .upload(bytes)
    verify_layout(prog) -> [(name, offset)]   the driver's std140 offsets, asserted equal to LAYOUT (the C++ mirror's offsetof)

A profile is a list of one or two layers (width m, exp_term, exp_scale 1/m, linear 1/m, constant), as the EEP settings hold them.
Needs atmosphere_gl's context (import it first).
"""
import ctypes as C
import math
import struct

from atmosphere_gl import fn, I, U, P  # noqa: F401  (the context is open once atmosphere_gl is imported)

BINDING = 4            # LLGLSLShader::UB_ATMOSPHERE
SIZE = 512             # sizeof(AtmosphereBlock)
MAX_ALT = 32           # detail::MAX_ALTITUDES

# offsetof(AtmosphereBlock, ...) in llatmospherebase.h, by the GLSL member name (std140)
LAYOUT = {
    'pa_atm.rayleigh_ext': 0, 'pa_atm.aerosol_ext': 12, 'pa_atm.ozone_ext': 16, 'pa_atm.slab_ext': 28, 'pa_atm.excess': 32,
    'pa_atm.slab_top_km': 44, 'pa_atm.planet_radius_km': 48, 'pa_atm.top_radius_km': 52, 'pa_atm.cut_count': 56,
    'pa_atm.rayleigh.layer0': 64, 'pa_atm.rayleigh.layer1': 80, 'pa_atm.rayleigh.constants': 96,
    'pa_atm.mie.layer0': 112, 'pa_atm.absorption.layer0': 160, 'pa_atm.absorption.constants': 192,
    'pa_lights.dir[0]': 208, 'pa_lights.dir[1]': 224, 'pa_lights.E[0]': 240, 'pa_lights.E[1]': 256, 'pa_lights.disc[0]': 272,
    'pa_lights.disc[1]': 288, 'pa_lights.count': 304, 'pa_rayleigh_scat': 320, 'pa_aerosol_scat': 332, 'pa_ms_scale': 336,
    'pa_slab_scat': 348, 'pa_glow_phase': 352, 'pa_aniso': 364, 'pa_max_phase': 368, 'pa_f_ms_max': 372, 'pa_ground_albedo': 376,
    'pa_camera_km': 380, 'pa_cuts[0]': 384,
}

gen_buffers = fn('glGenBuffers', None, I, C.POINTER(U))
bind_buffer = fn('glBindBuffer', None, U, U)
buffer_data = fn('glBufferData', None, U, C.c_ssize_t, P, U)
buffer_sub = fn('glBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)
bind_buffer_base = fn('glBindBufferBase', None, U, U, U)
block_index = fn('glGetUniformBlockIndex', U, U, C.c_char_p)
block_binding = fn('glUniformBlockBinding', None, U, U, U)
uniform_indices = fn('glGetUniformIndices', None, U, I, C.POINTER(C.c_char_p), C.POINTER(U))
active_uniforms_iv = fn('glGetActiveUniformsiv', None, U, I, C.POINTER(U), U, C.POINTER(I))
UNIFORM_BUFFER = 0x8A11


# --- LLAtmosphere::cut_altitudes_km (llatmosphericlight.h), ported to double precision Python ---
def _layer_value(l, h):
    _, e, s, lin, c = l
    ev = e * math.exp(min(max(s * h, -80.0), 80.0)) if e != 0.0 else 0.0
    return ev + lin * h + c


def _layer_crossings(l, lo, hi):
    _, e, s, lin, _ = l
    br = [lo]
    if s != 0.0 and lin != 0.0:
        q = -lin / (e * s)
        if q > 0.0 and math.isfinite(q):
            x = math.log(q) / s
            if lo < x < hi:
                br.append(x)
    br.append(hi)
    out = []
    for j in range(len(br) - 1):
        for level in (0.0, 1.0):
            a, b = br[j], br[j + 1]
            ga, gb = _layer_value(l, a) - level, _layer_value(l, b) - level
            if not ((ga < 0.0 < gb) or (ga > 0.0 > gb)):
                continue
            for _ in range(32):
                m = 0.5 * (a + b)
                gm = _layer_value(l, m) - level
                if (gm < 0.0) == (ga < 0.0):
                    a, ga = m, gm
                else:
                    b = m
            if len(out) < 4:
                out.append(0.5 * (a + b))
    return out


def _kinks_m(p, top_m):
    out = []

    def add(h):
        if not (h > 0.0) or not math.isfinite(h) or len(out) >= 16:
            return
        if any(abs(o - h) <= 1e-9 * h for o in out):
            return
        out.append(h)
    layers = 2 if len(p) > 1 else 1
    if layers == 2:
        add(p[0][0])
    for i in range(layers):
        l = p[i]
        lo = 0.0 if i == 0 else p[0][0]
        hi = p[0][0] if (layers == 2 and i == 0) else top_m
        if l[1] == 0.0:
            if l[3] == 0.0:
                continue
            for level in (0.0, 1.0):
                h = (level - l[4]) / l[3]
                if lo < h < hi:
                    add(h)
        elif not (l[3] == 0.0 and l[4] == 0.0 and l[2] <= 0.0 and 0.0 < l[1] <= 1.0):
            for c in _layer_crossings(l, lo, hi):
                add(c)
    return out


def _profile_cuts_km(p, top_km):
    out = [k * 1e-3 for k in _kinks_m(p, top_km * 1e3) if 0.0 < k * 1e-3 < top_km][:MAX_ALT]
    for l in p[:2]:
        if l[1] != 0.0 and l[2] < 0.0:
            for sc in (4.0, 10.0, 20.0):
                h = sc / -l[2] * 1e-3
                if 0.0 < h < top_km and len(out) < MAX_ALT:
                    out.append(h)
    return out


def profiles(a):
    """The atmosphere's three profiles: from a['profiles'], else the stock exponentials with a['rayleigh_scale'] / a['mie_scale'] and
    Bruneton's ozone (the scatter generator's form)."""
    if 'profiles' in a:
        return a['profiles']
    ozone = [(25000.0, 0.0, 0.0, 1.0 / 15000.0, -2.0 / 3.0), (0.0, 0.0, 0.0, -1.0 / 15000.0, 8.0 / 3.0)]
    return [[(0.0, 1.0, a['rayleigh_scale'], 0.0, 0.0)], [(0.0, 1.0, a['mie_scale'], 0.0, 0.0)], ozone]


def cut_altitudes(a):
    top = a['top_radius_km'] - a['planet_radius_km']
    out = [0.0]
    if a['slab_ext'] > 0.0 and 0.0 < a['slab_top_km'] < top:
        out += [0.75 * a['slab_top_km'], a['slab_top_km']]
    for p in profiles(a):
        for h in _profile_cuts_km(p, top):
            if len(out) < MAX_ALT:
                out.append(h)
    return out


# --- the std140 block ---
def _profile(p):
    a = p[0]
    b = p[1] if len(p) > 1 else p[0]
    return struct.pack('<12f', a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], a[4], b[4], 2.0 if len(p) > 1 else 1.0, 0.0)


def pack(a, camera_km=0.0):
    cuts = cut_altitudes(a)
    prof = profiles(a)
    lt = a.get('lights', dict(dir=[[0, 0, 1], [0, 0, -1]], E=[[0, 0, 0], [0, 0, 0]], disc=[0.0046, 0.0046], count=2))
    out = struct.pack('<3ff3ff3ff4f', *a['rayleigh_ext'], a['aerosol_ext'], *a['ozone_ext'], a['slab_ext'], *a['excess'], a['slab_top_km'],
                      a['planet_radius_km'], a['top_radius_km'], float(len(cuts)), 0.0)
    out += _profile(prof[0]) + _profile(prof[1]) + _profile(prof[2])
    for i in range(2):
        out += struct.pack('<4f', *lt['dir'][i], 0.0)
    for i in range(2):
        out += struct.pack('<4f', *lt['E'][i], 0.0)
    for i in range(2):
        out += struct.pack('<4f', lt['disc'][i], 0.0, 0.0, 0.0)
    out += struct.pack('<4i', int(lt['count']), 0, 0, 0)
    glow = a.get('glow', [0.0, 0.0, 0.0])
    out += struct.pack('<3ff3ff3ff4f', *a.get('rayleigh_scat', [0.0] * 3), a.get('aerosol_scat', 0.0), *a.get('ms_scale', [1.0] * 3),
                       a.get('slab_scat', 0.0), *glow, a.get('aniso', 0.8), a.get('max_phase', 1e4), a.get('f_ms_max', 0.6),
                       a.get('ground_albedo', 0.2), camera_km)
    out += struct.pack('<32f', *(cuts + [0.0] * (MAX_ALT - len(cuts))))
    assert len(out) == SIZE, len(out)
    return out


class Block:
    """A uniform buffer at the block's binding; programs that declare the block are pointed at it by attach(prog)."""
    def __init__(self):
        b = U()
        gen_buffers(1, C.byref(b))
        self.buf = b.value
        bind_buffer(UNIFORM_BUFFER, self.buf)
        buffer_data(UNIFORM_BUFFER, SIZE, None, 0x88E8)  # GL_DYNAMIC_DRAW
        bind_buffer(UNIFORM_BUFFER, 0)

    @staticmethod
    def attach(prog):
        idx = block_index(prog, b'Atmosphere')
        if idx != 0xFFFFFFFF:
            block_binding(prog, idx, BINDING)

    def upload(self, data):
        buf = C.create_string_buffer(data, len(data))
        bind_buffer(UNIFORM_BUFFER, self.buf)
        buffer_sub(UNIFORM_BUFFER, 0, len(data), C.cast(buf, P))
        bind_buffer(UNIFORM_BUFFER, 0)
        bind_buffer_base(UNIFORM_BUFFER, BINDING, self.buf)


def verify_layout(prog):
    """The driver's offsets of every LAYOUT member in `prog` (which must use all of them), asserted equal to the C++ mirror's."""
    names = list(LAYOUT)
    arr = (C.c_char_p * len(names))(*[n.encode() for n in names])
    idx = (U * len(names))()
    uniform_indices(prog, len(names), arr, idx)
    found = [(n, i) for n, i in zip(names, idx) if i != 0xFFFFFFFF]
    assert found, 'the program does not use the block'
    ids = (U * len(found))(*[i for _, i in found])
    offs = (I * len(found))()
    active_uniforms_iv(prog, len(found), ids, 0x8A3B, offs)  # GL_UNIFORM_OFFSET
    out = []
    for (n, _), o in zip(found, offs):
        assert o == LAYOUT[n], 'std140 offset of %s: driver %d, C++ mirror %d' % (n, o, LAYOUT[n])
        out.append((n, o))
    return out
