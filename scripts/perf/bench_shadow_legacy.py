# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU harness and check of the Legacy shadow method's sun light pass, no viewer launch.

Also the scene machinery of bench_shadow_stochastic.py, test_shadow_bias_altitude.py and
test_spot_shadow_fade.py, which run the part of this file before "timing + basic checks".

Renders synthetic scenes (a ground plane and boxes at known heights, ray cast in a fragment
shader) into
  * four sun cascade depth maps, GL_DEPTH_COMPONENT24 at the viewer's size for the screen
    (BlurHappySize: 1936x1088 at 1080p, 2576x1456 at 1440p), orthographic fits of the
    cascades' frustum sections, compare mode and filtering set as LLPipeline::allocateShadowBuffer
    sets them (linear, clamp, GL_COMPARE_REF_TO_TEXTURE, GL_LEQUAL);
  * two spot (projector) depth maps, perspective, screen width square;
  * a G-buffer of view space positions and normals at 1080p and 1440p;
then times the sun light pass (class2 deferred/sunLightF.glsl + class1 deferred/shadowUtil.glsl)
at RenderShadowDetail 2 (SUN_SHADOW and SPOT_SHADOW) and 1 (SUN_SHADOW) with GL_TIME_ELAPSED
queries for
  * "base"  -- both shaders from git ref SHADOW_BENCH_BASE_REF (default b9e01a2ca2, the devel tip
               stage S2 was cut from)
  * "tree"  -- this working tree's shaders
  * "legacy" -- the shadow method Legacy (RenderShadowMethod 0): its shaders are the working tree's.
               It is a labelled alias of "tree": the same shaders and defines, identical by
               construction (it is timed and checked like the others, but nothing compares it
               with "tree")
The stage S2 constants (SHADOW_FOOTPRINT_M, ...) are read from indra/newview/llshadowcascadefit.h,
the way the shader manager passes them. The timing interleaves the variants frame by frame (the GPU
clock drifts by tens of microseconds between sequential runs) and reports the median ms per pass.
getNorm is the benchmark's: it reads the synthetic G-buffer's normal (RGBA32F) instead of decoding the
viewer's packed one. getPosition is the viewer's (deferredUtil.glsl): a 24 bit depth texture, nearest, and
inv_proj. The camera's planes are 0.25 and 256 m.

Checks on the light map (RGBA16F, read back): on the slanted sun scene, lit ground far from the boxes
is 1.0, ground in the middle of a floating box's shadow is 0.0, and "tree" and "legacy" equal "base"
(the devel tip) bit for bit. The method-neutral block (SHADOW_COMMON) must compile as GLSL 1.50.

