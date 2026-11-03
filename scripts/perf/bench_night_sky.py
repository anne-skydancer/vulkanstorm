"""Offline GPU benchmark of the real night sky's shader (deferred/skyF.glsl), no viewer launch.

Times a full-screen pass that evaluates night_sky() for every pixel with GL_TIME_ELAPSED
queries, for two versions of the shader:
  * "old" -- the procedural gradient-noise Milky Way, skyF.glsl from git ref
             NIGHT_SKY_BENCH_OLD_REF (default origin/vkstorm-devel)
  * "new" -- the Milky Way map of milky_way.bin (Tycho-2), skyF.glsl from this working tree
Each version runs with the Milky Way on (night_sky_params.y 1) and off (0), so the Milky Way's
own cost is the difference; the stars are the real catalogue (app_settings/stars/stars.bin),
binned as llnightsky.cpp does.

Scenes:
  * "camera"     -- a 60 degree (vertical) perspective view centred on the galactic centre,
                    50 degrees above the horizon: the band fills much of the screen and every
                    pixel is smaller than a lattice cell, so the map's points are all drawn
  * "hemisphere" -- the whole sky above the horizon, azimuth across and altitude up (pixels a
                    little larger than a lattice cell near the bottom: the points fade there)

The shader functions are taken verbatim from the <Vulkanstorm> block of skyF.glsl; only main()
is the benchmark's. Environment: NIGHT_SKY_BENCH_W, NIGHT_SKY_BENCH_H (default 2560x1440),
NIGHT_SKY_BENCH_FRAMES (default 20), NIGHT_SKY_BENCH_OLD_REF. Pass --opengl PATH to select another
GL DLL (Mesa).
"""
import math, os, statistics, struct, subprocess, zlib
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
W = int(os.environ.get('NIGHT_SKY_BENCH_W', 2560)); H = int(os.environ.get('NIGHT_SKY_BENCH_H', 1440))
FRAMES = int(os.environ.get('NIGHT_SKY_BENCH_FRAMES', 20)); WARMUP = 5
OLD_REF = os.environ.get('NIGHT_SKY_BENCH_OLD_REF', 'origin/vkstorm-devel')
SKY = 'indra/newview/app_settings/shaders/class1/deferred/skyF.glsl'
STARS = root / 'indra/newview/app_settings/stars'

# --- data, as llnightsky.cpp prepares it ------------------------------------------------
FACE_CELLS = 24; CELLS = 6 * FACE_CELLS * FACE_CELLS; MARGIN = 0.021
CELLS_WIDTH = 64; STARS_WIDTH = 1024
MW_W, MW_H = 2048, 512

def cell_of(q):
    ax, ay, az = abs(q[0]), abs(q[1]), abs(q[2])
    if ax >= ay and ax >= az: face, u, v, m = (0 if q[0] >= 0 else 1), q[1], q[2], ax
    elif ay >= az: face, u, v, m = (2 if q[1] >= 0 else 3), q[0], q[2], ay
    else: face, u, v, m = (4 if q[2] >= 0 else 5), q[0], q[1], az
    m = max(m, 1e-20); n = FACE_CELLS
    cx = int(min(max(math.floor((u / m * 0.5 + 0.5) * n), 0), n - 1))
    cy = int(min(max(math.floor((v / m * 0.5 + 0.5) * n), 0), n - 1))
    return face * n * n + cy * n + cx

def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): l = math.sqrt(sum(x*x for x in a)); return tuple(x / l for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))

