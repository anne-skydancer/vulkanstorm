# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Evaluate the viewer's class1/windlight/atmosphereCommonF.glsl (the scattering core of the physical atmosphere, stage AT2-0 of the
item-4 plan; physicalAtmosphereF.glsl before it) on the GPU for fixed pseudo-random atmospheres and write
indra/newview/tests/llatmosphere_scatter_vectors.inc, the vectors that llatmosphericscatter_test.cpp holds
LLScatter (llatmosphericlight.h) to. CI runs only the C++ test; this script is what ties the vectors to the GLSL; the .inc also holds the SHA-256 of both GLSL files, which the test checks.

    python scripts/light/gen_atmosphere_scatter_vectors.py            # rewrite the .inc file
    python scripts/light/gen_atmosphere_scatter_vectors.py --check    # fail if the GLSL no longer produces it

Needs Windows and a GPU. The GLSL is float, so --check allows 1e-3 relative + 1e-6 absolute between two runs on the same GPU (the test then holds the C++ to these numbers
at 1e-4 to 1e-3, see llatmosphericscatter_test.cpp).

The light transmittance is the exact pa_transmittance here (PA_LIGHT_T_EXACT) and the multiple scattering a smooth stand-in
(PA_MS_OVERRIDE): the vectors hold the arithmetic of the march and the reference integrals, not a table lookup. AT2-1 holds the passes
that fill the tables to the C++ texels.
"""
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403
from atmosphere_gl import _loc  # noqa: E402

OUT = ROOT / 'indra' / 'newview' / 'tests' / 'llatmosphere_scatter_vectors.inc'
N_ATM = 14

u4f = fn('glUniform4f', None, I, F, F, F, F)

# Bruneton's ozone (two linear layers): layer = (width m, exp_term, exp_scale 1/m, linear 1/m, constant)
OZONE = [(25000.0, 0.0, 0.0, 1.0 / 15000.0, -2.0 / 3.0), (0.0, 0.0, 0.0, -1.0 / 15000.0, 8.0 / 3.0)]

# the stand-in for the multiple-scattering table; llatmosphericscatter_test.cpp has the same formula
MS_STUB = """
vec3 pa_ms_at(float h_km, float mu_s)
{
    float x = h_km / (pa_atm.top_radius_km - pa_atm.planet_radius_km);
    return vec3(0.02, 0.03, 0.05) * (0.5 + 0.5 * mu_s) * (1.0 - 0.5 * clamp(x, 0.0, 1.0));
}
"""

# one program per kind: a shader compiler inlines every call of pa_transmittance (its cut search holds arrays of 100 floats), and
# one program with all of them runs out of resources
MAIN_HEAD = """
uniform vec4 t_in0;
uniform vec4 t_in1;
layout(location = 0) out vec4 out_a;
layout(location = 1) out vec4 out_b;
void main() {
    out_a = vec4(0.0);
    out_b = vec4(0.0);
"""
MAINS = {
    0: """    vec2 uv = pa_trans_uv(t_in0.x, t_in0.y);          // the parameterisation, both ways
    float h; float mu;
    pa_trans_rmu(t_in0.zw, h, mu);
    out_a = vec4(uv, h, mu);""",
    1: """    out_a = vec4(pa_trans_texel(ivec2(t_in0.xy), ivec2(t_in0.zw)), 1.0);   // a table texel""",
    2: """    vec3 lin; vec3 t;                                  // the aerial perspective
    atmosphereAP(normalize(t_in0.xyz), t_in0.w, t_in1.x, int(t_in1.y), lin, t);
    out_a = vec4(lin, 0.0); out_b = vec4(t, 0.0);""",
    3: """    float f;                                           // the multiple-scattering texel
    vec3 psi = pa_ms_texel(t_in0.x, t_in0.y, int(t_in0.z), int(t_in0.w), f);
    out_a = vec4(psi, f);""",
    4: """    out_a = vec4(pa_sky_irradiance_texel(t_in0.x, t_in0.y, int(t_in0.z), int(t_in0.w)), 0.0);   // the sky irradiance texel""",
    5: """    out_a = vec4(pa_sky_radiance(normalize(t_in0.xyz), t_in0.w, int(t_in1.x)), 0.0);   // the sky""",
    6: """    out_a = vec4(pa_phase_r(t_in0.x), pa_phase_m(t_in0.x), 0.0, 0.0);   // phase functions""",
    7: """    vec3 s_r; float s_m; vec3 ext;                     // the medium
    pa_medium(t_in0.x, s_r, s_m, ext);
    out_a = vec4(s_r, s_m); out_b = vec4(ext, 0.0);""",
    8: """    out_a = vec4(pa_light_T(t_in0.xyz, t_in1.x, pa_lights, int(t_in0.w)), 0.0);   // the light transmittance""",
}


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def dir_of(el_deg, az_deg):
    e, a = math.radians(el_deg), math.radians(az_deg)
    return [math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)]


