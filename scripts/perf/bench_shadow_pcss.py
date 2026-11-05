"""Offline GPU benchmark and check of the deferred sun light pass with PCSS, no viewer launch.

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
  * "base"  -- both shaders from git ref PCSS_BENCH_BASE_REF (default 6ceaf3196c, the pre-PCSS devel tip)
  * "tree"  -- this working tree, PCSS_SHADOW undefined
  * "pcss1" -- this working tree, PCSS_SHADOW: stage 1, the fixed radius rotated PCF (what the
               viewer compiles below GL 3.3, without sampler objects)
  * "pcss"  -- this working tree, PCSS_SHADOW and PCSS_SHADOW_FETCH: stage 2, blocker search and
               contact hardening on cascades 0-1 and the spots; shadowDepth0/1/4/5 are the same
               depth maps bound a second time with a sampler object (compare off, nearest), as
               LLPipeline::bindShadowDepthFetch binds them
and reports the median ms per pass. Timing uses the physical sun, L = 2 tan(0.265 deg).
getPosition/getNorm are the benchmark's: they read the synthetic G-buffer (RGBA32F) instead of
decoding the viewer's depth and packed normal, the same for every variant.

Checks on the light map (RGBA16F, read back):
  * slanted sun scene: lit ground far from the boxes is 1.0 (for "pcss": no blocker in the
    search, the early out), ground in the middle of a floating box's shadow is 0.0, "tree"
    equals "base" bit for bit, and the PCSS shadow edges (averaged along the edge) rise
    monotonically;
  * penumbra scene (1080p, sun overhead, thin floating boxes): the 10-90 % width of the shadow
    edge across the ground. A disc light of angular size L behind an edge at height h gives a
    penumbra of full width w = h L whose profile is a disc's segment area, 10-90 % = 0.687 w;
    the PCF disc of diameter w reproduces it. Checked for two heights in cascade 0, two in
    cascade 1 (L = 0.1 so the widths span several texels) and two under a spot light
    (w = S (d_r - d_b) / d_b with PCSS_SPOT_LIGHT_SIZE S set to 0.35 m in the bench's copy),
    within one map texel plus one screen pixel's footprint on the ground (the resolution of the
    measurement), and the width must grow with the height.

Environment: PCSS_BENCH_BASE_REF, PCSS_BENCH_FRAMES (default 30), PCSS_BENCH_RES (default
"1920x1080,2560x1440"). Pass --opengl PATH to select another GL DLL (Mesa).
"""
import math, os, re, statistics, subprocess, sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
# pre-PCSS devel tip (the #294 merge): the last shadowUtil.glsl without PCSS_SHADOW
BASE_REF = os.environ.get('PCSS_BENCH_BASE_REF', '6ceaf3196c')
FRAMES = int(os.environ.get('PCSS_BENCH_FRAMES', 30)); WARMUP = 5
RESOLUTIONS = [tuple(int(v) for v in r.split('x')) for r in os.environ.get('PCSS_BENCH_RES', '1920x1080,2560x1440').split(',')]
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
# the depth fetch sampler, as LLRender::getDepthFetchSampler makes it
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
%s
void main()
{
    vec4 p = inv_proj * vec4(vary_fragcoord * 2.0 - 1.0, -1.0, 1.0);
    vec3 dir_v = normalize(p.xyz / p.w);
    vec3 o = (inv_view * vec4(0, 0, 0, 1)).xyz, d = normalize((inv_view * vec4(dir_v, 0)).xyz);
    float t; vec3 n;
    if (trace(o, d, 1000.0, t, n)) { out_pos = view * vec4(o + d * t, 1.0); out_norm = vec4(normalize((view * vec4(n, 0)).xyz), 0.0); }
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
uniform sampler2D gbuf_pos;
uniform sampler2D normalMap;
vec4 getPosition(vec2 pos_screen) { return texture(gbuf_pos, pos_screen); }
vec4 getNorm(vec2 pos_screen) { return vec4(texture(normalMap, pos_screen).xyz, 0.0); }
'''
VARIANTS = ['base', 'tree', 'pcss1', 'pcss']
VARIANT_DEFINES = {'base': [], 'tree': [], 'pcss1': ['PCSS_SHADOW'], 'pcss': ['PCSS_SHADOW', 'PCSS_SHADOW_FETCH']}

def git_show(ref, path):
    return subprocess.run(['git', 'show', f'{ref}:{path}'], cwd=root, capture_output=True, text=True, check=True).stdout
def stage(text, defines):
    head = '#version 430 core\n#define FRAGMENT_SHADER 1\n' + ''.join(f'#define {d} 1\n' for d in defines)
    return head + text.replace('/*[EXTRA_CODE_HERE]*/', '')
def sun_program(variant, spot, overrides={}):
    sun = git_show(BASE_REF, SUN_F) if variant == 'base' else (root/SUN_F).read_text()
    util = git_show(BASE_REF, UTIL_F) if variant == 'base' else (root/UTIL_F).read_text()
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
    def __init__(self, W, H, boxes, eye, target, sun, spots, clip, fovy=math.radians(60.0)):
        self.W, self.H, self.boxes, self.sun, self.spots_def, self.clip = W, H, boxes, normalize(sun), spots, clip
        self.view = look_at(eye, target); self.inv_view = inverse(self.view)
        self.proj = perspective(fovy, W / H, 0.5, 512.0); self.fovy = fovy
        self.sw, self.sh = blur_happy(W), blur_happy(H); self.spot_res = W
        glsl = scene_glsl(boxes)
        gprog = program([(0x8B31, VERTEX), (0x8B30, GBUFFER_F % glsl)])
        sprog = program([(0x8B31, VERTEX), (0x8B30, SHADOW_F % glsl)])
        self.gpos = make_tex(0x8814, W, H); self.gnorm = make_tex(0x8814, W, H)
        self.gfbo = make_fbo([self.gpos, self.gnorm]); viewport(0, 0, W, H); use(gprog)
        set_mat(gprog, 'inv_proj', [inverse(self.proj)]); set_mat(gprog, 'view', [self.view]); set_mat(gprog, 'inv_view', [self.inv_view])
        draw(4, 0, 3)
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
        units = [('gbuf_pos', self.gpos), ('normalMap', self.gnorm)] + [(f'shadowMap{i}', self.maps[i]) for i in range(6)]
        units += [(f'shadowDepth{i}', self.maps[i]) for i in (0, 1, 4, 5)]
        for unit, (name, tex) in enumerate(units):
            active(0x84C0 + unit); bindtex(TEX2D, tex); u1i(loc(prog, name.encode()), unit)
            bindsampler(unit, depth_fetch.value if name.startswith('shadowDepth') else 0)
        active(0x84C0)
        set_mat(prog, 'shadow_matrix', self.shadow_matrix); set_mat(prog, 'inv_proj', [inverse(self.proj)])
        u3f(loc(prog, b'sun_dir'), *self.sun_v); u3f(loc(prog, b'moon_dir'), *self.sun_v); u1i(loc(prog, b'sun_up_factor'), 1)
        u2f(loc(prog, b'shadow_res'), self.sw, self.sh); u2f(loc(prog, b'proj_shadow_res'), self.spot_res, self.spot_res)
        u2f(loc(prog, b'screen_res'), self.W, self.H); u4f(loc(prog, b'shadow_clip'), *self.clip)
        u1f(loc(prog, b'shadow_bias'), SHADOW_BIAS); u1f(loc(prog, b'shadow_offset'), SHADOW_OFFSET)
        u1f(loc(prog, b'spot_shadow_bias'), SPOT_SHADOW_BIAS); u1f(loc(prog, b'spot_shadow_offset'), SPOT_SHADOW_OFFSET)
        u1f(loc(prog, b'shadow_light_size'), light_size)
        bindfbo(FB, self.lfbo); viewport(0, 0, self.W, self.H)

    def unbind(self):
        for unit in range(12): bindsampler(unit, 0)

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
        tex = (U*(4 + len(self.maps)))(self.gpos, self.gnorm, self.light_map, 0, *self.maps); deltex(len(tex), tex)

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
for (W, H) in RESOLUTIONS:
    sc = Scene(W, H, TIMING_BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], TIMING_SPOTS, (10.0, 25.0, 60.0, 150.0))
    images = {}
    for spot in (True, False):
        for variant in VARIANTS:
            prog = sun_program(variant, spot); sc.bind(prog, LIGHT_SIZE)
            samples = []
            for f in range(WARMUP + FRAMES):
                beginq(TIME_ELAPSED, queries[0]); draw(4, 0, 3); endq(TIME_ELAPSED)
                v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v))
                if f >= WARMUP: samples.append(v.value / 1e6)
            finish(); assert geterror() == 0
            results[(W, H, spot, variant)] = statistics.median(samples)
            if spot: images[variant] = sc.read_channel(0x1903)
            sc.unbind(); fn('glDeleteProgram', None, U)(prog)

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
            exact = v == 'pcss' and want == 1.0     # no blocker in the search: the early out gives exactly 1
            check(r == want if exact else abs(r - want) < 1e-3, f'{v}: light map R at {p} = {r:.4f}, expected {want}{" exactly" if exact else ""}', label)
    same = 0 if bytes(images['base']) == bytes(images['tree']) else sum(1 for i in range(W*H) if images['base'][i] != images['tree'][i])
    # PCSS off differs from the base only by pcfShadow's receiver-plane bias (the altitude-bias
    # hotfix): on slanted lit surfaces, where the base had acne between the taps
    check(same <= W * H // 1000, f'tree (PCSS off) equals base except the receiver-plane bias ({same} pixels differ, at most 0.1 %)', label)
    for v in ('pcss1', 'pcss'):
        differ = sum(1 for i in range(W*H) if images['base'][i] != images[v][i])
        print(f'  info {v} differs from base in {differ} of {W*H} pixels')
    # across the right (+x) edge of box 2's shadow at z = -19.6, averaged along z
    lo, hi = TIMING_BOXES[1]
    edge_x = hi[0] - sc.sun[0] / sc.sun[1] * hi[1]
    for v in ('base', 'pcss1', 'pcss'):
        xs = [edge_x - 0.5 + 0.01 * i for i in range(101)]
        prof = [sum(at(images[v], *sc.project((gx, 0.0, -20.2 + 1.2 * zi / 47))) for zi in range(48)) / 48 for gx in xs]
        worst = max(prof[i] - prof[i+1] for i in range(len(prof) - 1))
        lo10 = next(xs[i] for i, p in enumerate(prof) if p >= 0.1); hi90 = next(xs[i] for i, p in enumerate(prof) if p >= 0.9)
        msg = f'{v}: edge profile {prof[0]:.3f} -> {prof[-1]:.3f}, 10-90% width {100*(hi90-lo10):.1f} cm, largest step back {worst:.4f}'
        if v != 'base': check(worst <= 0.01 and prof[0] < 0.05 and prof[-1] > 0.95, msg + ' (monotonic within 0.01)', label)
        else: print('  info ' + msg)
    sc.release()

# --- penumbra: sun overhead, thin boxes at known heights --------------------------------------
PEN_L = 0.1           # light size for the check (about 11 x the sun) so the widths span texels
PEN_SPOT_SIZE = 0.35  # PCSS_SPOT_LIGHT_SIZE in the bench's copy of the shader
T = 0.05              # box thickness
# (name, x range, z range of the box, height, cascade or spot); the front (+z) edge is measured
PEN_CASES = [('cascade 0', (-1.5, -0.3), (-3.0, -1.5), 1.0), ('cascade 0', (0.3, 1.5), (-3.0, -1.5), 2.0),
             ('cascade 1', (-1.5, -0.3), (-14.0, -12.0), 1.5), ('cascade 1', (0.3, 1.5), (-14.0, -12.0), 3.0),
             ('spot', (3.4, 4.2), (-2.6, -1.8), 1.0), ('spot', (5.8, 6.6), (-2.6, -1.8), 1.5)]
SPOT_EYE = [5.0, 8.0, -2.2]
pen_boxes = [((x[0], h, z[0]), (x[1], h + T, z[1])) for _, x, z, h in PEN_CASES]
W, H = RESOLUTIONS[0]
print(f'\npenumbra checks at {W}x{H}: sun overhead, L = {PEN_L}, spot emitter {PEN_SPOT_SIZE} m; '
      f'expected 10-90 % = {DISC_10_90:.3f} x full width w')
sc = Scene(W, H, pen_boxes, [0.0, 10.0, 6.0], [0.0, 0.0, -10.0], [0.0, 1.0, 0.0],
           [(SPOT_EYE, [SPOT_EYE[0], 0.0, SPOT_EYE[2]]), ([30.0, 8.0, 30.0], [30.0, 0.0, 30.0])], (16.0, 40.0, 80.0, 150.0))
prog = sun_program('pcss', True, {'PCSS_SPOT_LIGHT_SIZE': PEN_SPOT_SIZE}); sc.bind(prog, PEN_L)
draw(4, 0, 3); finish()
red, blue = sc.read_channel(0x1903), sc.read_channel(0x1905)
sc.unbind()
widths = {}
for name, xr, zr, h in PEN_CASES:
    img = blue if name == 'spot' else red
    if name == 'spot':
        d_r = SPOT_EYE[1]; d_b = SPOT_EYE[1] - h
        w = PEN_SPOT_SIZE * (d_r - d_b) / d_b
        edge = SPOT_EYE[2] + (zr[1] - SPOT_EYE[2]) * d_r / d_b      # the box edge projected from the projector
        texel = 2.0 * d_r * math.tan(math.radians(30.0)) / sc.spot_res
    else:
        w = h * PEN_L; edge = zr[1]
        j = int(name[-1]); m = sc.cascades[j]
        # orthographic light: world metres per texel along the map's two axes
        ax = math.sqrt(sum(v * v for v in m[0][:3])); ay = math.sqrt(sum(v * v for v in m[1][:3]))
        texel = max(2.0 / ax / sc.sw, 2.0 / ay / sc.sh)
        depth = sc.view_depth((sum(xr) / 2, 0.0, edge))
        lo_c = 0.5 if j == 0 else sc.clip[j-1] * 1.25; hi_c = sc.clip[j] * 0.75
        assert lo_c <= depth <= hi_c, f'{name} box edge at view depth {depth:.1f} is not in that cascade alone'
    xs_mid = [xr[0] + 0.15 + (xr[1] - xr[0] - 0.3) * k / 31 for k in range(32)]
    if name == 'spot':   # the box's x range as its shadow falls on the ground
        xs_mid = [SPOT_EYE[0] + (x - SPOT_EYE[0]) * d_r / d_b for x in xs_mid]
    zs = [edge - 0.3 + 0.002 * i for i in range(301)]
    prof = []
    for gz in zs:
        acc = 0.0
        for gx in xs_mid:
            x, y = sc.project((gx, 0.0, gz)); acc += img[y * W + x]
        prof.append(acc / len(xs_mid))
    # front edge: shadow (0) at z < edge, lit (1) beyond
    lo10 = next(zs[i] for i, p in enumerate(prof) if p >= 0.1); hi90 = next(zs[i] for i, p in enumerate(prof) if p >= 0.9)
    # resolution limits of the measurement: one map texel (the bilinear compare) and one screen
    # pixel's footprint on the ground across the edge
    xm = sum(xs_mid) / len(xs_mid); pa, pb = sc.project((xm, 0.0, edge)), sc.project((xm, 0.0, edge + 0.2))
    pixel = 0.2 / math.hypot(pb[0] - pa[0], pb[1] - pa[1])
    measured = hi90 - lo10; expected = DISC_10_90 * w; tol = texel + pixel
    widths.setdefault(name, []).append((h, measured))
    check(prof[0] < 0.02 and prof[-1] > 0.98 and abs(measured - expected) <= tol,
          f'{name}, h {h} m: full width w {100*w:.1f} cm, 10-90 % measured {100*measured:.1f} cm, expected {100*expected:.1f} cm '
          f'(+-{100*tol:.1f}: texel {100*texel:.2f} + pixel {100*pixel:.2f} cm); ends {prof[0]:.3f} / {prof[-1]:.3f}', 'penumbra: ')
for name, rows in widths.items():
    (h1, w1), (h2, w2) = rows
    check(w2 > w1, f'{name}: width grows with height (h {h1} -> {h2}: {100*w1:.1f} -> {100*w2:.1f} cm)', 'penumbra: ')
lit = (-6.0, 0.0, -3.0); x, y = sc.project(lit)
check(red[y * W + x] == 1.0, f'no blocker in the search region (ground {lit}): R = {red[y * W + x]} exactly 1', 'penumbra: ')
fn('glDeleteProgram', None, U)(prog); sc.release()

print(f'\nmedian ms per sun light pass, {FRAMES} frames after {WARMUP} warmup; base = {BASE_REF} '
      f'({subprocess.run(["git", "rev-parse", "--short", BASE_REF], cwd=root, capture_output=True, text=True).stdout.strip()})')
print(f'{"resolution":<12}{"detail":<8}' + ''.join(f'{v:>9}' for v in VARIANTS) + f'{"pcss/base":>11}')
for (W, H) in RESOLUTIONS:
    for spot in (True, False):
        row = [results[(W, H, spot, v)] for v in VARIANTS]
        print(f'{f"{W}x{H}":<12}{(2 if spot else 1):<8}' + ''.join(f'{t:>9.3f}' for t in row) + f'{row[-1]/row[0]:>10.2f}x')
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
