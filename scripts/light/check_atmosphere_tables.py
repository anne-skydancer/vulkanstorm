# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage AT2-1's gate on the GPU: run the viewer's table passes (class1/windlight/atmosphereTablesF.glsl with atmosphereCommonF.glsl
linked in as its own unit, and skyFilterF.glsl), in the viewer's formats and order (LLAtmosphereLUTs, llatmosphereluts.cpp), with the
`Atmosphere` block the viewer uploads, and hold every table to the C++ (scripts/light/atmosphere_tables_dump.cpp, double):

    T to 1e-3 absolute; Psi, E and the sky view to 1 % (of the texel, or of 1 % of the table's brightest texel, whichever is larger)

and time each pass with GL_TIME_ELAPSED (the median of --reps draws, each in its own query; the cost on a change of the tables, since
nothing is drawn every frame). Needs Windows, a GPU and g++ on PATH; no viewer build. Set TEMP and TMP to the repository's .fixture-tmp.

    python scripts/light/check_atmosphere_tables.py [--reps 40]
"""
import ctypes as C
import math
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403
from atmosphere_gl import _loc  # noqa: E402
import atmosphere_block  # noqa: E402

REPS = int(sys.argv[sys.argv.index('--reps') + 1]) if '--reps' in sys.argv else 40
RGBA16F, RGBA32F_ = 0x881A, 0x8814
texstorage = fn('glTexStorage2D', None, U, I, U, I, I)
gen_queries = fn('glGenQueries', None, I, C.POINTER(U))
begin_query = fn('glBeginQuery', None, U, U)
end_query = fn('glEndQuery', None, U)
query_u64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
finish = fn('glFinish', None)
TIME_ELAPSED = 0x88BF

# the lights of the cases: (label, sun el, sun az, moon el, moon az, source rgb (normalised), sunlight scale, moon gain)
CASES = [
    ('noon-ish sun 60', 60.0, 30.0, -60.0, 210.0, (1.0, 0.95, 0.9), 1.5, 2.5e-6 * 0.5),
    ('low sun 3, moon up', 3.0, 100.0, 25.0, 300.0, (1.0, 0.8, 0.6), 1.5, 0.01),
    ('night: sun -20, moon 40', -20.0, 0.0, 40.0, 180.0, (0.7, 0.75, 0.9), 1.5, 0.0625),
]


class Table:
    """a w x h texture of `fmt` with an FBO of `count` attachments, as LLRenderTarget allocates them"""
    def __init__(self, w, h, fmt, count=1):
        self.w, self.h = w, h
        self.fbo = U(); genfbo(1, C.byref(self.fbo)); bindfbo(0x8D40, self.fbo.value)
        self.tex = []
        for k in range(count):
            t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
            texstorage(TEX2D, 1, fmt, w, h)
            texparam(TEX2D, 0x2801, 0x2601); texparam(TEX2D, 0x2800, 0x2601)   # bilinear, as the passes sample T, Psi and E
            texparam(TEX2D, 0x2802, 0x812F); texparam(TEX2D, 0x2803, 0x812F)   # clamp to edge
            attach2(0x8D40, 0x8CE0 + k, TEX2D, t.value, 0)
            self.tex.append(t.value)
        bufs = (U * count)(*[0x8CE0 + k for k in range(count)]); drawbuffers(count, bufs)

    def bind(self):
        bindfbo(0x8D40, self.fbo.value); viewport(0, 0, self.w, self.h)

    def read(self, k=0):
        bindtex(TEX2D, self.tex[k]); px = (F * (self.w * self.h * 4))(); getteximage(TEX2D, 0, 0x1908, FLOAT, px)
        return [(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]) for i in range(self.w * self.h)]


def dump(case, steps, exe):
    out = Path(tempfile.gettempdir()) / ('at2-1-tables-%d.bin' % abs(hash(case[0])))
    _, sel, saz, mel, maz, src, scale, gain = case
    subprocess.run([str(exe), str(out), str(sel), str(saz), str(mel), str(maz), *[str(x) for x in src], str(scale), str(gain), str(steps)], check=True)
    data = out.read_bytes()
    out.unlink()
    block, rest = data[:512], data[512:]
    vals = struct.unpack('<%dd' % (len(rest) // 8), rest)
    tri = [tuple(vals[i:i + 3]) for i in range(0, len(vals), 3)]
    sizes = [256 * 64, 32 * 32, 64 * 16, 192 * 108]
    tables, o = [], 0
    for n in sizes:
        tables.append(tri[o:o + n]); o += n
    return block, tables


def compare(name, gpu, ref, rel=None, absolute=None):
    peak = max(max(c) for c in ref)
    worst, where = 0.0, -1
    for k, (g, r) in enumerate(zip(gpu, ref)):
        for c in range(3):
            if absolute is not None:
                e = abs(g[c] - r[c])
            else:
                e = abs(g[c] - r[c]) / max(abs(r[c]), 0.01 * peak, 1e-12)
            if e > worst:
                worst, where = e, k
    limit = absolute if absolute is not None else rel
    ok = worst <= limit
    print('  %-10s worst %s %.3g at texel %d (gate %g)%s' % (name, 'absolute' if absolute is not None else 'relative', worst, where, limit, '' if ok else '  FAIL'))
    return ok


def main():
    tmp = Path(tempfile.gettempdir())
    exe = tmp / 'atmosphere_tables_dump.exe'
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'indra' / 'newview'), str(ROOT / 'scripts' / 'light' / 'atmosphere_tables_dump.cpp'), '-o', str(exe)], check=True)

    common = ATMOS_HEAD + shader_text('class1/windlight/atmosphereCommonF.glsl')
    tables_src = shader_text('class1/windlight/atmosphereTablesF.glsl')
    progs = [program([(VERT, VERTEX), (FRAG, common), (FRAG, ATMOS_HEAD + '#define AT_PASS %d\n' % (k + 1) + tables_src)]) for k in range(6)]
    for p in progs:
        atmosphere_block.Block.attach(p)
    atmosphere_block.verify_layout(progs[5])
    srgb = ATMOS_HEAD + shader_text('class1/environment/srgbF.glsl')
    sf_src = shader_text('class1/windlight/skyFilterF.glsl')
    sf = [program([(VERT, VERTEX), (FRAG, srgb), (FRAG, ATMOS_HEAD + '#define SF_PASS %d\n' % (k + 1) + sf_src)]) for k in range(3)]
    block = atmosphere_block.Block()

    T = Table(256, 64, RGBA16F)
    MSP = Table(256, 256, RGBA32F_, 2)
    MS = Table(32, 32, RGBA16F)
    EP = Table(512, 128, RGBA32F_)
    E = Table(64, 16, RGBA16F)
    SV = Table(192, 108, RGBA16F)
    AB = Table(192, 108, RGBA16F, 2)
    ROWS = Table(1, 54, RGBA32F_)
    K = Table(1, 1, RGBA32F_)

    def bind_units(prog, pairs):
        for unit, (name, tex) in enumerate(pairs):
            active(0x84C0 + unit); bindtex(TEX2D, tex)
            uniform(prog, name, unit)
        active(0x84C0)

    steps = 64
    # the passes in the viewer's order, each a closure that binds its program, its inputs and its target, and draws
    passes = [
        ('T', lambda: (T.bind(), use(progs[0]), draw(0x0004, 0, 3))),
        ('Psi split', lambda: (MSP.bind(), use(progs[1]), bind_units(progs[1], [('pa_trans_lut', T.tex[0])]), draw(0x0004, 0, 3))),
        ('Psi', lambda: (MS.bind(), use(progs[2]), bind_units(progs[2], [('at_part0', MSP.tex[0]), ('at_part1', MSP.tex[1])]), draw(0x0004, 0, 3))),
        ('E split', lambda: (EP.bind(), use(progs[3]), bind_units(progs[3], [('pa_trans_lut', T.tex[0]), ('pa_ms_lut', MS.tex[0])]), draw(0x0004, 0, 3))),
        ('E', lambda: (E.bind(), use(progs[4]), bind_units(progs[4], [('at_part0', EP.tex[0])]), draw(0x0004, 0, 3))),
        ('sky view', lambda: (SV.bind(), use(progs[5]), uniform(progs[5], 'at_steps', steps),
                              bind_units(progs[5], [('pa_trans_lut', T.tex[0]), ('pa_ms_lut', MS.tex[0]), ('pa_e_lut', E.tex[0])]), draw(0x0004, 0, 3))),
    ]
    # a classic sky's filter (Default.xml) for the filter passes' timing
    sky = load_legacy_sky(ROOT / 'indra' / 'newview' / 'app_settings' / 'windlight' / 'skies' / 'Default.xml')

    def set_sky(prog):
        ln = [0.0, sky['light_y'], math.sqrt(max(0.0, 1.0 - sky['light_y'] ** 2))]
        for k, v in (('lightnorm', ln), ('sun_glow_norm', ln), ('sunlight_color', sky['sunlight_color']), ('moonlight_color', sky['sunlight_color']),
                     ('ambient_color', sky['ambient']), ('blue_horizon', sky['blue_horizon']), ('blue_density', sky['blue_density']), ('glow', sky['glow'])):
            uniform(prog, k, *[float(x) for x in v])
        for k in ('haze_horizon', 'haze_density', 'cloud_shadow', 'density_multiplier', 'max_y'):
            uniform(prog, k, float(sky[k]))
        uniform(prog, 'sun_moon_blend', 0.0); uniform(prog, 'sky_hdr_scale', 1.0); uniform(prog, 'classic_mode', 1)
    filter_passes = [
        ('A, B', lambda: (AB.bind(), use(sf[0]), set_sky(sf[0]), draw(0x0004, 0, 3))),
        ('k rows', lambda: (ROWS.bind(), use(sf[1]), set_sky(sf[1]),
                            bind_units(sf[1], [('sf_sky_view', SV.tex[0]), ('sf_table_a', AB.tex[0]), ('sf_table_b', AB.tex[1])]), draw(0x0004, 0, 3))),
        ('k', lambda: (K.bind(), use(sf[2]), bind_units(sf[2], [('sf_rows', ROWS.tex[0])]), draw(0x0004, 0, 3))),
    ]

    ok = True
    for case in CASES:
        print('%s:' % case[0])
        blk, ref = dump(case, steps, exe)
        block.upload(blk)
        for _, run in passes:
            run()
        for _, run in filter_passes:
            run()
        finish()
        ok &= compare('T', T.read(), ref[0], absolute=1e-3)
        ok &= compare('Psi', MS.read(), ref[1], rel=0.01)
        ok &= compare('E', E.read(), ref[2], rel=0.01)
        ok &= compare('sky view', SV.read(), ref[3], rel=0.01)
        k = K.read()[0]
        print('  cast light (Default.xml over this base): base irradiance %.4f, filtered %.4f, k %.4f' % k)

    # timings: each pass alone in its query, interleaved, the median of REPS
    q = (U * 1)(); gen_queries(1, q)
    times = {name: [] for name, _ in passes + filter_passes}
    for _ in range(REPS):
        for name, run in passes + filter_passes:
            begin_query(TIME_ELAPSED, q[0]); run(); end_query(TIME_ELAPSED)
            ns = C.c_uint64(); query_u64(q[0], 0x8866, C.byref(ns))   # GL_QUERY_RESULT
            times[name].append(ns.value / 1e6)
    print('GPU time per pass (ms, median of %d, one draw per query):' % REPS)
    total_once = total_change = 0.0
    for name, ts in times.items():
        m = sorted(ts)[len(ts) // 2]
        if name in ('T', 'Psi split', 'Psi', 'E split', 'E'):
            total_once += m
        else:
            total_change += m
        print('  %-10s %.4f' % (name, m))
    print('  built once (T, Psi, E): %.4f ms; on a change of the lights or the sky (sky view, A and B, k): %.4f ms' % (total_once, total_change))
    print('check_atmosphere_tables: %s' % ('OK' if ok else 'FAILED'))
    sys.exit(0 if ok else 1)


main()
