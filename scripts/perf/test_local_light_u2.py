# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU gates of unified lighting stage U2 (every light casts), no viewer launch.

Reuses the scenes and GL context of bench_shadow_legacy.py and runs the shipped Stochastic shaders: the sun pass, the shadow
util and the core's noise (class1/deferred/shadowStochasticUtil.glsl, stochasticNoiseUtil.glsl), the core's screen march
(stochasticScreenMarchUtil.glsl), the shadow pool's local light ratio pass (class2/deferred/localLightRatioF.glsl) with the
light pool's weights (localLightWeightUtil.glsl) and the ratio's resolve (localLightRatioResolveF.glsl).

Scenes: a bedroom (6 lamps) and a club (36 lights), the survey's bench scenes, with every light a pick candidate in every tile.
Ground truth: the same weights, and each light's visibility from the scene's analytic ray cast (16 rays to the emitter disc, the
own prim excluded by the exclusion radius).

Gates:
  * the sun: raw r and AO g of the sun pass, and the resolved sun, are bit identical to the base (the stage before, U1);
    SUN_RAYS is unchanged; LOCAL_PICK, LOCAL_RAYS and SUN_RAYS are decorrelated from one another (pixels and frames);
  * unbiased: the mean over 64 frames of the stochastic ratio equals the mean of the deterministic estimator (every light, 8
    rays, the same march) within 0.005 (signed mean over the lit pixels); a quad's merge keeps it;
  * where nothing occludes, R is exactly 1; no acne on lit planes; the umbra of an on-screen occluder is dark;
  * every light shadows: for each light, the pixels it dominates where the ground truth occludes it are darkened;
  * a thin occluder, a shade and a housing (the own prim excluded, a linked part casts);
  * the resolve converges in 8 to 16 frames;
  * the cost: the ratio pass + resolve at 1080p, 1440p and 4K against the survey's numbers (bedroom 0.46 ms, club 0.63 ms at 1440p).

