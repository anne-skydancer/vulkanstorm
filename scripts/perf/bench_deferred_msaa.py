# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU benchmark of multisampled deferred shading for the MSAA study
(doc/msaa/deferred-msaa-study.md), no viewer launch.

It builds the viewer's G-buffer (pipeline.cpp allocateScreenBufferInternal: diffuse RGBA8,
specular/ORM RGBA8, normal RGBA16, emissive RGB16F, depth D24 -- here D24S8, the design's
format, same size) at 1, 2, 4 and 8 samples, fills it from a synthetic scene, and times
with GL_TIME_ELAPSED queries (median of MSAA_BENCH_FRAMES frames after a warm-up):
  gfill        clear + draw the scene into the G-buffer (MRT 4 + depth), depth test LESS
  classify     the edge classification: a full-screen pass that reads every sample's depth,
               normal/flag and albedo, discards simple pixels and writes stencil 1 on complex
               ones (the GLSL mirrors msaa_bench_model.is_edge); a GL_SAMPLES_PASSED query
               of the same pass gives the edge fraction
  classify_dn  the same with a cheaper rule: depth and normal/flag only (no albedo)
  pixel        samples = 1 only: today's frame, one full-screen lighting pass, one shade per pixel
  a_all        design A, per-sample shading everywhere: a sample-rate pass (gl_SampleID)
               into an N-sample RGBA16F scene colour
  a_edge       design A with classification: pass 1 shades sample 0 once per simple pixel
               (stencil 0) and writes it to all samples; pass 2 shades every sample of the
               complex pixels (stencil 1, sample rate)
  b_all        design B (the 2011 Linden code): one pass per pixel loops over all samples,
               shades each and writes the average into a single-sample colour
  b_edge       design B with an in-shader branch: the loop only on complex pixels
  resolve_blit glBlitFramebuffer of the N-sample scene colour (box resolve), timed after
               a_edge (resolve_blit_all: after a_all)
  resolve_tone the tonemap-weighted resolve shader (Karis 2013, Lottes 2016; msaa_bench_model.
               tonemap_weighted_resolve), timed after a_edge (resolve_tone_all: after a_all).
               The two differ: what the lighting wrote decides how well the driver's MSAA
               colour compression holds (a pixel whose samples are equal stays compressed).
  resolve_depth a single-sample depth for the post chain (sample 0, gl_FragDepth)
The lighting function is a stand-in for softenLightF: G-buffer decode, position
reconstruction, sun + MSAA_BENCH_LIGHTS point lights with GGX, and two probe lookups
(textureLod on a mipped RGBA16F map); MSAA_BENCH_LIGHTS selects the ALU weight per run.

Checks (print ok/FAIL; any FAIL exits 1): every G-buffer configuration is framebuffer
complete; a_edge after a box resolve matches a_all, and b_all/b_edge match a_all's box
resolve, within tolerances in 8-bit units after a Reinhard tonemap over a window of the
image; the tonemap-weighted resolve shader matches the CPU model on an exact per-sample
pattern. The GL limits and the sample counts each G-buffer format supports
(glGetInternalformativ) are printed first. With GL_NVX_gpu_memory_info the measured drop
of free video memory per configuration is printed next to the format arithmetic.

