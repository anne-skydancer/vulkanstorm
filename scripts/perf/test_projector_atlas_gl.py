# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GL check of the projector shadow atlas (stage S7b), no viewer launch.

LLPipeline::updateProjectorShadows renders a projector's shadow into a tile of a large depth
texture (viewport and scissor on the tile, the field of view widened so that the cone fills the
map inside the tile's border) and the spot light pass samples it with
    matrix = Atlas * TexSpace * (proj * view) * inverse(camera view)
where Atlas maps the tile's [0,1] uv to atlas uv (x' = sx x + bx w), through
class1 deferred/shadowUtil.glsl's sampleProjectorShadow (PCF, PCSS stage 1 and PCSS contact
hardening with the plain-depth sampler on the same atlas).

With the bench_shadow_pcss.py scene (ray-cast exact geometry, a box floating over the ground under
a spot light) this renders the same projector shadow twice:
  * into a tile of a 3072 x 3072 atlas (512 x 512 at (1024, 2048), a border of 8 texels), and
  * into a plain 1024 x 1024 map that is the whole texture (border 0),
and compares the shadow along a line across the ground through the umbra:
  * the umbra is dark (< 0.1) and the far ground lit (> 0.9) in both;
  * the shadow's two edges (where the value crosses 0.5) are at the same place in both, within the
    texel size of the coarser map (the atlas tile is the coarser one);
  * a tile next to it holding a different shadow does not leak into this one (a second tile with a
    box in another place is rendered beside it and the profile does not change);
  * a point outside the tile's map reads as lit.
Exit status 0 when all checks pass. The GL window is the hidden one of gl_test_context.py.
"""
import math, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
exec(compile((HERE / 'bench_shadow_pcss.py').read_text().split('\nfailures = []')[0], str(HERE / 'bench_shadow_pcss.py'), 'exec'))

W, H = 1920, 1080
FOV = math.radians(60.0)
LIGHT_POS = [0.0, 6.0, 0.0]
LIGHT_TARGET = [0.0, 0.0, 0.0]
NEAR, FAR = 0.1, 20.0

STUB_MAIN = '''#version 430 core
in vec2 vary_fragcoord; out vec4 frag_color;
float sampleProjectorShadow(vec3 pos, vec3 norm);
vec4 getPosition(vec2 s); vec4 getNorm(vec2 s);
void main() { vec4 p = getPosition(vary_fragcoord); vec4 n = getNorm(vary_fragcoord);
  frag_color = vec4(sampleProjectorShadow(p.xyz, n.xyz), 0.0, 0.0, 1.0); }
'''
STUBS = '''#version 430 core
uniform sampler2D gbuf_pos; uniform sampler2D normalMap;
vec4 getPosition(vec2 s) { return texture(gbuf_pos, s); }
vec4 getNorm(vec2 s) { return vec4(texture(normalMap, s).xyz, 0.0); }
'''

def program_for(variant):
    util = (root / UTIL_F).read_text()
    local = VARIANT_DEFINES[variant]
    defs = ['SPOT_SHADOW'] + local
    return program([(0x8B31, VERTEX), (0x8B30, STUB_MAIN), (0x8B30, stage(util, defs)), (0x8B30, STUBS)])

def render_tile(atlas, aw, ah, tx, ty, size, border, box):
    """Render one projector's shadow (a box over the ground) into a tile of the atlas texture."""
    fovr = 2.0 * math.atan(math.tan(0.5 * FOV) * size / (size - 2.0 * border))
    proj = perspective(fovr, 1.0, NEAR, FAR)
    view = look_at(LIGHT_POS, LIGHT_TARGET, (0.0, 0.0, -1.0))
    pv = mmul(proj, view)
    glsl = scene_glsl([box])
    sprog = program([(0x8B31, VERTEX), (0x8B30, SHADOW_F % glsl)])
    fbo = make_fbo([], atlas)
    viewport(tx, ty, size, size)
    enable(0x0C11); fn('glScissor', None, I, I, I, I)(tx, ty, size, size)
    enable(0x0B71); depthfunc(0x0207); depthmask(1)
    fn('glClear', None, U)(0x100)
    use(sprog); set_mat(sprog, 'light_proj_view', [pv]); set_mat(sprog, 'inv_light', [inverse(pv)])
    draw(4, 0, 3)
    disable(0x0B71); disable(0x0C11)
    bindfbo(FB, 0); ff = U(fbo.value if hasattr(fbo, 'value') else fbo); delfbo(1, C.byref(ff))
    return pv, fovr

def make_atlas(aw, ah):
    t = make_tex(0x81A6, aw, ah, 0x2601)
    bindtex(TEX2D, t)
    texparam(TEX2D, 0x884C, 0x884E); texparam(TEX2D, 0x884D, 0x0203)
    f = make_fbo([], t)
    viewport(0, 0, aw, ah); fn('glClear', None, U)(0x100)
    bindfbo(FB, 0); ff = U(f.value if hasattr(f, 'value') else f); delfbo(1, C.byref(ff))
    return t