Environment: LOCAL_LIGHT_BASE_REF (default the pinned SHA of stage U1), ST_FRAMES (timing frames, default 40),
U2_CHECK_RES (the resolution of the correctness gates, default 1280x720).
"""
import math, os, re, statistics, sys
from pathlib import Path
import numpy as np
os.environ.setdefault('SHADOW_BENCH_RES', '1920x1080,2560x1440,3840x2160')
_src = Path(__file__).with_name('bench_shadow_legacy.py').read_text()
exec(compile(_src.split('# --- timing + basic checks')[0], 'bench_prefix', 'exec'))

REF = os.environ.get('LOCAL_LIGHT_BASE_REF', '17c5d527cc')   # the branch of stage U1
FRAMES = int(os.environ.get('ST_FRAMES', 40))
CW, CH = (int(v) for v in os.environ.get('U2_CHECK_RES', '1280x720').split('x'))
DEEP = SHADERS + 'class1/deferred/'
CLASS2 = SHADERS + 'class2/deferred/'
UTIL_NEW = (root / DEEP / 'shadowStochasticUtil.glsl').read_text()
UTIL_OLD = git_show(REF, DEEP + 'shadowStochasticUtil.glsl')
NOISE = (root / DEEP / 'stochasticNoiseUtil.glsl').read_text()
NOISE_OLD = git_show(REF, DEEP + 'stochasticNoiseUtil.glsl')
MARCH = (root / DEEP / 'stochasticScreenMarchUtil.glsl').read_text()
if os.environ.get('U2_THICK'):   # exploration only
    MARCH = MARCH.replace('#define ST_SCREEN_THICK_MIN 0.15', '#define ST_SCREEN_THICK_MIN ' + os.environ['U2_THICK'])
SUN_NEW = (root / SUN_F).read_text()
SUN_OLD = git_show(REF, SUN_F)
RES_NEW = (root / DEEP / 'shadowStochasticResolveUtilF.glsl').read_text()
RES_OLD = git_show(REF, DEEP + 'shadowStochasticResolveUtilF.glsl')
RESOLVE_F = (root / DEEP / 'shadowStochasticResolveF.glsl').read_text()
ENGINE_RESOLVE = (root / DEEP / 'stochasticResolveUtilF.glsl').read_text()
RATIO_F = (root / CLASS2 / 'localLightRatioF.glsl').read_text()
if os.environ.get('U2_ABL') == 'nomarch':   # exploration only
    RATIO_F = RATIO_F.replace('float b = stMarchRayScreen(po, c0, e, xr, reach, nz.y, k, max(ratio_steps_max, 2));', 'float b = 0.0;')
if os.environ.get('U2_ABL') == 'nosteps':
    MARCH = MARCH.replace('for (int i = 0; i < steps; ++i)', 'for (int i = 0; i < 0; ++i)')
if os.environ.get('U2_ABL') == 'unipick':
    RATIO_F = RATIO_F.replace('if (u * den_all < ws)', 'if (pick < 0)')
if os.environ.get('U2_ABL') == 'noloop':
    RATIO_F = RATIO_F.replace('for (int i = 0; i < count; ++i)', 'for (int i = 0; i < min(count, 1); ++i)')
WEIGHT_F = (root / CLASS2 / 'localLightWeightUtil.glsl').read_text()
RATIO_RESOLVE_F = (root / DEEP / 'localLightRatioResolveF.glsl').read_text()
UTIL_COMMON = (root / UTIL_F).read_text()
STUBS_RATIO = '''#version 430 core
float calcLegacyDistanceAttenuation(float distance, float falloff)   // deferredUtil.glsl
{
    float dist_atten = 1.0 - clamp((distance + falloff)/(1.0 + falloff), 0.0, 1.0);
    dist_atten *= dist_atten;
    dist_atten *= 2.0;
    return dist_atten;
}
'''
texsub = fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)
_enable, _disable = fn('glEnable', None, U), fn('glDisable', None, U)
_pixelstore = fn('glPixelStorei', None, U, I)
u4i = fn('glUniform4fv', None, I, I, C.POINTER(F))
u3fv = fn('glUniform3fv', None, I, I, C.POINTER(F))
u1fv = fn('glUniform1fv', None, I, I, C.POINTER(F))
CLIP = (10.0, 25.0, 60.0, 150.0)
DUMMY_SPOTS = [([2.0, 2.0, 2.0], [2.0, 0.0, 2.0]), ([3.0, 2.0, 2.0], [3.0, 0.0, 2.0])]
queries = (U * 1)(); genq(1, queries)


def tq(f):
    beginq(TIME_ELAPSED, queries[0]); f(); endq(TIME_ELAPSED)
    v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v)); return v.value / 1e6


def read_rgba(fbo, w, h):
    bindfbo(FB, fbo); readbuffer(COLOR0)
    buf = np.empty((h, w, 4), np.float32); readpixels(0, 0, w, h, 0x1908, 0x1406, buf.ctypes.data_as(C.c_void_p))
    return buf


def read_att(fbo, att, w, h):
    bindfbo(FB, fbo); readbuffer(COLOR0 + att)
    buf = np.empty((h, w, 4), np.float32); readpixels(0, 0, w, h, 0x1908, 0x1406, buf.ctypes.data_as(C.c_void_p))
    return buf


# --- programs ---------------------------------------------------------------------------------------------
def stoch_program(sun, util, noise, new):
    defines = ['SUN_SHADOW', 'SPOT_SHADOW']
    common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
    return program([(0x8B31, VERTEX), (0x8B30, stage(sun, ['STOCHASTIC_SHADOW_SUN'] if new else [])), (0x8B30, stage(UTIL_COMMON, defines + common)),
                    (0x8B30, stage(util, defines + S2_DEFINES)), (0x8B30, STUBS_F), (0x8B30, stage(noise, []))])


def ratio_stages(extra):
    defines = ['SUN_SHADOW', 'SPOT_SHADOW']
    common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
    return [(0x8B31, VERTEX), (0x8B30, stage(UTIL_COMMON, defines + common)), (0x8B30, stage(UTIL_NEW, defines + S2_DEFINES)),
            (0x8B30, stage(NOISE, [])), (0x8B30, stage(WEIGHT_F, [])), (0x8B30, stage(MARCH, [])), (0x8B30, STUBS_F), (0x8B30, STUBS_RATIO)] + extra


def ratio_program():
    return program(ratio_stages([(0x8B30, stage(RATIO_F, ['STOCHASTIC_SHADOW_SUN']))]))


# the deterministic estimator: every light of the table, 8 rays each, the same march (the expectation of the ratio pass)
REFMARCH_F = '''
uniform sampler2D depthMap;
uniform int lcount;
uniform int ref_steps;
in vec2 vary_fragcoord;
out vec4 frag_color;
uniform int shadow_st_frame;
uniform mat4 st_march_proj;
vec4 getNorm(vec2 pos_screen);
vec4 getPosition(vec2 pos_screen);
vec2 stNoiseStream(vec2 px, int frame, int stream);
vec3 localLightWR(int idx, vec3 pos, vec3 n, out float rel);
void localLightEmitter(int idx, out vec3 eye, out float radius, out float r_excl);
float localLightReach(int idx);
float stMarchRayScreen(vec3 p, vec4 c0, vec3 e, float xr, float reach, float jitter, int k, int steps_max);
void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    if (texelFetch(depthMap, px, 0).r >= 1.0) { frag_color = vec4(1.0, 1.0, 1.0, 0.0); return; }
    vec4 pos = getPosition(vary_fragcoord); vec3 n = normalize(getNorm(vary_fragcoord).xyz);
    vec3 po = pos.xyz + n * (0.005 + 0.001 * -pos.z);
    vec4 c0 = st_march_proj * vec4(po, 1.0);
    vec2 nz = stNoiseStream(gl_FragCoord.xy, shadow_st_frame, 2);
    vec3 num = vec3(0.0), den = vec3(0.0);
    for (int i = 0; i < lcount; ++i)
    {
        float rel; vec3 w = localLightWR(i, pos.xyz, n, rel);
        if (w.r + w.g + w.b <= 0.0 || rel < 0.02) continue;
        vec3 eye; float er, xr; localLightEmitter(i, eye, er, xr);
        vec3 ld = normalize(eye - po);
        vec3 t1 = normalize(cross(ld, abs(ld.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0))); vec3 t2 = cross(ld, t1);
        float blocked = 0.0;
        for (int k = 0; k < 8; ++k)
        {
            float r = sqrt((float(k) + 0.5) / 8.0); float phi = float(k) * 2.39996323 + 6.28318531 * nz.x;
            vec3 e = eye + (er * r) * (cos(phi) * t1 + sin(phi) * t2);
            blocked += stMarchRayScreen(po, c0, e, xr, localLightReach(i), nz.y, k, ref_steps);
        }
        num += w * (1.0 - blocked / 8.0); den += w;
    }
    frag_color = vec4(mix(vec3(1.0), num / max(den, vec3(1e-20)), greaterThan(den, vec3(0.0))), 0.0);
}
'''
# the ground truth: the same weights, the visibility from the scene's analytic ray cast (16 rays to the emitter disc, the
# own prim cut by the exclusion radius). Output 0: the ratio; output 1: the dominant light, its share of the weights, its V, the sum
TRUTH_F = '''
%s
uniform sampler2D depthMap;
uniform int lcount;
uniform mat4 inv_view;
uniform vec3 leye_w[64];
in vec2 vary_fragcoord;
layout(location = 0) out vec4 out0;
layout(location = 1) out vec4 out1;
vec4 getNorm(vec2 pos_screen);
vec4 getPosition(vec2 pos_screen);
vec3 localLightWR(int idx, vec3 pos, vec3 n, out float rel);
void localLightEmitter(int idx, out vec3 eye, out float radius, out float r_excl);
void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    if (texelFetch(depthMap, px, 0).r >= 1.0) { out0 = vec4(1.0); out1 = vec4(-1.0, 0.0, 1.0, 0.0); return; }
    vec4 pos = getPosition(vary_fragcoord); vec3 n = normalize(getNorm(vary_fragcoord).xyz);
    vec3 po = pos.xyz + n * (0.005 + 0.001 * -pos.z);
    vec3 pw = (inv_view * vec4(po, 1.0)).xyz;
    vec3 num = vec3(0.0), den = vec3(0.0); float best = 0.0, vbest = 1.0, bi = -1.0, sum = 0.0;
    for (int i = 0; i < lcount; ++i)
    {
        float rel; vec3 w = localLightWR(i, pos.xyz, n, rel);
        float ws = w.r + w.g + w.b;
        if (ws <= 0.0 || rel < 0.02) continue;
        vec3 eye_v; float er, xr; localLightEmitter(i, eye_v, er, xr);
        vec3 eye = leye_w[i];
        vec3 ld = normalize(eye - pw);
        vec3 t1 = normalize(cross(ld, abs(ld.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0))); vec3 t2 = cross(ld, t1);
        float blocked = 0.0;
        for (int k = 0; k < 16; ++k)
        {
            float r = sqrt((float(k) + 0.5) / 16.0); float phi = float(k) * 2.39996323;
            vec3 e = eye + (er * r) * (cos(phi) * t1 + sin(phi) * t2);
            vec3 d = e - pw; float len = length(d); d /= len;
            float tmax = len - xr; float t; vec3 nn;
            if (tmax > 0.0 && trace(pw, d, tmax, t, nn)) blocked += 1.0;
        }
        float v = 1.0 - blocked / 16.0;
        num += w * v; den += w; sum += ws;
        if (ws > best) { best = ws; vbest = v; bi = float(i); }
    }
    out0 = vec4(mix(vec3(1.0), num / max(den, vec3(1e-20)), greaterThan(den, vec3(0.0))), 0.0);
    out1 = vec4(bi, sum > 0.0 ? best / sum : 0.0, vbest, sum);
}
'''
NOISE_F = '''#version 430 core
uniform int nframe;
out vec4 frag_color;
vec2 stNoiseStream(vec2 px, int frame, int stream);
void main()
{
    vec2 a = stNoiseStream(gl_FragCoord.xy, nframe, 0), b = stNoiseStream(gl_FragCoord.xy, nframe, 1), c = stNoiseStream(gl_FragCoord.xy, nframe, 2);
    frag_color = vec4(a.x, b.x, c.x, 0.0);
}
'''


# --- the light table and tile list, as the viewer builds them ---------------------------------------------
class Light:
    def __init__(self, centre, size, colour, falloff, emitter, excl, origin=None, matrix=None, slot=-1, gobo=1.0, world=None):
        self.centre, self.size, self.colour, self.falloff, self.emitter, self.excl = centre, size, colour, falloff, emitter, excl
        self.origin = origin if origin is not None else centre
        self.matrix, self.slot, self.gobo, self.world = matrix, slot, gobo, world

    def row(self):
        r = [*self.centre, self.size, *self.colour, self.falloff, *self.origin, 1.0 if self.matrix is not None else 0.0, self.gobo, self.emitter, self.excl, 0.0]
        m = self.matrix if self.matrix is not None else [[0.0] * 4] * 4
        r += [m[i][j] for j in range(4) for i in range(4)]
        return r


TABLE_ROWS = 64


def upload_table(lights):
    rows = np.zeros((TABLE_ROWS, 8 * 4), np.float32)
    for i, light in enumerate(lights):
        rows[i] = light.row()
    tex = make_tex(0x8814, 8, TABLE_ROWS)
    bindtex(TEX2D, tex); texsub(TEX2D, 0, 0, 0, 8, TABLE_ROWS, 0x1908, 0x1406, rows.ctypes.data_as(C.c_void_p))
    return tex


def sphere_rect(sc, light):
    """lllocallighttable.h sphereScreenRect: the bounding box of the sphere's box through the projection"""
    xs, ys = [], []
    for i in range(8):
        v = [light.centre[0] + (light.size if i & 1 else -light.size), light.centre[1] + (light.size if i & 2 else -light.size), light.centre[2] + (light.size if i & 4 else -light.size), 1.0]
        c = mvec(sc.proj, v)
        if c[3] <= 1e-4:
            return (0, 0, sc.W, sc.H)
        xs.append((c[0] / c[3] * 0.5 + 0.5) * sc.W); ys.append((c[1] / c[3] * 0.5 + 0.5) * sc.H)
    return (max(int(math.floor(min(xs))) - 1, 0), max(int(math.floor(min(ys))) - 1, 0), min(int(math.ceil(max(xs))) + 1, sc.W), min(int(math.ceil(max(ys))) + 1, sc.H))