def binned_stars():
    data = (STARS / 'stars.bin').read_bytes()
    count = struct.unpack_from('<I', data, 8)[0]
    stars = []
    for i in range(count):
        ra, dec, mag, _bv = struct.unpack_from('<Hhhh', data, 12 + 8 * i)
        ra = ra / 65536 * 2 * math.pi; dec = math.radians(dec / 32767 * 90); mag /= 100
        d = (math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec))
        intensity = min(2 * 10 ** (-0.4 * 0.75 * mag), 3)
        size = 0.6 + 0.6 * min(max((3 - mag) / 4, 0), 1)
        stars.append((d, intensity, size))
    lists = [[] for _ in range(CELLS)]
    for s, (d, _i, _s) in enumerate(stars):
        around = [cell_of(d)]
        side = (0, 0, 1) if abs(d[2]) < 0.9 else (1, 0, 0)
        e1 = norm(cross(d, side)); e2 = cross(d, e1)
        for ring in (0.25, 0.5, 0.75, 1.0, 1.08):
            for k in range(32):
                a = k / 32 * 2 * math.pi; r = MARGIN * ring
                p = tuple(d[j] + (e1[j] * math.cos(a) + e2[j] * math.sin(a)) * r for j in range(3))
                c = cell_of(p)
                if c not in around: around.append(c)
        for c in around: lists[c].append(s)
    offsets, binned = [], []
    for l in lists:
        offsets.append(len(binned)); binned.extend(stars[i] for i in l)
    offsets.append(len(binned))
    cells_h = (len(offsets) + CELLS_WIDTH - 1) // CELLS_WIDTH
    cells = [float(len(binned))] * (CELLS_WIDTH * cells_h)
    cells[:len(offsets)] = [float(o) for o in offsets]
    stars_h = max(1, (len(binned) * 2 + STARS_WIDTH - 1) // STARS_WIDTH)
    texels = [0.0] * (STARS_WIDTH * stars_h * 4)
    for i, (d, inten, size) in enumerate(binned):
        texels[i*8:i*8+8] = [d[0], d[1], d[2], inten, 1.0, 1.0, 1.0, size]
    return count, len(binned), cells_h, cells, stars_h, texels

def milky_way_texels():
    data = (STARS / 'milky_way.bin').read_bytes()
    assert data[:8] == b'VKSMWAY1'
    w, h, reach, cw, ch, length = struct.unpack_from('<IIfIII', data, 8)
    assert (w, h) == (MW_W, MW_H)
    raw = zlib.decompress(data[32:32 + length])
    colour = raw[w * h * 2:]
    out = []
    for i in range(w * h):
        x, y = i % w, i // w
        fx = (x + 0.5) * cw / w - 0.5
        fy = min(max((y + 0.5) * ch / h - 0.5, 0), ch - 1)
        x0, y0 = math.floor(fx), math.floor(fy); tx, ty = fx - x0, fy - y0
        xa = x0 % cw; xb = (xa + 1) % cw; ya = int(y0); yb = min(ya + 1, ch - 1)
        c = lambda cx, cy: colour[cy * cw + cx]
        top = c(xa, ya) + (c(xb, ya) - c(xa, ya)) * tx
        bottom = c(xa, yb) + (c(xb, yb) - c(xa, yb)) * tx
        out.append(raw[2*i] | raw[2*i+1] << 8 | int(round(top + (bottom - top) * ty)) << 16)
    return out

# --- GL ---------------------------------------------------------------------------------
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
teximage=fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P);texparam=fn('glTexParameteri',None,U,U,I)
texstorage=fn('glTexStorage2D',None,U,I,U,I,I);active=fn('glActiveTexture',None,U)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I)
geterror=fn('glGetError',U);loc=fn('glGetUniformLocation',I,U,C.c_char_p)
uniform1i=fn('glUniform1i',None,I,I);uniform2f=fn('glUniform2f',None,I,C.c_float,C.c_float)
uniform4f=fn('glUniform4f',None,I,C.c_float,C.c_float,C.c_float,C.c_float)
uniform3fv=fn('glUniform3fv',None,I,I,P);uniform1f=fn('glUniform1f',None,I,C.c_float)
draw=fn('glDrawArrays',None,U,I,I)
genq=fn('glGenQueries',None,I,C.POINTER(U));beginq=fn('glBeginQuery',None,U,U);endq=fn('glEndQuery',None,U)
qresult=fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64));finish=fn('glFinish',None)
readpix=fn('glReadPixels',None,I,I,I,I,U,U,P)
TEX2D=0x0DE1;NEAREST=0x2600;FLOAT=0x1406;TIME_ELAPSED=0x88BF;QUERY_RESULT=0x8866

def texture(unit, internal, w, h, fmt, kind, values, ctype):
    t = U(); gentex(1, C.byref(t)); active(0x84C0 + unit); bindtex(TEX2D, t)
    for p in (0x2801, 0x2800): texparam(TEX2D, p, NEAREST)
    for p in (0x2802, 0x2803): texparam(TEX2D, p, 0x812F)
    texparam(TEX2D, 0x813D, 0)
    buf = (ctype * len(values))(*values)
    teximage(TEX2D, 0, internal, w, h, 0, fmt, kind, buf)
    return t

count, nbinned, cells_h, cells, stars_h, star_texels = binned_stars()
mw = milky_way_texels()
texture(0, 0x822E, CELLS_WIDTH, cells_h, 0x1903, FLOAT, cells, C.c_float)            # R32F
texture(1, 0x8814, STARS_WIDTH, stars_h, 0x1908, FLOAT, star_texels, C.c_float)      # RGBA32F
texture(2, 0x8236, MW_W, MW_H, 0x8D94, 0x1405, mw, C.c_uint32)                      # R32UI
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
color=U();gentex(1,C.byref(color));active(0x84C3);bindtex(TEX2D,color);texstorage(TEX2D,1,0x881A,W,H)  # RGBA16F
fbo=U();genfbo(1,C.byref(fbo));bindfbo(0x8D40,fbo);attachtex(0x8D40,0x8CE0,TEX2D,color,0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,W,H)
assert geterror()==0

# The catalogue-to-region rotation: the galactic centre (J2000 RA 266.405, Dec -28.936)
# 50 degrees above the southern horizon (region frame x east, y north, z up).
def equatorial(ra, dec):
    ra, dec = math.radians(ra), math.radians(dec)
    return (math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec))
