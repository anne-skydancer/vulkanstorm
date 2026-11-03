"""Offline GPU benchmark and check of the deferred sun light pass with PCSS stage 1, no viewer launch.

Renders a synthetic scene (a ground plane and three boxes at known heights, ray cast in a
fragment shader) into
  * four sun cascade depth maps, GL_DEPTH_COMPONENT24 at the viewer's size for the screen
    (BlurHappySize: 1936x1088 at 1080p, 2576x1456 at 1440p), orthographic fits of the
    cascades' frustum sections, compare mode and filtering set as LLPipeline::allocateShadowBuffer
    sets them (linear, clamp, GL_COMPARE_REF_TO_TEXTURE, GL_LEQUAL);
  * two spot (projector) depth maps, perspective, screen width square;
  * a G-buffer of view space positions and normals at 1080p and 1440p;
then times the sun light pass (class2 deferred/sunLightF.glsl + class1 deferred/shadowUtil.glsl,
RenderShadowDetail 2: SUN_SHADOW and SPOT_SHADOW) with GL_TIME_ELAPSED queries for
  * "base"  -- both shaders from git ref PCSS_BENCH_BASE_REF (default origin/vkstorm-devel)
  * "tree"  -- both shaders from this working tree, PCSS_SHADOW undefined
  * "pcss"  -- both shaders from this working tree, PCSS_SHADOW defined
and reports the median ms per pass. getPosition/getNorm are the benchmark's: they read the
synthetic G-buffer (RGBA32F) instead of decoding the viewer's depth and packed normal, the same
for every variant. The light map (RGBA16F) is read back and checked: lit ground far from the
boxes is 1.0, ground in the middle of a floating box's shadow is 0.0, "tree" equals "base" bit
for bit, and the PCSS shadow edge (averaged along the edge) rises monotonically.

Environment: PCSS_BENCH_BASE_REF, PCSS_BENCH_FRAMES (default 30), PCSS_BENCH_RES (default
"1920x1080,2560x1440"). Pass --opengl PATH to select another GL DLL (Mesa).
"""
import math, os, statistics, subprocess, sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
BASE_REF = os.environ.get('PCSS_BENCH_BASE_REF', 'origin/vkstorm-devel')
FRAMES = int(os.environ.get('PCSS_BENCH_FRAMES', 30)); WARMUP = 5
RESOLUTIONS = [tuple(int(v) for v in r.split('x')) for r in os.environ.get('PCSS_BENCH_RES', '1920x1080,2560x1440').split(',')]
SHADERS = 'indra/newview/app_settings/shaders/'
SUN_F = SHADERS + 'class2/deferred/sunLightF.glsl'
UTIL_F = SHADERS + 'class1/deferred/shadowUtil.glsl'

# viewer defaults (settings.xml)
SHADOW_BIAS = -0.002; SHADOW_OFFSET = 0.01; SPOT_SHADOW_BIAS = -0.0002; SPOT_SHADOW_OFFSET = 0.04
SHADOW_CLIP = (10.0, 25.0, 60.0, 150.0)      # view distances of the cascade splits (mSunClipPlanes)
LIGHT_SIZE = 2.0 * math.tan(math.radians(0.53) / 2.0)

# --- GL entry points ----------------------------------------------------------------------
F=C.c_float
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
texstorage=fn('glTexStorage2D',None,U,I,U,I,I);texparam=fn('glTexParameteri',None,U,U,I)
active=fn('glActiveTexture',None,U)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
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
TEX2D=0x0DE1;FB=0x8D40;DEPTH_ATT=0x8D00;COLOR0=0x8CE0;TIME_ELAPSED=0x88BF;QUERY_RESULT=0x8866
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)

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
def mat_arg(mats):
    flat = [x for m in mats for row in m for x in row]
    return (F*len(flat))(*flat)
def set_mat(prog, name, mats):
    umat(loc(prog, name.encode()), len(mats), 1, mat_arg(mats))

# --- scene ---------------------------------------------------------------------------------
# boxes (min, max) in world metres, y up: two floating at known heights, one standing
BOXES = [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)),
         ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)),
         ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0))]