def tile_data(sc, lights):
    tx, ty = (sc.W + 31) // 32, (sc.H + 31) // 32
    data = np.zeros((ty, tx * 33), np.uint8); worst = 0
    rects = [sphere_rect(sc, l) for l in lights]
    for y in range(ty):
        for x in range(tx):
            lst = [i for i, r in enumerate(rects) if r[0] < (x + 1) * 32 and r[2] > x * 32 and r[1] < (y + 1) * 32 and r[3] > y * 32][:32]
            worst = max(worst, len(lst))
            data[y, x * 33] = len(lst); data[y, x * 33 + 1:x * 33 + 1 + len(lst)] = lst
    tex = make_tex(0x8232, tx * 33, ty)
    bindtex(TEX2D, tex); _pixelstore(0x0CF5, 1)
    texsub(TEX2D, 0, 0, 0, tx * 33, ty, 0x8D94, 0x1401, data.ctypes.data_as(C.c_void_p))
    return tex, worst


def view_light(sc, pos, radius, falloff, colour, spot, emitter, excl, slot=-1):
    """a bench light (world position; spot = (dir, cos half angle) or None) as a table row"""
    lc = mvec(sc.view, list(pos) + [1.0])[:3]
    if spot is None:
        return Light(lc, radius, colour, falloff, emitter, excl, world=list(pos))
    half = math.acos(spot[1])
    up = (0.0, 1.0, 0.0) if abs(spot[0][1]) < 0.99 else (0.0, 0.0, 1.0)
    m = mmul(TEXSPACE, mmul(mmul(perspective(2.0 * half, 1.0, 0.1, radius), look_at(list(pos), [pos[i] + spot[0][i] for i in range(3)], up)), sc.inv_view))
    return Light(lc, radius, colour, falloff, emitter, excl, origin=lc, matrix=m, slot=slot, world=list(pos))


