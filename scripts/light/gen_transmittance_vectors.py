# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Evaluate the viewer's class1/windlight/physicalAtmosphereF.glsl (pa_transmittance) on the GPU for fixed
pseudo-random atmospheres and write indra/newview/tests/llatmosphere_vectors.inc, the vectors that
llatmosphericlight_test.cpp holds LLAtmosphere::transmittance (llatmosphericlight.h) to (stage AT0 of the
item-4 plan). CI runs only the C++ test; this script is what ties the vectors to the GLSL.

    python scripts/light/gen_transmittance_vectors.py            # rewrite the .inc file
    python scripts/light/gen_transmittance_vectors.py --check    # fail if the GLSL no longer produces it

Needs Windows and a GPU. The GLSL is float, so --check allows 1e-4 relative + 1e-9 absolute.
"""
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403
from atmosphere_gl import _loc  # noqa: E402

OUT = ROOT / 'indra' / 'newview' / 'tests' / 'llatmosphere_vectors.inc'
N = 128

# The absorption (ozone) profile variants; the C++ test builds the same ones.
#   0: Bruneton's ozone (two linear layers)             1: a ramp, then a lower exponential layer (a jump at 12 km)
#   2: one linear layer that clamps at 1 at 20 km
#   3: exp + linear + constant crossing 1 at 0.33 km and 0 at 6.2 km (clamp crossings inside a layer)
#   4: a valley (2 e^(-h/3000) + h/8000 - 1.2): two crossings of 0 around its minimum, then 1 at 17.6 km
#   Variants 3 and 4 are given a hundred times the ozone coefficients, so that the crossings show in T.
# layer = (width m, exp_term, exp_scale 1/m, linear 1/m, constant)
OZONE_LAYERS = {
    0: [(25000.0, 0.0, 0.0, 1.0 / 15000.0, -2.0 / 3.0), (0.0, 0.0, 0.0, -1.0 / 15000.0, 8.0 / 3.0)],
    1: [(12000.0, 0.0, 0.0, 1.0 / 12000.0, 0.0), (0.0, 0.8, -1.0 / 6000.0, 0.0, 0.0)],
    2: [(0.0, 0.0, 0.0, 1.0 / 20000.0, 0.0)],
    3: [(0.0, 1.0, -1.0 / 4000.0, -1.0 / 20000.0, 0.1)],
    4: [(0.0, 2.0, -1.0 / 3000.0, 1.0 / 8000.0, -1.2)],
}

MAIN = """
uniform float test_h;
uniform float test_mu;
uniform float test_disc;
layout(location = 0) out vec4 out_t;
void main() {
    out_t = vec4(pa_transmittance(test_h, test_mu, test_disc), 1.0);
}
"""


def cases():
    rnd = random.Random(20261110)
    u = rnd.random
    out = []
    for i in range(N):
        rg = 6360.0 if i % 4 else float(round(2000 + u() * 6000))
        height = 60.0 if i % 4 else float(round(20 + u() * 80))
        mu = round(u() * 1.05 - 0.05, 5) if i % 3 else round((u() - 0.5) * 0.12, 5)   # a third of the cases near the horizon
        out.append(dict(
            rayleigh_ext=[round(0.0219 * (0.3 + 2.7 * u()) * k, 6) for k in (1.0, 1.4, 2.2)],
            aerosol_ext=round(u() * 0.4, 6) if i % 5 else 0.0,
            slab_ext=round(u() * 1.2, 6) if i % 3 else 0.0,
            slab_top_km=round(0.05 + u() * 0.5, 4),
            ozone_ext=[round(x * (u() * 3.0) * (100.0 if i % 5 >= 3 else 1.0), 6) for x in (0.00065, 0.001881, 0.000085)],
            rayleigh_scale=-1.0 / round(6000 + u() * 4000), mie_scale=-1.0 / round(800 + u() * 1200),
            absorption=i % 5, planet_radius_km=rg, top_radius_km=rg + height,
            h_km=round(u() * 3.0, 4) if i % 2 else 0.0, mu=mu,
            disc_sin=round(0.002 + u() * 0.008, 5) if i % 4 else 0.0))
    return out


def layers_of(c):
    return [[(0.0, 1.0, c['rayleigh_scale'], 0.0, 0.0)], [(0.0, 1.0, c['mie_scale'], 0.0, 0.0)], OZONE_LAYERS[c['absorption']]]


def setf(prog, name, v):
    l = _loc(prog, name.encode())
    if l >= 0:
        u1f(l, float(v))


def seti(prog, name, v):
    l = _loc(prog, name.encode())
    if l >= 0:
        u1i(l, int(v))


def set_atmosphere(prog, c):
    uniform(prog, 'pa_atm.rayleigh_ext', *c['rayleigh_ext'])
    uniform(prog, 'pa_atm.ozone_ext', *c['ozone_ext'])
    for name in ('aerosol_ext', 'slab_ext', 'slab_top_km', 'planet_radius_km', 'top_radius_km'):
        setf(prog, 'pa_atm.' + name, c[name])
    for pname, layers in zip(('rayleigh', 'mie', 'absorption'), layers_of(c)):
        seti(prog, 'pa_atm.%s.count' % pname, len(layers))
        for j, (width, exp_term, exp_scale, linear, constant) in enumerate(layers):
            for key, val in (('width', width), ('exp_term', exp_term), ('exp_scale', exp_scale),
                             ('linear_term', linear), ('constant_term', constant)):
                setf(prog, 'pa_atm.%s.%s[%d]' % (pname, key, j), val)


def evaluate(cs):
    frag = ATMOS_HEAD + shader_text('class1/windlight/physicalAtmosphereF.glsl') + MAIN
    prog = program([(VERT, VERTEX), (FRAG, frag)])
    tgt = Target(1, 1)
    res = []
    for c in cs:
        tgt.bind(); use(prog); set_atmosphere(prog, c)
        uniform(prog, 'test_h', float(c['h_km'])); uniform(prog, 'test_mu', float(c['mu'])); uniform(prog, 'test_disc', float(c['disc_sin']))
        draw(0x0004, 0, 3)
        res.append(tgt.read(0)[0][:3])
    return res


def render(cs, res):
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)',
             '// Generated by scripts/light/gen_transmittance_vectors.py from physicalAtmosphereF.glsl on the GPU; do not edit.',
             '// {rayleigh_ext rgb, aerosol_ext, slab_ext, slab_top_km, ozone_ext rgb, rayleigh exp_scale, mie exp_scale,',
             '//  absorption variant, planet radius km, top radius km, h km, mu, disc_sin, expected transmittance rgb}']
    for c, t in zip(cs, res):
        v = c['rayleigh_ext'] + [c['aerosol_ext'], c['slab_ext'], c['slab_top_km']] + c['ozone_ext'] + [
            c['rayleigh_scale'], c['mie_scale'], c['absorption'], c['planet_radius_km'], c['top_radius_km'],
            c['h_km'], c['mu'], c['disc_sin']] + list(t)
        lines.append('{ ' + ', '.join(str(x) if isinstance(x, int) else repr(float(x)) for x in v) + ' },')
    return '\n'.join(lines) + '\n'


def numbers(text):
    return [float(x) for line in text.splitlines() if line.startswith('{') for x in line.strip('{ },').split(',')]


cs = cases()
text = render(cs, evaluate(cs))
if '--check' in sys.argv:
    old, new = numbers(OUT.read_text()), numbers(text)
    bad = [(i, a, b) for i, (a, b) in enumerate(zip(old, new)) if abs(a - b) > 1e-4 * max(abs(a), abs(b)) + 1e-9]
    print('numbers:', len(new), 'mismatches:', len(bad), bad[:5])
    sys.exit(1 if bad or len(old) != len(new) else 0)
with open(OUT, 'w', encoding='utf8', newline='\n') as f:
    f.write(text)
print('wrote', OUT, len(cs), 'vectors')