Environment: SHADOW_BENCH_BASE_REF, SHADOW_BENCH_FRAMES (default 30), SHADOW_BENCH_RES (default
"1920x1080,2560x1440"). Pass --opengl PATH to select another GL DLL (Mesa).
"""
import math, os, re, statistics, subprocess, sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
# the devel tip stage S2 was cut from (the #453 merge): the Legacy shadowUtil.glsl it must equal
BASE_REF = os.environ.get('SHADOW_BENCH_BASE_REF', 'b9e01a2ca2')
FRAMES = int(os.environ.get('SHADOW_BENCH_FRAMES', 30)); WARMUP = 5
RESOLUTIONS = [tuple(int(v) for v in r.split('x')) for r in os.environ.get('SHADOW_BENCH_RES', '1920x1080,2560x1440').split(',')]
SHADERS = 'indra/newview/app_settings/shaders/'
SUN_F = SHADERS + 'class2/deferred/sunLightF.glsl'
UTIL_F = SHADERS + 'class1/deferred/shadowUtil.glsl'

# viewer defaults (settings.xml)
SHADOW_BIAS = -0.002; SHADOW_OFFSET = 0.01; SPOT_SHADOW_BIAS = -0.0002; SPOT_SHADOW_OFFSET = 0.04
LIGHT_SIZE = 2.0 * math.tan(math.radians(0.53) / 2.0)
DISC_10_90 = 0.6870488            # 10-90 % width of a disc light's edge profile / full width

# --- GL entry points ----------------------------------------------------------------------
F=C.c_float
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
geterror=fn('glGetError',U);finish=fn('glFinish',None);readpixels=fn('glReadPixels',None,I,I,I,I,U,U,P)
loc=fn('glGetUniformLocation',I,U,C.c_char_p)
u1i=fn('glUniform1i',None,I,I);u1f=fn('glUniform1f',None,I,F);u2f=fn('glUniform2f',None,I,F,F)
u3f=fn('glUniform3f',None,I,F,F,F);u4f=fn('glUniform4f',None,I,F,F,F,F)
umat=fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(F))
genq=fn('glGenQueries',None,I,C.POINTER(U));beginq=fn('glBeginQuery',None,U,U);endq=fn('glEndQuery',None,U)
qresult=fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64))
gensampler=fn('glGenSamplers',None,I,C.POINTER(U));samplerparam=fn('glSamplerParameteri',None,U,U,I)
bindsampler=fn('glBindSampler',None,U,U)
TEX2D=0x0DE1;FB=0x8D40;DEPTH_ATT=0x8D00;COLOR0=0x8CE0;TIME_ELAPSED=0x88BF;QUERY_RESULT=0x8866
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
# a depth fetch sampler (compare off, nearest, clamp): the Stochastic bench reads the shadow maps through it
depth_fetch=U();gensampler(1,C.byref(depth_fetch))
for pname,value in ((0x884C,0),(0x2801,0x2600),(0x2800,0x2600),(0x2802,0x812F),(0x2803,0x812F)):
    samplerparam(depth_fetch,pname,value)

# --- small matrix helpers (row major lists, uploaded transposed) ---------------------------
def mmul(a, b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def mvec(m, v): return [sum(m[i][k]*v[k] for k in range(4)) for i in range(4)]
def sub(a, b): return [a[i]-b[i] for i in range(3)]
def dot3(a, b): return sum(a[i]*b[i] for i in range(3))
def cross3(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def normalize(a): l = math.sqrt(dot3(a, a)); return [x/l for x in a]
def look_at(eye, target, up=(0.0, 1.0, 0.0)):
    f = normalize(sub(target, eye)); s = normalize(cross3(f, list(up))); u = cross3(s, f)
    return [[s[0], s[1], s[2], -dot3(s, eye)], [u[0], u[1], u[2], -dot3(u, eye)],
            [-f[0], -f[1], -f[2], dot3(f, eye)], [0.0, 0.0, 0.0, 1.0]]
def perspective(fovy, aspect, n, f):
    t = 1.0/math.tan(fovy/2.0)
    return [[t/aspect, 0, 0, 0], [0, t, 0, 0], [0, 0, (f+n)/(n-f), 2*f*n/(n-f)], [0, 0, -1, 0]]
def ortho(l, r, b, t, n, f):
    return [[2/(r-l), 0, 0, -(r+l)/(r-l)], [0, 2/(t-b), 0, -(t+b)/(t-b)], [0, 0, -2/(f-n), -(f+n)/(f-n)], [0, 0, 0, 1]]
def inverse(m):
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(4)] for i, row in enumerate(m)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c])); a[c], a[p] = a[p], a[c]
        d = a[c][c]; a[c] = [x/d for x in a[c]]
        for r in range(4):
            if r != c:
                k = a[r][c]; a[r] = [x - k*y for x, y in zip(a[r], a[c])]
    return [row[4:] for row in a]
TEXSPACE = [[0.5, 0, 0, 0.5], [0, 0.5, 0, 0.5], [0, 0, 0.5, 0.5], [0, 0, 0, 1]]
def set_mat(prog, name, mats):
    flat = [x for m in mats for row in m for x in row]
    umat(loc(prog, name.encode()), len(mats), 1, (F*len(flat))(*flat))

# --- shaders -------------------------------------------------------------------------------
def scene_glsl(boxes):
    return '''
const int BOX_COUNT = %d;
const vec3 box_min[BOX_COUNT] = vec3[BOX_COUNT](%s);
const vec3 box_max[BOX_COUNT] = vec3[BOX_COUNT](%s);
// nearest hit of the world ray o + t d (t in [0, tmax]): ground plane y = 0 and the boxes
bool trace(vec3 o, vec3 d, float tmax, out float t_hit, out vec3 n_hit)
{
    t_hit = tmax; n_hit = vec3(0.0); bool hit = false;
    if (d.y < 0.0) { float t = -o.y / d.y; if (t >= 0.0 && t < t_hit) { t_hit = t; n_hit = vec3(0, 1, 0); hit = true; } }
    for (int i = 0; i < BOX_COUNT; ++i)
    {
        vec3 inv = 1.0 / d;
        vec3 t0 = (box_min[i] - o) * inv, t1 = (box_max[i] - o) * inv;
        vec3 tn = min(t0, t1), tf = max(t0, t1);
        float a = max(max(tn.x, tn.y), tn.z), b = min(min(tf.x, tf.y), tf.z);
        if (a <= b && a >= 0.0 && a < t_hit)
        {
            t_hit = a; hit = true;
            n_hit = (a == tn.x) ? vec3(-sign(d.x), 0, 0) : (a == tn.y) ? vec3(0, -sign(d.y), 0) : vec3(0, 0, -sign(d.z));
        }
    }
    return hit;
}
''' % (len(boxes), ', '.join('vec3(%r, %r, %r)' % b[0] for b in boxes), ', '.join('vec3(%r, %r, %r)' % b[1] for b in boxes))

VERTEX = '''#version 430 core
out vec2 vary_fragcoord;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0; gl_Position = vec4(p, 0.0, 1.0); vary_fragcoord = p * 0.5 + 0.5; }
'''
GBUFFER_F = '''#version 430 core
in vec2 vary_fragcoord;
uniform mat4 inv_proj; uniform mat4 view; uniform mat4 inv_view;
layout(location = 0) out vec4 out_pos;
layout(location = 1) out vec4 out_norm;
uniform mat4 proj;
uniform float bump_deg;   // tilts the stored normal like a normal map would, by this many degrees, in a pattern over the surface
%s
void main()
{
    gl_FragDepth = 1.0;
    vec4 p = inv_proj * vec4(vary_fragcoord * 2.0 - 1.0, -1.0, 1.0);
    vec3 dir_v = normalize(p.xyz / p.w);
    vec3 o = (inv_view * vec4(0, 0, 0, 1)).xyz, d = normalize((inv_view * vec4(dir_v, 0)).xyz);
    float t; vec3 n;
    if (trace(o, d, 1000.0, t, n)) { out_pos = view * vec4(o + d * t, 1.0);
        vec3 w = o + d * t; vec3 nb = n;
        if (bump_deg > 0.0) { vec3 a = abs(n.y) > 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0); vec3 u = normalize(cross(n, a)), v = cross(n, u);
            float ph = 41.0 * dot(w, u) + 29.0 * dot(w, v), tilt = radians(bump_deg) * (0.75 + 0.25 * sin(7.0 * ph));
            nb = normalize(n * cos(tilt) + (u * cos(ph) + v * sin(ph)) * sin(tilt)); }
        out_norm = vec4(normalize((view * vec4(nb, 0)).xyz), 0.0);
        vec4 c = proj * out_pos; gl_FragDepth = c.z / c.w * 0.5 + 0.5; }
    else { out_pos = vec4(dir_v * 1000.0, 1.0); out_norm = vec4(0.0); }
}
'''
# depth map by ray casting: inv_light maps the map's clip space back to world; ortho rays are
# parallel, perspective rays start at the projector; depth is the clip-space depth of the hit
SHADOW_F = '''#version 430 core
in vec2 vary_fragcoord;
uniform mat4 light_proj_view; uniform mat4 inv_light;
%s
void main()
{
    vec2 ndc = vary_fragcoord * 2.0 - 1.0;
    vec4 a = inv_light * vec4(ndc, -1.0, 1.0); a /= a.w;
    vec4 b = inv_light * vec4(ndc, 1.0, 1.0); b /= b.w;
    vec3 d = b.xyz - a.xyz; float len = length(d); d /= len;
    float t; vec3 n;
    if (trace(a.xyz, d, len, t, n)) { vec4 c = light_proj_view * vec4(a.xyz + d * t, 1.0); gl_FragDepth = c.z / c.w * 0.5 + 0.5; }
    else gl_FragDepth = 1.0;
}
'''
STUBS_F = '''#version 430 core
uniform sampler2D depthMap;
uniform sampler2D normalMap;
uniform mat4 inv_proj;
vec4 getPosition(vec2 pos_screen)   // deferredUtil.glsl
{
    float depth = texture(depthMap, pos_screen).r;
    vec4 ndc = vec4(pos_screen * 2.0 - 1.0, 2.0 * depth - 1.0, 1.0);
    vec4 pos = inv_proj * ndc;
    pos /= pos.w;
    pos.w = 1.0;
    return pos;
}
vec4 getNorm(vec2 pos_screen) { return vec4(texture(normalMap, pos_screen).xyz, 0.0); }
'''
# stage S2 constants, as the shader manager passes them (llshadowcascadefit.h is the one source)
S2_CONSTANTS = {m.group(1): m.group(2) for m in re.finditer(r'constexpr float (SHADOW_\w+) = ([0-9.]+)f;', (root/'indra/newview/llshadowcascadefit.h').read_text())}
assert set(S2_CONSTANTS) >= {'SHADOW_FOOTPRINT_M', 'SHADOW_BIAS_TEXELS', 'SHADOW_NORMAL_OFFSET_TEXELS', 'SHADOW_PLANE_BIAS_MAX_TEXELS'}, S2_CONSTANTS
S2_DEFINES = [f'{k}={v}' for k, v in S2_CONSTANTS.items()]
VARIANTS = ['base', 'tree', 'legacy']
VARIANT_DEFINES = {'base': [], 'tree': [], 'legacy': []}
FROM_REF = ('base',)   # this one uses the git ref's shaders, the others the working tree's

def git_show(ref, path):
    return subprocess.run(['git', 'show', f'{ref}:{path}'], cwd=root, capture_output=True, text=True, check=True).stdout
def stage(text, defines):
    head = '#version 430 core\n#define FRAGMENT_SHADER 1\n' + ''.join('#define ' + (d.replace('=', ' ') if '=' in d else d + ' 1') + '\n' for d in defines)
    return head + text.replace('/*[EXTRA_CODE_HERE]*/', '')
def sun_program(variant, spot, overrides={}):
    sun = git_show(BASE_REF, SUN_F) if variant in FROM_REF else (root/SUN_F).read_text()
    util = git_show(BASE_REF, UTIL_F) if variant in FROM_REF else (root/UTIL_F).read_text()
    for name, value in overrides.items():
        util, n = re.subn(r'(#define %s\s+)[-0-9.eE]+' % name, r'\g<1>%s' % value, util)
        assert n == 1, name
    defines = ['SUN_SHADOW'] + (['SPOT_SHADOW'] if spot else [])
    local = VARIANT_DEFINES[variant]
    return program([(0x8B31, VERTEX), (0x8B30, stage(sun, local)), (0x8B30, stage(util, defines + local)), (0x8B30, STUBS_F)])

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
    return f
    return f.value
def blur_happy(x): return (x + 16) & ~0xF

class Scene:
    """Boxes over a ground plane, a camera, a sun direction, two projectors; rendered at W x H."""
    def __init__(self, W, H, boxes, eye, target, sun, spots, clip, fovy=math.radians(60.0), bump=0.0):
        self.W, self.H, self.boxes, self.sun, self.spots_def, self.clip = W, H, boxes, normalize(sun), spots, clip
        self.view = look_at(eye, target); self.inv_view = inverse(self.view)
        self.proj = perspective(fovy, W / H, 0.25, 256.0); self.fovy = fovy
        self.sw, self.sh = blur_happy(W), blur_happy(H); self.spot_res = W
        glsl = scene_glsl(boxes)
        gprog = program([(0x8B31, VERTEX), (0x8B30, GBUFFER_F % glsl)])
        sprog = program([(0x8B31, VERTEX), (0x8B30, SHADOW_F % glsl)])
        self.gpos = make_tex(0x8814, W, H); self.gnorm = make_tex(0x8814, W, H)
        self.gdepth = make_tex(0x81A6, W, H)       # GL_DEPTH_COMPONENT24, nearest, as the viewer's depth buffer
        self.gfbo = make_fbo([self.gpos, self.gnorm], self.gdepth); viewport(0, 0, W, H); use(gprog)
        set_mat(gprog, 'inv_proj', [inverse(self.proj)]); set_mat(gprog, 'view', [self.view]); set_mat(gprog, 'inv_view', [self.inv_view])
        set_mat(gprog, 'proj', [self.proj]); u1f(loc(gprog, b'bump_deg'), bump)
        enable(0x0B71); depthfunc(0x0207); depthmask(1)
        draw(4, 0, 3); disable(0x0B71)
        self.cascades = self.cascade_matrices()
        self.spots = [mmul(perspective(math.radians(60.0), 1.0, 0.1, 20.0), look_at(p, t, (0.0, 0.0, -1.0))) for p, t in spots]
        self.maps = []
        for m in self.cascades:
            t = make_tex(0x81A6, self.sw, self.sh, 0x2601); self.render_depth(sprog, t, self.sw, self.sh, m); self.maps.append(t)
        for m in self.spots:
            t = make_tex(0x81A6, self.spot_res, self.spot_res, 0x2601); self.render_depth(sprog, t, self.spot_res, self.spot_res, m); self.maps.append(t)
        self.shadow_matrix = [mmul(TEXSPACE, mmul(m, self.inv_view)) for m in self.cascades + self.spots]
        self.light_map = make_tex(0x881A, W, H); self.lfbo = make_fbo([self.light_map])
        self.sun_v = normalize(mvec(self.view, self.sun + [0.0])[:3])
        assert geterror() == 0

    def cascade_matrices(self):
        """Ortho fit (light space bounding box) of each cascade's frustum section, as the
        viewer's ortho fallback: section j spans clip[j-1]*0.75 .. clip[j]*1.25."""
        aspect = self.W / self.H; th = math.tan(self.fovy / 2)
        up = [0.0, 0.0, 1.0] if abs(self.sun[1]) > 0.99 else [0.0, 1.0, 0.0]
        light_view = look_at([0.0, 0.0, 0.0], [-x for x in self.sun], up)
        mats = []
        for j in range(4):
            n = 0.5 if j == 0 else self.clip[j-1] * 0.75; f = self.clip[j] * 1.25
            corners = [mvec(self.inv_view, [sx*d*th*aspect, sy*d*th, -d, 1.0])[:3] for d in (n, f) for sx in (-1, 1) for sy in (-1, 1)]
            ls = [mvec(light_view, c + [1.0]) for c in corners]
            lo = [min(p[i] for p in ls) for i in range(3)]; hi = [max(p[i] for p in ls) for i in range(3)]
            mats.append(mmul(ortho(lo[0], hi[0], lo[1], hi[1], -hi[2] - 20.0, -lo[2] + 20.0), light_view))
        return mats

    def render_depth(self, sprog, tex, w, h, light):
        f = make_fbo([], tex); viewport(0, 0, w, h)
        enable(0x0B71); depthfunc(0x0207); depthmask(1)        # depth test ALWAYS, write
        use(sprog); set_mat(sprog, 'light_proj_view', [light]); set_mat(sprog, 'inv_light', [inverse(light)])
        draw(4, 0, 3); disable(0x0B71)
        bindfbo(FB, 0); delfbo(1, C.byref(f))
        bindtex(TEX2D, tex)                                    # compare state as allocateShadowBuffer
        texparam(TEX2D, 0x884C, 0x884E); texparam(TEX2D, 0x884D, 0x0203)

    def bind(self, prog, light_size):
        use(prog)
        units = [('depthMap', self.gdepth), ('normalMap', self.gnorm)] + [(f'shadowMap{i}', self.maps[i]) for i in range(6)]
        for unit, (name, tex) in enumerate(units):
            active(0x84C0 + unit); bindtex(TEX2D, tex); u1i(loc(prog, name.encode()), unit)
            bindsampler(unit, 0)
        active(0x84C0)
        set_mat(prog, 'shadow_matrix', self.shadow_matrix); set_mat(prog, 'inv_proj', [inverse(self.proj)])
        u3f(loc(prog, b'sun_dir'), *self.sun_v); u3f(loc(prog, b'moon_dir'), *self.sun_v); u1i(loc(prog, b'sun_up_factor'), 1)
        # stage L3: the shadow shader reads primary_light_dir and primary_shadow_strength (the old names stay for the program of the git ref)
        u3f(loc(prog, b'primary_light_dir'), *self.sun_v); u1f(loc(prog, b'primary_shadow_strength'), 1.0)
        u2f(loc(prog, b'shadow_res'), self.sw, self.sh); u2f(loc(prog, b'proj_shadow_res'), self.spot_res, self.spot_res)
        u2f(loc(prog, b'screen_res'), self.W, self.H); u4f(loc(prog, b'shadow_clip'), *self.clip)
        u1f(loc(prog, b'shadow_bias'), SHADOW_BIAS); u1f(loc(prog, b'shadow_offset'), SHADOW_OFFSET)
        u1f(loc(prog, b'spot_shadow_bias'), SPOT_SHADOW_BIAS); u1f(loc(prog, b'spot_shadow_offset'), SPOT_SHADOW_OFFSET)
        u1f(loc(prog, b'shadow_light_size'), light_size)
        bindfbo(FB, self.lfbo); viewport(0, 0, self.W, self.H)

    def unbind(self):
        for unit in range(16): bindsampler(unit, 0)

    def project(self, point):
        c = mvec(self.proj, mvec(self.view, list(point) + [1.0]))
        x, y = int((c[0]/c[3]*0.5 + 0.5) * self.W), int((c[1]/c[3]*0.5 + 0.5) * self.H)
        assert 0 <= x < self.W and 0 <= y < self.H, f'{point} is off screen'
        return x, y
    def view_depth(self, point): return -mvec(self.view, list(point) + [1.0])[2]
    def read_channel(self, channel):     # 0x1903 GL_RED, 0x1905 GL_BLUE
        bindfbo(FB, self.lfbo); readbuffer(COLOR0)
        buf = (F*(self.W*self.H))(); readpixels(0, 0, self.W, self.H, channel, 0x1406, C.byref(buf))
        return buf
    def read_pixel(self, x, y):
        bindfbo(FB, self.gfbo); readbuffer(COLOR0)
        buf = (F*4)(); readpixels(x, y, 1, 1, 0x1908, 0x1406, C.byref(buf)); return list(buf)
    def release(self):
        bindfbo(FB, 0); fbos = (U*2)(self.gfbo, self.lfbo); delfbo(2, fbos)
        tex = (U*(4 + len(self.maps)))(self.gpos, self.gnorm, self.light_map, self.gdepth, *self.maps); deltex(len(tex), tex)

