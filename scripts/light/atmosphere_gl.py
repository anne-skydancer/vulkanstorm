# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Shared helpers for the L1 light scripts: a hidden OpenGL context (scripts/perf/gl_test_context.py)
and the viewer's own atmosphericsFuncs.glsl / deferredUtil.glsl compiled and evaluated on the GPU.

Importing this module opens the context (Windows). Unknown command line options are left to the
importing script.
"""
import ctypes as C
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / 'indra' / 'newview' / 'app_settings' / 'shaders'

_bootstrap = (ROOT / 'scripts' / 'perf' / 'gl_test_context.py').read_text().replace(
    'parser.parse_args()', 'parser.parse_known_args()[0]')
exec(compile(_bootstrap, 'gl_test_bootstrap', 'exec'), globals())

F = C.c_float
FRAG, VERT = 0x8B30, 0x8B31
TEX2D, RGBA32F, FLOAT = 0x0DE1, 0x8814, 0x1406
gentex = fn('glGenTextures', None, I, C.POINTER(U)); bindtex = fn('glBindTexture', None, U, U)
texparam = fn('glTexParameteri', None, U, U, I); active = fn('glActiveTexture', None, U)
texstorage2 = fn('glTexStorage2D', None, U, I, U, I, I)
texsub2 = fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)
genfbo = fn('glGenFramebuffers', None, I, C.POINTER(U)); bindfbo = fn('glBindFramebuffer', None, U, U)
attach2 = fn('glFramebufferTexture2D', None, U, U, U, U, I)
drawbuffers = fn('glDrawBuffers', None, I, C.POINTER(U))
viewport = fn('glViewport', None, I, I, I, I)
_loc = fn('glGetUniformLocation', I, U, C.c_char_p)
u1i = fn('glUniform1i', None, I, I); u1f = fn('glUniform1f', None, I, F)
u3f = fn('glUniform3f', None, I, F, F, F)
draw = fn('glDrawArrays', None, U, I, I)
readpixels = fn('glReadPixels', None, I, I, I, I, U, U, P)
getteximage = fn('glGetTexImage', None, U, I, U, U, P)

VERTEX = """#version 400 compatibility
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""


def shader_text(rel):
    return (SHADERS / rel).read_text(encoding='utf8')


def uniform(prog, name, *vals):
    l = _loc(prog, name.encode())
    if l < 0:
        return
    if len(vals) == 1:
        (u1i if isinstance(vals[0], int) else u1f)(l, vals[0])
    else:
        u3f(l, *vals)


class Target:
    """An RGBA32F framebuffer of n x 1 pixels with `count` colour attachments."""
    def __init__(self, n, count=1):
        self.n, self.count = n, count
        self.tex = []
        self.fbo = U(); genfbo(1, C.byref(self.fbo)); bindfbo(0x8D40, self.fbo.value)
        for i in range(count):
            t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
            texstorage2(TEX2D, 1, RGBA32F, n, 1)
            texparam(TEX2D, 0x2801, 0x2600); texparam(TEX2D, 0x2800, 0x2600)
            attach2(0x8D40, 0x8CE0 + i, TEX2D, t.value, 0)
            self.tex.append(t.value)
        bufs = (U * count)(*[0x8CE0 + i for i in range(count)]); drawbuffers(count, bufs)

    def bind(self):
        bindfbo(0x8D40, self.fbo.value); viewport(0, 0, self.n, 1)

    def read(self, i):
        bindtex(TEX2D, self.tex[i]); px = (F * (self.n * 4))(); getteximage(TEX2D, 0, 0x1908, FLOAT, px)
        return [tuple(px[k * 4:k * 4 + 4]) for k in range(self.n)]


def linear_texture(rg):
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
    texstorage2(TEX2D, 1, RGBA32F, 1, 1)
    data = (F * 4)(rg[0], rg[1], 0.0, 1.0)
    texsub2(TEX2D, 0, 0, 0, 1, 1, 0x1908, FLOAT, C.cast(data, P))
    texparam(TEX2D, 0x2801, 0x2600); texparam(TEX2D, 0x2800, 0x2600)
    return t.value


# --- EEP sky values, as the viewer sends them (llsettingsvo.cpp applyToUniforms / applySpecial) ---
def _llsd_value(node):
    tag = node.tag
    if tag == 'map':
        kids = list(node); return {kids[i].text: _llsd_value(kids[i + 1]) for i in range(0, len(kids), 2)}
    if tag == 'array':
        return [_llsd_value(k) for k in node]
    if tag == 'real': return float(node.text)
    if tag == 'integer': return int(node.text)
    if tag == 'boolean': return node.text.strip() in ('1', 'true')
    return node.text