gc = equatorial(266.405, -28.936)
target = (0.0, -math.cos(math.radians(50)), math.sin(math.radians(50)))
def rotation(a, b):
    v = cross(a, b); c = dot(a, b); k = 1 / (1 + c)
    return [[v[0]*v[0]*k + c, v[0]*v[1]*k - v[2], v[0]*v[2]*k + v[1]],
            [v[1]*v[0]*k + v[2], v[1]*v[1]*k + c, v[1]*v[2]*k - v[0]],
            [v[2]*v[0]*k - v[1], v[2]*v[1]*k + v[0], v[2]*v[2]*k + c]]
R = rotation(gc, target)
axes = [R[r][c] for c in range(3) for r in range(3)]  # columns: region directions of x, y, z
right = norm(cross(target, (0, 0, 1))); up = cross(right, target)

def show(ref, path):
    return subprocess.run(['git','show',f'{ref}:{path}'],cwd=root,capture_output=True,text=True,check=True).stdout
def night_sky_block(src):
    start = src.index('// <Vulkanstorm> The real night sky')
    end = src.index('// </Vulkanstorm>', start)
    return src[start:end].replace('in vec3 vary_sky_dir;', '')
MAIN = '''
uniform vec2 screen;
uniform int scene;
uniform vec3 look, look_right, look_up;
out vec4 color;
void main()
{
    vec2 ndc = gl_FragCoord.xy / screen * 2.0 - 1.0;
    vec3 d;
    if (scene == 0)
    {
        float t = tan(radians(30.0));
        d = normalize(look + ndc.x * t * screen.x / screen.y * look_right + ndc.y * t * look_up);
    }
    else
    {
        float az = (ndc.x * 0.5 + 0.5) * 6.28318531;
        float alt = (ndc.y * 0.5 + 0.5) * 1.5707963;
        d = vec3(cos(alt) * sin(az), cos(alt) * cos(az), sin(alt));
    }
    vec3 dx = dFdx(d);
    vec3 dy = dFdy(d);
    float pixel = max(max(length(dx), length(dy)), 1e-6);
    color = vec4(%s, 1.0);
}
'''
vertex='#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}'
sources = {
    'old': (show(OLD_REF, SKY), 'night_sky(d, pixel, vec3(0.0))'),
    'new': ((root / SKY).read_text(), 'night_sky(d, dx, dy, pixel, vec3(0.0))'),
}
progs = {}
for name, (src, call) in sources.items():
    progs[name] = program([(0x8B31, vertex), (0x8B30, '#version 430 core\n' + night_sky_block(src) + MAIN % call)])
query = U(); genq(1, C.byref(query))

def frame(prog, scene, milky_way):
    fn('glUseProgram',None,U)(prog)
    uniform1i(loc(prog, b'night_sky_cells'), 0); uniform1i(loc(prog, b'night_sky_stars'), 1)
    l = loc(prog, b'night_sky_milky_way')
    if l >= 0: uniform1i(l, 2)
    uniform3fv(loc(prog, b'night_sky_axes'), 3, (C.c_float * 9)(*axes))
    uniform4f(loc(prog, b'night_sky_params'), 1.0, milky_way, 100.0, 0.0)
    uniform2f(loc(prog, b'screen'), W, H); uniform1i(loc(prog, b'scene'), scene)
    uniform3fv(loc(prog, b'look'), 1, (C.c_float * 3)(*target))
    uniform3fv(loc(prog, b'look_right'), 1, (C.c_float * 3)(*right))
    uniform3fv(loc(prog, b'look_up'), 1, (C.c_float * 3)(*up))
    beginq(TIME_ELAPSED, query); draw(4, 0, 3); endq(TIME_ELAPSED); finish()
    v = C.c_uint64(); qresult(query, QUERY_RESULT, C.byref(v))
    assert geterror() == 0
    return v.value / 1e6

def mean_luminance():
    px = (C.c_float * (W * H * 4))(); readpix(0, 0, W, H, 0x1908, FLOAT, px)
    return sum(0.2126 * px[i] + 0.7152 * px[i+1] + 0.0722 * px[i+2] for i in range(0, W * H * 4, 4 * 97)) / len(range(0, W * H * 4, 4 * 97))

print(f'GL night sky pass, {W}x{H}, median of {FRAMES} frames after {WARMUP} warmup; {count} stars, {nbinned} binned')
print(f'old: skyF.glsl at {OLD_REF}; new: working tree')
print(f'{"scene":<12}{"shader":<8}{"MW on":>9}{"MW off":>9}{"MW cost":>9}{"mean lum (MW on)":>19}')
for scene, scene_name in ((0, 'camera'), (1, 'hemisphere')):
    for name in ('old', 'new'):
        times = {}
        for milky_way in (1.0, 0.0):
            samples = [frame(progs[name], scene, milky_way) for _ in range(WARMUP + FRAMES)][WARMUP:]
            times[milky_way] = statistics.median(samples)
            if milky_way == 1.0: lum = mean_luminance()
        print(f'{scene_name:<12}{name:<8}{times[1.0]:>9.3f}{times[0.0]:>9.3f}{times[1.0]-times[0.0]:>9.3f}{lum:>19.5f}')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
