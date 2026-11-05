# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU benchmark of the shadow pipeline's other costs, no viewer launch.

Complements bench_shadow_pcss.py (which times the deferred sun light pass) for the shadow
model study (doc/shadows/shadow-model-rework.md):

  blur      the light-map blur as the viewer runs it (class1 deferred/blurLightF.glsl, two
            separable passes, RenderShadowGaussian 3,2 / RenderShadowBlurSize 1.4 kernel),
            full and half resolution, over a synthetic G-buffer (ground plane and boxes);
  raster    caster rendering into the four sun cascades, depth only (DEPTH_COMPONENT24, the
            viewer's BlurHappySize screen-sized maps), for N procedural boxes (12 triangles
            each): ms per cascade per million triangles. This is a PROXY: no textures, no
            alpha-mask fetches, and one draw call instead of the viewer's thousands, so the
            CPU and driver cost of real batches is not in it;
  raster_half  the same into half-resolution maps (cascade caching / lower tiers);
  trans     the transmittance proxy: the same casters again with 10 % of them also writing an
            RGBA8 colour target with multiplicative blending (a coloured transmittance map
            beside the depth map), and the receiver-side cost of one more RGBA8 fetch per
            pixel in a full-screen pass.

The GL context is the hidden window of gl_test_context.py (style 0, never shown): nothing
appears on screen. Timings are GL_TIME_ELAPSED medians. Environment: SHADOW_COST_FRAMES
(default 30), SHADOW_COST_RES (default "1920x1080,2560x1440").
"""
import math, os, statistics, sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
FRAMES = int(os.environ.get('SHADOW_COST_FRAMES', 30)); WARMUP = 5
RESOLUTIONS = [tuple(int(v) for v in r.split('x')) for r in os.environ.get('SHADOW_COST_RES', '1920x1080,2560x1440').split(',')]
BLUR_F = root / 'indra/newview/app_settings/shaders/class1/deferred/blurLightF.glsl'

F = C.c_float
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
deltex=fn('glDeleteTextures',None,I,C.POINTER(U))
texstorage=fn('glTexStorage2D',None,U,I,U,I,I);texparam=fn('glTexParameteri',None,U,U,I)
active=fn('glActiveTexture',None,U)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
delfbo=fn('glDeleteFramebuffers',None,I,C.POINTER(U))
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I);fbstatus=fn('glCheckFramebufferStatus',U,U)
drawbuffers=fn('glDrawBuffers',None,I,C.POINTER(U));drawbuffer=fn('glDrawBuffer',None,U);readbuffer=fn('glReadBuffer',None,U)
viewport=fn('glViewport',None,I,I,I,I);draw=fn('glDrawArrays',None,U,I,I)
enable=fn('glEnable',None,U);disable=fn('glDisable',None,U);depthfunc=fn('glDepthFunc',None,U);depthmask=fn('glDepthMask',None,C.c_ubyte)
colormask=fn('glColorMask',None,C.c_ubyte,C.c_ubyte,C.c_ubyte,C.c_ubyte)
blendfunc=fn('glBlendFunc',None,U,U)
clear=fn('glClear',None,U);cleardepth=fn('glClearDepth',None,C.c_double);clearcolor=fn('glClearColor',None,F,F,F,F)
geterror=fn('glGetError',U);finish=fn('glFinish',None)
loc=fn('glGetUniformLocation',I,U,C.c_char_p)
u1i=fn('glUniform1i',None,I,I);u1f=fn('glUniform1f',None,I,F);u2f=fn('glUniform2f',None,I,F,F)
u3fv=fn('glUniform3fv',None,I,I,C.POINTER(F));u1u=fn('glUniform1ui',None,I,U)
genq=fn('glGenQueries',None,I,C.POINTER(U));beginq=fn('glBeginQuery',None,U,U);endq=fn('glEndQuery',None,U)
qresult=fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64))
TEX2D=0x0DE1;FB=0x8D40;DEPTH_ATT=0x8D00;COLOR0=0x8CE0;TIME_ELAPSED=0x88BF;QUERY_RESULT=0x8866
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
print('GPU:', get_string(0x1F01).decode(), flush=True)

def make_tex(fmt, w, h, filt=0x2600):
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t); texstorage(TEX2D, 1, fmt, w, h)
    texparam(TEX2D, 0x2801, filt); texparam(TEX2D, 0x2800, filt)
    texparam(TEX2D, 0x2802, 0x812F); texparam(TEX2D, 0x2803, 0x812F)
    return t.value
def make_fbo(colors, depth=None):
    f = U(); genfbo(1, C.byref(f)); bindfbo(FB, f)
    for i, t in enumerate(colors): attachtex(FB, COLOR0 + i, TEX2D, t, 0)
    if depth: attachtex(FB, DEPTH_ATT, TEX2D, depth, 0)
    if colors:
        bufs = (U*len(colors))(*[COLOR0 + i for i in range(len(colors))]); drawbuffers(len(colors), bufs)
    else:
        drawbuffer(0); readbuffer(0)
    assert fbstatus(FB) == 0x8CD5, 'incomplete framebuffer'
    return f.value
def blur_happy(x): return (x + 16) & ~0xF

def timed(fn_draw, frames=FRAMES):
    q = U(); genq(1, C.byref(q)); out = []
    for i in range(WARMUP + frames):
        beginq(TIME_ELAPSED, q.value); fn_draw(); endq(TIME_ELAPSED)
        r = C.c_uint64(); qresult(q.value, QUERY_RESULT, C.byref(r))
        if i >= WARMUP: out.append(r.value / 1e6)
    return statistics.median(out)

FS_VERTEX = '''#version 430 core
out vec2 vary_fragcoord;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0; gl_Position = vec4(p, 0.0, 1.0); vary_fragcoord = p * 0.5 + 0.5; }
'''
# synthetic G-buffer: a ground plane seen from 1.7 m with boxes; view space position + normal
GBUF_F = '''#version 430 core
in vec2 vary_fragcoord;
uniform vec2 screen_res;
layout(location = 0) out vec4 out_pos;
layout(location = 1) out vec4 out_norm;
void main() {
    vec2 ndc = vary_fragcoord * 2.0 - 1.0;
    float t = tan(radians(30.0));
    vec3 dir = normalize(vec3(ndc.x * t * screen_res.x / screen_res.y, ndc.y * t - 0.35, -1.0));
    float d = dir.y < -1e-3 ? 1.7 / -dir.y : 400.0;
    vec3 p = dir * min(d, 400.0);
    vec3 n = vec3(0.0, 1.0, 0.0);
    // boxes on a 6 m grid: a bump in the normal and position where a box would be
    vec2 g = fract(p.xz / 6.0) - 0.5;
    if (max(abs(g.x), abs(g.y)) < 0.15) { p.y += 0.8; n = normalize(vec3(g.x, 0.2, g.y)); }
    out_pos = vec4(p, 1.0); out_norm = vec4(n, 0.0);
}
'''
STUBS_F = '''#version 430 core
uniform sampler2D gbuf_pos;
uniform sampler2D normalMap;
vec4 getPosition(vec2 pos_screen) { return texture(gbuf_pos, pos_screen); }
vec4 getNorm(vec2 pos_screen) { return vec4(texture(normalMap, pos_screen).xyz, 0.0); }
'''
NOISE_F = '''#version 430 core
in vec2 vary_fragcoord;
out vec4 frag_color;
void main() { float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
              frag_color = vec4(n, 1.0, n, n); }
'''
# procedural casters: box b (12 triangles, 36 vertices) on a jittered grid inside the cascade
CASTER_V = '''#version 430 core
uniform float extent;  // metres the grid covers (light space square)
uniform uint boxes_per_row;
flat out uint box_id;
const vec3 corners[8] = vec3[8](vec3(0,0,0),vec3(1,0,0),vec3(0,1,0),vec3(1,1,0),vec3(0,0,1),vec3(1,0,1),vec3(0,1,1),vec3(1,1,1));
const int idx[36] = int[36](0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5);
uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
void main() {
    uint b = uint(gl_VertexID) / 36u; uint v = uint(gl_VertexID) % 36u;
    uint h = hash(b);
    vec2 cell = vec2(b % boxes_per_row, b / boxes_per_row) / float(boxes_per_row);
    vec2 jitter = vec2(h & 0xffu, (h >> 8) & 0xffu) / 255.0 / float(boxes_per_row);
    float size = (0.3 + float((h >> 16) & 0xffu) / 255.0) * extent / float(boxes_per_row) * 0.5;
    vec3 p = vec3(cell + jitter, 0.2 + 0.6 * float((h >> 24) & 0xffu) / 255.0) * vec3(extent, extent, 1.0)
           + corners[idx[v]] * vec3(size, size, 0.05);
    box_id = b;
    gl_Position = vec4(p.xy / extent * 2.0 - 1.0, p.z * 2.0 - 1.0, 1.0);
}
'''
CASTER_F = '''#version 430 core
void main() { }
'''
CASTER_TRANS_F = '''#version 430 core
flat in uint box_id;
out vec4 frag_color;
void main() { if ((box_id % 10u) != 0u) discard; frag_color = vec4(0.8, 0.9, 1.0, 0.5); }
'''
FETCH_F = '''#version 430 core
in vec2 vary_fragcoord;
uniform sampler2D trans_map;
out vec4 frag_color;
void main() { frag_color = texture(trans_map, vary_fragcoord); }
'''

def kern():
    """LLPipeline's blur kernel: llgaussian(x, sigma) for RenderShadowGaussian (3, 2)."""
    def g(x, s): return 1.0 / math.sqrt(2.0 * math.pi * s * s) * math.exp(-(x * x) / (2.0 * s * s))
    return [v for x in range(4) for v in (g(x, 3.0), g(x, 2.0), float(x))]