failures = []
def check(cond, msg, label=''):
    print(('  ok   ' if cond else '  FAIL ') + msg)
    if not cond: failures.append(f'{label}{msg}')

# --- timing + basic checks: slanted sun -----------------------------------------------------
TIMING_BOXES = [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)),
                ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)),
                ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0))]
TIMING_SPOTS = [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])]
queries = (U*1)(); genq(1, queries)
results = {}
spread = {}
REPEATS = 3

def time_variants(sc, spot):
    """interleaved timing of every variant: REPEATS rounds of FRAMES frames; returns the median of the round
    medians, the spread (max - min of the round medians) and the first frame's light map of each variant"""
    images = {}
    progs = {variant: sun_program(variant, spot) for variant in VARIANTS}
    rounds = {variant: [] for variant in VARIANTS}
    for rep_ in range(REPEATS):
        samples = {variant: [] for variant in VARIANTS}
        for f in range(WARMUP + FRAMES):         # round robin: every variant sees the same GPU clock
            for variant in VARIANTS:
                sc.bind(progs[variant], LIGHT_SIZE)
                beginq(TIME_ELAPSED, queries[0]); draw(4, 0, 3); endq(TIME_ELAPSED)
                v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v))
                if f >= WARMUP: samples[variant].append(v.value / 1e6)
                if f == 0 and rep_ == 0 and spot: images[variant] = sc.read_channel(0x1903)
                sc.unbind()
        for variant in VARIANTS: rounds[variant].append(statistics.median(samples[variant]))
    finish(); assert geterror() == 0
    for variant in VARIANTS: fn('glDeleteProgram', None, U)(progs[variant])
    return ({v: statistics.median(r) for v, r in rounds.items()}, {v: max(r) - min(r) for v, r in rounds.items()}, images)

