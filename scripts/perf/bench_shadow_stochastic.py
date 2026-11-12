# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU check and benchmark of the Stochastic shadow method, no viewer launch.

Reuses the scenes, depth maps, G-buffer and GL context of bench_shadow_legacy.py. Runs the shipped
shaders: the Stochastic shadow util (class1/deferred/shadowStochasticUtil.glsl) linked next to the
method-neutral block of shadowUtil.glsl built alone (SHADOW_COMMON_ONLY), the deferred sun light pass
(class2/deferred/sunLightF.glsl), and the temporal resolve and light map shaders with the stochastic
engine's resolve object. The shadow maps are read as plain depth (sampler objects with the compare mode
off), as the viewer sets them for the method.

Checks (stage M1, the estimator):
  * lit pixels read exactly 1 and the umbra of boxes at 1 m to 3.5 m exactly 0 at 1080p and 1440p;
  * the converged 10-90 % width of the penumbra of thin boxes at known heights against a disc light's
    (cascades 0 and 1, and a projector), within one map texel plus one pixel;
  * the Stochastic util links on its own, with and without the depth buffer behind the program
    (SHADOW_COMMON_NO_DEPTH, the build the forward programs take);
  * a lit slanted plane under a low sun (25, 15 degrees) reads exactly 1 everywhere: no self-shadowing.
Checks (stage M2, the resolve, the same shaders the viewer runs):
  * a static scene converges: the temporal standard deviation of the penumbra after the history is
    below 0.03 (the raw 8-ray pass is printed for comparison);
  * the 3 x 3 clamp snaps a lit area to exactly 1 and an umbra to exactly 0 in one frame, whatever the history held;
  * a history whose stored depth disagrees with the surface (a revealed surface) is rejected: this frame's rays alone;
  * the sun when its light changed (shadow_st_sun_valid 0) takes this frame's rays alone;
  * a camera move (a non-identity reprojection): the history is kept where its depth agrees with the surface
    and rejected where the move disoccluded it;
  * the compute and the fragment resolve give bit identical histories.
The viewer's uniform setup of the resolve is checked against the shaders' declarations by
scripts/tests/test_shadow_method_tables.py (the compute path once missed inv_proj).
Prints the cost of each pass against the Legacy sun pass and light-map blur.

Environment: ST_FRAMES (default 40, timing), SHADOW_BENCH_RES (as bench_shadow_legacy.py).
"""
import math, os, statistics, sys
from pathlib import Path
import numpy as np
_src = Path(__file__).with_name('bench_shadow_legacy.py').read_text()
exec(compile(_src.split('# --- timing + basic checks')[0], 'bench_prefix', 'exec'))

FRAMES = int(os.environ.get('ST_FRAMES', 40))
DEEP = root / SHADERS / 'class1/deferred/'
UTIL_ST = (DEEP / 'shadowStochasticUtil.glsl').read_text()
NOISE_ST = (DEEP / 'stochasticNoiseUtil.glsl').read_text()   # the core's noise, linked next to the util (unified lighting U1)
UTIL_COMMON = (root / UTIL_F).read_text()
RESOLVE_ST = (DEEP / 'shadowStochasticResolveF.glsl').read_text()
RESOLVE_C = (DEEP / 'shadowStochasticResolveC.glsl').read_text()
RESOLVE_UTIL = (DEEP / 'shadowStochasticResolveUtilF.glsl').read_text()
COMPOSE_ST = (DEEP / 'shadowStochasticComposeF.glsl').read_text()
ENGINE_RESOLVE = (DEEP / 'stochasticResolveUtilF.glsl').read_text()
COMMON_DEFINES = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
bindsampler_ = bindsampler


def stoch_program(spot, depth=True, consts=None):
    """The deferred sun light pass with the Stochastic shadow util (the viewer's STOCHASTIC_SHADOW_SUN programs)."""
    sun = (root / SUN_F).read_text()
    defines = ['SUN_SHADOW'] + (['SPOT_SHADOW'] if spot else [])
    consts = consts or S2_DEFINES
    common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + consts + ([] if depth else ['SHADOW_COMMON_NO_DEPTH'])
    return program([(0x8B31, VERTEX), (0x8B30, stage(sun, [])), (0x8B30, stage(UTIL_COMMON, defines + common)),
                    (0x8B30, stage(UTIL_ST, defines + consts)), (0x8B30, stage(NOISE_ST, [])), (0x8B30, STUBS_F)])


def bind_stoch(prog, sc, light_size, frame):
    sc.bind(prog, light_size)
    for unit in range(2, 8):
        bindsampler(unit, depth_fetch.value)       # shadowMap0-5: plain depth, compare off
    u1i(loc(prog, b'shadow_st_frame'), frame); u1f(loc(prog, b'spot_shadow_reach'), 128.0)
    u2f(loc(prog, b'spot_emitter_radius'), 0.075, 0.075); u2f(loc(prog, b'spot_shadow_excl'), 0.075, 0.075)   # lllocallight.h: PROJECTOR_EMITTER_U0


def cascade_texels(sc):
    out = []
    for m in sc.cascades:
        ax = math.sqrt(sum(v * v for v in m[0][:3])); ay = math.sqrt(sum(v * v for v in m[1][:3]))
        out.append(max(2.0 / ax / sc.sw, 2.0 / ay / sc.sh))
    return out


queries = (U * 1)(); genq(1, queries)
def timed(draw_once, frames=FRAMES):
    s = []
    for f in range(WARMUP + frames):
        beginq(TIME_ELAPSED, queries[0]); draw_once(f); endq(TIME_ELAPSED)
        v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v))
        if f >= WARMUP: s.append(v.value / 1e6)
    finish(); assert geterror() == 0
    return statistics.median(s)