def atmospheres():
    rnd = random.Random(20261111)
    u = rnd.random
    out = []
    for i in range(N_ATM):
        rg = 6360.0 if i % 4 else float(round(3000 + u() * 4000))
        height = 60.0 if i % 4 else float(round(30 + u() * 60))
        lobe = i % 3 != 2
        sun_el = round(-10.0 + u() * 90.0, 3)
        sun = dir_of(sun_el, round(u() * 360.0, 3))
        moon = [-c for c in sun] if i % 2 else dir_of(round(5.0 + u() * 60.0, 3), round(u() * 360.0, 3))
        e_sun = [round(0.2 + u() * 2.8, 6) for _ in range(3)]
        gain = round(0.25 * u(), 6)
        a = dict(
            rayleigh_ext=[round(0.0219 * (0.3 + 2.7 * u()) * k, 6) for k in (1.0, 1.4, 2.2)],
            aerosol_ext=round(u() * 0.4, 6) if i % 5 else 0.0,
            slab_ext=round(u() * 1.2, 6) if i % 3 else 0.0,
            slab_top_km=round(0.05 + u() * 0.5, 4),
            ozone_ext=[round(x * (0.2 + u() * 2.0), 6) for x in (0.00065, 0.001881, 0.000085)],
            rayleigh_scale=-1.0 / round(6000 + u() * 4000), mie_scale=-1.0 / round(800 + u() * 1200),
            planet_radius_km=rg, top_radius_km=rg + height,
            excess=[round(u() * 4.0, 6), 0.0, round(u() * 2.0, 6)] if i % 4 == 1 else [0.0, 0.0, 0.0])
        a['rayleigh_scat'] = [round(x * (0.3 + 0.7 * u()), 6) for x in a['rayleigh_ext']]
        a['aerosol_scat'] = round(a['aerosol_ext'] * (0.3 + 0.65 * u()), 6)
        a['slab_scat'] = round(a['slab_ext'] * (0.3 + 0.65 * u()), 6)
        a['ms_scale'] = [round(0.5 + 2.5 * u(), 4) for _ in range(3)]
        gx, gz = round(0.2 + 7.8 * u(), 4), round(-2.5 + 2.8 * u(), 4)
        norm = 0.0
        if lobe:
            # the integral over the sphere of (max(1 - c, 0.001) gx)^gz + 0.25, closed form as LLScatter::glow_integral
            x0 = 0.001
            integ = x0 * (x0 * gx) ** gz
            integ += (math.log(2.0 / x0) / gx) if abs(gz + 1.0) < 1e-9 else gx ** gz * (2.0 ** (gz + 1.0) - x0 ** (gz + 1.0)) / (gz + 1.0)
            norm = 1.0 / (2.0 * math.pi * (integ + 0.5))
        a['glow'] = [gx if lobe else 0.0, gz if lobe else 0.0, norm]
        a['aniso'] = round(0.2 + 0.7 * u(), 4)
        a['lights'] = dict(dir=[sun, moon], E=[e_sun, [round(c * gain, 8) for c in e_sun]], disc=[0.0046, 0.0045], count=1 if i % 5 == 4 else 2)
        out.append(a)
    return out


def setf(prog, name, v):
    l = _loc(prog, name.encode())
    if l >= 0:
        u1f(l, float(v))


def seti(prog, name, v):
    l = _loc(prog, name.encode())
    if l >= 0:
        u1i(l, int(v))


def set3(prog, name, v):
    l = _loc(prog, name.encode())
    if l >= 0:
        u3f(l, float(v[0]), float(v[1]), float(v[2]))