SUN = normalize([-0.5, 1.0, 0.2])                # towards the sun
EYE = [0.0, 8.0, 4.0]; TARGET = [0.0, 0.0, -16.0]; FOVY = math.radians(60.0); NEAR = 0.5; FAR = 512.0
VIEW = look_at(EYE, TARGET); INV_VIEW = inverse(VIEW)
SPOTS = [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])]
SPOT_NEAR, SPOT_FAR = 0.1, 20.0

SCENE_GLSL = '''
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
''' % (len(BOXES), ', '.join('vec3(%r, %r, %r)' % b[0] for b in BOXES), ', '.join('vec3(%r, %r, %r)' % b[1] for b in BOXES))

VERTEX = '''#version 430 core
out vec2 vary_fragcoord;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0; gl_Position = vec4(p, 0.0, 1.0); vary_fragcoord = p * 0.5 + 0.5; }
'''
GBUFFER_F = '''#version 430 core
in vec2 vary_fragcoord;
uniform mat4 inv_proj; uniform mat4 view; uniform mat4 inv_view;
layout(location = 0) out vec4 out_pos;
layout(location = 1) out vec4 out_norm;
''' + SCENE_GLSL + '''
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
''' + SCENE_GLSL + '''
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

def git_show(ref, path):
    return subprocess.run(['git', 'show', f'{ref}:{path}'], cwd=root, capture_output=True, text=True, check=True).stdout
def stage(text, defines):
    head = '#version 430 core\n#define FRAGMENT_SHADER 1\n' + ''.join(f'#define {d} 1\n' for d in defines)
    return head + text.replace('/*[EXTRA_CODE_HERE]*/', '')
def sun_program(variant, spot):
    sun = git_show(BASE_REF, SUN_F) if variant == 'base' else (root/SUN_F).read_text()
    util = git_show(BASE_REF, UTIL_F) if variant == 'base' else (root/UTIL_F).read_text()
    defines = ['SUN_SHADOW'] + (['SPOT_SHADOW'] if spot else [])
    local = ['PCSS_SHADOW'] if variant == 'pcss' else []
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
    return f.value

gbuffer_prog = program([(0x8B31, VERTEX), (0x8B30, GBUFFER_F)])
shadow_prog = program([(0x8B31, VERTEX), (0x8B30, SHADOW_F)])

def blur_happy(x): return (x + 16) & ~0xF

def cascade_matrices(w, h):
    """Ortho fit (light space bounding box) of each cascade's frustum section, as the
    viewer's ortho fallback: section j spans clip[j-1]*0.75 .. clip[j]*1.25."""
    aspect = w / h; th = math.tan(FOVY / 2)
    up = [0.0, 0.0, 1.0] if abs(SUN[1]) > 0.99 else [0.0, 1.0, 0.0]
    mats = []
    for j in range(4):
        n = NEAR if j == 0 else SHADOW_CLIP[j-1] * 0.75; f = SHADOW_CLIP[j] * 1.25
        corners = []
        for d in (n, f):
            for sx in (-1, 1):
                for sy in (-1, 1):
                    corners.append(mvec(INV_VIEW, [sx*d*th*aspect, sy*d*th, -d, 1.0])[:3])
        light_view = look_at([0.0, 0.0, 0.0], [-SUN[0], -SUN[1], -SUN[2]], up)
        ls = [mvec(light_view, c + [1.0]) for c in corners]
        lo = [min(p[i] for p in ls) for i in range(3)]; hi = [max(p[i] for p in ls) for i in range(3)]
        proj = ortho(lo[0], hi[0], lo[1], hi[1], -hi[2] - 20.0, -lo[2] + 20.0)
        mats.append(mmul(proj, light_view))
    return mats
def spot_matrices():
    return [mmul(perspective(math.radians(60.0), 1.0, SPOT_NEAR, SPOT_FAR), look_at(p, t, (0.0, 0.0, -1.0))) for p, t in SPOTS]

def render_depth(tex, w, h, light):
    fbo = make_fbo([], tex); viewport(0, 0, w, h)
    enable(0x0B71); depthfunc(0x0207); depthmask(1)        # depth test ALWAYS, write
    use(shadow_prog)
    set_mat(shadow_prog, 'light_proj_view', [light]); set_mat(shadow_prog, 'inv_light', [inverse(light)])
    draw(4, 0, 3); disable(0x0B71)
    bindtex(TEX2D, tex)                                    # compare state as allocateShadowBuffer
    texparam(TEX2D, 0x884C, 0x884E); texparam(TEX2D, 0x884D, 0x0203)

def project(point, proj, w, h):
    c = mvec(proj, mvec(VIEW, list(point) + [1.0]))
    x, y = int((c[0]/c[3]*0.5 + 0.5) * w), int((c[1]/c[3]*0.5 + 0.5) * h)
    assert 0 <= x < w and 0 <= y < h, f'{point} is off screen'
    return x, y

def read_red(fbo, w, h):
    bindfbo(FB, fbo); readbuffer(COLOR0)
    buf = (F*(w*h))(); readpixels(0, 0, w, h, 0x1903, 0x1406, C.byref(buf))   # GL_RED, GL_FLOAT
    return buf
def read_pixel(fbo, att, x, y):
    bindfbo(FB, fbo); readbuffer(COLOR0 + att)
    buf = (F*4)(); readpixels(x, y, 1, 1, 0x1908, 0x1406, C.byref(buf)); return list(buf)

queries = (U*1)(); genq(1, queries)
variants = ['base', 'tree', 'pcss']
results = {}; failures = []

for (W, H) in RESOLUTIONS:
    proj = perspective(FOVY, W / H, NEAR, FAR)
    sw, sh = blur_happy(W), blur_happy(H); spot_res = W
    # G-buffer
    gpos = make_tex(0x8814, W, H); gnorm = make_tex(0x8814, W, H)
    gfbo = make_fbo([gpos, gnorm]); viewport(0, 0, W, H); use(gbuffer_prog)
    set_mat(gbuffer_prog, 'inv_proj', [inverse(proj)]); set_mat(gbuffer_prog, 'view', [VIEW]); set_mat(gbuffer_prog, 'inv_view', [INV_VIEW])
    draw(4, 0, 3)
    # shadow maps
    cascades = cascade_matrices(W, H); spots = spot_matrices()
    maps = []
    for j in range(4):
        t = make_tex(0x81A6, sw, sh, 0x2601); render_depth(t, sw, sh, cascades[j]); maps.append(t)
    for j in range(2):
        t = make_tex(0x81A6, spot_res, spot_res, 0x2601); render_depth(t, spot_res, spot_res, spots[j]); maps.append(t)
    shadow_matrix = [mmul(TEXSPACE, mmul(m, INV_VIEW)) for m in cascades + spots]
    light_map = make_tex(0x881A, W, H); lfbo = make_fbo([light_map])
    assert geterror() == 0
    sun_v = normalize(mvec(VIEW, SUN + [0.0])[:3])
    images = {}
    for spot in (True, False):
        for variant in variants:
            prog = sun_program(variant, spot); use(prog)
            for unit, (name, tex) in enumerate([('gbuf_pos', gpos), ('normalMap', gnorm)] + [(f'shadowMap{i}', maps[i]) for i in range(6)]):
                active(0x84C0 + unit); bindtex(TEX2D, tex); u1i(loc(prog, name.encode()), unit)
            active(0x84C0)
            set_mat(prog, 'shadow_matrix', shadow_matrix); set_mat(prog, 'inv_proj', [inverse(proj)])
            u3f(loc(prog, b'sun_dir'), *sun_v); u3f(loc(prog, b'moon_dir'), *sun_v); u1i(loc(prog, b'sun_up_factor'), 1)
            u2f(loc(prog, b'shadow_res'), sw, sh); u2f(loc(prog, b'proj_shadow_res'), spot_res, spot_res)
            u2f(loc(prog, b'screen_res'), W, H); u4f(loc(prog, b'shadow_clip'), *SHADOW_CLIP)
            u1f(loc(prog, b'shadow_bias'), SHADOW_BIAS); u1f(loc(prog, b'shadow_offset'), SHADOW_OFFSET)
            u1f(loc(prog, b'spot_shadow_bias'), SPOT_SHADOW_BIAS); u1f(loc(prog, b'spot_shadow_offset'), SPOT_SHADOW_OFFSET)
            u1f(loc(prog, b'shadow_light_size'), LIGHT_SIZE)
            bindfbo(FB, lfbo); viewport(0, 0, W, H)
            samples = []
            for f in range(WARMUP + FRAMES):
                beginq(TIME_ELAPSED, queries[0]); draw(4, 0, 3); endq(TIME_ELAPSED)
                v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v))
                if f >= WARMUP: samples.append(v.value / 1e6)
            finish(); assert geterror() == 0
            results[(W, H, spot, variant)] = statistics.median(samples)
            if spot: images[variant] = read_red(lfbo, W, H)
            fn('glDeleteProgram', None, U)(prog)

    # --- correctness (RenderShadowDetail 2 images) ---
    def at(img, x, y): return img[y * W + x]
    def check(cond, msg):
        print(('  ok   ' if cond else '  FAIL ') + msg)
        if not cond: failures.append(f'{W}x{H}: {msg}')
    print(f'{W}x{H} checks (sun maps {sw}x{sh}, spot maps {spot_res}x{spot_res}):')
    lit_points = [(-10.0, 0.0, -8.0), (8.0, 0.0, -25.0), (-8.0, 0.0, -30.0)]
    umbra = []   # middle of each floating box's shadow on the ground
    for lo, hi in BOXES[:2]:
        c = [(lo[0]+hi[0])/2, lo[1], (lo[2]+hi[2])/2]; s = c[1] / SUN[1]
        umbra.append((c[0] - SUN[0]*s, 0.0, c[2] - SUN[2]*s))
    for p in lit_points + umbra:
        x, y = project(p, proj, W, H); gp = read_pixel(gfbo, 0, x, y)
        wp = mvec(INV_VIEW, gp[:3] + [1.0])
        check(abs(wp[1]) < 0.05 and math.dist(wp[:3], p) < 0.2, f'pixel ({x},{y}) sees ground point {p} (got {[round(v, 2) for v in wp[:3]]})')
        want = 0.0 if p in umbra else 1.0
        for v in variants:
            r = at(images[v], x, y)
            check(abs(r - want) < 1e-3, f'{v}: light map R at {p} = {r:.4f}, expected {want}')
    same = 0 if bytes(images['base']) == bytes(images['tree']) else sum(1 for i in range(W*H) if images['base'][i] != images['tree'][i])
    check(same == 0, f'tree (PCSS off) equals base bit for bit ({same} pixels differ)')
    differ = sum(1 for i in range(W*H) if images['base'][i] != images['pcss'][i])
    print(f'  info pcss differs from base in {differ} of {W*H} pixels')
    # PCSS edge: across the right (+x) edge of box 2's shadow at z = -19.6, averaged along z
    lo, hi = BOXES[1]
    edge_x = hi[0] - SUN[0] / SUN[1] * hi[1]         # top face's far corner projected to the ground
    for v in ('base', 'pcss'):
        prof = []
        xs = [edge_x - 0.5 + 0.01 * i for i in range(101)]
        for gx in xs:
            acc = 0.0; k = 0
            for zi in range(48):
                gz = -20.2 + 1.2 * zi / 47
                px, py = project((gx, 0.0, gz), proj, W, H); acc += at(images[v], px, py); k += 1
            prof.append(acc / k)
        drops = [prof[i] - prof[i+1] for i in range(len(prof) - 1)]
        worst = max(drops)
        lo10 = next(xs[i] for i, p in enumerate(prof) if p >= 0.1); hi90 = next(xs[i] for i, p in enumerate(prof) if p >= 0.9)
        msg = f'{v}: edge profile {prof[0]:.3f} -> {prof[-1]:.3f}, 10-90% width {100*(hi90-lo10):.1f} cm, largest step back {worst:.4f}'
        if v == 'pcss': check(worst <= 0.01 and prof[0] < 0.05 and prof[-1] > 0.95, msg + ' (monotonic within 0.01)')
        else: print('  info ' + msg)

print(f'\nmedian ms per sun light pass, {FRAMES} frames after {WARMUP} warmup; base = {BASE_REF} '
      f'({subprocess.run(["git", "rev-parse", "--short", BASE_REF], cwd=root, capture_output=True, text=True).stdout.strip()})')
print(f'{"resolution":<12}{"detail":<8}' + ''.join(f'{v:>9}' for v in variants) + f'{"pcss/base":>11}')
for (W, H) in RESOLUTIONS:
    for spot in (True, False):
        row = [results[(W, H, spot, v)] for v in variants]
        print(f'{f"{W}x{H}":<12}{(2 if spot else 1):<8}' + ''.join(f'{t:>9.3f}' for t in row) + f'{row[2]/row[0]:>10.2f}x')
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