def read_rgba(fbo, w, h):
    bindfbo(FB, fbo); readbuffer(COLOR0)
    buf = np.empty((h, w, 4), np.float32); readpixels(0, 0, w, h, 0x1908, 0x1406, buf.ctypes.data_as(C.c_void_p))
    return buf


class Resolve:
    """The viewer's resolve and light map passes: raw rays + history -> history (the other texture) -> light map."""
    def __init__(self, sc, compute=False):
        self.sc = sc; W, H = sc.W, sc.H; self.compute = compute
        engine = stage(ENGINE_RESOLVE, [])
        util = stage(RESOLVE_UTIL, [])
        if compute:   # A/B only (VKSTORM_STOCH_COMPUTE_RESOLVE=1): the compute resolve, the history written as an image
            self.prog = program([(0x91B9, stage(RESOLVE_C, [])), (0x91B9, util), (0x91B9, engine)])
        else:         # the shipped default on every device: the fragment resolve into the history's framebuffer
            self.prog = program([(0x8B31, VERTEX), (0x8B30, stage(RESOLVE_ST, [])), (0x8B30, util), (0x8B30, engine)])
        self.compose = program([(0x8B31, VERTEX), (0x8B30, stage(COMPOSE_ST, []))])
        self.hist = [make_tex(0x881A, W, H) for _ in range(2)]
        self.fbo = [make_fbo([t]) for t in self.hist]
        self.out = make_tex(0x881A, W, H); self.out_fbo = make_fbo([self.out])
        self.idx = 0; self.valid = False
        self.reset_uniforms((1.0, 1.0, 1.0))

    def reset_uniforms(self, channel_valid):
        use(self.prog)
        set_mat(self.prog, 'stoch_reproj', [[[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]])
        u1f(loc(self.prog, b'stoch_weight'), 0.125)
        u1f(loc(self.prog, b'shadow_st_sun_valid'), channel_valid[0])   # the sun's channel only: the projectors' are the local light ratio's
        set_mat(self.prog, 'inv_proj', [inverse(self.sc.proj)])
        for unit, name in enumerate(('shadow_st_raw', 'shadow_st_history', 'depthMap')):
            u1i(loc(self.prog, name.encode()), unit)
        self.channel_valid = channel_valid

    def run(self, raw=None, history_valid=None):
        """One frame: raw (default the scene's light map) -> history; returns nothing, the latest is self.latest."""
        sc = self.sc
        raw = raw or sc.light_map
        use(self.prog)
        u1i(loc(self.prog, b'stoch_history_valid'), 1 if (self.valid if history_valid is None else history_valid) else 0)
        for unit, tex in enumerate((raw, self.hist[self.idx], sc.gdepth)):
            active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0)
        active(0x84C0)
        if self.compute:
            fn('glBindImageTexture', None, U, U, I, C.c_ubyte, I, U, U)(0, self.hist[1 - self.idx], 0, 0, 0, 0x88B9, 0x881A)
            fn('glDispatchCompute', None, U, U, U)((sc.W + 15) // 16, (sc.H + 15) // 16, 1)
            fn('glMemoryBarrier', None, U)(0x8 | 0x20 | 0x400)   # texture fetch, image access (the engine's endComputeWrite), framebuffer (the bench's read)
        else:
            bindfbo(FB, self.fbo[1 - self.idx]); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
        self.idx = 1 - self.idx; self.valid = True

    def light_map(self, raw=None):
        sc = self.sc
        use(self.compose)
        for unit, (name, tex) in enumerate((('shadow_st_raw', raw or sc.light_map), ('shadow_st_resolved', self.hist[self.idx]))):
            active(0x84C0 + unit); bindtex(TEX2D, tex); u1i(loc(self.compose, name.encode()), unit); bindsampler(unit, 0)
        active(0x84C0)
        bindfbo(FB, self.out_fbo); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)

    def read_history(self):
        return read_rgba(self.fbo[self.idx], self.sc.W, self.sc.H)

    def release(self):
        for p in (self.prog, self.compose): fn('glDeleteProgram', None, U)(p)
        delfbo(3, (U * 3)(*self.fbo, self.out_fbo)); deltex(3, (U * 3)(*self.hist, self.out))