def profile(variant, aw, ah, tx, ty, size, border, extra_tiles=()):
    """Shadow value along the ground line z = 0, x in [-3, 3], seen from a camera 9 m away."""
    atlas = make_atlas(aw, ah)
    for (ex, ey, ebox) in extra_tiles:
        render_tile(atlas, aw, ah, ex, ey, size, border, ebox)
    box = ((-0.5, 2.0, -0.5), (0.5, 2.5, 0.5))
    pv, fovr = render_tile(atlas, aw, ah, tx, ty, size, border, box)
    sc = Scene(W, H, [box], [4.0, 5.0, 8.0], [0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [([0, 6, 0], [0, 0, 0])] * 2, (10.0, 25.0, 60.0, 150.0))
    prog = program_for(variant)
    sc.bind(prog, LIGHT_SIZE)
    # the lighting pass's matrix: Atlas * TexSpace * (proj * view) * inverse(camera view)
    A = [[size / aw, 0, 0, tx / aw], [0, size / ah, 0, ty / ah], [0, 0, 1, 0], [0, 0, 0, 1]]
    m = mmul(A, mmul(TEXSPACE, mmul(pv, sc.inv_view)))
    set_mat(prog, 'proj_shadow_matrix', [m])
    rect = ((tx + border) / aw, (ty + border) / ah, (tx + size - border) / aw, (ty + size - border) / ah)
    u4f(loc(prog, b'proj_shadow_rect'), *rect)
    u1f(loc(prog, b'proj_shadow_k'), 2.0 * math.tan(0.5 * fovr) / size)
    u1f(loc(prog, b'proj_shadow_weight'), 1.0)
    u1f(loc(prog, b'spot_shadow_reach'), 128.0)
    u2f(loc(prog, b'proj_shadow_res'), aw, ah)
    u1f(loc(prog, b'spot_shadow_bias'), -0.0002); u1f(loc(prog, b'spot_shadow_offset'), 0.04)
    # units 12 and 13: the bench's own scene setup uses 0-11 (and put the plain-depth sampler on 8-11)
    active(0x84C0 + 12); bindtex(TEX2D, atlas); u1i(loc(prog, b'shadowMap4'), 12); bindsampler(12, 0)
    active(0x84C0 + 13); bindtex(TEX2D, atlas); u1i(loc(prog, b'shadowDepth4'), 13); bindsampler(13, depth_fetch.value)
    active(0x84C0)
    draw(4, 0, 3); finish()
    img = np.frombuffer(sc.read_channel(0x1903), dtype=np.float32).reshape(H, W)
    xs = np.linspace(-3.0, 3.0, 241)
    vals = []
    for x in xs:
        px, py = sc.project([float(x), 0.0, 0.0])
        vals.append(float(img[py, px]))
    # a point well outside the cone (the cone covers a 7 m disc on the ground): lit
    out_px = sc.project([5.5, 0.0, 0.0])
    outside = float(img[out_px[1], out_px[0]])
    sc.unbind(); bindsampler(12, 0); bindsampler(13, 0); sc.release()
    t = U(atlas); deltex(1, C.byref(t))
    return xs, np.array(vals), outside

def edges(xs, vals):
    """x where the value crosses 0.5 going into and out of the umbra."""
    e = []
    for i in range(1, len(xs)):
        if (vals[i - 1] < 0.5 <= vals[i]) or (vals[i] < 0.5 <= vals[i - 1]):
            e.append(xs[i - 1] + (0.5 - vals[i - 1]) / (vals[i] - vals[i - 1]) * (xs[i] - xs[i - 1]))
    return e

fails = []
def ok(cond, msg):
    print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
    if not cond: fails.append(msg)

def main():
    print('GL:', get_string(0x1F01).decode(), flush=True)
    for variant in ('tree', 'pcss1', 'pcss'):
        name = {'tree': 'PCF', 'pcss1': 'PCSS stage 1', 'pcss': 'PCSS contact hardening'}[variant]
        print(f'\n{name}')
        xs, ref, ref_out = profile(variant, 1024, 1024, 0, 0, 1024, 0)
        xs, atl, atl_out = profile(variant, 3072, 3072, 1024, 2048, 512, 8)
        # a neighbour tile with a different shadow, rendered next to it (left and above)
        other = ((-1.5, 2.0, -1.5), (-0.5, 2.5, -0.5))
        xs, nb, nb_out = profile(variant, 3072, 3072, 1024, 2048, 512, 8, extra_tiles=[(512, 2048, other), (1024, 2560, other), (1536, 2048, other), (1024, 1536, other)])
        er, ea, en = edges(xs, ref), edges(xs, atl), edges(xs, nb)
        print(f'  umbra edges (m): separate map {["%.3f" % v for v in er]}, atlas tile {["%.3f" % v for v in ea]}, with neighbours {["%.3f" % v for v in en]}')
        ok(len(er) == 2 and len(ea) == 2, 'the shadow has two edges in both')
        if len(er) == 2 and len(ea) == 2:
            # the coarser map's texel: the tile covers 2 * 6 * tan(30) m = 6.9 m over 496 texels
            texel = 6.93 / 496.0
            ok(all(abs(a - b) <= max(3 * texel, 0.03) for a, b in zip(er, ea)), f'edges agree within 3 texels ({3 * texel * 100:.1f} cm)')
        ok(min(ref) < 0.1 and min(atl) < 0.1, f'the umbra is dark (separate {min(ref):.2f}, atlas {min(atl):.2f})')
        ok(ref[0] > 0.9 and atl[0] > 0.9 and ref[-1] > 0.9 and atl[-1] > 0.9, 'the ground away from the umbra is lit')
        ok(np.allclose(nb, atl, atol=0.02), f'neighbouring tiles do not leak (largest change {float(np.max(np.abs(nb - atl))):.3f})')
        ok(atl_out > 0.98 and nb_out > 0.98, f'a point outside the tile\'s map is lit ({atl_out:.2f})')
    print(f'\n{"all checks passed" if not fails else str(len(fails)) + " check(s) failed"}')
    return 1 if fails else 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
