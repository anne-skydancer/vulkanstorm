# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline check and bench of the volumetric clouds' own shaders (stage L7, doc/light/volumetric-clouds.md), no
viewer launch. Windows, an OpenGL 4.6 driver.

1. Builds the six programs the viewer makes (LLViewerShaderMgr: weather, trace, still, reconstruction, composite and
   the volumetric cloud shadow map) from the shader files themselves, the way LLShaderMgr assembles them (#version 420,
   the stage define, the G-buffer flags, the permutation, srgbF.glsl linked in), and checks that every uniform the C++
   side sets is there.
2. Runs them on the real noise file (CLOUD_CHECK_NOISE, else app_settings/clouds/cloud_noise.vkcn of a staged build)
   and a stand-in cloud texture, for presets of the EEP sliders; the bands are llcloudscape.h's own (a small C++
   program prints LLCloudscape::make() for each preset; clang++ or g++ on PATH). Every pixel is sky (the worst case).
3. Checks the output (finite radiance, transmittance in [0, 1], cloud where the sliders put cloud; the shadow map
   in [0, 1] with no light added) and the weather map's mean cover against llcloudscape.h's mean-preserving
   modulation (the patch and growth fields must not change a band's mean cover).
4. Times every pass (GL_TIME_ELAPSED medians of the draw alone, its uniforms and bindings set before the query, so a
   busy CPU does not stretch the interval): the per-frame cost against the light and shadow budget.
5. Checks the shader's light inputs (cv_light_inputs) against their C++ twin (LLCloudscape::cloud_light), 24 skies.

Environment: CLOUD_CHECK_NOISE (path), CLOUD_CHECK_SIZES (default "1920x1080,2560x1440"), CLOUD_CHECK_FRAMES (20).
"""
import ctypes as C
import json
import math
import os
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
bootstrap = (REPO / 'scripts' / 'perf' / 'gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))

FRAMES = int(os.environ.get('CLOUD_CHECK_FRAMES', 20)); WARMUP = 5
SIZES = [tuple(int(v) for v in s.split('x')) for s in os.environ.get('CLOUD_CHECK_SIZES', '1920x1080,2560x1440').split(',')]
SHADERS = REPO / 'indra' / 'newview' / 'app_settings' / 'shaders'

F = C.c_float
VERT, FRAG = 0x8B31, 0x8B30
TEX2D, TEX3D = 0x0DE1, 0x806F
RGBA8, RGBA16F, R16F, R32F = 0x8058, 0x881A, 0x822D, 0x822E
LINEAR, NEAREST, REPEAT, CLAMP = 0x2601, 0x2600, 0x2901, 0x812F
FB, COLOR0 = 0x8D40, 0x8CE0
TIME_ELAPSED, QUERY_RESULT = 0x88BF, 0x8866
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print('FAIL', what, flush=True)


# --- GL entry points ---------------------------------------------------------------------------------------------
gentex = fn('glGenTextures', None, I, C.POINTER(U)); bindtex = fn('glBindTexture', None, U, U)
texparam = fn('glTexParameteri', None, U, U, I); active = fn('glActiveTexture', None, U)
texstorage2 = fn('glTexStorage2D', None, U, I, U, I, I); texstorage3 = fn('glTexStorage3D', None, U, I, U, I, I, I)
subimage2 = fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)
subimage3 = fn('glTexSubImage3D', None, U, I, I, I, I, I, I, I, U, U, P)
genfbo = fn('glGenFramebuffers', None, I, C.POINTER(U)); bindfbo = fn('glBindFramebuffer', None, U, U)
attach2 = fn('glFramebufferTexture2D', None, U, U, U, U, I)
drawbuffers = fn('glDrawBuffers', None, I, C.POINTER(U)); checkfbo = fn('glCheckFramebufferStatus', U, U)
viewport = fn('glViewport', None, I, I, I, I); geterror = fn('glGetError', U)
loc_ = fn('glGetUniformLocation', I, U, C.c_char_p)
u1i = fn('glUniform1i', None, I, I); u1f = fn('glUniform1f', None, I, F); u3f = fn('glUniform3f', None, I, F, F, F)
u4f = fn('glUniform4f', None, I, F, F, F, F); u4fv = fn('glUniform4fv', None, I, I, P)
umat4 = fn('glUniformMatrix4fv', None, I, I, C.c_ubyte, P)
draw = fn('glDrawArrays', None, U, I, I); enable = fn('glEnable', None, U); disable = fn('glDisable', None, U)
scissor = fn('glScissor', None, I, I, I, I)
genq = fn('glGenQueries', None, I, C.POINTER(U)); beginq = fn('glBeginQuery', None, U, U); endq = fn('glEndQuery', None, U)
qresult = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64)); finish = fn('glFinish', None)
getteximage = fn('glGetTexImage', None, U, I, U, U, P)
active_uniforms = fn('glGetProgramiv', None, U, U, C.POINTER(I))
uniform_info = fn('glGetActiveUniform', None, U, U, I, C.POINTER(I), C.POINTER(I), C.POINTER(U), C.c_char_p)
genbuf = fn('glGenBuffers', None, I, C.POINTER(U)); bindbuf = fn('glBindBuffer', None, U, U)
bufdata = fn('glBufferData', None, U, S, P, U)
vattrib = fn('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P); venable = fn('glEnableVertexAttribArray', None, U)
bindattrib = fn('glBindAttribLocation', None, U, U, C.c_char_p)

vao = U(); fn('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao)); fn('glBindVertexArray', None, U)(vao)
query = U(); genq(1, C.byref(query))
# the viewer's screen triangle (LLPipeline::mScreenTriangleVB), attribute 0 "position"
tri = (F * 9)(-1, 1, 0, -1, -3, 0, 3, 1, 0)
vbo = U(); genbuf(1, C.byref(vbo)); bindbuf(0x8892, vbo.value); bufdata(0x8892, C.sizeof(tri), tri, 0x88E4)
vattrib(0, 3, 0x1406, 0, 0, None); venable(0)

# --- the viewer's shader assembly (LLShaderMgr::loadShaderFile) ----------------------------------------------------
EXTRA = ('#define GBUFFER_FLAG_SKIP_ATMOS   0.0 \n#define GBUFFER_FLAG_HAS_ATMOS    0.34\n#define GBUFFER_FLAG_HAS_PBR      0.67\n'
         '#define GBUFFER_FLAG_HAS_HDRI      1.0\n#define GET_GBUFFER_FLAG(data, flag)    (abs(data-flag)< 0.1)\n')
GBUFFER = 'struct GBufferInfo { vec4 albedo; vec4 specular; vec3 normal; vec4 emissive; float gbufferFlag; float envIntensity; };\n'


def shader_text(rel, kind, defines):
    for cls in ('class3', 'class2', 'class1'):
        path = SHADERS / cls / rel
        if path.exists():
            break
    text = path.read_text(encoding='utf-8')
    extra = ('#define FRAGMENT_SHADER 1\n' if kind == FRAG else '#define VERTEX_SHADER 1\n') + EXTRA
    extra += ''.join(f'#define {k} {v}\n' for k, v in defines) + GBUFFER
    if '[EXTRA_CODE_HERE]' in text:
        text = text.replace('/*[EXTRA_CODE_HERE]*/', extra)
    else:
        text = extra + text
    return '#version 420\n' + text


link_program = fn('glLinkProgram', None, U)


def viewer_program(vertex, fragments, defines=()):
    stages = [(VERT, shader_text(vertex, VERT, defines))]
    stages += [(FRAG, shader_text(f, FRAG, defines)) for f in fragments + ['environment/srgbF.glsl']]
    prog = create_program()
    for kind, text in stages:
        shader = create_shader(kind); source = C.c_char_p(text.encode()); source_shader(shader, 1, C.byref(source), None)
        compile_shader(shader)
        status = I(); shader_iv(shader, 0x8B81, C.byref(status))
        log = C.create_string_buffer(16384); shader_log(shader, len(log), None, log)
        assert status.value, log.value.decode()
        attach(prog, shader); fn('glDeleteShader', None, U)(shader)
    bindattrib(prog, 0, b'position')
    link_program(prog); status = I(); program_iv(prog, 0x8B82, C.byref(status))
    log = C.create_string_buffer(16384); program_log(prog, len(log), None, log)
    assert status.value, log.value.decode()
    return prog


def uniforms_of(prog):
    n = I(); active_uniforms(prog, 0x8B86, C.byref(n)); names = set()
    for i in range(n.value):
        length = I(); size = I(); kind = U(); name = C.create_string_buffer(256)
        uniform_info(prog, i, 256, C.byref(length), C.byref(size), C.byref(kind), name)
        names.add(name.value.decode().split('[')[0])
    return names


FUNCS = 'deferred/cloudsVolFuncsF.glsl'
progs = {}
for name, vertex, fragments, defines in [
        ('weather', 'deferred/postDeferredNoTCV.glsl', ['deferred/cloudsVolWeatherF.glsl'], ()),
        ('trace', 'deferred/postDeferredNoTCV.glsl', ['deferred/cloudsVolTraceF.glsl', FUNCS], ()),
        ('still', 'deferred/postDeferredNoTCV.glsl', ['deferred/cloudsVolTraceF.glsl', FUNCS], (('CLOUDS_VOL_STILL', '1'),)),
        ('recon', 'deferred/postDeferredNoTCV.glsl', ['deferred/cloudsVolReconF.glsl'], ()),
        ('composite', 'deferred/cloudsVolCompositeV.glsl', ['deferred/cloudsVolCompositeF.glsl'], ()),
        ('shadow', 'deferred/postDeferredNoTCV.glsl', ['deferred/cloudShadowVolF.glsl', FUNCS], ())]:
    try:
        progs[name] = viewer_program(vertex, fragments, defines)
        print(f'built {name}', flush=True)
    except AssertionError as e:
        check(False, f'{name} does not build: {e}')
if len(progs) < 6:
    print('\n'.join(failures)); sys.exit(1)

# the uniforms LLVolumetricClouds and LLCloudShadowMap set must be live in the programs that read them
BANDS = {'cv_layer', 'cv_shape', 'cv_place', 'cv_noise', 'cv_march', 'cv_map', 'cv_light', 'cv_gain', 'cloud_vol_base', 'cloud_vol_erosion',
         'cloud_vol_curl', 'cloud_vol_weather'}
expected = {
    'weather': {'cvw_sky', 'cvw_density1', 'cvw_density2', 'cvw_cover', 'cvw_growth', 'cvw_mean', 'cloud_noise_texture', 'cloud_noise_texture_next',
                'blend_factor', 'cloud_vol_base'},
    'trace': BANDS | {'depthMap', 'cv_inv_vp', 'cv_sizes', 'cv_frame', 'sunlight_color', 'ambient_color', 'cloud_color', 'cloud_light_transmittance',
                      'cloud_moon_blend', 'sky_hdr_scale', 'blue_horizon', 'blue_density', 'haze_horizon', 'haze_density', 'density_multiplier',
                      'max_y', 'glow', 'cloud_shadow'},
    'recon': {'depthMap', 'cloud_vol_trace', 'cloud_vol_tdepth', 'cloud_vol_history', 'cv_inv_vp', 'cv_prev_vp', 'cv_sizes', 'cv_frame', 'cv_delta'},
    'composite': {'cloud_vol_history', 'cv_sizes'},
    'shadow': (BANDS - {'cv_light', 'cv_gain', 'cv_march'}) | {'cvs_map', 'cvs_light', 'ctm_optics'},
}
expected['still'] = expected['trace']
for name, names in expected.items():
    missing = names - uniforms_of(progs[name])
    check(not missing, f'{name}: uniforms not live: {sorted(missing)}')

# --- textures ------------------------------------------------------------------------------------------------------
def tex(target, internal, w, h, d=1, wrap=REPEAT, filt=LINEAR):
    t = U(); gentex(1, C.byref(t)); bindtex(target, t.value)
    if target == TEX3D: texstorage3(TEX3D, 1, internal, w, h, d)
    else: texstorage2(TEX2D, 1, internal, w, h)
    for p in (0x2801, 0x2800): texparam(target, p, filt)
    for p in (0x2802, 0x2803, 0x8072): texparam(target, p, wrap)
    texparam(target, 0x813D, 0)
    return t.value


def fbo(*colors):
    f = U(); genfbo(1, C.byref(f)); bindfbo(FB, f.value)
    for i, t in enumerate(colors): attach2(FB, COLOR0 + i, TEX2D, t, 0)
    bufs = (U * len(colors))(*[COLOR0 + i for i in range(len(colors))]); drawbuffers(len(colors), bufs)
    assert checkfbo(FB) == 0x8CD5
    return f.value


def noise_path():
    env = os.environ.get('CLOUD_CHECK_NOISE')
    if env: return Path(env)
    for build in sorted(REPO.glob('build-*')) + sorted(REPO.parent.parent.glob('build-*')):
        for p in build.glob('newview/*/app_settings/clouds/cloud_noise.vkcn'):
            return p
        p = build / 'packages' / 'clouds' / 'cloud_noise.vkcn'
        if p.exists(): return p
    return None


path = noise_path()
if not path or not path.exists():
    print('no cloud_noise.vkcn found (set CLOUD_CHECK_NOISE)'); sys.exit(2)
data = path.read_bytes()
assert data[:8] == b'VKCLDNZ1' and len(data) == 8585472, 'not the noise file'
entries = {}
for i in range(3):
    e = 52 + i * 68
    kind, fmt, w, h, d = struct.unpack_from('<5I', data, e); offset, length = struct.unpack_from('<2Q', data, e + 20)
    entries[kind] = (w, h, d, offset, length)
print(f'noise: {path}', flush=True)
base_t = tex(TEX3D, RGBA8, 128, 128, 128)
w, h, d, off, n = entries[1]; buf = C.create_string_buffer(data[off:off + n], n)
subimage3(TEX3D, 0, 0, 0, 0, w, h, d, 0x1908, 0x1401, buf)
slice_mean = [sum(data[off + z * 128 * 128 * 4 + 1: off + (z + 1) * 128 * 128 * 4: 4]) / (255.0 * 128 * 128) for z in range(128)]
erosion_t = tex(TEX3D, RGBA8, 32, 32, 32)
w, h, d, off, n = entries[2]; subimage3(TEX3D, 0, 0, 0, 0, w, h, d, 0x1908, 0x1401, C.create_string_buffer(data[off:off + n], n))
curl_t = tex(TEX2D, RGBA8, 128, 128)
w, h, d, off, n = entries[3]; subimage2(TEX2D, 0, 0, 0, w, h, 0x1908, 0x1401, C.create_string_buffer(data[off:off + n], n))

# a stand-in EEP cloud texture: tileable value-noise fbm (as the light study's bench and the Aurora Viewer's demo)
CLOUD_TEX = """#version 420
out vec4 o;
float vhash(ivec2 c) { uint h = uint(c.x) * 73856093u ^ uint(c.y) * 19349663u; h = (h ^ (h >> 13u)) * 1274126177u; return float(h & 65535u) / 65535.0; }
float vnoise(vec2 p, int period) { ivec2 i = ivec2(floor(p)); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);
    ivec2 a = (i + ivec2(8 * period)) % period, b = (i + ivec2(1) + ivec2(8 * period)) % period;
    return mix(mix(vhash(a), vhash(ivec2(b.x, a.y)), f.x), mix(vhash(ivec2(a.x, b.y)), vhash(b), f.x), f.y); }