# --- the util links on its own, with and without the depth buffer behind it ----------------------------
STOCH_MAIN = '''#version 430 core
in vec2 vary_fragcoord;
layout(location = 0) out vec4 out_color;
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen);
void main() { out_color = vec4(sampleDirectionalShadow(vec3(0.0, 0.0, -5.0), vec3(0.0, 0.0, 1.0), vary_fragcoord),
                               sampleSpotShadow(vec3(0.0, 0.0, -5.0), vec3(0.0, 0.0, 1.0), 0, vary_fragcoord), 0.0, 1.0); }
'''
print('stochastic: the util on its own')
for spot in (True, False):
    for depth in (True, False):
        defines = ['SUN_SHADOW'] + (['SPOT_SHADOW'] if spot else [])
        common = COMMON_DEFINES + ([] if depth else ['SHADOW_COMMON_NO_DEPTH'])
        # the build without the depth buffer links with no getPosition at all (a program without deferredUtil.glsl)
        parts = [(0x8B31, VERTEX), (0x8B30, STOCH_MAIN), (0x8B30, stage(UTIL_COMMON, defines + common)), (0x8B30, stage(UTIL_ST, defines + S2_DEFINES)), (0x8B30, stage(NOISE_ST, []))]
        if depth: parts.append((0x8B30, STUBS_F))
        try:
            fn('glDeleteProgram', None, U)(program(parts)); ok = True
        except AssertionError as e:
            ok = False; print(e)
        check(ok, f'links at RenderShadowDetail {2 if spot else 1}, {"with" if depth else "without"} the depth buffer', 'stochastic: ')