def bench_blur(W, H):
    blur_src = '#version 430 core\n#define FRAGMENT_SHADER 1\n' + BLUR_F.read_text().replace('/*[EXTRA_CODE_HERE]*/', '')
    blur = program([(0x8B31, FS_VERTEX), (0x8B30, blur_src), (0x8B30, STUBS_F)])
    gprog = program([(0x8B31, FS_VERTEX), (0x8B30, GBUF_F)])
    nprog = program([(0x8B31, FS_VERTEX), (0x8B30, NOISE_F)])
    res = {}
    for scale in (1, 2):
        w, h = W // scale, H // scale
        gpos = make_tex(0x8814, w, h); gnorm = make_tex(0x8814, w, h); gfbo = make_fbo([gpos, gnorm])
        viewport(0, 0, w, h); use(gprog); u2f(loc(gprog, b'screen_res'), w, h); draw(4, 0, 3)
        light = make_tex(0x881A, w, h); lfbo = make_fbo([light]); use(nprog); draw(4, 0, 3)
        tmp = make_tex(0x881A, w, h); tfbo = make_fbo([tmp])
        use(blur)
        k = (F * 12)(*kern())
        u3fv(loc(blur, b'kern'), 4, k)
        u1f(loc(blur, b'kern_scale'), 1.4 * (4 / 2.0 - 0.5)); u1f(loc(blur, b'dist_factor'), 0.0)
        u2f(loc(blur, b'screen_res'), w, h)
        u1i(loc(blur, b'lightMap'), 0); u1i(loc(blur, b'gbuf_pos'), 1); u1i(loc(blur, b'normalMap'), 2)
        active(0x84C1); bindtex(TEX2D, gpos); active(0x84C2); bindtex(TEX2D, gnorm)
        def two_passes():
            bindfbo(FB, tfbo); active(0x84C0); bindtex(TEX2D, light); u2f(loc(blur, b'delta'), 1.0, 0.0); draw(4, 0, 3)
            bindfbo(FB, lfbo); active(0x84C0); bindtex(TEX2D, tmp); u2f(loc(blur, b'delta'), 0.0, 1.0); draw(4, 0, 3)
        viewport(0, 0, w, h)
        res[scale] = timed(two_passes)
        for f in (gfbo, lfbo, tfbo): ff = U(f); delfbo(1, C.byref(ff))
        for t in (gpos, gnorm, light, tmp): tt = U(t); deltex(1, C.byref(tt))
    assert geterror() == 0
    return res