for (W, H) in RESOLUTIONS:
    sc = Scene(W, H, TIMING_BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
    images = {}
    for spot in (True, False):
        med, spr, imgs = time_variants(sc, spot)
        images.update(imgs)
        for variant in VARIANTS:
            results[(W, H, spot, variant)] = med[variant]; spread[(W, H, spot, variant)] = spr[variant]

    label = f'{W}x{H}: '
    def at(img, x, y): return img[y * W + x]
    print(f'{W}x{H} checks (sun maps {sc.sw}x{sc.sh}, spot maps {sc.spot_res}x{sc.spot_res}):')
    lit_points = [(-10.0, 0.0, -8.0), (8.0, 0.0, -25.0), (-8.0, 0.0, -30.0)]
    umbra = []   # middle of each floating box's shadow on the ground
    for lo, hi in TIMING_BOXES[:2]:
        c = [(lo[0]+hi[0])/2, lo[1], (lo[2]+hi[2])/2]; s = c[1] / sc.sun[1]
        umbra.append((c[0] - sc.sun[0]*s, 0.0, c[2] - sc.sun[2]*s))
    for p in lit_points + umbra:
        x, y = sc.project(p); wp = mvec(sc.inv_view, sc.read_pixel(x, y)[:3] + [1.0])
        check(abs(wp[1]) < 0.05 and math.dist(wp[:3], p) < 0.2, f'pixel ({x},{y}) sees ground point {p} (got {[round(v, 2) for v in wp[:3]]})', label)
        want = 0.0 if p in umbra else 1.0
        for v in VARIANTS:
            r = at(images[v], x, y)
            check(abs(r - want) < 1e-3, f'{v}: light map R at {p} = {r:.4f}, expected {want}', label)
    for v in ('tree', 'legacy'):
        same = 0 if bytes(images['base']) == bytes(images[v]) else sum(1 for i in range(W*H) if images['base'][i] != images[v][i])
        check(same == 0, f'{v} (Legacy) light map equals the devel tip bit for bit ({same} pixels differ)', label)
    sc.release()

def read_gbuffer(sc):
    """world position (list of x, y, z) and world normal of every pixel, from the synthetic G-buffer"""
    out = []
    for attachment in (0, 1):
        bindfbo(FB, sc.gfbo); readbuffer(COLOR0 + attachment)
        buf = (F*(sc.W*sc.H*4))(); readpixels(0, 0, sc.W, sc.H, 0x1908, 0x1406, C.byref(buf)); out.append(buf)
    return out

# a sky-heavy view: the camera looks up at the horizon, most pixels are sky or beyond the shadow distance
for (W, H) in RESOLUTIONS:
    sc = Scene(W, H, TIMING_BOXES, [0.0, 2.0, 4.0], [0.0, 14.0, -40.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
    pos, nrm = read_gbuffer(sc)
    sky = sum(1 for i in range(W*H) if nrm[4*i] == 0.0 and nrm[4*i+1] == 0.0 and nrm[4*i+2] == 0.0) / (W*H)
    for spot in (True, False):
        med, spr, _ = time_variants(sc, spot)
        for variant in VARIANTS:
            results[(W, H, spot, variant, 'sky')] = med[variant]; spread[(W, H, spot, variant, 'sky')] = spr[variant]
    results[(W, H, 'skyshare')] = sky
    sc.release()

# the method-neutral block must compile as GLSL 1.50
def compile_check(version, defines, label):
    util = (root/UTIL_F).read_text()
    text = f'#version {version}\n#define FRAGMENT_SHADER 1\n' + ''.join(f'#define {d.replace("=", " ")}\n' for d in defines)
    text += 'uniform sampler2D depthMap;\nvec4 getPosition(vec2 p) { return vec4(texture(depthMap, p).rgb, 1.0); }\n' + util.replace('/*[EXTRA_CODE_HERE]*/', '')
    shader = fn('glCreateShader', U, U)(0x8B30); source = C.c_char_p(text.encode())
    fn('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None); fn('glCompileShader', None, U)(shader)
    status = I(); fn('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(status))
    log = C.create_string_buffer(8192); fn('glGetShaderInfoLog', None, U, I, P, C.c_char_p)(shader, len(log), None, log)
    fn('glDeleteShader', None, U)(shader)
    check(status.value == 1, f'{label} compiles as GLSL {version}' + ('' if status.value else ': ' + log.value.decode()), 'compile: ')
print('\nGLSL 1.50 compile of the method-neutral block (SHADOW_COMMON):')
compile_check(150, ['SUN_SHADOW', 'SHADOW_COMMON'] + S2_DEFINES, 'shadowUtil.glsl with SHADOW_COMMON')
compile_check(150, ['SUN_SHADOW'], 'shadowUtil.glsl without it (Legacy)')

print(f'\nmedian ms per sun light pass, {FRAMES} frames after {WARMUP} warmup; base = {BASE_REF} '
      f'({subprocess.run(["git", "rev-parse", "--short", BASE_REF], cwd=root, capture_output=True, text=True).stdout.strip()})')
print(f'{"resolution":<12}{"view":<8}{"detail":<8}' + ''.join(f'{v:>9}' for v in VARIANTS) + f'{"spread":>9}')
for (W, H) in RESOLUTIONS:
    for view in ('ground', 'sky'):
        for spot in (True, False):
            key = (lambda v: (W, H, spot, v)) if view == 'ground' else (lambda v: (W, H, spot, v, 'sky'))
            row = [results[key(v)] for v in VARIANTS]
            worst = max(spread[key(v)] for v in VARIANTS)    # run-to-run spread of the timings
            print(f'{f"{W}x{H}":<12}{view:<8}{(2 if spot else 1):<8}' + ''.join(f'{t:>9.3f}' for t in row) + f'{worst:>9.3f}')
    print(f'  ({W}x{H} sky view: {100 * results[(W, H, "skyshare")]:.0f} % of the pixels are sky; spread = max - min of {REPEATS} rounds of {FRAMES} frames)')
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