RES = {}
TIMING_BOXES = [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)), ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)), ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0))]
TIMING_SPOTS = [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])]
for (W, H) in RESOLUTIONS:
    sc = Scene(W, H, TIMING_BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
    print(f'{W}x{H}: sun maps {sc.sw}x{sc.sh}, cascade texels (cm) ' + ', '.join(f'{100*t:.2f}' for t in cascade_texels(sc)))
    for spot in (True, False):
        d = 2 if spot else 1
        for variant in ('tree',):
            prog = sun_program(variant, spot); sc.bind(prog, LIGHT_SIZE)
            RES[(W, H, d, variant)] = timed(lambda f: draw(4, 0, 3)); sc.unbind(); fn('glDeleteProgram', None, U)(prog)
        prog = stoch_program(spot)
        bind_stoch(prog, sc, LIGHT_SIZE, 1)
        RES[(W, H, d, 'stoch')] = timed(lambda f: (u1i(loc(prog, b'shadow_st_frame'), f + 1), draw(4, 0, 3)))
        sc.unbind(); fn('glDeleteProgram', None, U)(prog)
    # the light-map blur Legacy runs with SSAO (two passes, viewer defaults)
    blur = program([(0x8B31, VERTEX), (0x8B30, stage((DEEP / 'blurLightF.glsl').read_text(), [])), (0x8B30, STUBS_F)])
    tmp = make_tex(0x881A, W, H); tfbo = make_fbo([tmp])
    def gauss(x, s): return math.exp(-x * x / (2 * s * s)) / math.sqrt(2 * math.pi * s * s) if s > 0 else (1.0 if x == 0 else 0.0)
    kern = [x for i in range(4) for x in (gauss(i, 3.0), gauss(i, 2.0), float(i))]
    use(blur)
    for unit, (name, tex) in enumerate((('gbuf_pos', sc.gpos), ('normalMap', sc.gnorm), ('lightMap', sc.light_map))):
        active(0x84C0 + unit); bindtex(TEX2D, tex); u1i(loc(blur, name.encode()), unit)
    active(0x84C0)
    fn('glUniform3fv', None, I, I, C.POINTER(F))(loc(blur, b'kern'), 4, (F * 12)(*kern))
    u1f(loc(blur, b'kern_scale'), 1.4 * 1.5); u1f(loc(blur, b'dist_factor'), 0.0); u2f(loc(blur, b'screen_res'), W, H)
    def blur_twice(f):
        u2f(loc(blur, b'delta'), 1.0, 0.0); bindfbo(FB, tfbo); draw(4, 0, 3)
        u2f(loc(blur, b'delta'), 0.0, 1.0); bindfbo(FB, sc.lfbo); draw(4, 0, 3)
    viewport(0, 0, W, H)
    RES[(W, H, 0, 'blur')] = timed(blur_twice)
    fn('glDeleteProgram', None, U)(blur); deltex(1, (U * 1)(tmp)); delfbo(1, (U * 1)(tfbo))
    # the resolve and the light map pass
    rs = Resolve(sc)
    RES[(W, H, 0, 'resolve')] = timed(lambda f: rs.run())
    RES[(W, H, 0, 'compose')] = timed(lambda f: rs.light_map())
    rs.release()
    rs = Resolve(sc, compute=True)
    RES[(W, H, 0, 'resolve compute')] = timed(lambda f: rs.run())
    rs.release()

    # lit exactly 1, umbra exactly 0 (frame 1, 8 rays)
    prog = stoch_program(True); bind_stoch(prog, sc, LIGHT_SIZE, 1)
    draw(4, 0, 3); finish(); img = read_rgba(sc.lfbo, W, H)[:, :, 0]; sc.unbind()
    for p, want in (((-10.0, 0.0, -8.0), 1.0), ((8.0, 0.0, -25.0), 1.0), ((-8.0, 0.0, -30.0), 1.0)):
        x, y = sc.project(p); check(img[y, x] == want, f'{W}x{H} lit {p}: {img[y, x]:.4f} == {want}', 'stochastic: ')
    for lo, hi in TIMING_BOXES[:2]:
        c = [(lo[0]+hi[0])/2, lo[1], (lo[2]+hi[2])/2]; s = c[1] / sc.sun[1]
        p = (c[0] - sc.sun[0]*s, 0.0, c[2] - sc.sun[2]*s); x, y = sc.project(p)
        check(img[y, x] < 1e-3, f'{W}x{H} umbra {tuple(round(v, 2) for v in p)}: {img[y, x]:.4f} == 0', 'stochastic: ')

    # the resolve: clamp snap, disocclusion, light change (on the first resolution)
    if (W, H) == RESOLUTIONS[0]:
        rs = Resolve(sc); bind_stoch(prog, sc, LIGHT_SIZE, 1); draw(4, 0, 3)
        raw = read_rgba(sc.lfbo, W, H)
        lit = [sc.project(p) for p in ((-10.0, 0.0, -8.0), (8.0, 0.0, -25.0))]
        umbra = []
        for lo, hi in TIMING_BOXES[:2]:
            c = [(lo[0]+hi[0])/2, lo[1], (lo[2]+hi[2])/2]; s = c[1] / sc.sun[1]
            umbra.append(sc.project((c[0] - sc.sun[0]*s, 0.0, c[2] - sc.sun[2]*s)))
        # (a) a history of 0.5 everywhere, at the right depth: lit and umbra areas snap to this frame's value in one frame
        def fill_history(level, depth_scale):
            gp = read_rgba(sc.gfbo, W, H)       # view space positions
            data = np.empty((H, W, 4), np.float32); data[..., 0] = level; data[..., 1] = -gp[..., 2] * depth_scale; data[..., 2] = level; data[..., 3] = level
            bindtex(TEX2D, rs.hist[rs.idx]); fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, 0, 0, W, H, 0x1908, 0x1406, data.ctypes.data_as(C.c_void_p))
        for label, scale, channel_valid, expect in (('clamp snap', 1.0, (1.0, 1.0, 1.0), 'snap'), ('depth mismatch', 3.0, (1.0, 1.0, 1.0), 'raw'),
                                                    ('light changed', 1.0, (0.0, 1.0, 1.0), 'raw')):
            rs.reset_uniforms(channel_valid); fill_history(0.5, scale); rs.valid = True
            rs.run(raw=sc.light_map, history_valid=True)
            out = rs.read_history()
            for (x, y) in lit + umbra:
                want = raw[y, x, 0]
                if expect == 'snap':
                    check(out[y, x, 0] == want and want in (0.0, 1.0), f'{label}: resolved sun at ({x},{y}) = {out[y, x, 0]:.4f}, this frame {want:.4f}', 'resolve: ')
                else:
                    check(abs(out[y, x, 0] - want) < 1e-3, f'{label}: resolved sun at ({x},{y}) = {out[y, x, 0]:.4f}, this frame {want:.4f}', 'resolve: ')
        rs.release()
    fn('glDeleteProgram', None, U)(prog)
    sc.release()