def bench_raster(W, H, million_tris, half=False):
    sw, sh = blur_happy(W), blur_happy(H)
    if half: sw, sh = sw // 2, sh // 2
    boxes = int(million_tris * 1e6 / 12)
    per_row = int(math.ceil(math.sqrt(boxes)))
    prog = program([(0x8B31, CASTER_V), (0x8B30, CASTER_F)])
    maps = [make_tex(0x81A6, sw, sh, 0x2601) for _ in range(4)]
    fbos = [make_fbo([], m) for m in maps]
    use(prog); u1f(loc(prog, b'extent'), 64.0); u1u(loc(prog, b'boxes_per_row'), per_row)
    enable(0x0B71); depthfunc(0x0201); depthmask(1); enable(0x0B44); enable(0x864F)  # depth test, LESS, cull, depth clamp
    viewport(0, 0, sw, sh)
    def render():
        for f in fbos:
            bindfbo(FB, f); clear(0x100); draw(4, 0, boxes * 36)
    ms = timed(render, max(10, FRAMES // 2))
    disable(0x0B44); disable(0x864F); disable(0x0B71)
    for f in fbos: ff = U(f); delfbo(1, C.byref(ff))
    for m in maps: mm = U(m); deltex(1, C.byref(mm))
    assert geterror() == 0
    return ms

def bench_trans(W, H, million_tris):
    """Extra cost of a coloured transmittance target: the casters again, colour on (10 % write)."""
    sw, sh = blur_happy(W), blur_happy(H)
    boxes = int(million_tris * 1e6 / 12); per_row = int(math.ceil(math.sqrt(boxes)))
    dprog = program([(0x8B31, CASTER_V), (0x8B30, CASTER_F)])
    tprog = program([(0x8B31, CASTER_V), (0x8B30, CASTER_TRANS_F)])
    depth = [make_tex(0x81A6, sw, sh, 0x2601) for _ in range(4)]
    color = [make_tex(0x8058, sw, sh) for _ in range(4)]
    fd = [make_fbo([], d) for d in depth]
    ft = [make_fbo([c], d) for c, d in zip(color, depth)]
    enable(0x0B71); depthfunc(0x0201); enable(0x0B44); enable(0x864F); viewport(0, 0, sw, sh)
    def depth_only():
        use(dprog); u1f(loc(dprog, b'extent'), 64.0); u1u(loc(dprog, b'boxes_per_row'), per_row)
        depthmask(1)
        for f in fd: bindfbo(FB, f); clear(0x100); draw(4, 0, boxes * 36)
    def with_trans():
        depth_only()
        # transparent casters: depth test on, depth write off, multiplicative colour
        use(tprog); u1f(loc(tprog, b'extent'), 64.0); u1u(loc(tprog, b'boxes_per_row'), per_row)
        depthmask(0); enable(0x0BE2); blendfunc(0x0306, 0)  # DST_COLOR, ZERO: colour *= src
        for f in ft:
            bindfbo(FB, f); clearcolor(1, 1, 1, 1); clear(0x4000); draw(4, 0, (boxes // 10) * 36)
        disable(0x0BE2); depthmask(1)
    a = timed(depth_only, max(10, FRAMES // 2)); b = timed(with_trans, max(10, FRAMES // 2))
    disable(0x0B44); disable(0x864F); disable(0x0B71)
    # receiver side: one more RGBA8 fetch per screen pixel (four maps: one per pixel, the cascade's)
    fprog = program([(0x8B31, FS_VERTEX), (0x8B30, FETCH_F)])
    out = make_tex(0x881A, W, H); fo = make_fbo([out]); viewport(0, 0, W, H); use(fprog)
    u1i(loc(fprog, b'trans_map'), 0); active(0x84C0); bindtex(TEX2D, color[0])
    fetch = timed(lambda: draw(4, 0, 3))
    for f in fd + ft + [fo]: ff = U(f); delfbo(1, C.byref(ff))
    for t in depth + color + [out]: tt = U(t); deltex(1, C.byref(tt))
    assert geterror() == 0
    return a, b, fetch

def main():
    print(f'median ms, {FRAMES} frames after {WARMUP} warmup')
    for W, H in RESOLUTIONS:
        blur = bench_blur(W, H)
        print(f'{W}x{H}  light-map blur (2 passes): full {blur[1]:.3f} ms, half resolution {blur[2]:.3f} ms', flush=True)
        for mt in (0.5, 1.0, 2.0, 4.0):
            full = bench_raster(W, H, mt); halfr = bench_raster(W, H, mt, half=True)
            print(f'{W}x{H}  casters {mt:.1f} Mtri x 4 cascades: {full:.3f} ms ({full / 4 / mt:.3f} ms per cascade per Mtri);'
                  f' half-res maps {halfr:.3f} ms', flush=True)
        a, b, fetch = bench_trans(W, H, 2.0)
        print(f'{W}x{H}  transmittance proxy, 2 Mtri, 10 % transparent: depth only {a:.3f} ms, + colour pass {b:.3f} ms'
              f' (+{b - a:.3f}); receiver fetch {fetch:.3f} ms', flush=True)
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
