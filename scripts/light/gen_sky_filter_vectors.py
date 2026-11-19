# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage AT2-1: run the viewer's class1/deferred/skyV.glsl (today's sky, unmodified) and class1/windlight/skyFilterF.glsl (the EEP
filter's tables and the cast-light level) on the GPU for library and synthetic skies, and write
indra/newview/tests/llskyfilter_vectors.inc, the vectors that indra/newview/tests/llskyfilter_test.cpp holds the C++ twin (llskyfilter.h)
to. CI runs only the C++ test; this script is what ties it to the GLSL, and the .inc holds the SHA-256 of the three GLSL files.

    python scripts/light/gen_sky_filter_vectors.py            # rewrite the .inc file
    python scripts/light/gen_sky_filter_vectors.py --check    # fail if the GLSL no longer produces it

Needs Windows and a GPU (float; --check allows 1e-4 relative + 1e-6 absolute between two runs).

  SF_SKY(index, the inputs of LLSkyFilter::Inputs in their order)
  SF_HAZE(sky, direction xyz, skyV's vary_HazeColor rgb)               skyV.glsl itself: rel_pos along the direction, its own glow
  SF_AB(sky, j, A rgb, B rgb)                                            skyFilterF.glsl pass 1 at row j (1 x 108), RGBA32F
  SF_K(sky, black, base irradiance, filtered irradiance, k, defined)    passes 2 and 3 on a synthetic physical sky (sf_p in the test),
                                                                         or (black 1) a black one: a night with the moon down

The sizes are the viewer's defines (LLAtmosphereLUTs: SF_SKY_W, SF_SKY_H).
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403
from atmosphere_gl import _loc  # noqa: E402

OUT = ROOT / 'indra' / 'newview' / 'tests' / 'llskyfilter_vectors.inc'
SKIES = ROOT / 'indra' / 'newview' / 'app_settings' / 'windlight' / 'skies'
W, H = 192, 108
SIZES = '#define SF_SKY_W %d\n#define SF_SKY_H %d\n' % (W, H)

u4f = fn('glUniform4f', None, I, F, F, F, F)
attrib_loc = fn('glGetAttribLocation', I, U, C.c_char_p)
vattrib3f = fn('glVertexAttrib3f', None, U, F, F, F)
mat4 = fn('glUniformMatrix4fv', None, I, I, U, C.POINTER(F))
texstorage = fn('glTexStorage2D', None, U, I, U, I, I)
query_gen = fn('glGenQueries', None, I, C.POINTER(U))

LIBRARY = ['Default.xml', 'Foggy.xml', 'Sunset.xml', '%5BTOR%5D%20DUSK%20%2D%20Fairytale%20glow.xml',
           '%5BTOR%5D%20FOGGY%20%2D%20Sad%20purple.xml', '%5BTOR%5D%20MIDDAY%20%2D%20Anime%20Ciel.xml']

FIELDS = ['lightnorm', 'sunlight_color', 'moonlight_color', 'ambient_color', 'blue_horizon', 'blue_density', 'haze_horizon',
          'haze_density', 'cloud_shadow', 'density_multiplier', 'max_y', 'glow', 'sun_moon_blend', 'sun_glow_norm', 'sky_hdr_scale',
          'classic_mode']


def sky_dir(el_deg, az_deg):
    e, a = math.radians(el_deg), math.radians(az_deg)
    return [math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)]


def clamped_norm(s):
    """LLEnvironment::getClampedSunNorm: z clamped at -0.1 in the sky's frame (not renormalised), then toLightNorm (y, z, x)"""
    s = [s[0], s[1], max(s[2], -0.1)]
    return [s[1], s[2], s[0]]