# --- a lit slanted plane under a low sun reads exactly 1: no self-shadowing -------------------------------
W, H = RESOLUTIONS[0]
FAR_BOX = [((60.0, 0.0, 60.0), (61.0, 1.0, 61.0))]    # outside every cascade: the ground is all lit
# also with the constant bias cut to a quarter texel and no normal offset: the per texel plane alone keeps a lit plane lit
WEAK_BIAS = [d for d in S2_DEFINES if not d.startswith(('SHADOW_BIAS_TEXELS', 'SHADOW_NORMAL_OFFSET_TEXELS'))] + ['SHADOW_BIAS_TEXELS=0.25', 'SHADOW_NORMAL_OFFSET_TEXELS=0.0']
for elevation, consts, label in [(40.0, None, ''), (25.0, None, ''), (15.0, None, ''), (25.0, WEAK_BIAS, ' (bias 0.25 texel, no normal offset)'), (15.0, WEAK_BIAS, ' (bias 0.25 texel, no normal offset)')]:
    e = math.radians(elevation)
    sc = Scene(W, H, FAR_BOX, [0.0, 3.0, 4.0], [0.0, 0.0, -14.0], [-math.cos(e), math.sin(e), 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
    prog = stoch_program(True, consts=consts)
    worst = 1.0; dark = 0; ground = 0
    gpos = read_rgba(sc.gfbo, W, H)
    for frame in (1, 2, 3, 4):
        bind_stoch(prog, sc, LIGHT_SIZE, frame); draw(4, 0, 3); finish()
        img = read_rgba(sc.lfbo, W, H)[:, :, 0]
        ground_mask = (np.abs(gpos[..., 1]) < 1.0e30) & (gpos[..., 3] == 1.0)
        # the ground is the surface the harness's view space position maps to world y = 0
        wy = (np.linalg.inv(np.array(sc.view, np.float64)) @ np.concatenate([gpos[..., :3].reshape(-1, 3), np.ones((W * H, 1))], axis=1).T).T[:, 1].reshape(H, W)
        ground_mask = ground_mask & (np.abs(wy) < 0.02) & (-gpos[..., 2] < 60.0)
        ground = int(ground_mask.sum()); dark += int((img[ground_mask] < 1.0).sum()); worst = min(worst, float(img[ground_mask].min()))
    check(ground > 100000 and dark == 0, f'sun elevation {elevation:g} degrees{label}: {ground} lit ground pixels, {dark} below 1 (min {worst:.4f})', 'low sun: ')
    fn('glDeleteProgram', None, U)(prog); sc.release()

# --- the forward programs: 8 fixed rays per layer against the stock PCF, full screen layers -----------------
FWD_MAIN = '''#version 430 core
in vec2 vary_fragcoord;
layout(location = 0) out vec4 out_color;
uniform sampler2D gbuf_pos;
uniform sampler2D normalMap;
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
void main()
{
    vec4 p = texture(gbuf_pos, vary_fragcoord);
    vec3 n = texture(normalMap, vary_fragcoord).xyz;
    out_color = vec4(sampleDirectionalShadow(p.xyz, n, vary_fragcoord));
}
'''
for (W, H) in RESOLUTIONS:
    sc = Scene(W, H, TIMING_BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
    for label, parts in (('stock PCF', [(0x8B30, stage(UTIL_COMMON, ['SUN_SHADOW']))]),
                         ('Stochastic, 8 fixed rays', [(0x8B30, stage(UTIL_COMMON, ['SUN_SHADOW'] + COMMON_DEFINES + ['SHADOW_COMMON_NO_DEPTH'])), (0x8B30, stage(UTIL_ST, ['SUN_SHADOW'] + S2_DEFINES)), (0x8B30, stage(NOISE_ST, []))])):
        prog = program([(0x8B31, VERTEX), (0x8B30, FWD_MAIN)] + parts)
        sc.bind(prog, LIGHT_SIZE)
        for unit in range(2, 8): bindsampler(unit, depth_fetch.value if label.startswith('Stoch') else 0)
        u1i(loc(prog, b'shadow_st_frame'), 0)
        for unit, (name, tex) in enumerate((('gbuf_pos', sc.gpos), ('normalMap', sc.gnorm)), start=8):
            active(0x84C0 + unit); bindtex(TEX2D, tex); u1i(loc(prog, name.encode()), unit); bindsampler(unit, 0)
        active(0x84C0)
        for layers in (1, 4):
            RES[(W, H, 'fwd', label, layers)] = timed(lambda f: [draw(4, 0, 3) for _ in range(layers)])
        sc.unbind(); fn('glDeleteProgram', None, U)(prog)
    sc.release()

# --- a camera move: the reprojected history is kept where its depth agrees, rejected where disoccluded --------
W, H = RESOLUTIONS[0]
scA = Scene(W, H, TIMING_BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
gposA = read_rgba(scA.gfbo, W, H)
scB = Scene(W, H, TIMING_BOXES, [1.2, 8.0, 4.0], [1.2, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
gposB = read_rgba(scB.gfbo, W, H)
prog = stoch_program(True); bind_stoch(prog, scB, LIGHT_SIZE, 3); draw(4, 0, 3); finish()
rawB = read_rgba(scB.lfbo, W, H)
rs = Resolve(scB)
# this frame's clip space to the last frame's, as LLStochasticHistory::reprojection: last proj x last view x inverse(proj x view)
PV_A = np.array(mmul(scA.proj, scA.view), np.float64); PV_B = np.array(mmul(scB.proj, scB.view), np.float64)
reproj = PV_A @ np.linalg.inv(PV_B)
use(rs.prog); set_mat(rs.prog, 'stoch_reproj', [reproj.tolist()])
wA = -gposA[..., 2].astype(np.float64)
data = np.zeros((H, W, 4), np.float32); data[..., 0] = 0.5; data[..., 1] = wA; data[..., 2] = 0.5; data[..., 3] = 0.5
bindtex(TEX2D, rs.hist[rs.idx]); fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, 0, 0, W, H, 0x1908, 0x1406, data.ctypes.data_as(C.c_void_p))
rs.run(raw=scB.light_map, history_valid=True)
out = rs.read_history()
# numpy model of which pixels find their history: the surface's reprojection against the stored depth
yy, xx = np.mgrid[0:H, 0:W]
clip = np.stack([(xx + 0.5) / W * 2 - 1, (yy + 0.5) / H * 2 - 1, np.zeros_like(xx, np.float64), np.ones_like(xx, np.float64)], axis=-1)
depthB = np.array(read_rgba(scB.gfbo, W, H))   # position buffer: used for the expected depth
wB = -gposB[..., 2].astype(np.float64)
world = (np.linalg.inv(np.array(scB.view, np.float64)) @ np.concatenate([gposB[..., :3].reshape(-1, 3).astype(np.float64), np.ones((W * H, 1))], axis=1).T).T
last = (np.array(scA.view, np.float64) @ world.T).T
w_expected = (-last[:, 2]).reshape(H, W)
lc = (np.array(scA.proj, np.float64) @ last.T).T
u = (lc[:, 0] / lc[:, 3] * 0.5 + 0.5).reshape(H, W) * W; v = (lc[:, 1] / lc[:, 3] * 0.5 + 0.5).reshape(H, W) * H
inside = (u >= 1) & (u < W - 1) & (v >= 1) & (v < H - 1) & (wB < 150.0) & (wB > 0.5)
bx = np.floor(u - 0.5).astype(int); by = np.floor(v - 0.5).astype(int)
tap_agree = []
for oy in (0, 1):
    for ox in (0, 1):
        tx = np.clip(bx + ox, 0, W - 1); ty = np.clip(by + oy, 0, H - 1)
        tap_agree.append(np.abs(wA[ty, tx] - w_expected) <= 0.02 * w_expected + 0.02)   # the shader's test, per bilinear tap
tap_agree = np.stack(tap_agree)
kept_region = inside & tap_agree.all(axis=0); rejected_region = inside & ~tap_agree.any(axis=0)
cur = rawB[..., 0]
lo = cur.copy(); hi = cur.copy()
for dy in (-1, 0, 1):
    for dx in (-1, 0, 1):
        sh = np.roll(np.roll(cur, dy, 0), dx, 1); lo = np.minimum(lo, sh); hi = np.maximum(hi, sh)
penumbra = (lo < 0.5) & (hi > 0.5)                     # the clamp box holds the history's 0.5
kept = kept_region & penumbra; rejected = rejected_region & penumbra
want_kept = 0.5 * 0.875 + cur * 0.125
print(f'  info camera move: {int(kept.sum())} penumbra pixels with agreeing history, {int(rejected.sum())} with a disoccluded one')
check(kept.sum() > 20 and np.max(np.abs(out[..., 0][kept] - want_kept[kept])) < 2.0e-3, f'camera move: the reprojected history is blended where its depth agrees (max error {np.max(np.abs(out[..., 0][kept] - want_kept[kept])) if kept.sum() else -1:.4f} over {int(kept.sum())} pixels)', 'resolve: ')
check(rejected.sum() > 20 and np.max(np.abs(out[..., 0][rejected] - cur[rejected])) < 1.0e-6, f'camera move: a disoccluded pixel takes this frame alone ({int(rejected.sum())} pixels)', 'resolve: ')
rs.release(); fn('glDeleteProgram', None, U)(prog); scA.release(); scB.release()

# --- the compute and the fragment resolve give the same history, frame after frame ------------------------
W, H = RESOLUTIONS[0]
sc = Scene(W, H, TIMING_BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
prog = stoch_program(True)
frag, comp = Resolve(sc), Resolve(sc, compute=True)
worst = 0.0; same = True
for f in range(1, 25):
    bind_stoch(prog, sc, LIGHT_SIZE, f); draw(4, 0, 3)
    frag.run(); comp.run()
    a, b = frag.read_history(), comp.read_history()
    same = same and np.array_equal(a, b); worst = max(worst, float(np.max(np.abs(a - b))))
check(same, f'compute and fragment resolve: the history is bit identical over 24 frames (max difference {worst:g})', 'resolve: ')
frag.release(); comp.release(); fn('glDeleteProgram', None, U)(prog); sc.release()

# --- penumbra: converged width and temporal noise, through the real resolve ---------------------------------
PEN_L = 0.1; PEN_SPOT_SIZE = 0.15; T = 0.05
PEN_CASES = [('cascade 0', (-1.5, -0.3), (-3.0, -1.5), 1.0), ('cascade 0', (0.3, 1.5), (-3.0, -1.5), 2.0),
             ('cascade 1', (-1.5, -0.3), (-14.0, -12.0), 1.5), ('cascade 1', (0.3, 1.5), (-14.0, -12.0), 3.0),
             ('spot', (3.4, 4.2), (-2.6, -1.8), 1.0), ('spot', (5.8, 6.6), (-2.6, -1.8), 1.5)]
SPOT_EYE = [5.0, 8.0, -2.2]
pen_boxes = [((x[0], h, z[0]), (x[1], h + T, z[1])) for _, x, z, h in PEN_CASES]
W, H = RESOLUTIONS[0]
sc = Scene(W, H, pen_boxes, [0.0, 10.0, 6.0], [0.0, 0.0, -10.0], [0.0, 1.0, 0.0],
           [(SPOT_EYE, [SPOT_EYE[0], 0.0, SPOT_EYE[2]]), ([30.0, 8.0, 30.0], [30.0, 0.0, 30.0])], (16.0, 40.0, 80.0, 150.0))
prog = stoch_program(True)
print(f'\npenumbra scene {W}x{H}, L = {PEN_L}: cascade texels (cm) ' + ', '.join(f'{100*t:.2f}' for t in cascade_texels(sc)))
rs = Resolve(sc)
N_FR = 256
mean = None; s1 = None; s2 = None; raw_s1 = None; raw_s2 = None; n_tail = 0
for f in range(1, N_FR + 1):
    bind_stoch(prog, sc, PEN_L, f); draw(4, 0, 3)
    raw_img = read_rgba(sc.lfbo, W, H)[:, :, [0, 2]].astype(np.float64)
    rs.run()
    img = rs.read_history()[:, :, [0, 2]].astype(np.float64)
    mean = raw_img if mean is None else mean + raw_img
    if f > N_FR - 128:
        s1 = img if s1 is None else s1 + img; s2 = img * img if s2 is None else s2 + img * img
        raw_s1 = raw_img if raw_s1 is None else raw_s1 + raw_img; raw_s2 = raw_img * raw_img if raw_s2 is None else raw_s2 + raw_img * raw_img
        n_tail += 1
mean /= N_FR
ema_std = np.sqrt(np.maximum(s2 / n_tail - (s1 / n_tail) ** 2, 0))
raw_std = np.sqrt(np.maximum(raw_s2 / n_tail - (raw_s1 / n_tail) ** 2, 0))
u1i(loc(prog, b'shadow_st_frame'), 0); draw(4, 0, 3); fixed = read_rgba(sc.lfbo, W, H)[:, :, [0, 2]].astype(np.float64)
sc.unbind()
for name, xr, zr, h in PEN_CASES:
    ch = 1 if name == 'spot' else 0
    if name == 'spot':
        d_r = SPOT_EYE[1]; d_b = SPOT_EYE[1] - h; w = PEN_SPOT_SIZE * (d_r - d_b) / d_b
        edge = SPOT_EYE[2] + (zr[1] - SPOT_EYE[2]) * d_r / d_b; texel = 2.0 * d_r * math.tan(math.radians(30.0)) / sc.spot_res
    else:
        w = h * PEN_L; edge = zr[1]; texel = cascade_texels(sc)[int(name[-1])]
    xs_mid = [xr[0] + 0.15 + (xr[1] - xr[0] - 0.3) * k / 31 for k in range(32)]
    if name == 'spot': xs_mid = [SPOT_EYE[0] + (x - SPOT_EYE[0]) * d_r / d_b for x in xs_mid]
    zs = [edge - 0.3 + 0.002 * i for i in range(301)]
    prof, estd, rstd, fdev = [], [], [], []
    for gz in zs:
        vals = []
        for gx in xs_mid:
            x, y = sc.project((gx, 0.0, gz)); vals.append((mean[y, x, ch], ema_std[y, x, ch], raw_std[y, x, ch], abs(fixed[y, x, ch] - mean[y, x, ch])))
        m = sum(v[0] for v in vals) / len(vals); prof.append(m)
        if 0.1 < m < 0.9:
            estd += [v[1] for v in vals]; rstd += [v[2] for v in vals]; fdev += [v[3] for v in vals]
    lo10 = next(zs[i] for i, p in enumerate(prof) if p >= 0.1); hi90 = next(zs[i] for i, p in enumerate(prof) if p >= 0.9)
    xm = sum(xs_mid) / len(xs_mid); pa, pb = sc.project((xm, 0.0, edge)), sc.project((xm, 0.0, edge + 0.2))
    pixel = 0.2 / math.hypot(pb[0] - pa[0], pb[1] - pa[1])
    measured = hi90 - lo10; expected = DISC_10_90 * w; tol = texel + pixel
    # the converged width is about 0.85 of a disc light's (the quadratic node spacing); a bound of 25 % under or over
    relative = abs(measured / expected - 1.0) <= 0.25 if name != 'spot' else True
    check(prof[0] < 0.02 and prof[-1] > 0.98 and abs(measured - expected) <= tol and relative,
          f'{name}, h {h}: converged 10-90 % {100*measured:.1f} cm, disc {100*expected:.1f} cm (+-{100*tol:.1f}, ratio {measured/expected:.2f}); ends {prof[0]:.3f}/{prof[-1]:.3f}', 'penumbra: ')
    if not estd:
        print(f'  info {name}, h {h}: no pixel of the mean profile between 0.1 and 0.9 (the penumbra is under {100*0.002:.1f} cm wide)'); continue
    check(np.mean(estd) < 0.03, f'{name}, h {h}: penumbra temporal std after the history {np.mean(estd):.3f} (max {np.max(estd):.3f}) < 0.03; raw {np.mean(rstd):.3f}', 'resolve: ')
    print(f'  info {name}, h {h}: fixed seeds |dev| from converged {np.mean(fdev):.3f} (max {np.max(fdev):.3f})')
rs.release(); fn('glDeleteProgram', None, U)(prog); sc.release()

print(f'\nmedian ms, {FRAMES} frames after {WARMUP} warmup')
print(f'{"res":<11}{"detail":<8}{"tree":>8}{"stoch":>8}   (the sun light pass; tree = Legacy)')
for (W, H) in RESOLUTIONS:
    for d in (2, 1):
        print(f'{f"{W}x{H}":<11}{d:<8}' + ''.join(f'{RES[(W, H, d, l)]:>8.3f}' for l in ('tree', 'stoch')))
    comp = RES[(W, H, 0, 'resolve compute')]; frag = RES[(W, H, 0, 'resolve')]; compose = RES[(W, H, 0, 'compose')]; blur = RES[(W, H, 0, 'blur')]
    sun_legacy = RES[(W, H, 2, 'tree')]; sun_st = RES[(W, H, 2, 'stoch')]
    legacy = sun_legacy                      # Legacy pays its blur only with SSAO on
    print(f'{"":<19}resolve: fragment (the shipped path on every device) {frag:.3f}, compute (A/B only, GL 4.3+) {comp:.3f}; light map pass {compose:.3f}; blur {blur:.3f}')
    for path, resolve in (('fragment (shipped)', frag), ('compute (A/B)', comp)):
        off = sun_st + resolve + compose
        print(f'{"":<19}{path} resolve: Stochastic {off:.3f} ms vs Legacy sun pass {legacy:.3f} ms: {off - legacy:+.3f} ms'
              f' (with SSAO on both run the blur, {blur:.3f} ms)')
print(f'\nforward programs (water, particles, glass layers: a full screen layer each), 8 fixed rays vs the stock PCF, ms per frame')
for (W, H) in RESOLUTIONS:
    for layers in (1, 4):
        stock = RES[(W, H, 'fwd', 'stock PCF', layers)]; st = RES[(W, H, 'fwd', 'Stochastic, 8 fixed rays', layers)]
        print(f'{f"{W}x{H}":<11}{layers} layer(s): stock PCF {stock:.3f}, Stochastic {st:.3f} ({st - stock:+.3f}); of a 60 fps frame {100 * (st - stock) / 16.67:.1f} %')
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