Environment: MSAA_BENCH_RES (default "1920x1080,2560x1440"), MSAA_BENCH_SAMPLES ("1,2,4,8"),
MSAA_BENCH_SCENES ("open,dense,foliage"), MSAA_BENCH_LIGHTS ("4,24"), MSAA_BENCH_FRAMES
(20), MSAA_BENCH_JSON (write all results there). Pass --opengl PATH to select another GL
DLL (Mesa; with GALLIUM_DRIVER=zink for Zink)."""
import array
import json
import os
from pathlib import Path
import importlib.util
import sys

bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
spec = importlib.util.spec_from_file_location('msaa_bench_model', Path(__file__).with_name('msaa_bench_model.py'))
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)

RES = [tuple(int(v) for v in r.split('x')) for r in os.environ.get('MSAA_BENCH_RES', '1920x1080,2560x1440').split(',')]
SAMPLES = [int(s) for s in os.environ.get('MSAA_BENCH_SAMPLES', '1,2,4,8').split(',')]
SCENE_NAMES = os.environ.get('MSAA_BENCH_SCENES', 'open,dense,foliage').split(',')
LIGHTS = [int(k) for k in os.environ.get('MSAA_BENCH_LIGHTS', '4,24').split(',')]
FRAMES = int(os.environ.get('MSAA_BENCH_FRAMES', 20))
WARMUP = 3
WIN = 256

# --- GL entry points ---
F = C.c_float; D = C.c_double; B = C.c_ubyte
genbuf = fn('glGenBuffers', None, I, C.POINTER(U)); bindbuf = fn('glBindBuffer', None, U, U)
bufdata = fn('glBufferData', None, U, S, P, U)
gentex = fn('glGenTextures', None, I, C.POINTER(U)); deltex = fn('glDeleteTextures', None, I, C.POINTER(U))
bindtex = fn('glBindTexture', None, U, U); activetex = fn('glActiveTexture', None, U)
texstorage = fn('glTexStorage2D', None, U, I, U, I, I)
texstorage_ms = fn('glTexStorage2DMultisample', None, U, I, U, I, I, B)
texparam = fn('glTexParameteri', None, U, U, I); texsub = fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)
genmip = fn('glGenerateMipmap', None, U)
genfbo = fn('glGenFramebuffers', None, I, C.POINTER(U)); delfbo = fn('glDeleteFramebuffers', None, I, C.POINTER(U))
bindfbo = fn('glBindFramebuffer', None, U, U); attachtex = fn('glFramebufferTexture2D', None, U, U, U, U, I)
fbstatus = fn('glCheckFramebufferStatus', U, U)
drawbuffers = fn('glDrawBuffers', None, I, C.POINTER(U)); drawbuffer = fn('glDrawBuffer', None, U)
readbuffer = fn('glReadBuffer', None, U)
blit = fn('glBlitFramebuffer', None, I, I, I, I, I, I, I, I, U, U)
getint = fn('glGetIntegerv', None, U, C.POINTER(I)); geterror = fn('glGetError', U)
iformat = fn('glGetInternalformativ', None, U, U, U, I, C.POINTER(I))
loc = fn('glGetUniformLocation', I, U, C.c_char_p); uni1i = fn('glUniform1i', None, I, I)
uni1f = fn('glUniform1f', None, I, F); uni2f = fn('glUniform2f', None, I, F, F)
draw = fn('glDrawArrays', None, U, I, I)
genq = fn('glGenQueries', None, I, C.POINTER(U)); beginq = fn('glBeginQuery', None, U, U); endq = fn('glEndQuery', None, U)
qresult = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
enable = fn('glEnable', None, U); disable = fn('glDisable', None, U)
depthfunc = fn('glDepthFunc', None, U); depthmask = fn('glDepthMask', None, B)
colormask = fn('glColorMask', None, B, B, B, B)
stencilfunc = fn('glStencilFunc', None, U, I, U); stencilop = fn('glStencilOp', None, U, U, U)
stencilmask = fn('glStencilMask', None, U); clearstencil = fn('glClearStencil', None, I)
cleardepth = fn('glClearDepth', None, D); clearcolor = fn('glClearColor', None, F, F, F, F)
clear = fn('glClear', None, U); finish = fn('glFinish', None)
viewport = fn('glViewport', None, I, I, I, I)
readpixels = fn('glReadPixels', None, I, I, I, I, U, U, P)
attribptr = fn('glVertexAttribPointer', None, U, I, U, B, I, P); enattrib = fn('glEnableVertexAttribArray', None, U)
genvao = fn('glGenVertexArrays', None, I, C.POINTER(U)); bindvao = fn('glBindVertexArray', None, U)

TEX2D = 0x0DE1; TEX2DMS = 0x9100; FB = 0x8D40; DRAW_FB = 0x8CA9; READ_FB = 0x8CA8
COLOR0 = 0x8CE0; DEPTH_ATT = 0x8D00; DS_ATT = 0x821A; FB_COMPLETE = 0x8CD5
COLOR_BIT = 0x4000; DEPTH_BIT = 0x100; STENCIL_BIT = 0x400; NEAREST = 0x2600
DEPTH_TEST = 0x0B71; STENCIL_TEST = 0x0B90; BLEND = 0x0BE2; CULL = 0x0B44
LESS = 0x0201; ALWAYS = 0x0207; EQUAL = 0x0202; KEEP = 0x1E00; REPLACE = 0x1E01
TIME_ELAPSED = 0x88BF; SAMPLES_PASSED = 0x8914; QUERY_RESULT = 0x8866
RGBA = 0x1908; FLOAT = 0x1406; NONE = 0
ARRAY_BUFFER = 0x8892; STATIC_DRAW = 0x88E4; TRIANGLES = 4

ext_count = I(); getint(0x821D, C.byref(ext_count))
get_stringi = fn('glGetStringi', C.c_char_p, U, U)
EXTENSIONS = {get_stringi(0x1F03, i).decode() for i in range(ext_count.value)}
HAS_NVX = 'GL_NVX_gpu_memory_info' in EXTENSIONS
RENDERER = get_string(0x1F01).decode(); VERSION = get_string(0x1F02).decode()


def vidmem_free_kib():
    if not HAS_NVX:
        return None
    finish(); v = I(); getint(0x9049, C.byref(v)); return v.value


failures = []


def check(ok, text):
    print(('ok   ' if ok else 'FAIL ') + text, flush=True)
    if not ok:
        failures.append(text)


# --- limits ---
def get1(e):
    v = I(); getint(e, C.byref(v)); return v.value


LIMITS = {'GL_MAX_SAMPLES': get1(0x8D57), 'GL_MAX_COLOR_TEXTURE_SAMPLES': get1(0x910E),
          'GL_MAX_DEPTH_TEXTURE_SAMPLES': get1(0x910F), 'GL_MAX_INTEGER_SAMPLES': get1(0x9110),
          'GL_MAX_DRAW_BUFFERS': get1(0x8824)}
geterror()
FORMAT_SAMPLES = {}
for name, enum in model.GL_FORMATS.items():
    count = I(0); iformat(TEX2DMS, enum, 0x9380, 1, C.byref(count))
    vals = (I * max(count.value, 1))()
    if count.value:
        iformat(TEX2DMS, enum, 0x80A9, count.value, vals)
    FORMAT_SAMPLES[name] = sorted(vals[i] for i in range(count.value))
geterror()
print('limits:', json.dumps(LIMITS), flush=True)
for name, counts in FORMAT_SAMPLES.items():
    print(f'  {name:20s} multisample counts {counts}', flush=True)

# --- shaders ---
FS_TRI_VS = '''#version 430
void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
'''
GFILL_VS = '''#version 430
layout(location=0) in vec3 pos; layout(location=1) in vec4 attr;
out vec3 vn; flat out float vid;
void main(){ gl_Position = vec4(pos.xy, pos.z * 2.0 - 1.0, 1.0); vn = attr.xyz; vid = attr.w; }
'''
GFILL_FS = '''#version 430
in vec3 vn; flat in float vid;
layout(location=0) out vec4 g0; layout(location=1) out vec4 g1;
layout(location=2) out vec4 g2; layout(location=3) out vec4 g3;
vec3 hash3(float x){ return fract(sin(vec3(x * 12.9898, x * 78.233, x * 37.719)) * 43758.5453); }
void main(){
  vec3 n = normalize(vn); vec3 h = hash3(vid + 1.0);
  g0 = vec4(h, 0.0);                                            // diffuse (sRGB-ish albedo)
  g1 = vec4(1.0, 0.15 + 0.8 * h.y, h.z > 0.7 ? 1.0 : 0.0, 0.0);  // ORM
  g2 = vec4(n.xy / sqrt(8.0 * n.z + 8.0) + 0.5, 1.0, mod(vid, 3.0) / 3.0); // encodeNormal + flag
  g3 = vec4(h.x > 0.97 ? h * 4.0 : vec3(0.0), 0.0);              // emissive
}
'''


def common(samples):
    ms = samples > 1
    return f'''#version 430
#define SAMPLES {samples}
#define SAMPLER {'sampler2DMS' if ms else 'sampler2D'}
#define FETCH(t, p, s) texelFetch(t, p, {'s' if ms else '0'})
#define DEPTH_EPS {model.EDGE_DEPTH_EPS}
#define NORMAL_EPS {model.EDGE_NORMAL_EPS}
#define ALBEDO_EPS {model.EDGE_ALBEDO_EPS}
uniform SAMPLER g0, g1, g2, g3, dm;
uniform sampler2D probe; uniform int K; uniform vec2 res;
const vec3 sun_dir = normalize(vec3(0.3, 0.5, 0.8));
vec3 decodeN(vec2 e){{ vec2 f = e * 4.0 - 2.0; float d = dot(f, f); float g = sqrt(max(1.0 - d / 4.0, 0.0)); return vec3(f * g, 1.0 - d / 2.0); }}
float d_ggx(float noh, float a){{ float a2 = a * a; float d = noh * noh * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }}
vec3 shade(ivec2 p, int s){{
  vec4 alb = FETCH(g0, p, s); vec4 orm = FETCH(g1, p, s); vec4 nn = FETCH(g2, p, s);
  vec3 em = FETCH(g3, p, s).rgb; float z = FETCH(dm, p, s).r;
  vec3 n = decodeN(nn.xy); vec2 uv = (vec2(p) + 0.5) / res;
  vec3 pos = vec3(uv * 2.0 - 1.0, z * 2.0 - 1.0) * vec3(10.0, 10.0, 50.0);
  vec3 v = normalize(vec3(0.0, 0.0, 60.0) - pos);
  float rough = max(orm.g, 0.05); float a = rough * rough; float metal = orm.b;
  vec3 base = alb.rgb * alb.rgb; vec3 f0 = mix(vec3(0.04), base, metal);
  vec3 col = vec3(0.0);
  for (int i = 0; i <= K; ++i) {{
    vec3 lp = i == 0 ? pos + sun_dir * 1000.0 : vec3(sin(float(i) * 1.7) * 9.0, cos(float(i) * 2.3) * 9.0, 5.0 + 3.0 * sin(float(i) * 0.9));
    vec3 lv = lp - pos; vec3 l = normalize(lv); float d2 = i == 0 ? 1.0 : max(dot(lv, lv), 0.1);
    vec3 h = normalize(l + v);
    float nol = max(dot(n, l), 0.0); float noh = max(dot(n, h), 0.0); float voh = max(dot(v, h), 0.0);
    vec3 fr = f0 + (1.0 - f0) * pow(1.0 - voh, 5.0);
    vec3 lc = i == 0 ? vec3(3.0, 2.8, 2.5) : vec3(20.0) / d2;
    col += (base * (1.0 - metal) / 3.14159 + fr * d_ggx(noh, a) * 0.25) * lc * nol;
  }}
  vec3 r = reflect(-v, n); vec2 puv = vec2(atan(r.y, r.x) / 6.2832 + 0.5, r.z * 0.5 + 0.5);
  col += textureLod(probe, puv, rough * 7.0).rgb * f0 * orm.r + textureLod(probe, n.xy * 0.5 + 0.5, 7.0).rgb * base * orm.r;
  return col + em;
}}
bool is_edge(ivec2 p){{
  float z0 = FETCH(dm, p, 0).r; vec4 n0 = FETCH(g2, p, 0); vec3 a0 = FETCH(g0, p, 0).rgb;
  for (int s = 1; s < SAMPLES; ++s) {{
    float z = FETCH(dm, p, s).r; vec4 n = FETCH(g2, p, s); vec3 a = FETCH(g0, p, s).rgb;
    if (abs(z - z0) > DEPTH_EPS || any(greaterThan(abs(n.xy - n0.xy), vec2(NORMAL_EPS))) || n.w != n0.w
        || any(greaterThan(abs(a - a0), vec3(ALBEDO_EPS)))) return true;
  }}
  return false;
}}
bool is_edge_dn(ivec2 p){{   // the cheaper rule: depth and normal/flag only
  float z0 = FETCH(dm, p, 0).r; vec4 n0 = FETCH(g2, p, 0);
  for (int s = 1; s < SAMPLES; ++s) {{
    float z = FETCH(dm, p, s).r; vec4 n = FETCH(g2, p, s);
    if (abs(z - z0) > DEPTH_EPS || any(greaterThan(abs(n.xy - n0.xy), vec2(NORMAL_EPS))) || n.w != n0.w) return true;
  }}
  return false;
}}
'''


VARIANT_MAIN = {
    'pixel': 'out vec4 frag; void main(){ frag = vec4(shade(ivec2(gl_FragCoord.xy), 0), 0.0); }',
    'a_sample': 'out vec4 frag; void main(){ frag = vec4(shade(ivec2(gl_FragCoord.xy), gl_SampleID), 0.0); }',
    'b_all': '''out vec4 frag; void main(){ ivec2 p = ivec2(gl_FragCoord.xy); vec3 c = vec3(0.0);
      for (int s = 0; s < SAMPLES; ++s) c += shade(p, s); frag = vec4(c / float(SAMPLES), 0.0); }''',
    'b_edge': '''out vec4 frag; void main(){ ivec2 p = ivec2(gl_FragCoord.xy); vec3 c;
      if (is_edge(p)) { c = vec3(0.0); for (int s = 0; s < SAMPLES; ++s) c += shade(p, s); c /= float(SAMPLES); }
      else c = shade(p, 0);
      frag = vec4(c, 0.0); }''',
    'classify': 'void main(){ if (!is_edge(ivec2(gl_FragCoord.xy))) discard; }',
    'classify_dn': 'void main(){ if (!is_edge_dn(ivec2(gl_FragCoord.xy))) discard; }',
}

RESOLVE_TONE_FS = '''#version 430
#define SAMPLES %d
uniform sampler2DMS src; uniform float exposure; out vec4 frag;
void main(){ ivec2 p = ivec2(gl_FragCoord.xy); vec3 acc = vec3(0.0); float wsum = 0.0, alpha = 0.0;
  for (int s = 0; s < SAMPLES; ++s) { vec4 c = texelFetch(src, p, s);
    float w = 1.0 / (1.0 + exposure * max(c.r, max(c.g, c.b))); acc += c.rgb * w; wsum += w; alpha += c.a; }
  frag = vec4(acc / wsum, alpha / float(SAMPLES)); }   // rgb tonemap-weighted, alpha (glow) the plain mean
'''
RESOLVE_DEPTH_FS = '''#version 430
uniform sampler2DMS dm;
void main(){ gl_FragDepth = texelFetch(dm, ivec2(gl_FragCoord.xy), 0).r; }
'''
PATTERN_FS = '''#version 430
uniform int samples; out vec4 frag;
void main(){ ivec2 p = ivec2(gl_FragCoord.xy); int s = gl_SampleID;
  float v = float((p.x * 7 + p.y * 13 + s * 5) %% 17) / 16.0;
  float bright = s == (p.x + p.y) %% samples ? 40.0 : 0.0;
  frag = vec4(v + bright, 0.5 * v, 0.25 + 0.1 * float(s), float(s + 1) / float(samples)); }
'''.replace('%%', '%')

VS, FS = 0x8B31, 0x8B30
prog_cache = {}


def prog(key, vs, fs):
    if key not in prog_cache:
        prog_cache[key] = program([(VS, vs), (FS, fs)])
    return prog_cache[key]


def light_prog(variant, samples):
    p = prog((variant, samples), FS_TRI_VS, common(samples) + VARIANT_MAIN[variant])
    use(p)
    for i, n in enumerate(('g0', 'g1', 'g2', 'g3', 'dm')):
        uni1i(loc(p, n.encode()), i)
    uni1i(loc(p, b'probe'), 5)
    return p


# --- resources ---
vao = U(); genvao(1, C.byref(vao)); bindvao(vao)


def make_probe():
    activetex(0x84C0 + 5)
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t); texstorage(TEX2D, 9, 0x881A, 256, 256)
    data = array.array('f')
    for y in range(256):
        for x in range(256):
            data.extend((0.5 + x / 128.0, 0.4 + y / 160.0, 1.2 - x / 512.0, 1.0))
    texsub(TEX2D, 0, 0, 0, 256, 256, RGBA, FLOAT, data.buffer_info()[0])
    genmip(TEX2D)
    texparam(TEX2D, 0x2801, 0x2703); texparam(TEX2D, 0x2800, 0x2601)
    activetex(0x84C0)
    return t.value


PROBE = make_probe()
scene_vbo = {}


def scene_buffer(name):
    if name not in scene_vbo:
        tris = model.scene_triangles(name)
        ground = [((-1, -1), (3, -1), (-1, 3))]
        data = array.array('f')
        for corners in ground:
            for x, y in corners:
                data.extend((x, y, 0.99, 0.0, 0.0, 1.0, 4095.0))
        for tri in tris:
            for v in tri:
                data.extend(v)
        b = U(); genbuf(1, C.byref(b)); bindbuf(ARRAY_BUFFER, b)
        bufdata(ARRAY_BUFFER, len(data) * 4, data.buffer_info()[0], STATIC_DRAW)
        scene_vbo[name] = (b.value, len(data) // 7)
    return scene_vbo[name]


def tex2d(fmt, w, h, samples):
    t = U(); gentex(1, C.byref(t))
    if samples > 1:
        bindtex(TEX2DMS, t); texstorage_ms(TEX2DMS, samples, fmt, w, h, 1)
    else:
        bindtex(TEX2D, t); texstorage(TEX2D, 1, fmt, w, h)
        texparam(TEX2D, 0x2801, 0x2600); texparam(TEX2D, 0x2800, 0x2600)
    return t.value


def fbo(colors, ds=None, ds_att=DS_ATT, samples=1):
    f = U(); genfbo(1, C.byref(f)); bindfbo(FB, f)
    target = TEX2DMS if samples > 1 else TEX2D
    for i, c in enumerate(colors):
        attachtex(FB, COLOR0 + i, target, c, 0)
    if ds:
        attachtex(FB, ds_att, target, ds, 0)
    if colors:
        bufs = (U * len(colors))(*[COLOR0 + i for i in range(len(colors))]); drawbuffers(len(colors), bufs)
    else:
        drawbuffer(NONE); readbuffer(NONE)
    return f.value, fbstatus(FB)


class Targets:
    def __init__(self, w, h, n):
        self.w, self.h, self.n = w, h, n
        before = vidmem_free_kib()
        layout = model.gbuffer_layout(stencil=True)
        self.g = [tex2d(model.GL_FORMATS[f], w, h, n) for _, f in layout[:-1]]
        self.ds = tex2d(model.GL_FORMATS['DEPTH24_STENCIL8'], w, h, n)
        self.gfbo, st = fbo(self.g, self.ds, samples=n)
        check(st == FB_COMPLETE, f'G-buffer {w}x{h} x{n} complete (0x{st:04X})')
        self.cfbo, st = fbo([], self.ds, samples=n)
        check(st == FB_COMPLETE, f'classification fbo x{n} complete (0x{st:04X})')
        self.screen1 = tex2d(0x881A, w, h, 1)
        self.rfbo, st = fbo([self.screen1], None)
        self.screen_ms = None
        self.depth1 = self.dfbo = None
        if n > 1:   # the resolve targets exist only under MSAA, as msaa_memory() counts them
            self.depth1 = tex2d(model.GL_FORMATS['DEPTH_COMPONENT24'], w, h, 1)
            self.dfbo, _ = fbo([], self.depth1, ds_att=DEPTH_ATT)
            self.screen_ms = tex2d(0x881A, w, h, n)
            self.sfbo, st = fbo([self.screen_ms], self.ds, samples=n)
            check(st == FB_COMPLETE, f'MS scene colour fbo x{n} complete (0x{st:04X})')
        after = vidmem_free_kib()
        self.measured_mib = None if before is None else (before - after) / 1024.0
        self.bind_inputs()

    def bind_inputs(self):
        target = TEX2DMS if self.n > 1 else TEX2D
        for i, t in enumerate(self.g + [self.ds]):
            activetex(0x84C0 + i); bindtex(target, t)
        activetex(0x84C0 + 5); bindtex(TEX2D, PROBE); activetex(0x84C0)

    def release(self):
        texs = self.g + [self.ds, self.screen1] + ([self.screen_ms, self.depth1] if self.n > 1 else [])
        arr = (U * len(texs))(*texs); deltex(len(texs), arr)
        fbos = [self.gfbo, self.cfbo, self.rfbo] + ([self.sfbo, self.dfbo] if self.n > 1 else [])
        arr = (U * len(fbos))(*fbos); delfbo(len(fbos), arr)


# --- passes ---
def gfill(t, scene):
    vbo, count = scene_buffer(scene)
    bindfbo(FB, t.gfbo); viewport(0, 0, t.w, t.h)
    colormask(1, 1, 1, 1); depthmask(1); stencilmask(0xFF)
    clearcolor(1.0, 0.0, 1.0, 1.0); cleardepth(1.0); clearstencil(0)
    clear(COLOR_BIT | DEPTH_BIT | STENCIL_BIT)
    enable(DEPTH_TEST); depthfunc(LESS); disable(STENCIL_TEST)
    use(prog('gfill', GFILL_VS, GFILL_FS))
    bindbuf(ARRAY_BUFFER, vbo)
    attribptr(0, 3, FLOAT, 0, 28, None); enattrib(0)
    attribptr(1, 4, FLOAT, 0, 28, C.c_void_p(12)); enattrib(1)
    draw(TRIANGLES, 0, count)
    disable(DEPTH_TEST)


def fullscreen():
    draw(TRIANGLES, 0, 3)


def lighting_state(p, t, k):
    uni1i(loc(p, b'K'), k); uni2f(loc(p, b'res'), float(t.w), float(t.h))


def classify(t, rule='classify'):
    bindfbo(FB, t.cfbo); viewport(0, 0, t.w, t.h)
    depthmask(0); stencilmask(0xFF); clearstencil(0); clear(STENCIL_BIT)
    enable(STENCIL_TEST); stencilfunc(ALWAYS, 1, 0xFF); stencilop(KEEP, KEEP, REPLACE)
    light_prog(rule, t.n); fullscreen()
    disable(STENCIL_TEST)


def run_pixel(t, k):
    bindfbo(FB, t.rfbo); viewport(0, 0, t.w, t.h)
    p = light_prog('pixel', t.n); lighting_state(p, t, k); fullscreen()


def run_a_all(t, k):
    bindfbo(FB, t.sfbo); viewport(0, 0, t.w, t.h); depthmask(0); stencilmask(0)
    p = light_prog('a_sample', t.n); lighting_state(p, t, k); fullscreen()


def run_a_edge(t, k):
    bindfbo(FB, t.sfbo); viewport(0, 0, t.w, t.h); depthmask(0); stencilmask(0)
    enable(STENCIL_TEST); stencilop(KEEP, KEEP, KEEP)
    stencilfunc(EQUAL, 0, 0xFF)
    p = light_prog('pixel', t.n); lighting_state(p, t, k); fullscreen()
    stencilfunc(EQUAL, 1, 0xFF)
    p = light_prog('a_sample', t.n); lighting_state(p, t, k); fullscreen()
    disable(STENCIL_TEST)


def run_b(variant):
    def run(t, k):
        bindfbo(FB, t.rfbo); viewport(0, 0, t.w, t.h)
        p = light_prog(variant, t.n); lighting_state(p, t, k); fullscreen()
    return run


def resolve_blit(t):
    bindfbo(READ_FB, t.sfbo); readbuffer(COLOR0); bindfbo(DRAW_FB, t.rfbo)
    blit(0, 0, t.w, t.h, 0, 0, t.w, t.h, COLOR_BIT, NEAREST)


def resolve_tone(t, src=None, dst=None, n=None, w=None, h=None):
    n = n or t.n
    bindfbo(FB, dst or t.rfbo); viewport(0, 0, w or t.w, h or t.h)
    p = prog(('tone', n), FS_TRI_VS, RESOLVE_TONE_FS % n); use(p)
    activetex(0x84C0 + 6); bindtex(TEX2DMS, src or t.screen_ms); activetex(0x84C0)
    uni1i(loc(p, b'src'), 6); uni1f(loc(p, b'exposure'), 1.0)
    fullscreen()


def resolve_depth(t):
    bindfbo(FB, t.dfbo); viewport(0, 0, t.w, t.h)
    enable(DEPTH_TEST); depthfunc(ALWAYS); depthmask(1)
    p = prog('rdepth', FS_TRI_VS, RESOLVE_DEPTH_FS); use(p); uni1i(loc(p, b'dm'), 4)
    fullscreen()
    disable(DEPTH_TEST); depthfunc(LESS)


query_pool = (U * (FRAMES + WARMUP))(); genq(FRAMES + WARMUP, query_pool)


def timed(fn_):
    for i in range(FRAMES + WARMUP):
        beginq(TIME_ELAPSED, query_pool[i]); fn_(); endq(TIME_ELAPSED)
    out = []
    for i in range(WARMUP, FRAMES + WARMUP):
        v = C.c_uint64(); qresult(query_pool[i], QUERY_RESULT, C.byref(v)); out.append(v.value / 1e6)
    return model.median(out)


def edge_fraction(t):
    q = U(); genq(1, C.byref(q))
    beginq(SAMPLES_PASSED, q); classify(t); endq(SAMPLES_PASSED)
    v = C.c_uint64(); qresult(q, QUERY_RESULT, C.byref(v))
    return v.value / float(t.n * t.w * t.h)


def read_window(fbo_, w, h):
    bindfbo(READ_FB, fbo_); readbuffer(COLOR0)
    x0, y0 = (w - WIN) // 2, (h - WIN) // 2
    buf = (C.c_float * (WIN * WIN * 4))()
    readpixels(x0, y0, WIN, WIN, RGBA, FLOAT, buf)
    return list(buf)


def tonemapped_error(a, b):
    """Max and RMSE in 8-bit units after a per-channel Reinhard x/(1+x)."""
    worst, sq, n = 0.0, 0.0, 0
    for i in range(0, len(a), 4):
        for c in range(3):
            x, y = a[i + c], b[i + c]
            d = abs(x / (1.0 + x) - y / (1.0 + y)) * 255.0
            worst = max(worst, d); sq += d * d; n += 1
    return worst, (sq / n) ** 0.5


def check_resolve_pattern(n):
    w, h = 32, 16
    ms = tex2d(0x881A, w, h, n); one = tex2d(0x881A, w, h, 1)
    mfbo, st = fbo([ms], None, samples=n); ofbo, _ = fbo([one], None)
    bindfbo(FB, mfbo); viewport(0, 0, w, h)
    p = prog('pattern', FS_TRI_VS, PATTERN_FS); use(p); uni1i(loc(p, b'samples'), n); fullscreen()
    resolve_tone(None, src=ms, dst=ofbo, n=n, w=w, h=h)
    bindfbo(READ_FB, ofbo); readbuffer(COLOR0)
    buf = (C.c_float * (w * h * 4))(); readpixels(0, 0, w, h, RGBA, FLOAT, buf)
    worst = 0.0
    for y in range(h):
        for x in range(w):
            want = model.tonemap_weighted_resolve([model.resolve_sample_color(x, y, s, n) for s in range(n)])
            got = buf[(y * w + x) * 4:(y * w + x) * 4 + 4]
            worst = max(worst, max(abs(a - b) / max(1.0, abs(a)) for a, b in zip(want, got)))
    check(worst < 2e-3, f'tonemap-weighted resolve x{n} matches the CPU model (max rel err {worst:.2e})')
    arr = (U * 2)(ms, one); deltex(2, arr); arr = (U * 2)(mfbo, ofbo); delfbo(2, arr)


# --- run ---
results = {'renderer': RENDERER, 'version': VERSION, 'limits': LIMITS, 'format_samples': FORMAT_SAMPLES,
           'runs': [], 'memory': []}
print(f'renderer: {RENDERER} | {VERSION}', flush=True)
usable = [n for n in SAMPLES if n == 1 or n <= min(LIMITS['GL_MAX_COLOR_TEXTURE_SAMPLES'], LIMITS['GL_MAX_DEPTH_TEXTURE_SAMPLES'])]
for n in usable:
    if n > 1:
        check_resolve_pattern(n)

for (w, h) in RES:
    for n in usable:
        t = Targets(w, h, n)
        mem = model.msaa_memory(w, h, n)
        results['memory'].append({'w': w, 'h': h, 'samples': n, 'model_mib': model.mib(mem['total']),
                                  'measured_mib': t.measured_mib})
        print(f'\n== {w}x{h} x{n}: targets {model.mib(mem["total"]):.1f} MiB (format arithmetic)'
              + (f', measured {t.measured_mib:.1f} MiB' if t.measured_mib is not None else ''), flush=True)
        for scene in SCENE_NAMES:
            row = {'w': w, 'h': h, 'samples': n, 'scene': scene}
            row['gfill'] = timed(lambda: gfill(t, scene))
            gfill(t, scene)
            if n > 1:
                row['edge_fraction'] = edge_fraction(t)
                row['classify'] = timed(lambda: classify(t))
                row['classify_dn'] = timed(lambda: classify(t, 'classify_dn'))
                row['resolve_depth'] = timed(lambda: resolve_depth(t))
                classify(t)
            for k in LIGHTS:
                lk = dict(row); lk['lights'] = k
                if n == 1:
                    lk['pixel'] = timed(lambda: run_pixel(t, k))
                else:
                    # The resolve cost depends on what the lighting left in the MS colour (the
                    # driver's colour compression), so it is timed after each design A variant.
                    lk['a_all'] = timed(lambda: run_a_all(t, k))
                    lk['resolve_blit_all'] = timed(lambda: resolve_blit(t))
                    lk['resolve_tone_all'] = timed(lambda: resolve_tone(t))
                    lk['a_edge'] = timed(lambda: run_a_edge(t, k))
                    lk['resolve_blit'] = timed(lambda: resolve_blit(t))
                    lk['resolve_tone'] = timed(lambda: resolve_tone(t))
                    lk['b_all'] = timed(lambda: run_b('b_all')(t, k))
                    lk['b_edge'] = timed(lambda: run_b('b_edge')(t, k))
                    if (w, h) == RES[0] and k == LIGHTS[0]:
                        run_a_all(t, k); resolve_blit(t); ref = read_window(t.rfbo, w, h)
                        run_a_edge(t, k); resolve_blit(t); got = read_window(t.rfbo, w, h)
                        mx, rm = tonemapped_error(ref, got)
                        check(rm < 0.25 and mx < 8.0, f'{scene} x{n}: a_edge vs a_all max {mx:.2f} rmse {rm:.3f} (8-bit)')
                        run_b('b_all')(t, k); got = read_window(t.rfbo, w, h)
                        mx, rm = tonemapped_error(ref, got)
                        check(rm < 0.1 and mx < 2.0, f'{scene} x{n}: b_all vs a_all+box max {mx:.2f} rmse {rm:.3f} (8-bit)')
                        run_b('b_edge')(t, k); got = read_window(t.rfbo, w, h)
                        mx, rm = tonemapped_error(ref, got)
                        check(rm < 0.25 and mx < 8.0, f'{scene} x{n}: b_edge vs a_all+box max {mx:.2f} rmse {rm:.3f} (8-bit)')
                results['runs'].append(lk)
                cols = ' '.join(f'{key} {lk[key]:.3f}' for key in
                                ('gfill', 'classify', 'classify_dn', 'pixel', 'a_all', 'resolve_blit_all', 'resolve_tone_all',
                                 'a_edge', 'resolve_blit', 'resolve_tone', 'b_all', 'b_edge', 'resolve_depth')
                                if key in lk)
                ef = f" edges {lk['edge_fraction'] * 100:.1f}%" if 'edge_fraction' in lk else ''
                print(f'  {scene:8s} K={k:2d}{ef} | {cols}', flush=True)
        err = geterror()
        check(err == 0, f'{w}x{h} x{n}: no GL error (0x{err:04X})')
        t.release()

# --- summary: scene-pass ms (fill + classify + lighting + resolve) per design ---
print('\n| res | scene | K | edges | 1x | A-all 2x | A-edge 2x | B-edge 2x | A-all 4x | A-edge 4x | B-edge 4x | A-all 8x | A-edge 8x | B-edge 8x |')
print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
for (w, h) in RES:
    for scene in SCENE_NAMES:
        for k in LIGHTS:
            runs = {r['samples']: r for r in results['runs'] if (r['w'], r['h'], r['scene'], r['lights']) == (w, h, scene, k)}
            if 1 not in runs:
                continue
            cells = [f"{model.frame_estimate(runs[1], 1, 'pixel'):.2f}"]
            edges = []
            for n in (2, 4, 8):
                if n not in runs:
                    cells += ['-'] * 3; continue
                r = runs[n]; edges.append(f"{r['edge_fraction'] * 100:.0f}")
                cells += [f"{model.frame_estimate(r, n, v):.2f}" for v in ('a_all', 'a_edge', 'b_edge')]
            print(f"| {w}x{h} | {scene} | {k} | {'/'.join(edges)}% | " + ' | '.join(cells) + ' |')

if os.environ.get('MSAA_BENCH_JSON'):
    Path(os.environ['MSAA_BENCH_JSON']).write_text(json.dumps(results, indent=1))
print(f'\n{len(failures)} failure(s)' if failures else '\nall checks passed', flush=True)
sys.exit(1 if failures else 0)