def load_legacy_sky(path):
    """Read a legacy Windlight sky file and return the uniforms the shaders get (llsettingssky.cpp
    translateLegacySettings; Windlight skies carry no sun_up/probe fields, so they are classic)."""
    import xml.etree.ElementTree as ET
    d = _llsd_value(ET.parse(path).getroot()[0])
    sun_angle, east = d.get('sun_angle', 0.0), d.get('east_angle', 0.0)
    # convert_azimuth_and_altitude_to_quat: the sun direction's elevation is the sun angle
    # (sin(altitude)), which is all lightnorm.y needs
    return dict(
        sunlight_color=d['sunlight_color'][:3], ambient=d['ambient'][:3], blue_density=d['blue_density'][:3],
        blue_horizon=d['blue_horizon'][:3], haze_density=d['haze_density'][0], haze_horizon=d['haze_horizon'][0],
        cloud_shadow=d['cloud_shadow'][0], density_multiplier=d['density_multiplier'][0],
        distance_multiplier=d['distance_multiplier'][0], max_y=d['max_y'][0], glow=d['glow'][:3],
        gamma=d['gamma'][0], sun_angle=sun_angle, east_angle=east,
        light_y=max(math.sin(sun_angle), -0.1),   # LLEnvironment::getClampedLightNorm
        sun_up=1 if math.sin(sun_angle) >= 0.0 else 0,
        probe_ambiance=0.0)


ATMOS_HEAD = ("#version 400 compatibility\n")


def atmosphere_program(main_body, extra=''):
    """Compile srgbF + atmosphericsFuncs (the viewer's files, unmodified) with `extra` and a main."""
    frag = (ATMOS_HEAD + shader_text('class1/environment/srgbF.glsl') + shader_text('class1/windlight/atmosphericsFuncs.glsl')
            + extra + main_body)
    return program([(VERT, VERTEX), (FRAG, frag)])


def legacy_primary_light(p):
    """The ground light as it was before stage L3: the sun's (or the moon's) colour through today's airmass,
    exp(-(blue_density + haze_density/4) density_multiplier max_y / lightnorm.y) (atmosphericsFuncs.glsl, old lines 98-112)."""
    light = p['sunlight_color'] if p['sun_up'] else p.get('moonlight_color', p['sunlight_color'])
    haze, f = p['haze_density'] * 0.25, 1.0 / max(1e-6, p['light_y'])
    return [c * math.exp(-(bd + haze) * (p['density_multiplier'] * p['max_y']) * f) for c, bd in zip(light, p['blue_density'])]


def set_atmosphere_uniforms(prog, p):
    """Since stage L3 the GLSL's ground light is primary_light_color (the sun's or the moon's colour with its path through the
    atmosphere taken); p['primary_light_color'] sets it, else it is derived from the sun/moon colour with today's airmass."""
    uniform(prog, 'primary_light_color', *p.get('primary_light_color', legacy_primary_light(p)))
    uniform(prog, 'primary_shadow_strength', float(p.get('primary_shadow_strength', 1.0)))
    uniform(prog, 'lightnorm', 0.0, p['light_y'], math.sqrt(max(0.0, 1.0 - p['light_y'] ** 2)))
    uniform(prog, 'sunlight_color', *p['sunlight_color']); uniform(prog, 'moonlight_color', *p.get('moonlight_color', p['sunlight_color']))
    uniform(prog, 'sun_up_factor', int(p['sun_up'])); uniform(prog, 'ambient_color', *p['ambient'])
    uniform(prog, 'blue_horizon', *p['blue_horizon']); uniform(prog, 'blue_density', *p['blue_density'])
    uniform(prog, 'haze_horizon', float(p['haze_horizon'])); uniform(prog, 'haze_density', float(p['haze_density']))
    uniform(prog, 'cloud_shadow', float(p['cloud_shadow'])); uniform(prog, 'density_multiplier', float(p['density_multiplier']))
    uniform(prog, 'distance_multiplier', float(p['distance_multiplier'])); uniform(prog, 'max_y', float(p['max_y']))
    uniform(prog, 'glow', *p['glow']); uniform(prog, 'scene_light_strength', 1.0)
    uniform(prog, 'sun_moon_glow_factor', float(p.get('sun_moon_glow_factor', 1.0)))
    uniform(prog, 'sky_sunlight_scale', float(p['sky_sunlight_scale'])); uniform(prog, 'sky_ambient_scale', float(p['sky_ambient_scale']))
    uniform(prog, 'classic_mode', int(p['classic_mode']))
