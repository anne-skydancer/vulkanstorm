# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU bench of the remaining physical-atmosphere passes (AT2-1 to AT2-4), for the atmosphere refresh study
(doc/light/atmosphere-study.md, refresh of 2026-11-16). Not part of the build or CI.

Unlike the 2026-11-09 study bench (codex/atmosphere-study, scripts/light/bench_atmosphere.py), which marched the study's own GLSL,
every pass here runs the viewer's AT2-0 scattering core unmodified: class1/windlight/physicalAtmosphereF.glsl then
atmosphereCommonF.glsl (the GLSL twin of LLScatter, held to the C++ by gen_atmosphere_scatter_vectors.py). Only the pass mains,
the sky-view/froxel parameterisations and the split multiple-scattering fragment are bench code (fragment passes, no compute).

The atmosphere is the viewer's own translation of Default.xml, Foggy.xml and Sunset.xml (LLScatter::fromLegacy), read from
    bench_atmosphere_cpu.exe --dump > atm_dump.jsonl        (scripts/light/bench_atmosphere_cpu.cpp)

    python scripts/light/bench_atmosphere_passes.py --dump atm_dump.jsonl [--reps 60] [--batch 8]

GPU timing holds the lock file <main checkout>/.fixture-tmp/gpu-bench.lock (created exclusively, removed after; retried every 60 s), so
that two studies never time at once. Timings: GL_TIME_ELAPSED around `batch` back-to-back draws of the pass (so the GPU does not
drop its clock between tiny passes), divided by `batch`; the median of `reps` such samples after a 2 s warm-up. Windows, a GPU.
"""
import ctypes as C
import json
import math
import os
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403  (opens the hidden context)
from atmosphere_gl import _loc  # noqa: E402


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


DUMP = arg('--dump', 'atm_dump.jsonl')
REPS = int(arg('--reps', '60'))
BATCH = int(arg('--batch', '8'))
ONLY = arg('--only', '')   # time only the passes whose name contains this (the tables are still built)
# the lock and the output live in the MAIN checkout's .fixture-tmp (a worktree shares it), so studies in different worktrees see one lock
MAIN = Path(os.environ.get('VKS_MAIN_CHECKOUT') or __import__('subprocess').run(
    ['git', '-C', str(ROOT), 'rev-parse', '--path-format=absolute', '--git-common-dir'], capture_output=True, text=True).stdout.strip()).resolve()
MAIN = MAIN.parent if MAIN.name == '.git' else MAIN
LOCK = MAIN / '.fixture-tmp' / 'gpu-bench.lock'

u4f = fn('glUniform4f', None, I, F, F, F, F)
u2f = fn('glUniform2f', None, I, F, F)
genq = fn('glGenQueries', None, I, C.POINTER(U))
beginq = fn('glBeginQuery', None, U, U)
endq = fn('glEndQuery', None, U)
getq64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
finish = fn('glFinish', None)
enable = fn('glEnable', None, U)
disable = fn('glDisable', None, U)
blendsep = fn('glBlendFuncSeparate', None, U, U, U, U)
teximage2 = fn('glTexImage2D', None, U, I, I, I, I, I, U, U, P)
RGBA16F, R32F, HALF = 0x881A, 0x822E, 0x140B
TIME_ELAPSED = 0x88BF
BLEND = 0x0BE2


# ---------------------------------------------------------------------------------------------------------------------------
# GL helpers
# ---------------------------------------------------------------------------------------------------------------------------

def make_tex(w, h, fmt=RGBA16F, data=None):
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
    texstorage2(TEX2D, 1, fmt, w, h)
    if data is not None:
        texsub2(TEX2D, 0, 0, 0, w, h, 0x1903 if fmt == R32F else 0x1908, FLOAT, C.cast(data, P))
    texparam(TEX2D, 0x2801, 0x2601); texparam(TEX2D, 0x2800, 0x2601)   # linear
    texparam(TEX2D, 0x2802, 0x812F); texparam(TEX2D, 0x2803, 0x812F)   # clamp to edge
    return t.value


class RT:
    def __init__(self, w, h, count=1, fmt=RGBA16F):
        self.w, self.h = w, h
        self.tex = [make_tex(w, h, fmt) for _ in range(count)]
        self.fbo = U(); genfbo(1, C.byref(self.fbo)); bindfbo(0x8D40, self.fbo.value)
        for i, t in enumerate(self.tex):
            attach2(0x8D40, 0x8CE0 + i, TEX2D, t, 0)
        bufs = (U * count)(*[0x8CE0 + i for i in range(count)]); drawbuffers(count, bufs)

    def bind(self):
        bindfbo(0x8D40, self.fbo.value); viewport(0, 0, self.w, self.h)


def bind_tex(prog, name, unit_, tex):
    l = _loc(prog, name.encode())
    if l >= 0:
        active(0x84C0 + unit_); bindtex(TEX2D, tex); u1i(l, unit_)


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


def dir_of(el_deg, az_deg):
    e, a = math.radians(el_deg), math.radians(az_deg)
    return [math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)]


def set_atmosphere(prog, a, sun_el, moon_on):
    set3(prog, 'pa_atm.rayleigh_ext', a['rayleigh_ext']); set3(prog, 'pa_atm.ozone_ext', a['ozone_ext'])
    set3(prog, 'pa_atm.excess', a['excess'])
    for name in ('aerosol_ext', 'slab_ext', 'slab_top_km', 'planet_radius_km', 'top_radius_km'):
        setf(prog, 'pa_atm.' + name, a[name])
    for pname in ('rayleigh', 'mie', 'absorption'):
        ls = a[pname]
        seti(prog, 'pa_atm.%s.count' % pname, len(ls))
        for j, (width, exp_term, exp_scale, linear, constant) in enumerate(ls):
            for key, val in (('width', width), ('exp_term', exp_term), ('exp_scale', exp_scale), ('linear_term', linear),
                             ('constant_term', constant)):
                setf(prog, 'pa_atm.%s.%s[%d]' % (pname, key, j), val)
    set3(prog, 'pa_rayleigh_scat', a['rayleigh_scat']); setf(prog, 'pa_aerosol_scat', a['aerosol_scat'])
    setf(prog, 'pa_slab_scat', a['slab_scat']); set3(prog, 'pa_ms_scale', a['ms_scale']); set3(prog, 'pa_glow_phase', a['glow'])
    setf(prog, 'pa_aniso', a['aniso']); setf(prog, 'pa_max_phase', a['max_phase']); setf(prog, 'pa_f_ms_max', 0.6)
    setf(prog, 'pa_ground_albedo', 0.2)
    sun = dir_of(sun_el, 30.0)
    moon = [-c for c in sun]
    set3(prog, 'pa_lights.dir[0]', sun); set3(prog, 'pa_lights.dir[1]', moon)
    set3(prog, 'pa_lights.E[0]', a['E_sun']); set3(prog, 'pa_lights.E[1]', [c * 0.0625 for c in a['E_sun']])
    setf(prog, 'pa_lights.disc[0]', 0.0046); setf(prog, 'pa_lights.disc[1]', 0.0045)
    seti(prog, 'pa_lights.count', 2 if moon_on else 1)


# ---------------------------------------------------------------------------------------------------------------------------
# the passes (bench mains around the viewer's GLSL)
# ---------------------------------------------------------------------------------------------------------------------------

HEAD = '#version 460 compatibility\n'
CORE = shader_text('class1/windlight/physicalAtmosphereF.glsl') + shader_text('class1/windlight/atmosphereCommonF.glsl')

PASSES = {}

# T(h, mu) 256 x 64 from pa_transmittance (AT0's twin; the cut altitudes searched per texel, as the file does today)
PASSES['T LUT 256x64 (pa_trans_texel)'] = dict(size=(256, 64), outs=1, body="""
out vec4 o0;
void main() { o0 = vec4(pa_trans_texel(ivec2(gl_FragCoord.xy), ivec2(256, 64)), 1.0); }
""")

# Psi(h, mu_s) 32 x 32, single pass: 8 x 8 directions x 20 quadratic steps per texel (the C++ reference's form)
PASSES['MS LUT 32x32 single pass (pa_ms_texel 8x8 dirs, 20 steps)'] = dict(size=(32, 32), outs=1, body="""
out vec4 o0;
void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5) / 31.0;
    float f;
    o0 = vec4(pa_ms_texel(uv.y * pa_top_height(), uv.x * 2.0 - 1.0, 8, 20, f), f);
}
""")

# Psi split: one fragment per (texel, direction): 32 x 32 texels x 64 directions = 256 x 256; then a 32 x 32 reduce
MS_SPLIT = """
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 texel = p / 8;            // 32 x 32 texels, each an 8 x 8 block of directions
    ivec2 ij = p % 8;
    vec2 uv = vec2(texel) / 31.0;
    float h0 = clamp(uv.y * pa_top_height(), 0.0, pa_top_height());
    float mu_s = uv.x * 2.0 - 1.0;
    PALights lt;
    lt.dir[0] = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s); lt.dir[1] = vec3(0.0);
    lt.E[0] = vec3(1.0); lt.E[1] = vec3(0.0); lt.disc[0] = pa_lights.disc[0]; lt.disc[1] = pa_lights.disc[1]; lt.count = 1;
    const int n_dirs = 8, n_steps = 20;
    const float pu = 1.0 / (4.0 * PA_PI);
    float ct = 1.0 - 2.0 * (float(ij.y) + 0.5) / float(n_dirs);
    float st = sqrt(1.0 - ct * ct);
    float ph = 2.0 * PA_PI * (float(ij.x) + 0.5) / float(n_dirs);
    vec3 d = vec3(st * cos(ph), st * sin(ph), ct);
    float tg = pa_ground_distance(h0, d);
    float tmax = tg >= 0.0 ? tg : max(pa_ray_alt(h0, d, pa_top_height()), 0.0);
    vec3 T = vec3(1.0), L = vec3(0.0), Fi = vec3(0.0);
    float prev = 0.0;
    for (int k = 0; k <= n_steps; ++k) {
        bool ground = k == n_steps;
        if (ground && !(tg >= 0.0)) break;
        float a = float(k + 1) / float(n_steps);
        float am = (float(k) + 0.5) / float(n_steps);
        float dt = (a * a - prev) * tmax;
        prev = a * a;
        vec3 x;
        float h = pa_path_point(h0, d, ground ? tg : am * am * tmax, x);
        h = ground ? 0.0 : h;
        vec3 lT = pa_light_T(x, h, lt, 0);
        if (ground) { vec3 n = x / length(x); L += T * lT * (max(dot(n, lt.dir[0]), 0.0) * pa_ground_albedo / PA_PI); break; }
        vec3 s_r; float s_m; vec3 ext;
        pa_medium(h, s_r, s_m, ext);
        vec3 s = s_r + vec3(s_m);
        vec3 Ts = exp(-ext * dt);
        vec3 w = (vec3(1.0) - Ts) / max(ext, vec3(1e-7));
        L += T * s * lT * w * pu; Fi += T * s * w; T *= Ts;
    }
    o0 = vec4(L, 0.0); o1 = vec4(Fi * pu, 0.0);
}
"""
PASSES['MS LUT split, 256x256 fragments (one direction each)'] = dict(size=(256, 256), outs=2, body=MS_SPLIT)
PASSES['MS LUT reduce 32x32 (64 taps x 2)'] = dict(size=(32, 32), outs=1, reduce=8, body="""
uniform sampler2D part0;
uniform sampler2D part1;
out vec4 o0;
void main() {
    ivec2 t = ivec2(gl_FragCoord.xy) * 8;
    vec3 L2 = vec3(0.0), f = vec3(0.0);
    for (int j = 0; j < 8; ++j) for (int i = 0; i < 8; ++i) {
        L2 += texelFetch(part0, t + ivec2(i, j), 0).rgb; f += texelFetch(part1, t + ivec2(i, j), 0).rgb; }
    L2 /= 64.0; f *= 4.0 * PA_PI / 64.0;
    o0 = vec4(L2 / (vec3(1.0) - min(f, vec3(pa_f_ms_max))), 1.0);
}
""")

# E(h, mu_s) 64 x 16 sky irradiance, single pass: 8 x 8 cosine directions x 16 steps
PASSES['E LUT 64x16 single pass (pa_sky_irradiance_texel 8x8, 16 steps)'] = dict(size=(64, 16), outs=1, body="""
out vec4 o0;
void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5) / vec2(63.0, 15.0);
    float h = uv.y * uv.y * pa_top_height();
    o0 = vec4(pa_sky_irradiance_texel(h, uv.x * 2.0 - 1.0, 8, 16), 1.0);
}
""")

# E split: one fragment per (texel, direction): 64 x 16 texels x 64 cosine directions = 512 x 128; then a 64 x 16 reduce
PASSES['E LUT split, 512x128 fragments (one direction each, 16 steps)'] = dict(size=(512, 128), outs=1, body="""
out vec4 o0;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 texel = p / 8;
    ivec2 ij = p % 8;
    vec2 uv = vec2(texel) / vec2(63.0, 15.0);
    float h0 = clamp(uv.y * uv.y * pa_top_height(), 0.0, pa_top_height());
    float mu_s = uv.x * 2.0 - 1.0;
    PALights lt;
    lt.dir[0] = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s); lt.dir[1] = vec3(0.0);
    lt.E[0] = vec3(1.0); lt.E[1] = vec3(0.0); lt.disc[0] = pa_lights.disc[0]; lt.disc[1] = pa_lights.disc[1]; lt.count = 1;
    float ct = sqrt(1.0 - (float(ij.y) + 0.5) / 8.0);
    float st = sqrt((float(ij.y) + 0.5) / 8.0);
    float ph = 2.0 * PA_PI * (float(ij.x) + 0.5) / 8.0;
    vec3 d = vec3(st * cos(ph), st * sin(ph), ct);
    vec3 Lin = vec3(0.0), T = vec3(1.0);
    pa_march_seg(h0, d, 0.0, max(pa_ray_alt(h0, d, pa_top_height()), 0.0), 16, 2.0, false, lt, Lin, T);
    o0 = vec4(Lin, 0.0);
}
""")
PASSES['E LUT reduce 64x16 (64 taps)'] = dict(size=(64, 16), outs=1, body="""
uniform sampler2D epart;
out vec4 o0;
void main() {
    ivec2 t = ivec2(gl_FragCoord.xy) * 8;
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 8; ++j) for (int i = 0; i < 8; ++i) sum += texelFetch(epart, t + ivec2(i, j), 0).rgb;
    o0 = vec4(sum * (PA_PI / 64.0), 1.0);
}
""")

# the sky view 192 x 108 (Hillaire): longitude about the sun, latitude squared about the horizon; the ground term below the horizon
# takes the E table (AT2-1's form) instead of pa_sky_radiance's exact irradiance integral
SKYVIEW = """
uniform sampler2D e_lut;
uniform int steps;
out vec4 o0;
vec3 sky_view(vec3 d, float h_obs_km, int n)
{
    float h0 = clamp(h_obs_km, 0.0, pa_top_height());
    float tg = pa_ground_distance(h0, d);
    float tmax = pa_ray_length(h0, d);
    vec3 Lin = vec3(0.0), T = vec3(1.0);
    pa_march_seg(h0, d, 0.0, tmax, n, 1.0, false, pa_lights, Lin, T);
    if (tg >= 0.0) {
        vec3 p; pa_path_point(h0, d, tg, p);
        vec3 nrm = p / length(p);
        vec3 E = vec3(0.0);
        for (int l = 0; l < min(max(pa_lights.count, 0), 2); ++l) {
            float mu = dot(nrm, pa_lights.dir[l]);
            E += pa_lights.E[l] * (pa_light_T(p, 0.0, pa_lights, l) * max(mu, 0.0) + texture(e_lut, vec2(mu * 0.5 + 0.5, 0.0)).rgb);
        }
        Lin += T * E * (pa_ground_albedo / PA_PI);
    }
    return Lin;
}
void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5) / vec2(191.0, 107.0);
    float v = uv.y * 2.0 - 1.0;
    float el = sign(v) * v * v * 0.5 * PA_PI;
    float az = uv.x * 2.0 * PA_PI;
    vec3 d = vec3(cos(el) * cos(az), cos(el) * sin(az), sin(el));
    o0 = vec4(sky_view(d, 0.0, steps), 1.0);
}
"""
# the same sky view with the slab-split march (atmosphereAP to the end of the ray) instead of pa_march_seg's uniform steps: the
# uniform march skips the 0.25 km haze slab (scripts/light/check_skyview_steps.cpp: up to 98 % low in Foggy at 32 steps)
SKYVIEW_SPLIT = SKYVIEW.replace("pa_march_seg(h0, d, 0.0, tmax, n, 1.0, false, pa_lights, Lin, T);", "atmosphereAP(d, tmax, h0, n, pa_lights, Lin, T);")
assert SKYVIEW_SPLIT != SKYVIEW
PASSES['Sky view 192x108, slab-split, 32 steps'] = dict(size=(192, 108), outs=1, body=SKYVIEW_SPLIT, ints=dict(steps=32))
PASSES['Sky view 192x108, slab-split, 64 steps'] = dict(size=(192, 108), outs=1, body=SKYVIEW_SPLIT, ints=dict(steps=64))
PASSES['Sky view 192x108, 32 steps'] = dict(size=(192, 108), outs=1, body=SKYVIEW, ints=dict(steps=32))
PASSES['Sky view 192x108, 24 steps'] = dict(size=(192, 108), outs=1, body=SKYVIEW, ints=dict(steps=24))

# the main view's froxels as a 2D atlas 256 x 128 = 32 x 32 x 32 (8 x 4 slices of 32 x 32), one draw; 60 degree vertical
# field of view, 16:9, slices quadratic to the far clip (256 m); the camera's real height; in-scatter and transmittance (2 MRT)
FROXEL = """
uniform int steps;              // > 0: fixed steps; 0: 4 + slice / 2 (4 to 19)
uniform int grid;               // froxels per side (32 main view, 16 probe face); the atlas has `cols` slices per row
uniform int cols;
uniform float h_cam_km;
uniform float far_km;
uniform vec3 cam_fwd;
uniform vec3 cam_right;
uniform vec3 cam_up;
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int slice = (p.x / grid) + cols * (p.y / grid);
    vec2 f = (vec2(p % grid) + 0.5) / float(grid) * 2.0 - 1.0;
    float th = tan(radians(30.0));
    vec3 d = normalize(cam_fwd + cam_right * f.x * th * 16.0 / 9.0 + cam_up * f.y * th);
    float z = (float(slice) + 1.0) / float(grid);
    float dist = z * z * far_km;
    int n = steps > 0 ? steps : 4 + slice / 2;
    vec3 Lin, T;
    atmosphereAP(d, dist, h_cam_km, n, Lin, T);
    o0 = vec4(Lin, 1.0); o1 = vec4(T, 1.0);
}
"""
PASSES['AP froxels 32^3 atlas, 12 steps'] = dict(size=(256, 128), outs=2, body=FROXEL, ints=dict(steps=12, grid=32, cols=8))
PASSES['AP froxels 32^3 atlas, 24 steps'] = dict(size=(256, 128), outs=2, body=FROXEL, ints=dict(steps=24, grid=32, cols=8))
PASSES['AP froxels 32^3 atlas, 4 + slice/2 steps'] = dict(size=(256, 128), outs=2, body=FROXEL, ints=dict(steps=0, grid=32, cols=8))
PASSES['AP froxels 16^3 atlas (probe face), 12 steps'] = dict(size=(64, 64), outs=2, body=FROXEL, ints=dict(steps=12, grid=16, cols=4))

# the far table for the clouds (L7's R-AP): 32 azimuths x 32 elevations (0 to 90 degrees, squared) x 32 log distances 0.1 to 50 km,
# fixed observer on the ground, 16 steps
FAR = """
uniform int steps;
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int slice = (p.x / 32) + 8 * (p.y / 32);
    vec2 f = (vec2(p % 32) + 0.5) / 32.0;
    float el = f.y * f.y * 0.5 * PA_PI;
    float az = f.x * 2.0 * PA_PI;
    vec3 d = vec3(cos(el) * cos(az), cos(el) * sin(az), sin(el));
    float dist = 0.1 * pow(500.0, (float(slice) + 0.5) / 32.0);
    vec3 Lin, T;
    atmosphereAP(d, dist, 0.0, steps, Lin, T);
    o0 = vec4(Lin, 1.0); o1 = vec4(T, 1.0);
}
"""
PASSES['Far AP 32^3 atlas, 16 steps'] = dict(size=(256, 128), outs=2, body=FAR, ints=dict(steps=16))
PASSES['Far AP 32^3 atlas, 48 steps'] = dict(size=(256, 128), outs=2, body=FAR, ints=dict(steps=48))

# per pixel, full screen: the AP apply (4 taps of the atlas: two slices x in-scatter and T, dual-source blend into RGBA16F),
# against today's haze pass (calcAtmosphericVarsLinear per pixel, hazeF.glsl's arithmetic, blend ONE, SRC_ALPHA); and the sky
# radiance (sky-view tap, the sun's aerosol phase per pixel, the disc through the T table) for every pixel as sky
APPLY = """
uniform sampler2D depth_tex;
uniform sampler2D ap_in;
uniform sampler2D ap_T;
uniform vec2 screen;
layout(location = 0, index = 0) out vec4 o0;
layout(location = 0, index = 1) out vec4 o1;
vec2 atlas_uv(vec2 uv, int slice) { return (vec2(slice % 8, slice / 8) * 32.0 + clamp(uv * 32.0, vec2(0.5), vec2(31.5))) / vec2(256.0, 128.0); }
void main() {
    vec2 uv = gl_FragCoord.xy / screen;
    float z = texture(depth_tex, uv).r;
    float s = sqrt(z) * 32.0 - 1.0;
    int s0 = clamp(int(floor(s)), 0, 31), s1 = min(s0 + 1, 31);
    float w = clamp(s - float(s0), 0.0, 1.0);
    vec3 Lin = mix(texture(ap_in, atlas_uv(uv, s0)).rgb, texture(ap_in, atlas_uv(uv, s1)).rgb, w);
    vec3 T = mix(texture(ap_T, atlas_uv(uv, s0)).rgb, texture(ap_T, atlas_uv(uv, s1)).rgb, w);
    o0 = vec4(Lin, 0.0);
    o1 = vec4(T, dot(T, vec3(0.2126, 0.7152, 0.0722)));
}
"""

SKYPIX = """
uniform sampler2D sky_view_lut;
uniform vec2 screen;
uniform vec3 cam_fwd;
uniform vec3 cam_right;
uniform vec3 cam_up;
out vec4 o0;
void main() {
    vec2 f = gl_FragCoord.xy / screen * 2.0 - 1.0;
    float th = tan(radians(30.0));
    vec3 d = normalize(cam_fwd + cam_right * f.x * th * screen.x / screen.y + cam_up * f.y * th);
    float el = asin(clamp(d.z, -1.0, 1.0));
    float az = atan(d.y, d.x);
    vec2 uv = vec2(fract(az / (2.0 * PA_PI)), 0.5 + 0.5 * sign(el) * sqrt(abs(el) / (0.5 * PA_PI)));
    vec3 L = texture(sky_view_lut, uv).rgb;
    float c = dot(d, pa_lights.dir[0]);
    vec3 x = vec3(0.0, 0.0, pa_atm.planet_radius_km + 0.002);
    vec3 Ts = pa_light_T(x, 0.002, pa_lights, 0);
    L += pa_lights.E[0] * Ts * (pa_aerosol_scat + pa_slab_scat) * pa_phase_m(c) * 0.25;   // the aureole at full resolution
    L += pa_lights.E[0] * Ts * 5000.0 * smoothstep(0.99998, 0.99999, c);                  // the disc
    o0 = vec4(L, 1.0);
}
"""

TODAY_HAZE = """
uniform sampler2D depth_tex;
uniform vec2 screen;
uniform float sky_hdr_scale;
uniform vec3 primary_light_dir;
out vec4 frag_color;
vec3 srgb_to_linear(vec3 c);
void main() {
    vec2 uv = gl_FragCoord.xy / screen;
    float z = texture(depth_tex, uv).r;
    vec2 f = uv * 2.0 - 1.0;
    vec3 pos = vec3(f.x * 1.0, f.y * 0.57, -1.0) * (z * 256.0);
    vec3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(pos, vec3(0.0, 0.0, 1.0), primary_light_dir, sunlit, amblit, additive, atten);
    frag_color = max(vec4(srgb_to_linear(additive * 2.0) * sky_hdr_scale, atten.r), vec4(0.0));
}
"""


def compile_pass(body):
    return program([(VERT, VERTEX), (FRAG, HEAD + CORE + body)])


# ---------------------------------------------------------------------------------------------------------------------------
# timing
# ---------------------------------------------------------------------------------------------------------------------------

def acquire_lock():
    LOCK.parent.mkdir(exist_ok=True)
    while True:
        try:
            fd = os.open(str(LOCK), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, ('bench_atmosphere_passes.py pid %d\n' % os.getpid()).encode())
            os.close(fd)
            return
        except FileExistsError:
            print('gpu-bench.lock is held; retrying in 60 s', flush=True)
            time.sleep(60)


def release_lock():
    try:
        LOCK.unlink()
    except FileNotFoundError:
        pass


def time_draws(setup, reps=REPS, batch=BATCH):
    q = U(); genq(1, C.byref(q))
    out = C.c_uint64()
    setup()
    t_end = time.perf_counter() + 0.25
    while time.perf_counter() < t_end:      # warm-up for this pass
        for _ in range(batch):
            draw(0x0004, 0, 3)
        finish()
    samples = []
    for _ in range(reps):
        beginq(TIME_ELAPSED, q.value)
        for _ in range(batch):
            draw(0x0004, 0, 3)
        endq(TIME_ELAPSED)
        getq64(q.value, 0x8866, C.byref(out))   # GL_QUERY_RESULT
        samples.append(out.value / 1e6 / batch)
    samples.sort()
    return statistics.median(samples), samples[len(samples) // 10], samples[(len(samples) * 9) // 10]


def main():
    skies = [json.loads(l) for l in Path(DUMP).read_text().splitlines() if l.strip()]
    sun_el = {'Default': 65.9, 'Foggy': 90.0, 'Sunset': 4.3}
    progs = {name: compile_pass(p['body']) for name, p in PASSES.items()}
    apply_prog = compile_pass(APPLY)
    sky_prog = compile_pass(SKYPIX)
    haze_prog = program([(VERT, VERTEX), (FRAG, HEAD + shader_text('class1/environment/srgbF.glsl')
                                          + shader_text('class1/windlight/atmosphericsFuncs.glsl') + TODAY_HAZE)])
    # targets
    rts = {name: RT(*p['size'], count=p['outs']) for name, p in PASSES.items()}
    tlut = rts['T LUT 256x64 (pa_trans_texel)'].tex[0]
    mslut = rts['MS LUT reduce 32x32 (64 taps x 2)'].tex[0]
    elut = rts['E LUT 64x16 single pass (pa_sky_irradiance_texel 8x8, 16 steps)'].tex[0]
    split = rts['MS LUT split, 256x256 fragments (one direction each)']
    froxel = rts['AP froxels 32^3 atlas, 12 steps']
    epart = rts['E LUT split, 512x128 fragments (one direction each, 16 steps)'].tex[0]
    skyview = rts['Sky view 192x108, 32 steps'].tex[0]

    gpu = get_string(0x1F01).decode()
    acquire_lock()
    try:
        # 2 s of load so the GPU leaves its idle clock before anything is timed
        warm = progs['AP froxels 32^3 atlas, 12 steps']
        froxel.bind(); use(warm); set_atmosphere(warm, skies[0], 65.9, True)
        t_end = time.perf_counter() + 2.0
        while time.perf_counter() < t_end:
            draw(0x0004, 0, 3); finish()

        results = []
        for a in skies:
            el = sun_el.get(a['name'], 45.0)
            for moon_on in (True, False):
                if not moon_on and a['name'] != 'Default':
                    continue
                tag = a['name'] + (' (sun + moon)' if moon_on else ' (sun only)')
                # build the tables once in order, so that every later pass reads real content
                for name in ('T LUT 256x64 (pa_trans_texel)', 'MS LUT split, 256x256 fragments (one direction each)',
                             'MS LUT reduce 32x32 (64 taps x 2)', 'E LUT 64x16 single pass (pa_sky_irradiance_texel 8x8, 16 steps)'):
                    rts[name].bind(); use(progs[name]); set_atmosphere(progs[name], a, el, moon_on)
                    bind_tex(progs[name], 'pa_trans_lut', 0, tlut); bind_tex(progs[name], 'pa_ms_lut', 1, mslut)
                    bind_tex(progs[name], 'part0', 2, split.tex[0]); bind_tex(progs[name], 'part1', 3, split.tex[1])
                    draw(0x0004, 0, 3)
                finish()
                for name, p in PASSES.items():
                    if not moon_on and not name.startswith(('Sky view', 'AP froxels', 'Far AP')):
                        continue
                    if ONLY and ONLY not in name:
                        continue
                    prog, rt = progs[name], rts[name]

                    def setup(prog=prog, rt=rt, p=p):
                        rt.bind(); use(prog); set_atmosphere(prog, a, el, moon_on)
                        bind_tex(prog, 'pa_trans_lut', 0, tlut); bind_tex(prog, 'pa_ms_lut', 1, mslut)
                        bind_tex(prog, 'part0', 2, split.tex[0]); bind_tex(prog, 'part1', 3, split.tex[1])
                        bind_tex(prog, 'e_lut', 4, elut); bind_tex(prog, 'epart', 5, epart)
                        for k, v in p.get('ints', {}).items():
                            seti(prog, k, v)
                        setf(prog, 'h_cam_km', 0.025); setf(prog, 'far_km', 0.256)
                        set3(prog, 'cam_fwd', (1.0, 0.0, -0.05)); set3(prog, 'cam_right', (0.0, -1.0, 0.0)); set3(prog, 'cam_up', (0.05, 0.0, 1.0))
                    # a table that is rebuilt only when the atmosphere changes is timed with the draws it costs then
                    results.append((tag, name, '-') + time_draws(setup))
                    print('%-28s %-62s %.4f ms (p10 %.4f, p90 %.4f)' % ((tag, name) + results[-1][3:]), flush=True)
                if not moon_on or ONLY:
                    continue
                # full-screen passes at 1080p and 1440p
                for (w, h) in ((1920, 1080), (2560, 1440)):
                    import array
                    buf = array.array('f', [((i * 2654435761) % 1000) / 1000.0 for i in range(w * h)])
                    depth = make_tex(w, h, R32F, (F * (w * h)).from_buffer(buf))
                    target = RT(w, h, 1)

                    def s_apply():
                        target.bind(); use(apply_prog); set_atmosphere(apply_prog, a, el, True)
                        bind_tex(apply_prog, 'depth_tex', 0, depth); bind_tex(apply_prog, 'ap_in', 1, froxel.tex[0])
                        bind_tex(apply_prog, 'ap_T', 2, froxel.tex[1])
                        l = _loc(apply_prog, b'screen'); u2f(l, float(w), float(h))
                        enable(BLEND); blendsep(0x0001, 0x88F9, 0x0000, 0x8589)   # ONE, SRC1_COLOR; ZERO, SRC1_ALPHA

                    def s_haze():
                        target.bind(); use(haze_prog)
                        p = load_legacy_sky(ROOT / 'indra/newview/app_settings/windlight/skies/Default.xml')
                        p.update(sky_sunlight_scale=1.0, sky_ambient_scale=1.0, classic_mode=0)
                        set_atmosphere_uniforms(haze_prog, p)
                        setf(haze_prog, 'sky_hdr_scale', 2.0); set3(haze_prog, 'primary_light_dir', (0.0, 0.4, 0.9))
                        bind_tex(haze_prog, 'depth_tex', 0, depth)
                        l = _loc(haze_prog, b'screen'); u2f(l, float(w), float(h))
                        enable(BLEND); blendsep(0x0001, 0x0302, 0x0001, 0x0302)   # ONE, SRC_ALPHA (hazeF's BF_ONE, BF_SOURCE_ALPHA)

                    def s_sky():
                        target.bind(); use(sky_prog); set_atmosphere(sky_prog, a, el, True); disable(BLEND)
                        bind_tex(sky_prog, 'sky_view_lut', 0, skyview); bind_tex(sky_prog, 'pa_trans_lut', 1, tlut)
                        l = _loc(sky_prog, b'screen'); u2f(l, float(w), float(h))
                        set3(sky_prog, 'cam_fwd', (1.0, 0.0, 0.3)); set3(sky_prog, 'cam_right', (0.0, -1.0, 0.0)); set3(sky_prog, 'cam_up', (-0.3, 0.0, 1.0))

                    for label, s in (('AP apply (4 taps, dual-source blend)', s_apply), ("Today's haze pass (calcAtmosphericVarsLinear + blend)", s_haze),
                                     ('Sky radiance, every pixel sky', s_sky)):
                        results.append((tag, label, '%dx%d' % (w, h)) + time_draws(s))
                        disable(BLEND)
                        print('%-28s %-52s %-10s %.4f ms (p10 %.4f, p90 %.4f)' % ((tag, label, '%dx%d' % (w, h)) + results[-1][3:]), flush=True)
    finally:
        release_lock()
    out = MAIN / '.fixture-tmp' / 'bench_atmosphere_passes.json'
    out.write_text(json.dumps(dict(gpu=gpu, reps=REPS, batch=BATCH, results=results), indent=1))
    print('wrote', out)


main()