def set_atmosphere(prog, a):
    set3(prog, 'pa_atm.rayleigh_ext', a['rayleigh_ext'])
    set3(prog, 'pa_atm.ozone_ext', a['ozone_ext'])
    set3(prog, 'pa_atm.excess', a['excess'])
    for name in ('aerosol_ext', 'slab_ext', 'slab_top_km', 'planet_radius_km', 'top_radius_km'):
        setf(prog, 'pa_atm.' + name, a[name])
    layers = [[(0.0, 1.0, a['rayleigh_scale'], 0.0, 0.0)], [(0.0, 1.0, a['mie_scale'], 0.0, 0.0)], OZONE]
    for pname, ls in zip(('rayleigh', 'mie', 'absorption'), layers):
        seti(prog, 'pa_atm.%s.count' % pname, len(ls))
        for j, (width, exp_term, exp_scale, linear, constant) in enumerate(ls):
            for key, val in (('width', width), ('exp_term', exp_term), ('exp_scale', exp_scale), ('linear_term', linear), ('constant_term', constant)):
                setf(prog, 'pa_atm.%s.%s[%d]' % (pname, key, j), val)
    set3(prog, 'pa_rayleigh_scat', a['rayleigh_scat'])
    setf(prog, 'pa_aerosol_scat', a['aerosol_scat'])
    setf(prog, 'pa_slab_scat', a['slab_scat'])
    set3(prog, 'pa_ms_scale', a['ms_scale'])
    set3(prog, 'pa_glow_phase', a['glow'])
    setf(prog, 'pa_aniso', a['aniso'])
    setf(prog, 'pa_max_phase', 1e4)
    setf(prog, 'pa_f_ms_max', 0.6)
    setf(prog, 'pa_ground_albedo', 0.2)
    lt = a['lights']
    for i in range(2):
        set3(prog, 'pa_lights.dir[%d]' % i, lt['dir'][i])
        set3(prog, 'pa_lights.E[%d]' % i, lt['E'][i])
        setf(prog, 'pa_lights.disc[%d]' % i, lt['disc'][i])
    seti(prog, 'pa_lights.count', lt['count'])


def requests(a, rnd):
    """The evaluations for one atmosphere: (kind, in0, in1)."""
    u = rnd.random
    rg, rt = a['planet_radius_km'], a['top_radius_km']
    reqs = []
    for _ in range(6):   # parameterisation: a point with mu above the horizon (inputs h, mu; u, v), and a texel
        h = u() * (rt - rg) * u()
        mu_h = -math.sqrt(max(0.0, 1.0 - (rg / (rg + h)) ** 2))
        reqs.append((0, [h, mu_h + (1.0 - mu_h) * u(), round(u(), 4), round(0.02 + 0.98 * u(), 4)], [0, 0, 0, 0]))
    for _ in range(4):
        reqs.append((1, [float(int(u() * 256)), float(int(u() * 64)), 256.0, 64.0], [0, 0, 0, 0]))
    for _ in range(8):
        reqs.append((6, [round(2 * u() - 1, 5), 0, 0, 0], [0, 0, 0, 0]))
        reqs.append((7, [round(u() * (4.0 if _ % 2 else 0.6), 5), 0, 0, 0], [0, 0, 0, 0]))
    for _ in range(6):   # light transmittance at points around the observer, towards each light: in0 = position, light; in1 = altitude
        h = round(u() * 3.0, 4)
        r = rg + h
        ang = u() * 0.3
        x = [r * math.sin(ang), 0.0, r * math.cos(ang)]
        reqs.append((8, x + [float(_ % 2)], [h, 0, 0, 0]))
    heights = [0.0, 0.002, 0.0, 0.002, 0.1, 0.3, 2.0, 5.0]
    for k in range(12):  # the aerial perspective; steps 6, 12, 24; the first four at h = 0 and 0.002 km (a camera on the ground)
        el = round(-5.0 + u() * 95.0, 3) if k >= 4 else round(0.2 + u() * 6.0, 3)
        d = dir_of(el, round(u() * 360.0, 3))
        dist = round(0.1 + u() * (49.9 if k % 2 else 5.0), 4)
        h_obs = heights[k] if k < len(heights) else round(u() * 3.0, 4)
        reqs.append((2, d + [dist], [h_obs, float((6, 12, 24)[k % 3]), 0, 0]))
    for _ in range(2):
        reqs.append((3, [round(u() * 4.0, 4), round(2 * u() - 1, 4), 8.0, 20.0], [0, 0, 0, 0]))
    reqs.append((3, [0.0, round(2 * u() - 1, 4), 8.0, 20.0], [0, 0, 0, 0]))   # on the ground: rays below the horizon end at the ground at once
    for _ in range(2):
        reqs.append((4, [round(u() * 12.0, 4), round(2 * u() - 1, 4), 8.0, 16.0], [0, 0, 0, 0]))
    for k in range(6):   # the sky: up and down; the first two from the ground
        el = round(-60.0 + u() * 150.0, 3)
        h_obs = (0.0, 0.002)[k] if k < 2 else round(u() * 2.0, 4)
        reqs.append((5, dir_of(el if k >= 2 else -10.0 - 20.0 * u(), round(u() * 360.0, 3)) + [h_obs], [32.0, 0, 0, 0]))
    return reqs