class Pass:
    """the ratio pass and the reference programs of one scene"""
    def __init__(self, sc, lights):
        self.sc, self.lights = sc, lights
        self.table = upload_table(lights)
        self.tiles, self.max_tile = tile_data(sc, lights)
        self.p_ratio = ratio_program()
        self.p_ref = program(ratio_stages([(0x8B30, stage(REFMARCH_F, ['STOCHASTIC_SHADOW_SUN']))]))
        self.p_truth = program(ratio_stages([(0x8B30, stage(TRUTH_F % scene_glsl(sc.boxes), ['STOCHASTIC_SHADOW_SUN']))]))
        self.t_raw = make_tex(0x881A, sc.W, sc.H); self.f_raw = make_fbo([self.t_raw])
        self.t_a = make_tex(0x8814, sc.W, sc.H); self.t_b = make_tex(0x8814, sc.W, sc.H); self.f_truth = make_fbo([self.t_a, self.t_b])

    def bind(self, prog, frame, floor=False, steps=6):
        self.sc.bind(prog, LIGHT_SIZE)
        for unit in range(2, 8):
            bindsampler(unit, depth_fetch.value)
        u1i(loc(prog, b'shadow_st_frame'), frame); u1f(loc(prog, b'spot_shadow_reach'), 128.0)
        u2f(loc(prog, b'spot_emitter_radius'), 0.075, 0.075); u2f(loc(prog, b'spot_shadow_excl'), 0.075, 0.075)
        for unit, tex, name in ((8, self.table, b'local_light_table'), (9, self.tiles, b'local_light_tiles')):
            active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0); u1i(loc(prog, name), unit)
        active(0x84C0)
        slot = [-1.0, -1.0]
        for i, l in enumerate(self.lights):
            if l.slot >= 0:
                slot[l.slot] = float(i)
        u2f(loc(prog, b'spot_slot_light'), *slot); u2f(loc(prog, b'spot_slot_changed'), 0.0, 0.0); u2f(loc(prog, b'spot_shadow_fade'), 0.0, 0.0)
        set_mat(prog, 'st_march_proj', [self.sc.proj])
        u1i(loc(prog, b'ratio_steps_max'), steps); u1i(loc(prog, b'ratio_rays'), 1 if floor else 2); u1i(loc(prog, b'ref_steps'), steps)
        u1i(loc(prog, b'lcount'), len(self.lights))

    def ratio(self, frame, floor=False, steps=6, prog=None):
        self.bind(prog or self.p_ratio, frame, floor, steps); bindfbo(FB, self.f_raw); viewport(0, 0, self.sc.W, self.sc.H)
        draw(4, 0, 3); finish(); self.sc.unbind()
        return read_rgba(self.f_raw, self.sc.W, self.sc.H)

    def reference(self, frame, steps=6):
        return self.ratio(frame, False, steps, self.p_ref)

    def truth(self):
        prog = self.p_truth
        self.bind(prog, 0)
        set_mat(prog, 'inv_view', [self.sc.inv_view])
        eyes = np.zeros((64, 3), np.float32)
        for i, l in enumerate(self.lights):
            eyes[i] = l.world
            if l.matrix is not None:    # a projector's emitter is its eye (the apex): the world point of the row's origin
                eyes[i] = mvec(self.sc.inv_view, list(l.origin) + [1.0])[:3]
        u3fv(loc(prog, b'leye_w'), 64, eyes.ctypes.data_as(C.POINTER(F)))
        bindfbo(FB, self.f_truth); viewport(0, 0, self.sc.W, self.sc.H); draw(4, 0, 3); finish(); self.sc.unbind()
        return read_att(self.f_truth, 0, self.sc.W, self.sc.H), read_att(self.f_truth, 1, self.sc.W, self.sc.H)

    def release(self):
        bindfbo(FB, 0)
        delfbo(2, (U * 2)(self.f_raw, self.f_truth))
        deltex(5, (U * 5)(self.table, self.tiles, self.t_raw, self.t_a, self.t_b))


def make_scene(name, W, H):
    boxes, lights, eye, target = SCENES[name]()
    sc = Scene(W, H, boxes, list(eye), list(target), [-0.3, 1.0, 0.2], DUMMY_SPOTS, CLIP)
    ls = [view_light(sc, *l) for l in lights]
    return sc, ls


# --- the survey's scenes -----------------------------------------------------------------------------------
def room(x0, x1, z0, z1, hgt, t=0.2):
    return [((x0 - t, 0.0, z0 - t), (x0, hgt + t, z1 + t)), ((x1, 0.0, z0 - t), (x1 + t, hgt + t, z1 + t)),
            ((x0 - t, 0.0, z0 - t), (x1 + t, hgt + t, z0)), ((x0 - t, 0.0, z1), (x1 + t, hgt + t, z1 + t)),
            ((x0 - t, hgt, z0 - t), (x1 + t, hgt + t, z1 + t))]


# a light: (world position, radius, falloff, colour, spot (dir, cos half angle) or None, emitter radius s, exclusion radius)
WARM = (1.0, 0.75, 0.5)


def bedroom():
    boxes = room(0.0, 5.0, 0.0, 4.0, 2.6) + [
        ((1.5, 0.0, 0.0), (3.5, 0.55, 2.1)), ((1.5, 0.0, 0.0), (3.5, 1.1, 0.08)),
        ((0.9, 0.0, 0.0), (1.4, 0.55, 0.45)), ((3.6, 0.0, 0.0), (4.1, 0.55, 0.45)),
        ((1.03, 0.75, 0.1), (1.27, 0.98, 0.34)), ((3.73, 0.75, 0.1), (3.97, 0.98, 0.34)),
        ((4.4, 0.0, 2.5), (5.0, 2.1, 3.6)),
        ((0.0, 0.0, 2.6), (0.7, 0.75, 3.8)), ((0.8, 0.0, 3.0), (1.3, 0.9, 3.4))]
    lights = [((1.15, 0.86, 0.22), 4.0, 0.75, WARM, None, 0.05, 0.25), ((3.85, 0.86, 0.22), 4.0, 0.75, WARM, None, 0.05, 0.25),
              ((2.5, 2.45, 2.0), 6.0, 0.5, (1.0, 0.95, 0.85), None, 0.15, 0.25),
              ((0.35, 1.3, 3.2), 3.0, 0.5, (1.0, 0.9, 0.8), ((0.0, -1.0, 0.0), math.cos(math.radians(40))), 0.05, 0.15),
              ((4.6, 1.6, 1.4), 5.0, 0.75, WARM, None, 0.1, 0.25), ((0.1, 1.8, 1.5), 3.0, 0.75, (1.0, 0.8, 0.6), None, 0.05, 0.15)]
    return boxes, lights, (2.5, 1.6, 3.8), (2.5, 0.6, 0.0)