def skies():
    out = []
    for name in LIBRARY:
        p = load_legacy_sky(SKIES / name)
        el = math.degrees(math.asin(max(-1.0, min(1.0, math.sin(p['sun_angle'])))))
        sun = clamped_norm(sky_dir(el, math.degrees(p['east_angle'])))
        out.append(dict(name=name, lightnorm=sun, sunlight_color=p['sunlight_color'], moonlight_color=p['sunlight_color'],
                        ambient_color=p['ambient'], blue_horizon=p['blue_horizon'], blue_density=p['blue_density'],
                        haze_horizon=p['haze_horizon'], haze_density=p['haze_density'], cloud_shadow=p['cloud_shadow'],
                        density_multiplier=p['density_multiplier'], max_y=p['max_y'], glow=p['glow'], sun_moon_blend=0.0,
                        sun_glow_norm=sun, sky_hdr_scale=1.0, classic_mode=1))
    d = out[0]
    # a PBR path (sky_hdr_scale 2 sqrt(gamma), not classic), a sun in the L3 band with the moon's light mixed in, a night with the sun below
    # the clamp (not renormalised), a backward glow lobe and clouds, a collapsed dome (max_y 0: black)
    out.append(dict(d, name='synthetic PBR', sky_hdr_scale=2.0 * math.sqrt(0.8), classic_mode=0, cloud_shadow=0.5))
    band = clamped_norm(sky_dir(0.1, 30.0))
    out.append(dict(d, name='synthetic band', lightnorm=band, sun_glow_norm=band, sun_moon_blend=0.4, moonlight_color=[0.2, 0.25, 0.4]))
    night = clamped_norm(sky_dir(-40.0, 200.0))
    out.append(dict(d, name='synthetic night', lightnorm=clamped_norm(sky_dir(35.0, 20.0)), sun_glow_norm=night, sun_moon_blend=1.0,
                    moonlight_color=[0.1, 0.12, 0.2]))
    out.append(dict(d, name='synthetic backward lobe', glow=[2.0, 0.0, 0.5], cloud_shadow=0.9, haze_density=3.0))
    out.append(dict(d, name='synthetic max_y 0', max_y=0.0))
    return out


def sample_dirs(sky):
    """dome-frame directions (y up): around the sky, at the sun, below the horizon"""
    dirs = []
    for el, az in ((90, 0), (60, 45), (30, 120), (10, 200), (2, 300), (0.5, 10), (-5, 90), (-40, 250)):
        s = sky_dir(el, az)
        dirs.append([s[1], s[2], s[0]])
    sun = sky['sun_glow_norm']
    n = math.sqrt(sum(c * c for c in sun))
    if n > 0:
        u = [c / n for c in sun]
        dirs.append(u)
        dirs.append([u[0] + 0.02, u[1] + 0.01, u[2]])
    return dirs


def set_inputs(prog, s):
    for k in ('lightnorm', 'sunlight_color', 'moonlight_color', 'ambient_color', 'blue_horizon', 'blue_density', 'glow', 'sun_glow_norm'):
        uniform(prog, k, *[float(x) for x in s[k]])
    for k in ('haze_horizon', 'haze_density', 'cloud_shadow', 'density_multiplier', 'max_y', 'sun_moon_blend', 'sky_hdr_scale'):
        uniform(prog, k, float(s[k]))
    uniform(prog, 'classic_mode', int(s['classic_mode']))


SKYV_FRAG = """
in vec3 vary_HazeColor;
layout(location = 0) out vec4 out_haze;
void main() { out_haze = vec4(vary_HazeColor, 1.0); }
"""


def sf_p(i, j):
    """the synthetic physical sky of the cast-light vectors; llskyfilter_test.cpp has the same formula"""
    u, v = i / (W - 1.0), j / (H - 1.0)
    return [0.02 + 0.01 * u, 0.03 + 0.02 * v, 0.05 + 0.01 * u * v]


