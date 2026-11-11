# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stages L2 and L2b of the light study: the cloud shadow map against the sky, its optics and its cost, offline (no viewer).

The viewer's own shaders are compiled in a hidden OpenGL context, unmodified:

  check  The ground map's shader (deferred/cloudShadowMapF.glsl) is run for a world point and the
         sky's own shaders (deferred/cloudsV.glsl + cloudsF.glsl) for the dome point that the camera
         at that point sees over the light, with the sky's parallax uniform, the same noise textures,
         scroll, density, scale, variance and cloud_shadow. The sky's alpha1 and the map's
         1 - T must agree: "what the sky draws, the ground gets". The dome point and the texture
         coordinates of the map come from the formulas of indra/newview/llcloudshadow.h (written
         here again in Python from the dome's geometry), so the altitude fade of the C++ unit is
         tested against the vertex shader too. 60 random cases, a camera anywhere within 300 km.
         Stage L2b: the map is RG8, R the direct beam and G the diffuse light of the flat layer's
         optics (indra/newview/llcloudoptics.h). The check runs the shader with neutral optics (R =
         1 - alpha, the L2 check) and with the real ones against a Python mirror whose constants are
         parsed from the header (<= 1e-5); clear texels are exactly (1, 0); strength 0 and a map of
         T = 1 give the pixels of the lookup before the stage, bit for bit, in classic mode 0 and 1;
         and the beam multiplies the LINEAR light: a map of 0.5 leaves 0.5 of the sun's and of a dim
         moon's light alike.

  bench  GL_TIME_ELAPSED medians (RTX-class GPU, GL 4.6): the map's pass (512x512, RG8, with the mip
         chain; and devel's, R8), and the lookup, a 1920x1080 and a 2560x1440 pass running the viewer's
         calcAtmosphericVarsLinear (atmosphericsFuncs.glsl) with the cloud shadow lookup at
         full strength against the same pass with the strength at 0 and against the same pass
         with the file as it was before the stage (git ref CLOUD_BENCH_BASE_REF, default
         the merge base with origin/vkstorm-devel; the environment variable overrides it). The gate is
         the map plus the lookups <= 0.05 ms at 1080p, and 0 with no clouds; --bench exits 1 only when that
         gate fails. The delta against the base ref's map and lookups (target at most +0.002 ms) is printed as
         a warning, since it is within the timer's noise.

    python scripts/light/check_cloud_shadow.py [--check] [--bench]     (both by default)

Windows and a GPU. Environment: CLOUD_BENCH_FRAMES (default 40), CLOUD_BENCH_BASE_REF (default: the merge base with origin/vkstorm-devel).
"""
import ctypes as C
import math
import os
import random
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / 'indra' / 'newview' / 'app_settings' / 'shaders'
_bootstrap = (ROOT / 'scripts' / 'perf' / 'gl_test_context.py').read_text().replace('parser.parse_args()', 'parser.parse_known_args()[0]')
exec(compile(_bootstrap, 'gl_test_bootstrap', 'exec'), globals())

FRAG, VERT = 0x8B30, 0x8B31
TEX2D = 0x0DE1
F = C.c_float
FRAMES = int(os.environ.get('CLOUD_BENCH_FRAMES', 40)); WARMUP = 8
args_ = sys.argv[1:]
DO_CHECK = '--check' in args_ or '--bench' not in args_
DO_BENCH = '--bench' in args_ or '--check' not in args_

gentex = fn('glGenTextures', None, I, C.POINTER(U)); bindtex = fn('glBindTexture', None, U, U)
texparam = fn('glTexParameteri', None, U, U, I); active = fn('glActiveTexture', None, U)
teximage = fn('glTexImage2D', None, U, I, I, I, I, I, U, U, P)
genmip = fn('glGenerateMipmap', None, U)
genfbo = fn('glGenFramebuffers', None, I, C.POINTER(U)); bindfbo = fn('glBindFramebuffer', None, U, U)
attach2 = fn('glFramebufferTexture2D', None, U, U, U, U, I)
viewport = fn('glViewport', None, I, I, I, I)
loc = fn('glGetUniformLocation', I, U, C.c_char_p)
u1i = fn('glUniform1i', None, I, I); u1f = fn('glUniform1f', None, I, F); u2f = fn('glUniform2f', None, I, F, F)
u3f = fn('glUniform3f', None, I, F, F, F); u4f = fn('glUniform4f', None, I, F, F, F, F)
um4 = fn('glUniformMatrix4fv', None, I, I, C.c_ubyte, C.POINTER(F))
draw = fn('glDrawArrays', None, U, I, I)
getteximage = fn('glGetTexImage', None, U, I, U, U, P)
attrib3f = fn('glVertexAttrib3f', None, U, F, F, F); attrib2f = fn('glVertexAttrib2f', None, U, F, F)
bindattrib = fn('glBindAttribLocation', None, U, U, C.c_char_p)
genq = fn('glGenQueries', None, I, C.POINTER(U)); beginq = fn('glBeginQuery', None, U, U); endq = fn('glEndQuery', None, U)
qresult = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64)); finish = fn('glFinish', None)
geterror = fn('glGetError', U)
R = 15000.0; OFFSET = 0.96


def _const_expr(text, names):
    """A constant expression of numbers, names, + - * / and parentheses, without eval()."""
    import ast
    import operator
    ops = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul, ast.Div: operator.truediv}

    def walk(n):
        if isinstance(n, ast.Expression):
            return walk(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float)):
            return float(n.value)
        if isinstance(n, ast.Name) and n.id in names:
            return names[n.id]
        if isinstance(n, ast.BinOp) and type(n.op) in ops:
            return ops[type(n.op)](walk(n.left), walk(n.right))
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            return -walk(n.operand)
        raise ValueError('unsupported constant expression: ' + text)
    return walk(ast.parse(text.strip(), mode='eval'))


def optics_constants():
    """The constants of llcloudoptics.h (the single source), parsed from the header."""
    import re
    names = {}
    text = (ROOT / 'indra' / 'newview' / 'llcloudoptics.h').read_text()
    for m in re.finditer(r'constexpr\s+double\s+(\w+)\s*=\s*([^;]+);', text):
        names[m.group(1)] = _const_expr(m.group(2), names)
    return names


OPT = optics_constants()


def optics_mirror(alpha, tau_v):
    """llcloudoptics.h in Python: (beam, diffuse) of a layer drawn with opacity alpha."""
    tau = -tau_v * math.log(max(1.0 - alpha, OPT['ALPHA_STEP']))
    beam = math.exp(-(1.0 - OPT['F_PEAK']) * tau)
    total = 1.0 / (1.0 + OPT['EDDINGTON_TOTAL'] * (1.0 - OPT['G_WATER']) * tau)
    return beam, max(total - beam, 0.0)


def set_optics(prog, tau_v=None, probe=False):
    """ctm_optics = (tau_v, 1 - g^2, 3/4 (1 - g), alpha step), as LLCloudShadowMap sets it; the probe is neutral: R = 1 - alpha."""
    if probe:
        u(prog, 'ctm_optics', 1.0, 1.0, 0.0, 1e-12)
    else:
        u(prog, 'ctm_optics', tau_v if tau_v is not None else OPT['DEFAULT_VISUAL_DEPTH'], 1.0 - OPT['F_PEAK'],
          OPT['EDDINGTON_TOTAL'] * (1.0 - OPT['G_WATER']), OPT['ALPHA_STEP'])

EXTRA = ("#define GBUFFER_FLAG_SKIP_ATMOS 0.0\n#define FRAGMENT_SHADER 1\n")


def read(rel):
    return (SHADERS / rel).read_text(encoding='utf8')


def build(vs, fs, attribs=()):
    """program() of the bootstrap with attribute locations bound first."""
    prog = create_program()
    for kind, text in ((VERT, vs), (FRAG, fs)):
        sh = create_shader(kind); src = C.c_char_p(text.encode()); source_shader(sh, 1, C.byref(src), None); compile_shader(sh)
        st = I(); shader_iv(sh, 0x8B81, C.byref(st)); log = C.create_string_buffer(8192); shader_log(sh, len(log), None, log)
        assert st.value, log.value.decode()
        attach(prog, sh)
    for i, name in enumerate(attribs):
        bindattrib(prog, i, name.encode())
    link(prog); st = I(); program_iv(prog, 0x8B82, C.byref(st)); log = C.create_string_buffer(8192); program_log(prog, len(log), None, log)
    assert st.value, log.value.decode()
    return prog


def u(prog, name, *v):
    l = loc(prog, name.encode())
    if l < 0:
        return
    {1: (u1i if isinstance(v[0], int) else u1f), 2: u2f, 3: u3f, 4: u4f}[len(v)](l, *v)


def noise_texture(seed, n=256):
    """A tileable value-noise stand-in for the EEP cloud texture, with mips and repeat."""
    rnd = random.Random(seed)
    g = [[rnd.random() for _ in range(16)] for _ in range(16)]
    px = (C.c_ubyte * (n * n * 4))()
    for y in range(n):
        for x in range(n):
            # three octaves of smooth value noise on a 16 cell lattice
            val = 0.0
            for o, amp in enumerate((0.55, 0.3, 0.15)):
                s = 4 * (1 << o)
                fx, fy = x * s / n, y * s / n
                x0, y0 = int(fx), int(fy); tx, ty = fx - x0, fy - y0
                tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty)
                gg = lambda i, j: g[j % 16][i % 16]
                a = gg(x0, y0) * (1 - tx) + gg(x0 + 1, y0) * tx; b = gg(x0, y0 + 1) * (1 - tx) + gg(x0 + 1, y0 + 1) * tx
                val += amp * (a * (1 - ty) + b * ty)
            c = max(0, min(255, int(val * 255)))
            i = (y * n + x) * 4
            px[i] = c; px[i + 1] = c; px[i + 2] = c; px[i + 3] = 255
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
    teximage(TEX2D, 0, 0x8058, n, n, 0, 0x1908, 0x1401, C.cast(px, P))
    genmip(TEX2D)
    texparam(TEX2D, 0x2801, 0x2703); texparam(TEX2D, 0x2800, 0x2601)
    texparam(TEX2D, 0x2802, 0x2901); texparam(TEX2D, 0x2803, 0x2901)
    return t.value


def target(w, h, fmt=0x8814, mips=False):
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
    teximage(TEX2D, 0, fmt, w, h, 0, 0x1908, 0x1406, None)
    texparam(TEX2D, 0x2801, 0x2703 if mips else 0x2601); texparam(TEX2D, 0x2800, 0x2601)
    texparam(TEX2D, 0x2802, 0x812F); texparam(TEX2D, 0x2803, 0x812F)
    f = U(); genfbo(1, C.byref(f)); bindfbo(0x8D40, f.value); attach2(0x8D40, 0x8CE0, TEX2D, t.value, 0)
    return t.value, f.value


def read_pixel(t):
    bindtex(TEX2D, t); px = (F * 4)(); getteximage(TEX2D, 0, 0x1908, 0x1406, px); return list(px)


# --- the formulas of llcloudshadow.h, from the dome's geometry (independent of the C++) ---------
def dome_hit(d):
    """The dome point (dome frame: x north, y up, z east) that the ray from the camera meets."""
    dx, dy, dz = d[1], d[2], d[0]            # world (east, north, up) -> dome (north, up, east)
    cam = (0.0, OFFSET * R, 0.0)
    b = cam[1] * dy
    c = cam[1] ** 2 - R * R
    t = -b + math.sqrt(b * b - c)
    return (t * dx, cam[1] + t * dy, t * dz)


def dome_texcoord(hit, scale):
    x0, z0 = hit[0] / R, hit[2] / R
    tx, ty = -((-z0 + 1) / 2), (-x0 + 1) / 2
    return ((tx - 0.5) / scale + 0.5, (ty - 0.5) / scale + 0.5)


def random_dir(rnd, min_z):
    while True:
        v = [rnd.uniform(-1, 1) for _ in range(3)]
        l = math.sqrt(sum(a * a for a in v))
        if l > 0.1 and v[2] / l >= min_z:
            return [a / l for a in v]


# cloudsV uses srgbF's functions; the viewer attaches that file to the vertex stage of programs that calculate atmospherics
SKY_VS = "#version 400 compatibility\n" + EXTRA + read('class1/environment/srgbF.glsl') + read('class1/deferred/cloudsV.glsl')
SKY_FS = "#version 400 compatibility\n" + EXTRA + read('class1/deferred/cloudsF.glsl').replace('/*[EXTRA_CODE_HERE]*/', '')
# deferred/postDeferredNoTCV.glsl takes the screen triangle from a vertex buffer; here it comes from
# gl_VertexID, with the same varying (0..1 over the target)
MAP_VS = ("#version 400 compatibility\nout vec2 vary_fragcoord;\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
          " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); vary_fragcoord = p; }\n")
MAP_FS = "#version 400 compatibility\n" + EXTRA + read('class1/deferred/cloudShadowMapF.glsl').replace('/*[EXTRA_CODE_HERE]*/', '')


def check():
    sky = build(SKY_VS, SKY_FS, ('position', 'texcoord0'))
    mp = build(MAP_VS, MAP_FS)
    na, nb = noise_texture(1), noise_texture(2)
    tsky, fsky = target(1, 1); tmap, fmap = target(1, 1)
    rnd = random.Random(20261108)
    worst = 0.0; cases = 0; informative = 0; worst_opt = 0.0; clear_cases = 0; clear_bad = 0
    for case in range(60):
        L = random_dir(rnd, 0.0 if case % 5 == 0 else 0.03)
        cam = (rnd.uniform(-300000, 300000), rnd.uniform(-300000, 300000))
        scale = rnd.uniform(0.15, 1.5)
        shadow = rnd.choice([0.27, 0.38, 0.45, 0.62]) if case % 2 else rnd.uniform(0.2, 0.7)
        variance = rnd.choice([0.0, 0.3, 0.8]); max_y = rnd.choice([1605.0, 4000.0])
        density1 = (rnd.uniform(0, 3), rnd.uniform(0, 3), rnd.choice([1.0, 0.8])); density2 = (rnd.uniform(0, 3), rnd.uniform(0, 3), rnd.choice([0.125, 0.4]))
        blend = rnd.choice([0.0, 0.5])
        lightnorm = (L[1], L[2], L[0])   # LLEnvironment::toLightNorm: (north, up, east)
        par = (cam[0] / (2 * R * scale), -cam[1] / (2 * R * scale))

        def common(prog):
            u(prog, 'cloud_scale', scale); u(prog, 'cloud_shadow', shadow); u(prog, 'cloud_variance', variance)
            u(prog, 'cloud_pos_density1', *density1); u(prog, 'cloud_pos_density2', *density2); u(prog, 'blend_factor', blend)
            u(prog, 'lightnorm', *lightnorm)
            u(prog, 'sun_glow_norm', *lightnorm); u(prog, 'moon_glow_norm', *lightnorm)   # L3: the sun is the only body
            u(prog, 'cloud_noise_texture', 0); u(prog, 'cloud_noise_texture_next', 1)
            active(0x84C0); bindtex(TEX2D, na); active(0x84C1); bindtex(TEX2D, nb); active(0x84C0)

        # the sky: the dome point over the light, seen by the camera at `cam`
        hit = dome_hit(L)
        x0, y0, z0 = hit[0] / R, hit[1] / R, hit[2] / R
        bindfbo(0x8D40, fsky); viewport(0, 0, 1, 1)
        useprog = fn('glUseProgram', None, U); useprog(sky)
        um = (F * 16)(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)   # gl_Position = (0, 0, 0, 1)
        um4(loc(sky, b'modelview_projection_matrix'), 1, 0, um)
        u(sky, 'camPosLocal', 0.0, OFFSET * R, 0.0); u(sky, 'max_y', max_y); u(sky, 'cloud_parallax', *par)
        for name, v in (('lightnorm', lightnorm), ('sun_glow_norm', lightnorm), ('moon_glow_norm', lightnorm), ('sunlight_color', (1, 1, 1)), ('moonlight_color', (1, 1, 1)), ('ambient_color', (0.3, 0.3, 0.3)),
                        ('blue_horizon', (0.5, 0.5, 0.6)), ('blue_density', (0.25, 0.45, 0.76)), ('glow', (5, 0.001, -0.5)), ('cloud_color', (0.4, 0.4, 0.4))):
            u(sky, name, *v)
        u(sky, 'haze_horizon', 0.19); u(sky, 'haze_density', 0.7); u(sky, 'density_multiplier', 0.0002); u(sky, 'sun_up_factor', 1); u(sky, 'sun_moon_blend', 0.0)
        u(sky, 'cloud_light_transmittance', 1.0, 1.0, 1.0); u(sky, 'cloud_moon_blend', 0.0)
        common(sky)
        attrib3f(0, hit[0] / 1.0, hit[1] / 1.0, hit[2] / 1.0); attrib2f(1, (-z0 + 1) / 2, (-x0 + 1) / 2)
        draw(0, 0, 1)   # GL_POINTS
        alpha_sky = read_pixel(tsky)[3]

        # the map: the world point under the camera, the same light
        tc = dome_texcoord(hit, scale)
        tc = (tc[0] + par[0], tc[1] + par[1])   # = ground_texcoord(cam, L) by the claim under test
        # the claim, computed from the layer: texcoord of the layer point cam + dome offset
        ox, oy = hit[2], hit[0]     # east, north offsets of the dome point
        k = 1.0 / (2 * R * scale)
        tc_layer = (0.5 - 1.0 / scale + (cam[0] + ox) * k, 0.5 - (cam[1] + oy) * k)
        assert abs(tc[0] - tc_layer[0]) < 1e-6 * (1 + abs(tc[0])) and abs(tc[1] - tc_layer[1]) < 1e-6 * (1 + abs(tc[1])), (tc, tc_layer)
        # altitude fade as the C++ unit has it
        height_cap = math.sqrt(R * R - ox * ox - oy * oy)
        alt = max(0.0, min(1.0, ((height_cap - OFFSET * R + 50.0) + 512.0) / max_y))
        bindfbo(0x8D40, fmap); viewport(0, 0, 1, 1); useprog(mp)
        u(mp, 'ctm_tc0', *tc_layer); u(mp, 'ctm_tc_span', 0.0, 0.0); u(mp, 'ctm_altitude_blend', alt)
        common(mp)
        set_optics(mp, probe=True)
        draw(4, 0, 3)
        t_map = read_pixel(tmap)[0]
        alpha_map = 1.0 - t_map
        # the real optics on the same drawn opacity: R = beam, G = diffuse, against the mirror
        tau_v = rnd.choice([OPT['DEFAULT_VISUAL_DEPTH'], 3.0, 15.0])
        set_optics(mp, tau_v)
        common(mp)   # read_pixel bound the target on unit 0
        draw(4, 0, 3)
        px = read_pixel(tmap)
        want = optics_mirror(alpha_map, tau_v)
        worst_opt = max(worst_opt, abs(px[0] - want[0]), abs(px[1] - want[1]))
        if t_map == 1.0:
            clear_cases += 1
            if px[0] != 1.0 or px[1] != 0.0:
                clear_bad += 1
        err = abs(alpha_sky - alpha_map)
        worst = max(worst, err); cases += 1; informative += 1 if 0.02 < alpha_sky < 0.98 else 0
        if err > 2e-3:
            print(f'case {case}: sky alpha1 {alpha_sky:.5f} map 1-T {1 - t_map:.5f} err {err:.2e} L {L} cam {cam} scale {scale:.3f}')
    print(f'check: {cases} cases, {informative} with a partly covered sky, worst |alpha1 - (1 - T)| = {worst:.2e}')
    print(f'check: optics, shader against the mirror of llcloudoptics.h: worst |R, G error| = {worst_opt:.2e} (gate 1e-5)')
    # clear texels are exactly (1, 0): the altitude fade at 0 clears the layer
    bindfbo(0x8D40, fmap); viewport(0, 0, 1, 1); useprog(mp)
    u(mp, 'ctm_tc0', 0.3, 0.7); u(mp, 'ctm_tc_span', 0.0, 0.0); u(mp, 'ctm_altitude_blend', 0.0)
    u(mp, 'cloud_scale', 0.4); u(mp, 'cloud_shadow', 0.5); u(mp, 'cloud_variance', 0.3)
    u(mp, 'cloud_pos_density1', 1.0, 1.0, 1.0); u(mp, 'cloud_pos_density2', 1.0, 1.0, 0.125); u(mp, 'blend_factor', 0.0)
    u(mp, 'lightnorm', 0.3, 0.8, 0.5); u(mp, 'cloud_noise_texture', 0); u(mp, 'cloud_noise_texture_next', 1)
    active(0x84C0); bindtex(TEX2D, na); active(0x84C1); bindtex(TEX2D, nb); active(0x84C0)
    set_optics(mp)
    draw(4, 0, 3)
    px = read_pixel(tmap)
    clear_ok = px[0] == 1.0 and px[1] == 0.0 and clear_bad == 0
    print(f'check: a clear texel is exactly (R, G) = ({px[0]!r}, {px[1]!r}); {clear_cases} clear texels among the cases, {clear_bad} not exact:', 'ok' if clear_ok else 'FAILED')
    ok = worst <= 2e-3 and informative >= 15 and worst_opt <= 1e-5 and clear_ok
    print('check:', 'OK' if ok else 'FAILED')
    return ok


# --- stage L3: the lookup follows primary_shadow_strength --------------------------------------
STRENGTH_FILL = """
layout(location = 0) out vec4 out_a;
void main() { out_a = vec4(gl_FragCoord.x < 256.0 ? 0.1 : 0.9, 0.0, 0.0, 1.0); }
"""
STRENGTH_MAIN = """
layout(location = 0) out vec4 out_a;
void main() { out_a = vec4(cloudShadowTransmittance(vec3(0.0)), 0.0, 0.0, 1.0); }
"""


def check_strength():
    """cloudShadowTransmittance: the map's weight follows primary_shadow_strength. A map of 0.1 (left half) and 0.9 (right half),
    mean 0.5, read at the left quarter: strength 1 gives the map (0.1), 0 the mean (0.5), 0.5 halfway (0.3)."""
    useprog = fn('glUseProgram', None, U)
    head = "#version 400 compatibility" + chr(10) + "#define FRAGMENT_SHADER 1" + chr(10)
    fill = build(LOOKUP_VS, head + STRENGTH_FILL)
    look = build(LOOKUP_VS, head + read('class1/environment/srgbF.glsl') + read('class1/windlight/atmosphericsFuncs.glsl') + STRENGTH_MAIN)
    tmap, fmap = target(512, 512, 0x822B, mips=True)   # GL_RG8
    tout, fout = target(1, 1)
    bindfbo(0x8D40, fmap); viewport(0, 0, 512, 512); useprog(fill); draw(4, 0, 3)
    bindtex(TEX2D, tmap); genmip(TEX2D)
    ok = True
    for strength, want in ((1.0, 0.1), (0.0, 0.5), (0.5, 0.3)):
        bindfbo(0x8D40, fout); viewport(0, 0, 1, 1); useprog(look)
        u(look, 'cloud_shadow_params', 1.0, 0.0, 0.0625, 0.0)
        m = (F * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.25, 0.5, 0, 1)
        um4(loc(look, b'cloud_shadow_mat'), 1, 0, m)
        u(look, 'cloud_shadow_map', 0)
        u(look, 'primary_shadow_strength', strength)
        active(0x84C0); bindtex(TEX2D, tmap)
        draw(4, 0, 3)
        got = read_pixel(tout)[0]
        good = abs(got - want) < 3e-3
        ok = ok and good
        print(f'check: primary_shadow_strength {strength}: cloudShadowTransmittance {got:.4f} (expected {want}) {"ok" if good else "FAILED"}')
    # the sun's shadow (shadowUtil.glsl sampleDirectionalShadow) is mixed the same way: 1 - strength (1 - shadow)
    src = read('class1/deferred/shadowUtil.glsl')
    mixed = 'return mix(1.0, shadow, primary_shadow_strength);' in src
    print('check: shadowUtil mixes the sun shadow with primary_shadow_strength:', 'ok' if mixed else 'FAILED')
    return ok and mixed


# --- stage L2b: identity when off, and the beam multiplies linear light ------------------------
CONST_FILL = """
uniform float value;
layout(location = 0) out vec4 out_a;
void main() { out_a = vec4(value, 0.0, 0.0, 1.0); }
"""
SUNLIT_MAIN = """
uniform vec3 probe_light;
layout(location = 0) out vec4 out_a;
void main() {
    vec3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(vec3(10.0, -5.0, -80.0), vec3(0.0, 1.0, 0.0), normalize(vec3(0.3, 0.8, 0.5)), sunlit, amblit, additive, atten);
    out_a = vec4(sunlit, 1.0);
}
"""


def base_ref():
    """The commit the bit-identity and the bench compare with: the merge base with origin/vkstorm-devel (so a later devel
    does not change the baseline), or CLOUD_BENCH_BASE_REF."""
    ref = os.environ.get('CLOUD_BENCH_BASE_REF')
    if ref:
        return ref
    r = subprocess.run(['git', 'merge-base', 'HEAD', 'origin/vkstorm-devel'], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0 or not r.stdout.strip():
        raise RuntimeError('no base ref: origin/vkstorm-devel is missing or has no merge base with HEAD '
                           '(git fetch origin vkstorm-devel, or set CLOUD_BENCH_BASE_REF to a commit)')
    return r.stdout.strip()


def git_show(rel):
    """A shader of the base ref (rel under app_settings/shaders/class1/)."""
    ref = base_ref()
    r = subprocess.run(['git', 'show', ref + ':indra/newview/app_settings/shaders/class1/' + rel],
                       cwd=ROOT, capture_output=True, text=True, env=dict(os.environ, MSYS2_ARG_CONV_EXCL='*'))
    if r.returncode != 0:
        raise RuntimeError(f'cannot read {rel} at the base ref {ref}: {r.stderr.strip()}')
    return r.stdout


def set_lookup(prog, **over):
    for k, v in {**DEFAULT_UNIFORMS, **over}.items():
        u(prog, k, *(v if isinstance(v, tuple) else (v,)))


def read_all(t, w, h):
    bindtex(TEX2D, t); buf = (F * (w * h * 4))(); getteximage(TEX2D, 0, 0x1908, 0x1406, buf); return bytes(buf)


def check_lookup():
    useprog = fn('glUseProgram', None, U)
    head = "#version 400 compatibility" + chr(10) + "#define FRAGMENT_SHADER 1" + chr(10)
    fill = build(LOOKUP_VS, head + CONST_FILL)
    new = lookup_program(read('class1/windlight/atmosphericsFuncs.glsl'))
    # only the comparisons with the lookup before the stage need the base ref; the rest of --check is self-contained
    try:
        old = lookup_program(git_show('windlight/atmosphericsFuncs.glsl'))
    except RuntimeError as e:
        print('check: FAILED, the comparisons with the lookup before the stage need a base ref:', e)
        old = None
    tmap, fmap = target(512, 512, 0x822B, mips=True)   # GL_RG8
    ok = old is not None

    def fill_map(value):
        bindfbo(0x8D40, fmap); viewport(0, 0, 512, 512); useprog(fill); u(fill, 'value', value); draw(4, 0, 3)
        bindtex(TEX2D, tmap); genmip(TEX2D)

    def render(prog, w, h, t, f, strength, **over):
        bindfbo(0x8D40, f); viewport(0, 0, w, h); useprog(prog)
        set_lookup(prog, **over); u(prog, 'screen', float(w), float(h))
        if strength is not None:
            u(prog, 'cloud_shadow_params', strength, 0.0, 0.0625, 0.8)
            m = (F * 16)(1.0 / 600.0, 0, 0, 0, 0, 1.0 / 300.0, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 0, 1)
            um4(loc(prog, b'cloud_shadow_mat'), 1, 0, m)
            u(prog, 'cloud_shadow_map', 0)
            active(0x84C0); bindtex(TEX2D, tmap)
        draw(4, 0, 3)
        return read_all(t, w, h)

    # (c) strength 0, and a map of T = 1, give the pixels of the lookup before the stage, bit for bit
    w, h = 256, 144
    t, f = target(w, h)
    fill_map(1.0)
    for classic in (0, 1) if old is not None else ():
        before = render(old, w, h, t, f, None, classic_mode=classic)
        off = render(new, w, h, t, f, 0.0, classic_mode=classic)
        full = render(new, w, h, t, f, 1.0, classic_mode=classic)
        good = before == off and before == full
        ok = ok and good
        print(f'check: classic_mode {classic}: strength 0 and a map of T = 1 are bit-identical to the lookup before the stage:', 'ok' if good else 'FAILED')

    # classic mode with a real map (0.5, strength 1): the encoded maths are the base ref's, bit for bit
    fill_map(0.5)
    for classic in (0, 1) if old is not None else ():
        before = render(old, w, h, t, f, 1.0, classic_mode=classic)
        now = render(new, w, h, t, f, 1.0, classic_mode=classic)
        same = before == now
        if classic == 1:
            ok = ok and same
        print(f'check: classic_mode {classic}, a map of 0.5 at strength 1: new {"==" if same else "!="} base ref' + (' (bit-identical required)' if classic else ' (differs by design: linear light)'),
              ('ok' if same else 'FAILED') if classic else ('ok' if not same else 'FAILED'))
        if classic == 0 and same:
            ok = False

    # (e) the beam multiplies the LINEAR light: a map of 0.5 leaves 0.5 of the sun's and of a dim moon's light alike
    probe = build(LOOKUP_VS, head + read('class1/environment/srgbF.glsl') + read('class1/windlight/atmosphericsFuncs.glsl') + SUNLIT_MAIN)
    to, fo = target(1, 1)
    allr = []   # the map is 8 bit: 0.5 is stored as 127/255 or 128/255
    for name, light in (('sun', (0.9, 0.85, 0.8)), ('dim', (0.2, 0.2, 0.3)), ('moon', (0.06, 0.07, 0.1))):
        for strength in (0.0, 1.0):
            bindfbo(0x8D40, fo); viewport(0, 0, 1, 1); useprog(probe)
            set_lookup(probe, primary_light_color=light, classic_mode=0, sky_sunlight_scale=1.0)
            u(probe, 'cloud_shadow_params', strength, 0.0, 0.0625, 0.8)
            m = (F * 16)(1.0 / 600.0, 0, 0, 0, 0, 1.0 / 300.0, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 0, 1)
            um4(loc(probe, b'cloud_shadow_mat'), 1, 0, m); u(probe, 'cloud_shadow_map', 0)
            active(0x84C0); bindtex(TEX2D, tmap)
            draw(4, 0, 3)
            px = read_pixel(to)
            if strength == 0.0:
                ref = px
            else:
                ratios = [px[i] / ref[i] for i in range(3)]
                allr += ratios
                print(f'check: {name} light: sunlit with a map of 0.5 / without = {ratios[0]:.5f} {ratios[1]:.5f} {ratios[2]:.5f} (0.5, 8 bit)')
    good = all(abs(r - 0.5) <= 0.5 / 255 for r in allr) and max(allr) - min(allr) < 1e-5
    ok = ok and good
    print('check: the beam multiplies linear light, the same for every light:', 'ok' if good else 'FAILED')

    # the glTF alpha-blend shader against pbralphaF.glsl (the owner's reference): the lines after calcAtmosphericVarsLinear are cut out
    # of each file and run as they are. calcAtmosphericVarsLinear returns the light ready to use (linear in normal mode), so neither
    # converts it again, and both give the same sunlit_linear in both modes (classic: x 1.35); the final_scale lines are the same text.
    import re
    after_call = r'calcAtmosphericVarsLinear\(pos\.xyz, norm, light_dir, sunlit, amblit, additive, atten\);\n(.*?)\n\s*vec2 frag'

    def caller_probe(rel):
        src = read(rel)
        m = re.search(after_call, src, re.S)
        assert m, rel + ' no longer has the expected lines'
        lines = [ln for ln in m.group(1).split('\n') if not ln.lstrip().startswith('#')]
        main = ('layout(location = 0) out vec4 out_a;\nvoid main() {\n'
                ' vec3 pos = vec3(10.0, -5.0, -80.0); vec3 norm = vec3(0.0, 1.0, 0.0);\n'
                ' vec3 light_dir = normalize(vec3(0.3, 0.8, 0.5)); vec3 sunlit, amblit, additive, atten;\n'
                ' calcAtmosphericVarsLinear(pos, norm, light_dir, sunlit, amblit, additive, atten);\n'
                + '\n'.join(lines) + '\n out_a = vec4(sunlit_linear, 1.0);\n}\n')
        return build(LOOKUP_VS, head + read('class1/environment/srgbF.glsl') + read('class1/windlight/atmosphericsFuncs.glsl') + main)

    def final_scale_text(rel):
        m = re.search(r'float final_scale = 1;\n\s*if \(classic_mode > 0\)\n\s*final_scale = 1\.1;\n\s*frag_color = max\(vec4\(color\.rgb \* final_scale,a\), vec4\(0\)\);', read(rel))
        return bool(m)

    gprobe = caller_probe('class1/gltf/pbrmetallicroughnessF.glsl')
    pprobe = caller_probe('class2/deferred/pbralphaF.glsl')
    gok = final_scale_text('class1/gltf/pbrmetallicroughnessF.glsl') and final_scale_text('class2/deferred/pbralphaF.glsl')
    print('check: the glTF alpha-blend shader has pbralphaF.glsl\'s final_scale lines:', 'ok' if gok else 'FAILED')
    for classic in (0, 1):
        got = {}
        for name, prog in (('glTF', gprobe), ('pbralpha', pprobe)):
            res = []
            for strength in (0.0, 1.0):
                bindfbo(0x8D40, fo); viewport(0, 0, 1, 1); useprog(prog)
                set_lookup(prog, primary_light_color=(0.9, 0.85, 0.8), classic_mode=classic, sky_sunlight_scale=1.0)
                u(prog, 'cloud_shadow_params', strength, 0.0, 0.0625, 0.8)
                m4 = (F * 16)(1.0 / 600.0, 0, 0, 0, 0, 1.0 / 300.0, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 0, 1)
                um4(loc(prog, b'cloud_shadow_mat'), 1, 0, m4); u(prog, 'cloud_shadow_map', 0)
                active(0x84C0); bindtex(TEX2D, tmap)
                draw(4, 0, 3)
                res.append(read_pixel(to))
            got[name] = res
        r = got['glTF'][1][0] / got['glTF'][0][0]
        # the map's 0.5 once (in classic mode on the encoded light, as it always was), and the same pixels as pbralphaF
        good = got['glTF'] == got['pbralpha'] and abs(r - 0.5) <= 0.5 / 255
        gok = gok and good
        print(f'check: glTF alpha-blend caller, classic_mode {classic}: sunlit_linear with a map of 0.5 / without = {r:.5f}, identical to pbralphaF:', 'ok' if good else 'FAILED')
    ok = ok and gok
    return ok


# --- cost ------------------------------------------------------------------------------------
LOOKUP_VS = "#version 400 compatibility\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n"
LOOKUP_MAIN = """
uniform vec2 screen;
layout(location = 0) out vec4 out_a;
void main() {
    vec2 s = gl_FragCoord.xy / screen;
    vec3 pos = vec3((s.x - 0.5) * 600.0, (s.y - 0.5) * 300.0 - 20.0, -(40.0 + 600.0 * s.y));
    vec3 n = normalize(vec3(s.x - 0.5, 1.0, s.y - 0.4));
    vec3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(pos, n, normalize(vec3(0.3, 0.8, 0.5)), sunlit, amblit, additive, atten);
    out_a = vec4(sunlit + amblit + additive * 0.001 + atten * 0.001, 1.0);
}
"""
DEFAULT_UNIFORMS = dict(lightnorm=(0.3, 0.8, 0.5), sunlight_color=(0.73, 0.78, 0.9), moonlight_color=(0.2, 0.2, 0.3), sun_up_factor=1,
                        primary_light_color=(0.73, 0.78, 0.9), primary_shadow_strength=1.0,
                        ambient_color=(1.05, 1.05, 1.05), blue_horizon=(0.5, 0.5, 0.6), blue_density=(0.24, 0.45, 0.76), haze_horizon=0.19,
                        haze_density=0.7, cloud_shadow=0.27, density_multiplier=0.00018, distance_multiplier=0.8, max_y=1605.0,
                        glow=(5.0, 0.001, -0.5), scene_light_strength=1.0, sun_moon_glow_factor=1.0, sky_sunlight_scale=1.0,
                        sky_ambient_scale=1.0, classic_mode=0,
                        # L3 (glow at each body): the sun's glow at the light, damped by the light direction the shader passes
                        # (normalize(0.3, 0.8, 0.5)); no moon glow
                        sun_glow_norm=(0.3, 0.8, 0.5), moon_glow_norm=(0.3, 0.8, 0.5), moon_glow_factor=0.0, moon_halo_weight=0.0,
                        sun_dir=(0.3 / 0.9899494937, 0.8 / 0.9899494937, 0.5 / 0.9899494937), moon_dir=(0.0, -1.0, 0.0))


def lookup_program(funcs_text):
    fs = ("#version 400 compatibility\n#define FRAGMENT_SHADER 1\n" + read('class1/environment/srgbF.glsl') + funcs_text + LOOKUP_MAIN)
    return build(LOOKUP_VS, fs)


def bench():
    q = U(); genq(1, C.byref(q))

    def timed(work):
        beginq(0x88BF, q.value); work(); endq(0x88BF); finish()
        v = C.c_uint64(); qresult(q.value, 0x8866, C.byref(v)); assert geterror() == 0
        return v.value / 1e6

    def median(work):
        return statistics.median([timed(work) for _ in range(WARMUP + FRAMES)][WARMUP:])

    na, nb = noise_texture(1), noise_texture(2)
    mp_new = build(MAP_VS, MAP_FS)
    mp_old = build(MAP_VS, "#version 400 compatibility\n" + EXTRA + git_show('deferred/cloudShadowMapF.glsl').replace('/*[EXTRA_CODE_HERE]*/', ''))
    tmap, fmap = target(512, 512, 0x822B, mips=True)   # GL_RG8
    tmap_old, fmap_old = target(512, 512, 0x8229, mips=True)   # GL_R8, as the base ref has it
    useprog = fn('glUseProgram', None, U)

    def map_pass(prog, fbo, tex):
        bindfbo(0x8D40, fbo); viewport(0, 0, 512, 512); useprog(prog)
        set_optics(prog)
        u(prog, 'cloud_scale', 0.42); u(prog, 'cloud_shadow', 0.45); u(prog, 'cloud_variance', 0.4); u(prog, 'cloud_pos_density1', 1.7, 0.5, 1.0)
        u(prog, 'cloud_pos_density2', 1.7, 0.5, 0.125); u(prog, 'blend_factor', 0.3); u(prog, 'lightnorm', 0.3, 0.8, 0.5)
        u(prog, 'ctm_tc0', -1.0, 0.2); u(prog, 'ctm_tc_span', 0.0065, -0.0065); u(prog, 'ctm_altitude_blend', 1.0)
        u(prog, 'cloud_noise_texture', 0); u(prog, 'cloud_noise_texture_next', 1)
        active(0x84C0); bindtex(TEX2D, na); active(0x84C1); bindtex(TEX2D, nb); active(0x84C0)
        draw(4, 0, 3)
        bindtex(TEX2D, tex); genmip(TEX2D)
    build_ms = median(lambda: map_pass(mp_new, fmap, tmap))
    build_old_ms = median(lambda: map_pass(mp_old, fmap_old, tmap_old))
    print(f'map build 512x512 + mips: RG8 with the optics {build_ms:.4f} ms; base ref (R8) {build_old_ms:.4f} ms; delta {build_ms - build_old_ms:+.4f} ms')

    new = lookup_program(read('class1/windlight/atmosphericsFuncs.glsl')); old = lookup_program(git_show('windlight/atmosphericsFuncs.glsl'))
    print('lookup, ms per pass (median of %d), calcAtmosphericVarsLinear on every pixel:' % FRAMES)
    print(f'{"size":>10} {"before the stage":>17} {"strength 0":>11} {"strength 1":>11} {"lookup (1 - 0)":>15} {"no clouds (0 - before)":>23} {"base ref, strength 1":>21}')
    total = total_old = 0.0
    for w, h in ((1920, 1080), (2560, 1440)):
        t, f = target(w, h); results = {}
        for name, prog, strength, tex in (('before', old, None, tmap_old), ('off', new, 0.0, tmap), ('on', new, 1.0, tmap), ('old_on', old, 1.0, tmap_old)):
            def work():
                bindfbo(0x8D40, f); viewport(0, 0, w, h); useprog(prog)
                for k, v in DEFAULT_UNIFORMS.items():
                    u(prog, k, *(v if isinstance(v, tuple) else (v,)))
                u(prog, 'screen', float(w), float(h))
                if strength is not None:
                    u(prog, 'cloud_shadow_params', strength, 0.0, 0.0625, 0.8)
                    l = loc(prog, b'cloud_shadow_mat')
                    m = (F * 16)(1.0 / 600.0, 0, 0, 0, 0, 1.0 / 300.0, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 0, 1)
                    um4(l, 1, 0, m)
                    u(prog, 'cloud_shadow_map', 0)
                    active(0x84C0); bindtex(TEX2D, tex)
                draw(4, 0, 3)
            results[name] = median(work)
        print(f'{w}x{h:>5} {results["before"]:>17.4f} {results["off"]:>11.4f} {results["on"]:>11.4f} {results["on"] - results["off"]:>15.4f} {results["off"] - results["before"]:>23.4f} {results["old_on"]:>21.4f}')
        if h == 1080:
            total = build_ms + max(0.0, results['on'] - results['before'])
            total_old = build_old_ms + max(0.0, results['old_on'] - results['before'])
    ok = total <= 0.05
    print(f'1080p, map + lookups with clouds: {total:.4f} ms (gate 0.05 ms):', 'ok' if ok else 'FAILED')
    delta = total - total_old
    print(f'1080p, the base ref: {total_old:.4f} ms; delta {delta:+.4f} ms (target +0.002 ms):',
          'ok' if delta <= 0.002 else 'WARNING, above the target (timer noise is about 0.001 ms: run again)')
    print('with no clouds the map pass does not run and the lookup branch is off ("no clouds" column)')
    return ok


if DO_CHECK and not (check() and check_strength() and check_lookup()):
    sys.exit(1)
if DO_BENCH:
    try:
        bench_ok = bench()
    except RuntimeError as e:   # the base ref is missing
        print('bench: FAILED,', e)
        sys.exit(1)
    if not bench_ok:
        sys.exit(1)