def club():
    boxes = room(0.0, 30.0, 0.0, 20.0, 6.0) + [((8.0, 0, 6.0), (8.6, 6, 6.6)), ((21.4, 0, 6.0), (22.0, 6, 6.6)),
             ((8.0, 0, 13.4), (8.6, 6, 14.0)), ((21.4, 0, 13.4), (22.0, 6, 14.0)),
             ((2.0, 0.0, 14.0), (10.0, 1.1, 15.0)), ((10.0, 0.0, 0.0), (20.0, 0.8, 4.0))]
    rnd = np.random.default_rng(7)
    for i in range(24):
        x, zz = 7.0 + (i % 6) * 3.0 + rnd.uniform(-0.6, 0.6), 6.0 + (i // 6) * 2.2 + rnd.uniform(-0.5, 0.5)
        boxes.append(((x, 0.0, zz), (x + 0.45, 1.75, zz + 0.3)))
    lights = []
    hues = [(1, 0.2, 0.2), (0.2, 0.4, 1), (0.2, 1, 0.3), (1, 0.3, 1), (1, 0.8, 0.2), (0.3, 1, 1)]
    for i in range(20):
        x, zz = 4.0 + (i % 5) * 5.5, 3.0 + (i // 5) * 4.5
        dx, dz = rnd.uniform(-0.4, 0.4), rnd.uniform(-0.4, 0.4); dd = normalize([dx, -1.0, dz])
        lights.append(((x, 5.8, zz), 12.0, 0.5, hues[i % 6], (tuple(dd), math.cos(math.radians(28))), 0.1, 0.3))
    for i in range(12):
        lights.append(((3.0 + (i % 6) * 4.8, 3.0, 4.0 + (i // 6) * 10.0), 10.0, 0.75, hues[(i + 3) % 6], None, 0.1, 0.3))
    for i in range(4):
        lights.append(((11.0 + i * 2.7, 5.0, 9.0), 12.0, 0.5, hues[i], (tuple(normalize([0.0, -0.6, -1.0])), math.cos(math.radians(25))), 0.15, 0.3))
    return boxes, lights, (15.0, 1.7, 19.3), (15.0, 1.4, 2.0)


def thin_plate():
    """a floor, one point light at 2 m, a 2 cm shelf between it and the floor"""
    boxes = [((-1.0, 1.0, -1.0), (1.0, 1.02, 1.0))]
    return boxes, [((0.0, 2.0, 0.0), 6.0, 0.5, (1.0, 1.0, 1.0), None, 0.05, 0.1)], (3.5, 2.5, 3.5), (0.0, 0.0, 0.0)


def shade_housing():
    """a lamp whose own prim (a 0.2 cube around the light, excluded) sits under a linked hood (a 0.6 m plate 0.25 m above, casts onto the
    ceiling) and beside a linked shade (a 1 m plate on one side, casts onto the floor and wall); a ceiling at 2.5 m"""
    boxes = [((-3.0, 2.5, -3.0), (3.0, 2.7, 3.0)), ((-0.1, 1.4, -0.1), (0.1, 1.6, 0.1)),      # ceiling, the own prim
             ((-0.3, 1.75, -0.3), (0.3, 1.77, 0.3)),                                          # the hood
             ((0.35, 0.9, -0.5), (0.37, 1.9, 0.5))]                                           # the shade
    return boxes, [((0.0, 1.5, 0.0), 8.0, 0.5, (1.0, 1.0, 1.0), None, 0.05, 0.1732)], (-2.5, 1.2, 2.5), (0.5, 0.8, 0.0)


SCENES = {'bedroom': bedroom, 'club': club, 'thin': thin_plate, 'shade': shade_housing}


# --- the gates ---------------------------------------------------------------------------------------------
class RatioResolve:
    """the ratio's resolve: raw ratio + history (RGB10_A2) -> history, validated against a depth history (the scene's: static)"""
    def __init__(self, sc):
        self.sc = sc
        self.prog = program([(0x8B31, VERTEX), (0x8B30, stage(RATIO_RESOLVE_F, ['STOCH_RESOLVE'])), (0x8B30, stage(ENGINE_RESOLVE, []))])
        self.hist = [make_tex(0x8059, sc.W, sc.H) for _ in range(2)]; self.fbo = [make_fbo([t]) for t in self.hist]
        # the sun history's depth channel: g = the linear view depth (the scene's, static)
        d = read_att(sc.gfbo, 0, sc.W, sc.H)
        dep = np.zeros((sc.H, sc.W, 4), np.float32); dep[..., 1] = -d[..., 2]
        self.dtex = make_tex(0x881A, sc.W, sc.H)
        bindtex(TEX2D, self.dtex); texsub(TEX2D, 0, 0, 0, sc.W, sc.H, 0x1908, 0x1406, dep.ctypes.data_as(C.c_void_p))
        self.idx = 0; self.valid = False
        use(self.prog)
        set_mat(self.prog, 'stoch_reproj', [[[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]])
        u1f(loc(self.prog, b'stoch_weight'), 0.125); u1f(loc(self.prog, b'shadow_st_scale'), 1.0)
        set_mat(self.prog, 'inv_proj', [inverse(sc.proj)])
        for unit, name in enumerate(('shadow_st_raw', 'shadow_st_history', 'shadow_st_resolved', 'depthMap')):
            u1i(loc(self.prog, name.encode()), unit)

    def run(self, raw):
        sc = self.sc; use(self.prog)
        u1i(loc(self.prog, b'stoch_history_valid'), 1 if self.valid else 0)
        for unit, tex in enumerate((raw, self.hist[self.idx], self.dtex, sc.gdepth)):
            active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0)
        active(0x84C0)
        bindfbo(FB, self.fbo[1 - self.idx]); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
        self.idx = 1 - self.idx; self.valid = True

    def read(self):
        return read_rgba(self.fbo[self.idx], self.sc.W, self.sc.H)

    def release(self):
        fn('glDeleteProgram', None, U)(self.prog); delfbo(2, (U * 2)(*self.fbo)); deltex(3, (U * 3)(*self.hist, self.dtex))


print('== the sun and the core noise ==')
sunsc = Scene(CW, CH, [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)), ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)), ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0))],
              [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])], CLIP)