void main() { vec2 uv = gl_FragCoord.xy / 256.0; float s = 0.0, a = 0.5; int per = 4;
    for (int k = 0; k < 4; k++) { s += a * vnoise(uv * float(per), per); a *= 0.5; per *= 2; } s /= 0.9375; o = vec4(s, s, s, 1.0); }"""
VTRI = '#version 420\nin vec3 position; void main() { gl_Position = vec4(position, 1.0); }'
cloudtex = tex(TEX2D, RGBA8, 256, 256)
p = program([(VERT, VTRI), (FRAG, CLOUD_TEX)]); use(p); bindfbo(FB, fbo(cloudtex)); viewport(0, 0, 256, 256); draw(4, 0, 3)

# --- llcloudscape.h's bands for each preset (a C++ program prints LLCloudscape::make()) ---------------------------
DUMP = r'''
#include "llcloudscape.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv)
{
    if (argc == 16)
    {
        // the clouds' light: sunlight, ambient, cloud colour, the transmittance (3 each), cloud_shadow, moon blend, HDR scale
        LLCloudscape::SkyLight k;
        double v[15];
        for (int i = 0; i < 15; ++i)
        {
            v[i] = atof(argv[i + 1]);
        }
        k.sunlight = { v[0], v[1], v[2] };
        k.ambient = { v[3], v[4], v[5] };
        k.cloud_color = { v[6], v[7], v[8] };
        k.cloud_T = { v[9], v[10], v[11] };
        k.cloud_shadow = v[12];
        k.moon_blend = v[13];
        k.hdr_scale = v[14];
        const LLCloudscape::CloudLight l = LLCloudscape::cloud_light(k);
        std::printf("[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]\n", l.sun.r, l.sun.g, l.sun.b, l.ambient.r, l.ambient.g, l.ambient.b);
        return 0;
    }
    LLCloudscape::Sliders s;
    s.coverage = atof(argv[1]); s.scale = atof(argv[2]); s.variance = atof(argv[3]); s.density = atof(argv[4]); s.detail = atof(argv[5]);
    const LLCloudscape::Scape sc = LLCloudscape::make(s, LLCloudscape::Tuning());
    std::printf("{\"count\":%d,\"tile_km\":%.9g,\"tiles\":%d,\"map_km\":%.9g,\"map_size\":%d,\"patch\":%.9g,\"growth\":%.9g,\"rate\":%.9g,\"bands\":[",
                sc.count, sc.tile_km, sc.tiles_per_map, sc.map_km, sc.map_size, sc.patch, sc.growth, sc.growth_rate);
    for (int i = 0; i < sc.count; ++i)
    {
        const LLCloudscape::Band& b = sc.bands[i];
        std::printf("%s{\"genus\":\"%s\",\"channel\":%d,\"base\":%.9g,\"top\":%.9g,\"shape\":%.9g,\"sigma\":%.9g,\"erosion\":%.9g,\"anvil\":%.9g,"
                    "\"noise_scale\":%.9g,\"stretch\":%.9g,\"cover\":%.9g,\"sheet\":%.9g,\"drift\":%.9g,\"far\":%.9g,\"steps\":%d,\"cap\":%d,\"lsteps\":%d}",
                    i ? "," : "", LLCloudscape::genus_name(b.genus), b.channel, b.base_km, b.top_km, b.shape, b.sigma, b.erosion, b.anvil,
                    b.noise_scale, b.stretch, b.cover, b.sheet, b.drift, b.far_km, b.steps, b.step_cap, b.light_steps);
    }
    std::printf("]}\n");
    return 0;
}
'''
compiler = shutil.which('clang++') or shutil.which('g++')
assert compiler, 'clang++ or g++ is needed for the bands'
work = Path(tempfile.mkdtemp())
(work / 'dump.cpp').write_text(DUMP)
exe = work / 'dump.exe'
subprocess.run([compiler, '-std=c++20', '-O1', '-I', str(REPO / 'indra' / 'newview'), str(work / 'dump.cpp'), '-o', str(exe)], check=True)

PRESETS = [  # name, coverage, scale, variance, density D, detail D
    ('fair Cu', 0.27, 0.42, 0.0, 1.0, 0.12),
    ('broken Cu', 0.38, 0.42, 0.0, 1.0, 0.12),
    ('Sc', 0.45, 0.42, 0.0, 1.0, 0.12),
    ('overcast St', 0.62, 0.42, 0.0, 1.0, 0.12),
    ('Cu con', 0.30, 0.42, 0.0, 1.6, 0.12),
    ('Cb', 0.30, 0.42, 0.0, 2.6, 0.12),
    ('Ac layer', 0.30, 0.12, 0.0, 1.0, 0.12),
    ('Cu+Ac+Ci', 0.27, 0.42, 0.6, 1.0, 0.8),
]
scapes = {}
for name, *sliders in PRESETS:
    out = subprocess.run([str(exe)] + [str(v) for v in sliders], check=True, capture_output=True, text=True).stdout
    scapes[name] = json.loads(out)


# --- uniforms ------------------------------------------------------------------------------------------------------
def L(prog, name): return loc_(prog, name.encode())


def set_vec4s(prog, name, values):
    arr = (F * len(values))(*values)
    l = L(prog, name)
    if l >= 0: u4fv(l, len(values) // 4, arr)


SUN = (0.55, 0.62, 0.56)  # y up (east, up, north): about 38 degrees up
sun_len = math.sqrt(sum(v * v for v in SUN)); SUN = tuple(v / sun_len for v in SUN)


def band_uniforms(prog, sc):
    layer, shape, place, noise, march = [], [], [], [], []
    for i in range(3):
        if i < sc['count']:
            b = sc['bands'][i]
            layer += [b['base'], b['top'], b['shape'], 1.0]
            shape += [b['sigma'], b['erosion'], b['anvil'], 10.5 * b['noise_scale']]
            place += [3.7 % sc['map_km'], 5.1 % sc['map_km'], b['sheet'], b['channel']]
            noise += [3.7, 5.1, b['stretch'], b['far']]
            march += [b['steps'], b['lsteps'], b['cap'], 0]
        else:
            layer += [0, 1, 0, 0]; shape += [0, 0.3, 0, 10.5]; place += [0, 0, 0, 0]; noise += [0, 0, 1, 0]; march += [0, 0, 0, 0]
    for name, v in (('cv_layer', layer), ('cv_shape', shape), ('cv_place', place), ('cv_noise', noise), ('cv_march', march),
                    ('cv_map', [sc['map_km'], 1.0, 0.0, sc['count']]), ('cv_light', [*SUN, 0.0]), ('cv_gain', [4.5, 0.9, 0, 0])):
        set_vec4s(prog, name, v)
    for unit, name in enumerate((b'cloud_vol_base', b'cloud_vol_erosion', b'cloud_vol_curl', b'cloud_vol_weather')):
        l = loc_(prog, name)
        if l >= 0: u1i(l, unit)


def sky_uniforms(prog, coverage):
    for name, v in (('sunlight_color', (0.73, 0.73, 0.65)), ('ambient_color', (0.25, 0.25, 0.32)), ('cloud_color', (0.41, 0.41, 0.41)),
                    ('cloud_light_transmittance', (0.92, 0.88, 0.82)), ('blue_horizon', (0.4954, 0.4954, 0.64)),
                    ('blue_density', (0.2447, 0.4487, 0.76)), ('glow', (5.0, 0.001, -0.48))):
        l = L(prog, name)
        if l >= 0: u3f(l, *v)
    for name, v in (('haze_horizon', 0.19), ('haze_density', 0.7), ('density_multiplier', 0.0001), ('max_y', 1605.0), ('cloud_shadow', coverage),
                    ('cloud_moon_blend', 0.0), ('sky_hdr_scale', 1.0)):
        l = L(prog, name)
        if l >= 0: u1f(l, v)


def bind(unit, target, t):
    active(0x84C0 + unit); bindtex(target, t)


def bind_noise(weather):
    bind(0, TEX3D, base_t); bind(1, TEX3D, erosion_t); bind(2, TEX2D, curl_t); bind(3, TEX2D, weather)


def timed(work):
    beginq(TIME_ELAPSED, query.value); work(); endq(TIME_ELAPSED); finish()
    v = C.c_uint64(); qresult(query.value, QUERY_RESULT, C.byref(v))
    err = geterror(); assert err == 0, hex(err)
    return v.value / 1e6


def timed_draw():
    beginq(TIME_ELAPSED, query.value); draw(4, 0, 3); endq(TIME_ELAPSED); finish()
    v = C.c_uint64(); qresult(query.value, QUERY_RESULT, C.byref(v))
    err = geterror(); assert err == 0, hex(err)
    return v.value / 1e6


def median(work, frames=FRAMES):
    # work(i) sets a pass up and returns timed_draw(): only the draw is timed
    return statistics.median([work(i) for i in range(WARMUP + frames)][WARMUP:])


def read(t, w, h):
    bindtex(TEX2D, t); px = (F * (w * h * 4))(); getteximage(TEX2D, 0, 0x1908, 0x1406, px)
    return px


# --- 4x4 matrices (column-major, as GL) ------------------------------------------------------------------------------
def mul(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def inverse(m):
    a = [[m[c * 4 + r] for c in range(4)] + [1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]
    for i in range(4):
        piv = max(range(i, 4), key=lambda r: abs(a[r][i])); a[i], a[piv] = a[piv], a[i]
        d = a[i][i]; a[i] = [v / d for v in a[i]]
        for r in range(4):
            if r != i:
                f = a[r][i]; a[r] = [x - f * y for x, y in zip(a[r], a[i])]
    return [a[r][4 + c] for c in range(4) for r in range(4)]


def view_projection(elevation_deg, w, h):
    el = math.radians(elevation_deg)
    fwd = (math.cos(el), 0.0, math.sin(el)); up = (-math.sin(el), 0.0, math.cos(el))  # world z up
    right = (fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0])
    view = [right[0], up[0], -fwd[0], 0, right[1], up[1], -fwd[1], 0, right[2], up[2], -fwd[2], 0, 0, 0, 0, 1]
    f = 1.0 / math.tan(math.radians(30.0)); n, z = 0.1, 512.0
    proj = [f * h / w, 0, 0, 0, 0, f, 0, 0, 0, 0, (z + n) / (n - z), -1, 0, 0, 2 * z * n / (n - z), 0]
    return mul(proj, view)


# --- the weather map, and its mean cover against the modulation's promise -------------------------------------------
def weather_for(sc, coverage, scale, variance, detail, growth_t=0.0):
    size = sc['map_size']; wtex = tex(TEX2D, RGBA8, size, size); f = fbo(wtex)
    p = progs['weather']; use(p); viewport(0, 0, size, size)
    bind(0, TEX2D, cloudtex); bind(1, TEX2D, cloudtex); bind(2, TEX3D, base_t)
    u1i(L(p, 'cloud_noise_texture'), 0); u1i(L(p, 'cloud_noise_texture_next'), 1); u1i(L(p, 'cloud_vol_base'), 2); u1f(L(p, 'blend_factor'), 0.0)
    mid = next((b['cover'] for b in sc['bands'] if b['channel'] == 1), 0.0); high = next((b['cover'] for b in sc['bands'] if b['channel'] == 2), 0.0)
    zs = [((growth_t * sc['rate'] / 4.0) + band / 3.0) % 1.0 for band in range(3)]

    def mean_at(z):
        t = z * 128 - 0.5; i0 = math.floor(t); fr = t - i0; i0 %= 128
        return slice_mean[i0] + (slice_mean[(i0 + 1) % 128] - slice_mean[i0]) * fr
    u4f(L(p, 'cvw_sky'), coverage, scale, variance, sc['tiles']); u4f(L(p, 'cvw_density1'), 0.5, 0.526, size, 0.5 - 1.0 / scale)
    u4f(L(p, 'cvw_density2'), 0.5, 0.526, detail, 0.0); u4f(L(p, 'cvw_cover'), mid, high, sc['patch'], sc['growth'])
    u4f(L(p, 'cvw_growth'), zs[0], zs[1], zs[2], sc['tiles']); u4f(L(p, 'cvw_mean'), *(mean_at(z) for z in zs), 0.0)
    t = timed(lambda: draw(4, 0, 3))
    return wtex, f, t, (mid, high)


def unmodulated_mean(sc, coverage, scale, variance, detail):
    # the same map with the modulation off: what the modulation must keep
    keep = dict(sc); keep['patch'] = 0.0; keep['growth'] = 0.0
    wtex, f, _, covers = weather_for(keep, coverage, scale, variance, detail)
    return wtex


# the clouds' light inputs: the GLSL (cloudsVolFuncsF.glsl cv_light_inputs) against its C++ twin (LLCloudscape::cloud_light)
LIGHT_PROBE = '''#version 420
out vec4 o;
uniform int which;
void cv_light_inputs(out vec3 sun_col, out vec3 amb_col);
void main() { vec3 s; vec3 a; cv_light_inputs(s, a); o = which == 0 ? vec4(s, 1.0) : vec4(a, 1.0); }'''
light_prog = create_program()
for kind, text in ((VERT, VTRI), (FRAG, LIGHT_PROBE), (FRAG, shader_text(FUNCS, FRAG, ())), (FRAG, shader_text('environment/srgbF.glsl', FRAG, ()))):
    sh = create_shader(kind); src = C.c_char_p(text.encode()); source_shader(sh, 1, C.byref(src), None); compile_shader(sh)
    st = I(); shader_iv(sh, 0x8B81, C.byref(st)); lg = C.create_string_buffer(16384); shader_log(sh, len(lg), None, lg)
    assert st.value, lg.value.decode()
    attach(light_prog, sh)
bindattrib(light_prog, 0, b'position'); link_program(light_prog)
st = I(); program_iv(light_prog, 0x8B82, C.byref(st)); assert st.value
probe_tex = tex(TEX2D, 0x8814, 1, 1, wrap=CLAMP, filt=NEAREST); probe_fbo = fbo(probe_tex)  # RGBA32F
import random
rng = random.Random(1729)
worst_light = 0.0
for case in range(24):
    sun = [rng.uniform(0.0, 1.6) for _ in range(3)]; amb = [rng.uniform(0.0, 1.2) for _ in range(3)]
    cloud = [rng.uniform(0.0, 1.0) for _ in range(3)]; trans = [rng.uniform(0.05, 1.0) for _ in range(3)]
    shadow = rng.uniform(0.0, 1.0); moon = rng.choice([0.0, 0.0, 0.3, 1.0]); hdr = rng.choice([1.0, 2.0, 1.414])
    ref = json.loads(subprocess.run([str(exe)] + [str(v) for v in sun + amb + cloud + trans + [shadow, moon, hdr]], check=True,
                                    capture_output=True, text=True).stdout)
    use(light_prog); bindfbo(FB, probe_fbo); viewport(0, 0, 1, 1)
    u3f(L(light_prog, 'sunlight_color'), *sun); u3f(L(light_prog, 'ambient_color'), *amb); u3f(L(light_prog, 'cloud_color'), *cloud)
    u3f(L(light_prog, 'cloud_light_transmittance'), *trans); u1f(L(light_prog, 'cloud_shadow'), shadow)
    u1f(L(light_prog, 'cloud_moon_blend'), moon); u1f(L(light_prog, 'sky_hdr_scale'), hdr)
    got = []
    for which in (0, 1):
        u1i(L(light_prog, 'which'), which); draw(4, 0, 3); got += list(read(probe_tex, 1, 1))[:3]
    for g, r in zip(got, ref):
        worst_light = max(worst_light, abs(g - r) / max(abs(r), 1e-3))
check(worst_light < 2e-4, f"the clouds' light inputs: GLSL against the C++ twin, worst relative error {worst_light:.2e}")
print(f'light inputs, GLSL against LLCloudscape::cloud_light, 24 skies: worst relative error {worst_light:.2e}', flush=True)
shutil.rmtree(work, ignore_errors=True)

print(f'\nGL volumetric clouds check; median of {FRAMES} frames after {WARMUP} warm-up (ms); every pixel sky', flush=True)
results = []
for name, cov, scale, var, dens, det in PRESETS:
    sc = scapes[name]
    genera = '+'.join(b['genus'] for b in sc['bands'])
    wtex, wfbo, t_weather, (mid, high) = weather_for(sc, cov, scale, var, det, growth_t=731.0)
    size = sc['map_size']; px = read(wtex, size, size); n = size * size
    means = [sum(px[i + c] for i in range(0, n * 4, 4)) / n for c in range(3)]
    ref = read(unmodulated_mean(sc, cov, scale, var, det), size, size)
    ref_mean = sum(ref[i] for i in range(0, n * 4, 4)) / n
    # the modulation keeps the creator's band's mean cover (8-bit map, bilinear weights): within 1 %
    check(abs(means[0] - ref_mean) <= 0.01 * max(ref_mean, 0.02) + 2.0 / 255, f'{name}: the weather map keeps the mean cover ({means[0]:.4f} vs {ref_mean:.4f})')
    if mid > 0: check(abs(means[1] - mid) <= 0.01 + 2.0 / 255, f'{name}: the middle band keeps its mean cover ({means[1]:.4f} vs {mid:.4f})')
    if high > 0: check(abs(means[2] - high) <= 0.01 + 2.0 / 255, f'{name}: the high band keeps its mean cover ({means[2]:.4f} vs {high:.4f})')
    for (W, H) in SIZES:
        hw, hh = (W + 1) // 2, (H + 1) // 2; tw, th = (hw + 1) // 2, (hh + 1) // 2
        trace = tex(TEX2D, RGBA16F, tw, th, wrap=CLAMP, filt=NEAREST); tdepth = tex(TEX2D, R16F, tw, th, wrap=CLAMP, filt=NEAREST)
        hist = [tex(TEX2D, RGBA16F, hw, hh, wrap=CLAMP) for _ in range(2)]; full = tex(TEX2D, RGBA16F, W, H, wrap=CLAMP)
        depth = tex(TEX2D, R32F, W, H, wrap=CLAMP, filt=NEAREST)
        ones = (F * (W * H))(*([1.0] * (W * H))); bindtex(TEX2D, depth); subimage2(TEX2D, 0, 0, 0, W, H, 0x1903, 0x1406, ones)
        trace_fbo = fbo(trace, tdepth); hist_fbo = [fbo(hist[0]), fbo(hist[1])]; full_fbo = fbo(full)
        for view, elevation in (('horizon', 12.0), ('zenith', 60.0)):
            vp = view_projection(elevation, W, H); inv = (F * 16)(*inverse(vp)); vpa = (F * 16)(*vp)

            def frag(i):
                p = progs['trace']; use(p); bindfbo(FB, trace_fbo); viewport(0, 0, tw, th)
                band_uniforms(p, sc); sky_uniforms(p, cov); bind_noise(wtex); bind(4, TEX2D, depth); u1i(L(p, 'depthMap'), 4)
                umat4(L(p, 'cv_inv_vp'), 1, 0, inv); u4f(L(p, 'cv_sizes'), W, H, hw, hh); u4f(L(p, 'cv_frame'), i, 0, 0, 0); return timed_draw()

            def recon(i):
                p = progs['recon']; use(p); bindfbo(FB, hist_fbo[i % 2]); viewport(0, 0, hw, hh)
                bind(0, TEX2D, depth); bind(1, TEX2D, trace); bind(2, TEX2D, tdepth); bind(3, TEX2D, hist[(i + 1) % 2])
                u1i(L(p, 'depthMap'), 0); u1i(L(p, 'cloud_vol_trace'), 1); u1i(L(p, 'cloud_vol_tdepth'), 2); u1i(L(p, 'cloud_vol_history'), 3)
                umat4(L(p, 'cv_inv_vp'), 1, 0, inv); umat4(L(p, 'cv_prev_vp'), 1, 0, vpa)
                u4f(L(p, 'cv_sizes'), W, H, hw, hh); u4f(L(p, 'cv_frame'), i, 1, 0, 0); u4f(L(p, 'cv_delta'), 0, 0, 0, 1); return timed_draw()

            def composite(i):
                p = progs['composite']; use(p); bindfbo(FB, full_fbo); viewport(0, 0, W, H); bind(0, TEX2D, hist[0])
                u1i(L(p, 'cloud_vol_history'), 0); u4f(L(p, 'cv_sizes'), W, H, 0, 0)
                enable(0x0BE2); fn('glBlendFunc', None, U, U)(1, 0x0302); t = timed_draw(); disable(0x0BE2); return t
            tf = median(frag); tr = median(recon); tc = median(composite)
            out = read(trace, tw, th); cnt = tw * th
            finite = all(math.isfinite(v) for v in out)
            ts = [out[i + 3] for i in range(0, cnt * 4, 4)]
            check(finite, f'{name} {view}: the trace is finite')
            check(all(-1e-3 <= v <= 1.0 + 1e-3 for v in ts), f'{name} {view}: transmittance in [0, 1]')
            mean_t = sum(ts) / cnt
            results.append((W, H, name, genera, view, tf, tr, tc, t_weather, mean_t))
            print(f'{W}x{H} {name:<12}{genera:<14}{view:<8} trace {tf:6.3f}  recon {tr:6.3f}  comp {tc:6.3f}  total {tf + tr + tc:6.3f}  mean T {mean_t:5.2f}', flush=True)
        if (W, H) == SIZES[0]:
            # the still (a photo): every pixel, twice the steps, 4 sub-samples
            def still(i):
                p = progs['still']; use(p); bindfbo(FB, full_fbo); viewport(0, 0, W, H)
                band_uniforms(p, sc); sky_uniforms(p, cov); bind_noise(wtex); bind(4, TEX2D, depth); u1i(L(p, 'depthMap'), 4)
                umat4(L(p, 'cv_inv_vp'), 1, 0, (F * 16)(*inverse(view_projection(30.0, W, H)))); u4f(L(p, 'cv_sizes'), W, H, 0, 0)
                u4f(L(p, 'cv_frame'), 0, 0, 0, 4); return timed_draw()
            ts_still = median(still, frames=3)
            print(f'{W}x{H} {name:<12}still (a photo, once): {ts_still:7.2f} ms', flush=True)
    # the shadow map, 512^2 over 4 km, the sun as above
    smap = tex(TEX2D, RGBA8, 512, 512); sfbo = fbo(smap)

    def shadow(i):
        p = progs['shadow']; use(p); bindfbo(FB, sfbo); viewport(0, 0, 512, 512); band_uniforms(p, sc); bind_noise(wtex)
        u4f(L(p, 'cvs_map'), -2.048, -2.048, 4.096, 0); u4f(L(p, 'cvs_light'), *SUN, 0); u4f(L(p, 'ctm_optics'), 7.6, 1 - 0.85 ** 2, 0.75 * 0.15, 1 / 255.0)
        return timed_draw()
    t_shadow = median(shadow)
    sp = read(smap, 512, 512); r = [sp[i] for i in range(0, 512 * 512 * 4, 4)]; g = [sp[i + 1] for i in range(0, 512 * 512 * 4, 4)]
    check(all(0.0 <= v <= 1.0 for v in r) and all(v >= 0.0 for v in g) and all(a + b <= 1.0 + 2.0 / 255 for a, b in zip(r, g)),
          f'{name}: the shadow map adds no light')
    print(f'shadow map 512^2 {name:<12}{t_shadow:6.3f} ms, mean beam {sum(r) / len(r):5.2f}; weather map {size}^2 once {t_weather:6.3f} ms', flush=True)

worst = {}
for W, H, name, genera, view, tf, tr, tc, tw_, mt in results:
    worst[(W, H)] = max(worst.get((W, H), 0.0), tf + tr + tc)
for (W, H), v in worst.items():
    print(f'worst total at {W}x{H}: {v:.3f} ms')
# clouds where the sliders put clouds: the overcast sky is opaque overhead, the fair sky mostly clear
zen = {(n, v): mt for W, H, n, g, v, a, b, c, d, mt in results if (W, H) == SIZES[0]}
check(zen[('overcast St', 'zenith')] < 0.5, 'overcast: the zenith view is mostly cloud')
check(zen[('fair Cu', 'zenith')] > 0.5, 'fair cumulus: the zenith view is mostly clear')
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print(f'{len(failures)} failure(s)'); sys.exit(1)
print('volumetric clouds: all checks passed')