class Grid:
    """a W x H (or w x h) RGBA32F framebuffer with `count` attachments"""
    def __init__(self, w, h, count=1):
        self.w, self.h = w, h
        self.fbo = U(); genfbo(1, C.byref(self.fbo)); bindfbo(0x8D40, self.fbo.value)
        self.tex = []
        for k in range(count):
            t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
            texstorage(TEX2D, 1, RGBA32F, w, h)
            texparam(TEX2D, 0x2801, 0x2600); texparam(TEX2D, 0x2800, 0x2600)
            texparam(TEX2D, 0x2802, 0x812F); texparam(TEX2D, 0x2803, 0x812F)
            attach2(0x8D40, 0x8CE0 + k, TEX2D, t.value, 0)
            self.tex.append(t.value)
        bufs = (U * count)(*[0x8CE0 + k for k in range(count)]); drawbuffers(count, bufs)

    def bind(self):
        bindfbo(0x8D40, self.fbo.value); viewport(0, 0, self.w, self.h)

    def read(self, k):
        bindtex(TEX2D, self.tex[k]); px = (F * (self.w * self.h * 4))(); getteximage(TEX2D, 0, 0x1908, FLOAT, px)
        return px


def evaluate():
    srgb = ATMOS_HEAD + shader_text('class1/environment/srgbF.glsl')
    skyv = program([(VERT, ATMOS_HEAD + shader_text('class1/deferred/skyV.glsl')), (FRAG, ATMOS_HEAD + SKYV_FRAG)])
    passes = {n: program([(VERT, VERTEX), (FRAG, srgb), (FRAG, ATMOS_HEAD + '#define SF_PASS %d\n' % n + SIZES + shader_text('class1/windlight/skyFilterF.glsl'))])
              for n in (1, 2, 3)}
    point = Target(1, 1)
    ab = Grid(1, H, 2)
    rows = Grid(1, H // 2)
    kt = Grid(1, 1)

    def physical(fill):
        t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
        texstorage(TEX2D, 1, RGBA32F, W, H)
        data = (F * (W * H * 4))()
        for j in range(H):
            for i in range(W):
                c = fill(i, j)
                data[(j * W + i) * 4:(j * W + i) * 4 + 4] = [c[0], c[1], c[2], 1.0]
        texsub2(TEX2D, 0, 0, 0, W, H, 0x1908, FLOAT, C.cast(data, P))
        texparam(TEX2D, 0x2801, 0x2600); texparam(TEX2D, 0x2800, 0x2600)
        return t.value
    # the synthetic physical sky, and a black one (the clear-day base at night with the moon down)
    p_skies = [(0, physical(sf_p)), (1, physical(lambda i, j: [0.0, 0.0, 0.0]))]

    loc = attrib_loc(skyv, b'position')
    ident = (F * 16)(*([0.0] * 15 + [1.0]))   # every vertex at the centre of the 1 x 1 target
    sk = skies()
    haze, abv, kv = [], [], []
    for si, s in enumerate(sk):
        # skyV.glsl itself, one point per direction: rel_pos = position - camPosLocal + (0, 50, 0) along the direction
        point.bind(); use(skyv)
        set_inputs(skyv, s)
        uniform(skyv, 'camPosLocal', 0.0, 0.0, 0.0)
        mat4(_loc(skyv, b'modelview_projection_matrix'), 1, 0, ident)
        for d in sample_dirs(s):
            vattrib3f(loc, 1000.0 * d[0], 1000.0 * d[1] - 50.0, 1000.0 * d[2])
            draw(0x0000, 0, 1)   # GL_POINTS
            haze.append((si, d, point.read(0)[0][:3]))
        # pass 1: A and B, one row per elevation
        ab.bind(); use(passes[1]); set_inputs(passes[1], s)
        draw(0x0004, 0, 3)
        A, B = ab.read(0), ab.read(1)
        for j in (0, 17, 30, 53, 54, 55, 58, 60, 70, 80, 90, 100, 107):
            o = j * 4
            abv.append((si, j, list(A[o:o + 3]), list(B[o:o + 3])))
        # passes 2 and 3 on the synthetic physical sky and on a black one
        for black, p_tex in p_skies:
            rows.bind(); use(passes[2]); set_inputs(passes[2], s)
            for unit, (name, tex) in enumerate((('sf_sky_view', p_tex), ('sf_table_a', ab.tex[0]), ('sf_table_b', ab.tex[1]))):
                active(0x84C0 + unit); bindtex(TEX2D, tex); uniform(passes[2], name, unit)
            draw(0x0004, 0, 3)
            kt.bind(); use(passes[3]); set_inputs(passes[3], s)
            active(0x84C0 + 3); bindtex(TEX2D, rows.tex[0]); uniform(passes[3], 'sf_rows', 3)
            draw(0x0004, 0, 3)
            active(0x84C0)
            kv.append((si, black, list(kt.read(0)[0:4])))
    return sk, haze, abv, kv


def sha_lines():
    import hashlib
    out = []
    for rel in ('class1/deferred/skyV.glsl', 'class1/windlight/skyFilterF.glsl', 'class1/environment/srgbF.glsl'):
        data = shader_text(rel).replace('\r\n', '\n').replace('\r', '\n').encode('utf8')
        out.append('SF_SHA("%s", "%s")' % (rel, hashlib.sha256(data).hexdigest()))
    return out


def fmt(x):
    x = float(x)
    if math.isnan(x):
        return 'NAN'
    if math.isinf(x):
        return 'INFINITY' if x > 0 else '-INFINITY'
    return repr(x)


def render(sk, haze, abv, kv):
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)',
             '// Generated by scripts/light/gen_sky_filter_vectors.py from skyV.glsl and skyFilterF.glsl on the GPU; do not edit.',
             '// SF_SKY(index, ' + ', '.join(FIELDS) + ')',
             '// SF_HAZE(sky, direction xyz, vary_HazeColor rgb)   SF_AB(sky, row j, A rgb, B rgb)   SF_K(sky, black P, irr_p, irr_w, k, defined)']
    lines += sha_lines()
    for si, s in enumerate(sk):
        v = []
        for f in FIELDS:
            x = s[f]
            v += list(x) if isinstance(x, list) else [x]
        lines.append('SF_SKY(%d, %s)  // %s' % (si, ', '.join(fmt(x) for x in v), s['name']))
    for si, d, c in haze:
        lines.append('SF_HAZE(%d, %s)' % (si, ', '.join(fmt(x) for x in list(d) + list(c))))
    for si, j, a, b in abv:
        lines.append('SF_AB(%d, %d, %s)' % (si, j, ', '.join(fmt(x) for x in a + b)))
    for si, black, k in kv:
        lines.append('SF_K(%d, %d, %s)' % (si, black, ', '.join(fmt(x) for x in k)))
    return '\n'.join(lines) + '\n'


def numbers(text):
    out = []
    for line in text.splitlines():
        if line.startswith('SF_') and not line.startswith('SF_SHA'):
            body = line[line.index('(') + 1:line.index(')')]
            out += [float(x) for x in body.split(',')]
    return out


sk, haze, abv, kv = evaluate()
text = render(sk, haze, abv, kv)
if '--check' in sys.argv:
    old_text = OUT.read_text()
    if [l for l in old_text.splitlines() if l.startswith('SF_SHA')] != sha_lines():
        print('the shaders changed since the vectors were made')
        sys.exit(1)
    old, new = numbers(old_text), numbers(text)
    bad = [(i, a, b) for i, (a, b) in enumerate(zip(old, new)) if abs(a - b) > 1e-4 * max(abs(a), abs(b)) + 1e-6]
    print('numbers:', len(new), 'mismatches:', len(bad), bad[:5])
    sys.exit(1 if bad or len(old) != len(new) else 0)
with open(OUT, 'w', encoding='utf8', newline='\n') as f:
    f.write(text)
print('wrote', OUT, len(sk), 'skies,', len(haze), 'skyV vectors,', len(abv), 'A/B rows,', len(kv), 'cast-light levels')