p_old, p_new = stoch_program(SUN_OLD, UTIL_OLD, NOISE_OLD, False), stoch_program(SUN_NEW, UTIL_NEW, NOISE, True)


def render_sun(prog, sc, frame):
    sc.bind(prog, LIGHT_SIZE)
    for unit in range(2, 8):
        bindsampler(unit, depth_fetch.value)
    u1i(loc(prog, b'shadow_st_frame'), frame); u1f(loc(prog, b'spot_shadow_reach'), 128.0)
    u2f(loc(prog, b'spot_emitter_radius'), 0.075, 0.075); u2f(loc(prog, b'spot_shadow_excl'), 0.075, 0.075)
    draw(4, 0, 3); finish()
    img = read_rgba(sc.lfbo, sc.W, sc.H); sc.unbind(); return img


same = sum(int(np.array_equal(a[..., 0], b[..., 0]) and np.array_equal(a[..., 1], b[..., 1]))
           for a, b in ((render_sun(p_old, sunsc, f), render_sun(p_new, sunsc, f)) for f in range(4)))
check(same == 4, f'the sun pass: r and AO g equal the base ({REF}) bit for bit ({same}/4 frames)', 'sun: ')
sunsc.release()

# the noise streams: decorrelated from one another across pixels and across frames
nprog = program([(0x8B31, VERTEX), (0x8B30, NOISE_F), (0x8B30, stage(NOISE, []))])
nt = make_tex(0x8814, 512, 512); nf = make_fbo([nt]); viewport(0, 0, 512, 512)
use(nprog); samples = []
for frame in range(16):
    u1i(loc(nprog, b'nframe'), frame); draw(4, 0, 3); finish(); samples.append(read_rgba(nf, 512, 512)[..., :3].reshape(-1, 3))
s = np.concatenate(samples)
cc = np.corrcoef(s.T)
check(float(np.abs(cc[np.triu_indices(3, 1)]).max()) < 0.01, f'SUN_RAYS, LOCAL_PICK and LOCAL_RAYS are decorrelated pixel by pixel (|corr| max {np.abs(cc[np.triu_indices(3, 1)]).max():.4f} < 0.01 over 16 frames x 512^2)', 'noise: ')
lag = max(abs(float(np.corrcoef(samples[i][:, c], samples[i + 1][:, d])[0, 1])) for i in range(15) for c in range(3) for d in range(3) if c != d)
check(lag < 0.01, f'and across frames (|corr| of a stream with another stream of the next frame max {lag:.4f} < 0.01)', 'noise: ')
check(all(abs(float(s[:, c].mean()) - 0.5) < 0.005 for c in range(3)), f'each stream has mean 0.5 (means {[round(float(s[:, c].mean()), 4) for c in range(3)]})', 'noise: ')

# the decorrelation also holds along a quad's shared rotation (what the ratio pass draws: the stream at px & ~1)
sdx = np.concatenate([x.reshape(512, 512, 3)[::2, ::2].reshape(-1, 3) for x in samples])
cq = np.corrcoef(sdx.T); check(float(np.abs(cq[np.triu_indices(3, 1)]).max()) < 0.02, f'and on the quads\' corners (max {np.abs(cq[np.triu_indices(3, 1)]).max():.4f} < 0.02)', 'noise: ')
fn('glDeleteProgram', None, U)(nprog); delfbo(1, C.byref(nf)); deltex(1, C.byref(U(nt)))


if os.environ.get('U2_STOP') == 'noise':
    print(chr(10).join(failures) or 'noise ok'); sys.exit(0)


def lit_mask(w_sum, gpos):
    return (w_sum > 1e-3) & (gpos[..., 3] == 1.0)


def gbuf(sc):
    p = read_att(sc.gfbo, 0, sc.W, sc.H); n = read_att(sc.gfbo, 1, sc.W, sc.H); return p, n


def erode(mask, r=2):
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out &= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out