def evaluate():
    rnd = random.Random(7)
    frag_head = ATMOS_HEAD + '#define PA_LIGHT_T_EXACT\n#define PA_MS_OVERRIDE\n'
    common = shader_text('class1/windlight/physicalAtmosphereF.glsl') + MS_STUB + shader_text('class1/windlight/atmosphereCommonF.glsl')
    progs = {k: program([(VERT, VERTEX), (FRAG, frag_head + common + MAIN_HEAD + body + '\n}\n')]) for k, body in MAINS.items()}
    tgt = Target(1, 2)
    atms = atmospheres()
    rows = []
    for ai, a in enumerate(atms):
        for kind, in0, in1 in requests(a, rnd):
            prog = progs[kind]
            tgt.bind()
            use(prog)
            set_atmosphere(prog, a)
            l0, l1 = _loc(prog, b't_in0'), _loc(prog, b't_in1')
            u4f(l0, *[float(x) for x in in0])
            u4f(l1, *[float(x) for x in in1])
            draw(0x0004, 0, 3)
            ra, rb = tgt.read(0)[0], tgt.read(1)[0]
            rows.append((ai, kind, in0, in1, list(ra), list(rb)))
    return atms, rows


def sha_lines():
    """SHA-256 of the two GLSL files with the line ends normalised (the test recomputes them: a changed shader needs new vectors)."""
    import hashlib
    out = []
    for rel in ('class1/windlight/physicalAtmosphereF.glsl', 'class1/windlight/atmosphereCommonF.glsl'):
        data = shader_text(rel).replace('\r\n', '\n').replace('\r', '\n').encode('utf8')
        out.append('SC_SHA("%s", "%s")' % (rel.split('/')[-1], hashlib.sha256(data).hexdigest()))
    return out


def fmt(x):
    return repr(float(x))


def render(atms, rows):
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)',
             '// Generated by scripts/light/gen_atmosphere_scatter_vectors.py from atmosphereCommonF.glsl on the GPU; do not edit.',
             '// SC_ATM(rayleigh_ext rgb, aerosol_ext, slab_ext, slab_top_km, ozone_ext rgb, rayleigh exp_scale, mie exp_scale, planet radius km, top radius km,',
             '//        excess rgb, rayleigh_scat rgb, aerosol_scat, slab_scat, ms_scale rgb, glow.x, glow.z, glow_norm, aniso,',
             '//        sun dir xyz, moon dir xyz, E_sun rgb, E_moon rgb, sun disc, moon disc, light count)',
             '// SC_VEC(atmosphere index, kind, in0[4], in1[4], out_a[4], out_b[4])   kinds: 0 uv/rmu, 1 table texel, 2 AP, 3 MS texel, 4 sky irradiance,',
             '//        5 sky radiance, 6 phase, 7 medium, 8 light T']
    lines += sha_lines()
    for a in atms:
        lt = a['lights']
        v = (a['rayleigh_ext'] + [a['aerosol_ext'], a['slab_ext'], a['slab_top_km']] + a['ozone_ext'] + [a['rayleigh_scale'], a['mie_scale'], a['planet_radius_km'], a['top_radius_km']]
             + a['excess'] + a['rayleigh_scat'] + [a['aerosol_scat'], a['slab_scat']] + a['ms_scale'] + a['glow'] + [a['aniso']]
             + lt['dir'][0] + lt['dir'][1] + lt['E'][0] + lt['E'][1] + lt['disc'] + [lt['count']])
        lines.append('SC_ATM(' + ', '.join(fmt(x) for x in v) + ')')
    for ai, kind, in0, in1, ra, rb in rows:
        lines.append('SC_VEC(%d, %d, %s)' % (ai, kind, ', '.join(fmt(x) for x in list(in0) + list(in1) + ra + rb)))
    return '\n'.join(lines) + '\n'


def numbers(text):
    out = []
    for line in text.splitlines():
        if line.startswith('SC_') and not line.startswith('SC_SHA'):
            out += [float(x) for x in line[line.index('(') + 1:line.rindex(')')].split(',')]
    return out


atms, rows = evaluate()
text = render(atms, rows)
if '--check' in sys.argv:
    old_text = OUT.read_text()
    old_sha = [l for l in old_text.splitlines() if l.startswith('SC_SHA')]
    if old_sha != sha_lines():
        print('the shaders changed since the vectors were made:', old_sha, sha_lines())
        sys.exit(1)
    old, new = numbers(old_text), numbers(text)
    bad = [(i, a, b) for i, (a, b) in enumerate(zip(old, new)) if abs(a - b) > 1e-3 * max(abs(a), abs(b)) + 1e-6]
    print('numbers:', len(new), 'mismatches:', len(bad), bad[:5])
    sys.exit(1 if bad or len(old) != len(new) else 0)
with open(OUT, 'w', encoding='utf8', newline='\n') as f:
    f.write(text)
print('wrote', OUT, len(atms), 'atmospheres,', len(rows), 'vectors')