for name in ('bedroom', 'club'):
    print(f'\n== {name} ({CW}x{CH}) ==')
    sc, lights = make_scene(name, CW, CH)
    ps = Pass(sc, lights)
    print(f'  {len(lights)} lights; the most in one tile: {ps.max_tile}')
    check(ps.max_tile <= 32, f'every light fits its tiles (max {ps.max_tile} <= 32)', f'{name}: ')
    gpos, gnorm = gbuf(sc)
    truth, aux = ps.truth()
    total = aux[..., 3]
    lit = lit_mask(total, gpos)
    N = 64
    acc = np.zeros((CH, CW, 3)); accref = np.zeros((CH, CW, 3)); raws = []
    for f in range(1, N + 1):
        r = ps.ratio(f)[..., :3]; acc += r; raws.append(r)
        accref += ps.reference(f)[..., :3]
    acc /= N; accref /= N
    d = (acc - accref)[lit]
    check(abs(float(d.mean())) < 0.005, f'unbiased: the 64-frame mean of the ratio equals the deterministic estimator\'s (signed mean over {int(lit.sum())} lit pixels {float(d.mean()):+.5f}, |.| < 0.005; per-pixel RMS {float(np.sqrt((d ** 2).mean())):.4f})', f'{name}: ')
    shadowed = lit & (truth[..., :3].min(-1) < 0.98)
    es = float(np.sqrt(((acc - truth[..., :3])[shadowed] ** 2).mean()))
    er = float(np.sqrt(((accref - truth[..., :3])[shadowed] ** 2).mean()))
    print(f'  info: against the ground truth on {int(shadowed.sum())} shadow-affected pixels, the 64-frame mean RMS {es:.4f} (the deterministic march {er:.4f})')
    # nothing occludes: R is exactly 1 where the truth is 1 and nothing is near
    free = erode(lit & (truth[..., :3].min(-1) >= 0.999), 3)
    cnt = int(free.sum()); wrong = float((1.0 - acc[free]).mean()) if cnt else 0.0
    check(cnt > 1000 and wrong < 0.01, f'lit planes (ground truth 1 and no shadow within 3 px, {cnt} pixels): no acne, the 64-frame mean of 1-R is {wrong:.5f} < 0.01', f'{name}: ')
    rr = np.array([(1.0 - r[free]).mean() for r in raws])
    print(f'  info: single-frame acne (1-R on those pixels) {float(rr.mean()):.4f} (max {float(rr.max()):.4f})')
    # umbra: the truth is dark
    umbra = lit & (truth[..., :3].max(-1) < 0.05)
    if int(umbra.sum()) > 500:
        mu = float(acc[umbra].mean())
        check(mu < 0.3, f'umbra (ground truth < 0.05, {int(umbra.sum())} pixels): the 64-frame mean of R is {mu:.4f} < 0.3 (a screen march misses back sides)', f'{name}: ')
    else:
        print(f'  info: no umbra pixels (ground truth < 0.05) in this view ({int(umbra.sum())})')
    # every light shadows: where light i dominates and the truth occludes it, the ratio is darker than 1
    dom = aux[..., 0].astype(int); share = aux[..., 1]; vdom = aux[..., 2]
    bad, tested, rows = [], 0, []
    for i in range(len(lights)):
        sel = lit & (dom == i) & (share > 0.7) & (vdom < 0.2)
        n = int(sel.sum())
        if n < 150:
            rows.append((i, n, None)); continue
        tested += 1
        m = float(acc[sel].mean()); mr = float(accref[sel].mean())
        rows.append((i, n, m))
        if not (m < 0.97 and abs(m - mr) < 0.05):
            bad.append(i)
    check(tested > 0 and not bad, f'every light shadows: {tested} of {len(lights)} lights have 150+ pixels they dominate where the truth occludes them; the 64-frame mean of R there is below 1 (< 0.97, the screen march sees part of the occluder) and within 0.05 of the deterministic march, for all (failing: {bad})', f'{name}: ')
    print('  info: per light (pixels, mean R): ' + ', '.join(f'{i}:{n}' + ('' if m is None else f'/{m:.2f}') for i, n, m in rows))
    # each light alone in the table: wherever the deterministic march finds its shadow, the pick finds it too (no light is without shadow)
    casting, silent, lost = 0, [], []
    for i, l in enumerate(lights):
        one = Pass(sc, [l]); one.lights[0].slot = -1
        a1 = sum(one.ratio(f)[..., :3] for f in range(1, 17)) / 16.0; r1 = sum(one.reference(f)[..., :3] for f in range(1, 17)) / 16.0
        sel = (r1.min(-1) < 0.5) & lit
        if int(sel.sum()) < 150:
            silent.append(i)
        else:
            casting += 1
            if abs(float(a1[sel].mean()) - float(r1[sel].mean())) > 0.08:
                lost.append(i)
        one.release()
    check(not lost and casting > 0, f'every light shadows, each alone: {casting} of {len(lights)} lights have a screen-visible shadow (150+ pixels with R < 0.5 in the deterministic march) and the pick reproduces it (mean within 0.08; failing {lost}); no occluder on screen for {silent}', f'{name}: ')
    # convergence of the resolved ratio
    rs = RatioResolve(sc)
    errs = {}
    for f in range(1, 33):
        ps.ratio(100 + f); rs.run(ps.t_raw)
        if f in (1, 4, 8, 16, 32):
            errs[f] = float(np.sqrt(((rs.read()[..., :3] - truth[..., :3])[shadowed] ** 2).mean()))
    print('  info: resolved RMS against the ground truth on the shadow pixels by frame: ' + ', '.join(f'{f}: {e:.4f}' for f, e in errs.items()))
    check(errs[8] <= 1.2 * errs[32] and errs[16] <= 1.1 * errs[32], f'convergence: the resolved error at 8 frames {errs[8]:.4f} <= 1.2 x and at 16 frames {errs[16]:.4f} <= 1.1 x the error at 32 ({errs[32]:.4f})', f'{name}: ')
    rs.release()
    ps.release(); sc.release()


# --- scenes with one claim each ------------------------------------------------------------------------------
print('\n== thin occluder, shade and housing ==')
for name in ('thin', 'shade'):
    sc, lights = make_scene(name, CW, CH)
    ps = Pass(sc, lights)
    gpos, gnorm = gbuf(sc); truth, aux = ps.truth(); lit = lit_mask(aux[..., 3], gpos)
    acc = sum(ps.ratio(f)[..., :3] for f in range(1, 65)) / 64.0
    umbra = lit & (truth[..., :3].max(-1) < 0.05)
    free = erode(lit & (truth[..., :3].min(-1) >= 0.999), 3)
    mu = float(acc[umbra].mean()) if umbra.any() else 1.0
    if os.environ.get('U2_DUMP'):
        from PIL import Image
        img = np.concatenate([np.clip(truth[..., :3], 0, 1), np.clip(acc, 0, 1)], axis=1)
        Image.fromarray((img[::-1] * 255).astype(np.uint8)).save(os.environ['U2_DUMP'] + f'_{name}.png')
    fr = float((1.0 - acc[free]).mean()) if free.any() else 0.0
    if name == 'thin':
        check(int(umbra.sum()) > 500, f'thin: the 2 cm shelf throws an umbra of {int(umbra.sum())} pixels in the ground truth', 'thin: ')
        check(mu < 0.8, f'thin: the 64-frame mean of R in that umbra is {mu:.4f} < 0.8 (a 2 cm shelf is caught by the steps that land in its thickness: partial by nature)', 'thin: ')
        check(fr < 0.01, f'thin: lit ground ({int(free.sum())} pixels) stays lit (1-R {fr:.5f} < 0.01)', 'thin: ')
    else:
        # the own prim (a 0.2 cube around the light) is excluded: the floor and the ceiling near the lamp that the truth lights stay lit; the
        # linked hood and shade cast: their umbra is dark
        check(int(umbra.sum()) > 500, f'shade/housing: the hood and the shade throw an umbra of {int(umbra.sum())} pixels in the ground truth', 'shade: ')
        check(mu < 0.75, f'shade/housing: the 64-frame mean of R in the umbra is {mu:.4f} < 0.75 (a linked part casts, partially: thin plates)', 'shade: ')
        check(fr < 0.01, f'shade/housing: the surfaces the truth lights ({int(free.sum())} pixels, around the own prim included) stay lit (1-R {fr:.5f} < 0.01: the own prim is excluded)', 'shade: ')
    ps.release(); sc.release()

# nothing to occlude: exactly 1
sc = Scene(CW, CH, [((60.0, 0.0, 60.0), (61.0, 1.0, 61.0))], [0.0, 3.0, 4.0], [0.0, 0.0, -6.0], [-0.3, 1.0, 0.2], DUMMY_SPOTS, CLIP)
ls = [view_light(sc, (0.0, 4.0, -4.0), 10.0, 0.5, (1.0, 1.0, 1.0), None, 0.1, 0.2), view_light(sc, (3.0, 3.0, -3.0), 8.0, 0.5, (0.5, 0.5, 1.0), ((0.0, -1.0, 0.0), math.cos(math.radians(50))), 0.1, 0.2)]
ps = Pass(sc, ls)
ok = all(bool((ps.ratio(f)[..., :3] == 1.0).all()) for f in range(1, 9))
check(ok, 'a scene with nothing to occlude (only the ground): R is exactly 1 on every pixel and channel, 8 frames', 'free: ')
ps.release(); sc.release()


# --- a mapped projector adds its map as a hint ----------------------------------------------------------------
print('\n== a mapped projector: blocked by either ==')
boxes = [((-1.0, 1.0, -1.0), (1.0, 1.4, 1.0)), ((3.0, 0.0, 3.0), (3.4, 0.6, 3.4))]
SPOTH = [([0.0, 4.0, 0.0], [0.0, 0.0, 0.0]), ([5.0, 4.0, 0.0], [5.0, 0.0, 0.0])]
sc = Scene(CW, CH, boxes, [2.0, 3.0, 8.0], [0.0, 0.0, 0.0], [-0.3, 1.0, 0.2], SPOTH, CLIP)
l0 = Light(mvec(sc.view, [0.0, 4.0, 0.0, 1.0])[:3], 9.0, (1.0, 1.0, 1.0), 0.5, 0.1, 0.2,
           origin=mvec(sc.view, [0.0, 4.0, 0.0, 1.0])[:3], matrix=sc.shadow_matrix[4], slot=0, world=[0.0, 4.0, 0.0])
ps = Pass(sc, [l0]); gpos, gnorm = gbuf(sc); truth, aux = ps.truth(); lit = lit_mask(aux[..., 3], gpos)
umbra = lit & (truth[..., :3].max(-1) < 0.05)
acc_map = sum(ps.ratio(f)[..., :3] for f in range(1, 33)) / 32.0
l0.slot = -1
acc_screen = sum(ps.ratio(f)[..., :3] for f in range(1, 33)) / 32.0
check(int(umbra.sum()) > 500 and float(acc_map[umbra].mean()) <= float(acc_screen[umbra].mean()) + 1e-4, f'the map as a hint only adds shadow: the umbra\'s mean R with the map {float(acc_map[umbra].mean()):.4f} <= without {float(acc_screen[umbra].mean()):.4f}', 'hint: ')
check(float(acc_map[umbra].mean()) < 0.1, f'and with the map the box\'s umbra is dark ({float(acc_map[umbra].mean()):.4f} < 0.1)', 'hint: ')
ps.release(); sc.release()


# --- cost -------------------------------------------------------------------------------------------------
print('\n== cost: the ratio pass + its resolve (ms), median of the per-frame minima over rounds ==')
costs = {}
VARIANTS_COST = (('6 steps, quad (the default)', 6, False), ('4 steps, quad', 4, False), ('floor tier (1 ray per pixel), 6 steps', 6, True))
for (W, H) in RESOLUTIONS:
    for name in ('bedroom', 'club'):
        sc, lights = make_scene(name, W, H)
        ps = Pass(sc, lights)
        rs = RatioResolve(sc)
        for f in range(5):
            rs.run(ps.t_raw); ps.ratio(f)
        for label, steps, floor in VARIANTS_COST:
            meds = []
            for rnd in range(3):
                ts = []
                for f in range(FRAMES):
                    def go():
                        ps.bind(ps.p_ratio, 200 + f, floor, steps); bindfbo(FB, ps.f_raw); viewport(0, 0, W, H); draw(4, 0, 3)
                        rs.run(ps.t_raw)
                    ts.append(tq(go))
                meds.append(statistics.median(ts))
            costs[(W, H, name, label)] = statistics.median(meds)
            print(f'  {W}x{H} {name}, {label}: {costs[(W, H, name, label)]:.3f} ms')
        def only_ratio():
            ps.bind(ps.p_ratio, 7, False, 6); bindfbo(FB, ps.f_raw); viewport(0, 0, W, H); draw(4, 0, 3)
        split = [statistics.median([min(tq(fn) for _ in range(3)) for _ in range(30)]) for fn in (only_ratio, lambda: rs.run(ps.t_raw))]
        print(f'  {W}x{H} {name}: of the default tier, the ratio pass {split[0]:.3f} ms and the resolve {split[1]:.3f} ms')
        rs.release(); ps.release(); sc.release()
survey = {('bedroom', 2560): 0.46, ('club', 2560): 0.63}
for (W, H, name, label), v in costs.items():
    if W == 2560 and label.startswith('4 steps'):
        print(f'  survey (1440p, {name}, 4 steps, quad): {survey[(name, W)]:.2f} ms; here {v:.3f} ms ({(v / survey[(name, W)] - 1) * 100:+.0f} %)')

print()
if failures:
    print('FAILED:'); [print('  ' + f) for f in failures]; sys.exit(1)
print('all gates passed')
