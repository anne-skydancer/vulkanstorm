# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Atmosphere study bench (doc/light/atmosphere-study.md): today's EEP/Windlight haze model against
a physical atmosphere driven by the same EEP values, on the GPU, with no viewer launch.

Today is the viewer's own shader code, unmodified, in a hidden OpenGL context
(scripts/perf/gl_test_context.py through atmosphere_gl):
  * the sky: class1/deferred/skyV.glsl, run as it is on one vertex per panorama pixel and read back
    by transform feedback (vary_HazeColor), then skyF.glsl's x2 and clamp 5 and the lighting
    pass's decode and sky_hdr_scale (softenLightF.glsl, GBUFFER_FLAG_SKIP_ATMOS branch);
  * objects: class1/windlight/atmosphericsFuncs.glsl calcAtmosphericVarsLinear, with
    class3/deferred/hazeF.glsl's use of it (additive * 2 decoded, x sky_hdr_scale, blended with
    the transmittance's red channel), on a grey ground plane; positions and the light direction in
    eye space and lightnorm in world space, as the viewer passes them.
The PBR path (a sky with reflection_probe_ambiance) is shown: classic skies stay on today's model
(study section 4.4).

Proposed is the study's model, written here as GLSL from the papers (no sample code):
Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (LUTs:
transmittance 256x64, multiple scattering 32x32, sky view 192x108, aerial perspective 32^3), on
Bruneton & Neyret 2008's parameterisation. The atmosphere's profiles are EEP's stored
rayleigh_config / mie_config / absorption_config; the coefficients come from the legacy haze fields
through the mapping of study section 4.2 (map_sky below).

Outputs:
  1. side-by-side panoramas (today above, proposed below) of the study's skies, PNG, in --out
     (default doc/light/atmosphere/);
  2. tables: the sun's colour at the ground against elevation, the change per sky over the shipped
     library (768 sky files), the eye/world mismatch of today's object haze;
  3. GL_TIME_ELAPSED medians of the LUT passes and the per-pixel passes at 1080p and 1440p,
     against today's haze pass.

    python scripts/light/bench_atmosphere.py [--out DIR] [--no-images] [--no-library] [--no-timing]

Needs Windows and a GPU.
"""
import ctypes as C
import math
import statistics
import struct
import sys
import zlib
from pathlib import Path
from urllib.parse import unquote

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403
from atmosphere_gl import _llsd_value  # noqa: E402

OUT_DIR = ROOT / 'doc' / 'light' / 'atmosphere'
for _k, _a in enumerate(sys.argv):
    if _a == '--out' and _k + 1 < len(sys.argv):
        OUT_DIR = Path(sys.argv[_k + 1])
NO_IMAGES = '--no-images' in sys.argv
NO_LIBRARY = '--no-library' in sys.argv
NO_TIMING = '--no-timing' in sys.argv
SKY_DIR = SHADERS.parent / 'windlight' / 'skies'


def sky_file(name):
    """The sky file by its decoded name (the files are stored URL-encoded)."""
    for f in SKY_DIR.iterdir():
        if unquote(f.name) == name:
            return f
    raise FileNotFoundError(name)

# --- GL entry points beyond atmosphere_gl ----------------------------------------------------
genbuf = fn('glGenBuffers', None, I, C.POINTER(U)); bindbuf = fn('glBindBuffer', None, U, U)
bufdata = fn('glBufferData', None, U, S, P, U); getbufsub = fn('glGetBufferSubData', None, U, S, S, P)
bindbufbase = fn('glBindBufferBase', None, U, U, U)
tfvaryings = fn('glTransformFeedbackVaryings', None, U, I, C.POINTER(C.c_char_p), U)
begintf = fn('glBeginTransformFeedback', None, U); endtf = fn('glEndTransformFeedback', None)
attribptr = fn('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)
enableattrib = fn('glEnableVertexAttribArray', None, U)
bindattrib = fn('glBindAttribLocation', None, U, U, C.c_char_p)
genvao = fn('glGenVertexArrays', None, I, C.POINTER(U)); bindvao = fn('glBindVertexArray', None, U)
enable = fn('glEnable', None, U); disable = fn('glDisable', None, U)
blendfunc = fn('glBlendFunc', None, U, U)
texstorage3 = fn('glTexStorage3D', None, U, I, U, I, I, I)
attachlayer = fn('glFramebufferTextureLayer', None, U, U, U, I, I)
checkfbo = fn('glCheckFramebufferStatus', U, U)
genq = fn('glGenQueries', None, I, C.POINTER(U)); beginq = fn('glBeginQuery', None, U, U); endq = fn('glEndQuery', None, U)
qresult = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64)); finish = fn('glFinish', None)
geterror = fn('glGetError', U)
u2f = fn('glUniform2f', None, I, F, F); u4f = fn('glUniform4f', None, I, F, F, F, F)
u3fv = fn('glUniform3fv', None, I, I, P)
umat3 = fn('glUniformMatrix3fv', None, I, I, C.c_ubyte, P)
readpix = fn('glReadPixels', None, I, I, I, I, U, U, P)

TEX3D, RGBA16F, R32F, RGBA8 = 0x806F, 0x881A, 0x822E, 0x8058
FB, COLOR0 = 0x8D40, 0x8CE0
TIME_ELAPSED, QUERY_RESULT = 0x88BF, 0x8866
LINEAR, CLAMP = 0x2601, 0x812F

_vao = U(); genvao(1, C.byref(_vao)); bindvao(_vao.value)
_query = U(); genq(1, C.byref(_query))


def build(stages, varyings=None, attribs=None):
    prog = create_program()
    for kind, text in stages:
        sh = create_shader(kind); src = C.c_char_p(text.encode()); source_shader(sh, 1, C.byref(src), None); compile_shader(sh)
        st = I(); shader_iv(sh, 0x8B81, C.byref(st)); log = C.create_string_buffer(16384); shader_log(sh, len(log), None, log)
        assert st.value, log.value.decode()
        attach(prog, sh)
    for i, name in enumerate(attribs or []):
        bindattrib(prog, i, name.encode())
    if varyings:
        arr = (C.c_char_p * len(varyings))(*[v.encode() for v in varyings])
        tfvaryings(prog, len(varyings), arr, 0x8C8C)  # GL_INTERLEAVED_ATTRIBS
    link(prog); st = I(); program_iv(prog, 0x8B82, C.byref(st)); log = C.create_string_buffer(16384); program_log(prog, len(log), None, log)
    assert st.value, log.value.decode()
    return prog


def tex2d(w, h, internal=RGBA32F):
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value); texstorage2(TEX2D, 1, internal, w, h)
    for p in (0x2801, 0x2800): texparam(TEX2D, p, LINEAR)
    for p in (0x2802, 0x2803): texparam(TEX2D, p, CLAMP)
    return t.value


def tex3d(w, h, d, internal=RGBA16F):
    t = U(); gentex(1, C.byref(t)); bindtex(TEX3D, t.value); texstorage3(TEX3D, 1, internal, w, h, d)
    for p in (0x2801, 0x2800): texparam(TEX3D, p, LINEAR)
    for p in (0x2802, 0x2803, 0x8072): texparam(TEX3D, p, CLAMP)
    return t.value


def fbo2d(*texs):
    f = U(); genfbo(1, C.byref(f)); bindfbo(FB, f.value)
    for i, t in enumerate(texs): attach2(FB, COLOR0 + i, TEX2D, t, 0)
    bufs = (U * len(texs))(*[COLOR0 + i for i in range(len(texs))]); drawbuffers(len(texs), bufs)
    assert checkfbo(FB) == 0x8CD5
    return f.value


def fbo_layer(texs, layer):
    f = U(); genfbo(1, C.byref(f)); bindfbo(FB, f.value)
    for i, t in enumerate(texs): attachlayer(FB, COLOR0 + i, t, 0, layer)
    bufs = (U * len(texs))(*[COLOR0 + i for i in range(len(texs))]); drawbuffers(len(texs), bufs)
    assert checkfbo(FB) == 0x8CD5
    return f.value


def read_tex(t, w, h):
    bindtex(TEX2D, t); px = (F * (w * h * 4))(); getteximage(TEX2D, 0, 0x1908, FLOAT, px)
    return px


def upload(t, w, h, data):
    bindtex(TEX2D, t); arr = (F * len(data))(*data); texsub2(TEX2D, 0, 0, 0, w, h, 0x1908, FLOAT, C.cast(arr, P))


def bind_unit(unit, target, t):
    active(0x84C0 + unit); bindtex(target, t); active(0x84C0)


def set_u(prog, name, *v):
    l = _loc_(prog, name)
    if l < 0:
        return
    if len(v) == 1:
        (u1i if isinstance(v[0], int) else u1f)(l, v[0])
    elif len(v) == 2:
        u2f(l, *v)
    elif len(v) == 3:
        u3f(l, *v)
    else:
        u4f(l, *v)


_loc_cache = {}
def _loc_(prog, name):
    k = (prog, name)
    if k not in _loc_cache:
        _loc_cache[k] = fn('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
    return _loc_cache[k]


def timed(work):
    beginq(TIME_ELAPSED, _query.value); work(); endq(TIME_ELAPSED); finish()
    v = C.c_uint64(); qresult(_query.value, QUERY_RESULT, C.byref(v))
    err = geterror(); assert err == 0, hex(err)
    return v.value / 1e6


def median_ms(work, frames=60, warmup=10):
    return statistics.median([timed(work) for _ in range(warmup + frames)][warmup:])


# --- sRGB, sky files ----------------------------------------------------------------------------
def s2l(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def load_sky(path, **over):
    """A Windlight sky file as the viewer's uniforms see it (LLSettingsSky::translateLegacySettings),
    with the light chosen as LLSettingsSky::getLightDirection does: the sun if it is up, else the moon,
    which a Windlight sky puts diametrically opposite the sun."""
    import xml.etree.ElementTree as ET
    d = _llsd_value(ET.parse(path).getroot()[0])
    g = lambda k, n=3: list(d[k][:n]) if n > 1 else d[k][0]
    p = dict(name=unquote(Path(path).name), sunlight_color=g('sunlight_color'), ambient=g('ambient'), blue_density=g('blue_density'),
             blue_horizon=g('blue_horizon'), haze_density=g('haze_density', 1), haze_horizon=g('haze_horizon', 1),
             cloud_shadow=g('cloud_shadow', 1), density_multiplier=g('density_multiplier', 1),
             distance_multiplier=g('distance_multiplier', 1), max_y=g('max_y', 1), glow=g('glow'), gamma=g('gamma', 1),
             sun_angle=d.get('sun_angle', 0.0), east_angle=d.get('east_angle', 0.0), moon_brightness=0.5)
    p.update(over)
    set_elevation(p, p.pop('elevation_deg', None))
    return p


def set_elevation(p, elevation_deg=None):
    """Sun elevation from the file's sun_angle (sin of it is the light's vertical component), or forced."""
    e = math.asin(math.sin(p['sun_angle'])) if elevation_deg is None else math.radians(elevation_deg)
    p['sun_el'] = e
    p['sun_up'] = 1 if e >= 0.0 else 0
    # light: sun when up, else the moon (opposite the sun in a Windlight sky; LLSettingsSky::getLightDirection)
    p['light_el'] = e if p['sun_up'] else -e
    p['light_az'] = 0.0   # the panorama is centred on the light (sun by day, moon by night)
    return p


# --- today: skyV.glsl by transform feedback -----------------------------------------------------
SKYV_TF = '#version 400 compatibility\n' + shader_text('class1/deferred/skyV.glsl')
DUMMY_FS = '#version 400 compatibility\nout vec4 c;\nvoid main() { c = vec4(0.0); }\n'


class TodaySky:
    def __init__(self):
        self.prog = build([(VERT, SKYV_TF), (FRAG, DUMMY_FS)], varyings=['vary_HazeColor'], attribs=['position'])
        self.vbo = U(); genbuf(1, C.byref(self.vbo)); self.tfb = U(); genbuf(1, C.byref(self.tfb))

    def run(self, p, dirs):
        """dirs: list of (x, y, z), y up, in the frame where the light's azimuth is p['light_az'] about y
        from +x. Returns the sky radiance (linear, PBR path) per direction."""
        n = len(dirs)
        R = 15000.0  # EEP dome radius
        arr = (F * (3 * n))(*[c * R for dd in dirs for c in dd])
        bindbuf(0x8892, self.vbo.value); bufdata(0x8892, C.sizeof(arr), C.cast(arr, P), 0x88E4)
        attribptr(0, 3, FLOAT, 0, 0, None); enableattrib(0)
        bindbuf(0x8C8E, self.tfb.value); bufdata(0x8C8E, 12 * n, None, 0x88E5)
        bindbufbase(0x8C8E, 0, self.tfb.value)
        use(self.prog); set_today_uniforms(self.prog, p)
        enable(0x8C89)  # GL_RASTERIZER_DISCARD
        begintf(0x0000); draw(0x0000, 0, n); endtf()
        disable(0x8C89)
        out = (F * (3 * n))(); getbufsub(0x8C8E, 0, 12 * n, C.cast(out, P))
        hdr = 2.0 * math.sqrt(max(p['gamma'], 0.0))  # PBR sky: sky_hdr_scale = 2 sqrt(gamma) (llsettingsvo.cpp)
        res = []
        for i in range(n):
            c = [min(max(out[3 * i + k] * 2.0, 0.0), 5.0) for k in range(3)]  # skyF.glsl x2, clamp 5
            res.append([s2l(x) * hdr for x in c])
        return res


def light_vec(p):
    el, az = p['light_el'], p['light_az']
    return (math.cos(el) * math.cos(az), max(math.sin(el), -0.1), math.cos(el) * math.sin(az))


def set_today_uniforms(prog, p):
    lx, ly, lz = light_vec(p)
    q = dict(p); q['light_y'] = ly
    q.update(sky_sunlight_scale=1.0, sky_ambient_scale=1.0, classic_mode=0)
    set_atmosphere_uniforms(prog, q)
    nl = math.sqrt(lx * lx + ly * ly + lz * lz)
    uniform(prog, 'lightnorm', lx / nl, ly / nl, lz / nl)
    uniform(prog, 'sun_up_factor', int(p['sun_up']))
    uniform(prog, 'moonlight_color', *p['sunlight_color'])   # LLSettingsSky::getMoonlightColor = sunlight
    glow_factor = 1.0 if p['sun_up'] else p['moon_brightness'] * 0.25  # getSunMoonGlowFactor (moon up)
    uniform(prog, 'sun_moon_glow_factor', float(glow_factor))
    uniform(prog, 'camPosLocal', 0.0, 0.0, 0.0)
    uniform(prog, 'cube_snapshot', 0)


# --- the proposed atmosphere (GLSL) -------------------------------------------------------------
ATMOS = r"""
#define PI 3.14159265358979
uniform float Rg; uniform float Rt;              // km (EEP planet_radius, sky_top_radius)
uniform vec3 sr; uniform vec3 er;                // Rayleigh scattering / extinction at density 1, 1/km
uniform float sm; uniform float em;              // Mie (aerosol), 1/km
uniform float sh; uniform float eh; uniform float hh;  // ground haze slab, 1/km, top (km)
uniform vec3 eo;                                 // ozone absorption at density 1, 1/km
uniform vec4 pr; uniform vec4 pm;                // EEP layer: exp_term, exp_scale (1/km), linear (1/km), constant
uniform vec4 po0; uniform vec4 po1; uniform float ow;  // EEP absorption: two layers, the first `ow` km wide
uniform float gm;                                // Mie anisotropy (Cornette-Shanks), used when glow_phase.z == 0
uniform vec3 glow_phase;                         // the creator's glow as the haze phase: glow.x, glow.z, 1 / its integral
uniform vec3 ms_scale;
uniform float ground_albedo;
uniform sampler2D trans_lut;
uniform sampler2D ms_lut;
uniform vec3 L_dir[2];                           // sun, moon (unit, y up, frame of the caller)
uniform vec3 L_E[2];                             // irradiance above the atmosphere (linear)

float prof(vec4 p, float h) { return clamp(p.x * exp(p.y * h) + p.z * h + p.w, 0.0, 1.0); }

void medium(float h, out vec3 s_r, out vec3 s_m, out vec3 ext)
{
    h = max(h, 0.0);
    float dr = prof(pr, h);
    float dm = prof(pm, h);
    float dh = 1.0 - smoothstep(0.75 * hh, hh, h);
    float doz = h < ow ? prof(po0, h) : prof(po1, h);
    s_r = sr * dr;
    s_m = vec3(sm * dm + sh * dh);
    ext = er * dr + vec3(em * dm + eh * dh) + eo * doz;
}

// distance to the sphere of radius R along d from o (|o| <= R: the far hit), -1 if none
float ray_sphere(vec3 o, vec3 d, float R)
{
    float b = dot(o, d); float c = dot(o, o) - R * R; float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float s = sqrt(disc); float t0 = -b - s; float t1 = -b + s;
    if (t0 > 0.0) return t0;
    if (t1 > 0.0) return t1;
    return -1.0;
}

// Bruneton & Neyret 2008 / Hillaire 2020 transmittance parameterisation
vec2 trans_uv(float r, float mu)
{
    float H = sqrt(max(0.0, Rt * Rt - Rg * Rg)); float rho = sqrt(max(0.0, r * r - Rg * Rg));
    float disc = r * r * (mu * mu - 1.0) + Rt * Rt; float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dmin = Rt - r; float dmax = rho + H;
    return vec2((d - dmin) / max(dmax - dmin, 1e-6), rho / H);
}

vec3 T_lut(float r, float mu)
{
    vec2 uv = trans_uv(r, mu);
    uv = vec2(0.5 / 256.0, 0.5 / 64.0) + uv * vec2(255.0 / 256.0, 63.0 / 64.0);
    return texture(trans_lut, uv).rgb;
}

// transmittance from x to the light, 0 behind the planet (soft over the sun's own disc)
vec3 light_T(vec3 x, vec3 L)
{
    float r = length(x); float mu = dot(x, L) / r;
    float mu_h = -sqrt(max(0.0, 1.0 - (Rg / r) * (Rg / r)));
    float vis = smoothstep(mu_h - 0.0047, mu_h + 0.0047, mu);
    return T_lut(r, mu) * vis;
}

vec3 ms_at(float r, float mu_s)
{
    vec2 uv = vec2(mu_s * 0.5 + 0.5, clamp((r - Rg) / (Rt - Rg), 0.0, 1.0));
    uv = vec2(0.5 / 32.0) + uv * (31.0 / 32.0);
    return texture(ms_lut, uv).rgb;
}

float phase_r(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
float phase_m(float c)
{
    if (glow_phase.z > 0.0)   // EEP's glow lobe (skyV.glsl haze_glow, + 0.25), normalised over the sphere
        return min((pow(max(1.0 - c, 0.001) * glow_phase.x, glow_phase.y) + 0.25) * glow_phase.z, 1e4);
    // Cornette-Shanks
    float g = gm; float g2 = g * g;
    return 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + c * c)) / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

// single + multiple scattering along o + t d, t in [0, tmax]; two lights
void integrate(vec3 o, vec3 d, float tmax, int steps, out vec3 Lin, out vec3 T)
{
    Lin = vec3(0.0); T = vec3(1.0);
    float dt = tmax / float(steps);
    float c0 = dot(d, L_dir[0]); float c1 = dot(d, L_dir[1]);
    float pr0 = phase_r(c0); float pm0 = phase_m(c0); float pr1 = phase_r(c1); float pm1 = phase_m(c1);
    for (int i = 0; i < steps; ++i)
    {
        float t = (float(i) + 0.5) * dt;
        vec3 x = o + d * t; float r = length(x); vec3 up = x / r;
        vec3 s_r, s_m, ext; medium(r - Rg, s_r, s_m, ext);
        vec3 S = L_E[0] * (light_T(x, L_dir[0]) * (s_r * pr0 + s_m * pm0) + ms_at(r, dot(up, L_dir[0])) * (s_r + s_m) * ms_scale);
        S += L_E[1] * (light_T(x, L_dir[1]) * (s_r * pr1 + s_m * pm1) + ms_at(r, dot(up, L_dir[1])) * (s_r + s_m) * ms_scale);
        vec3 Ts = exp(-ext * dt);
        Lin += T * (S - S * Ts) / max(ext, vec3(1e-7));
        T *= Ts;
    }
}

float ray_length(vec3 o, vec3 d)
{
    float tg = ray_sphere(o, d, Rg);
    return tg > 0.0 ? tg : max(ray_sphere(o, d, Rt), 0.0);
}

// sky view LUT: u = azimuth from the light frame's +x, v = elevation, square-root spaced about the horizon
vec3 sky_dir(vec2 uv)
{
    float a = (uv.x * 2.0 - 1.0) * PI;
    float s = uv.y * 2.0 - 1.0; float e = sign(s) * s * s * 0.5 * PI;
    return vec3(cos(e) * cos(a), sin(e), cos(e) * sin(a));
}
vec2 sky_uv(vec3 d)
{
    float e = asin(clamp(d.y, -1.0, 1.0)); float a = atan(d.z, d.x);
    float s = sign(e) * sqrt(abs(e) / (0.5 * PI));
    vec2 uv = vec2(a / PI * 0.5 + 0.5, s * 0.5 + 0.5);
    return vec2(0.5 / 192.0, 0.5 / 108.0) + uv * vec2(191.0 / 192.0, 107.0 / 108.0);
}
"""

HEAD = '#version 400 compatibility\n'
FS_VERT = HEAD + """
void main() { vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
"""

TRANS_FS = HEAD + ATMOS + r"""
out vec4 o;
void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5) / vec2(255.0, 63.0);
    float H = sqrt(Rt * Rt - Rg * Rg); float rho = H * uv.y; float r = sqrt(rho * rho + Rg * Rg);
    float dmin = Rt - r; float dmax = rho + H; float d = dmin + uv.x * (dmax - dmin);
    float mu = d == 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
    vec3 x0 = vec3(0.0, r, 0.0); vec3 dir = vec3(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);
    float len = max(ray_sphere(x0, dir, Rt), 0.0);
    const int N = 40; vec3 tau = vec3(0.0); float dt = len / float(N);
    for (int i = 0; i < N; ++i)
    {
        vec3 x = x0 + dir * (float(i) + 0.5) * dt; vec3 a, b, ext; medium(length(x) - Rg, a, b, ext);
        tau += ext * dt;
    }
    o = vec4(exp(-tau), 1.0);
}
"""

MS_FS = HEAD + ATMOS + r"""
out vec4 o;
void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5) / 31.0;
    float mu_s = uv.x * 2.0 - 1.0; float r = Rg + max(uv.y, 1e-3) * (Rt - Rg);
    vec3 x0 = vec3(0.0, r, 0.0); vec3 Ls = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), mu_s, 0.0);
    vec3 L2 = vec3(0.0); vec3 fms = vec3(0.0);
    const float pu = 1.0 / (4.0 * PI);
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j)
    {
        float ct = 1.0 - 2.0 * (float(j) + 0.5) / 8.0; float st = sqrt(1.0 - ct * ct); float ph = 2.0 * PI * (float(i) + 0.5) / 8.0;
        vec3 d = vec3(st * cos(ph), ct, st * sin(ph));
        float tg = ray_sphere(x0, d, Rg); float tmax = tg > 0.0 ? tg : max(ray_sphere(x0, d, Rt), 0.0);
        const int N = 20; float dt = tmax / float(N);
        vec3 T = vec3(1.0); vec3 L = vec3(0.0); vec3 Fi = vec3(0.0);
        for (int k = 0; k < N; ++k)
        {
            vec3 x = x0 + d * (float(k) + 0.5) * dt; float rx = length(x);
            vec3 s_r, s_m, ext; medium(rx - Rg, s_r, s_m, ext); vec3 s = s_r + s_m;
            vec3 Ts = exp(-ext * dt); vec3 w = (vec3(1.0) - Ts) / max(ext, vec3(1e-7));
            L += T * s * light_T(x, Ls) * pu * w;
            Fi += T * s * w;
            T *= Ts;
        }
        if (tg > 0.0)
        {
            vec3 p = x0 + d * tg; vec3 n = normalize(p);
            L += T * light_T(p, Ls) * max(dot(n, Ls), 0.0) * ground_albedo / PI;
        }
        L2 += L; fms += Fi * pu;
    }
    L2 /= 64.0; fms = fms * (4.0 * PI) / 64.0;
    o = vec4(L2 / max(vec3(1.0) - fms, vec3(1e-3)), 1.0);
}
"""

SKYVIEW_FS = HEAD + ATMOS + r"""
uniform float h_obs;   // km
out vec4 o;
void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5) / vec2(191.0, 107.0);
    vec3 d = sky_dir(uv); vec3 x0 = vec3(0.0, Rg + h_obs, 0.0);
    vec3 Lin, T; integrate(x0, d, ray_length(x0, d), 32, Lin, T);
    o = vec4(Lin, 1.0);
}
"""

# aerial perspective froxels: one draw per depth slice (no compute stage in the viewer's shader
# manager); a camera frustum of tan_half (vertical), aspect; slices ((k + 0.5) / 32)^2 * ap_range
AP_FS = HEAD + ATMOS + r"""
uniform float h_obs; uniform int slice; uniform float ap_range; uniform float tan_half; uniform float aspect;
uniform mat3 view_to_atmo;
layout(location = 0) out vec4 o_in;
layout(location = 1) out vec4 o_tr;
void main()
{
    vec2 uv = (gl_FragCoord.xy) / 32.0 * 2.0 - 1.0;
    vec3 dv = normalize(vec3(uv.x * tan_half * aspect, uv.y * tan_half, -1.0));
    vec3 d = view_to_atmo * dv;
    float z = (float(slice) + 0.5) / 32.0; float t = z * z * ap_range;
    vec3 x0 = vec3(0.0, Rg + h_obs, 0.0);
    vec3 Lin, T; integrate(x0, d, min(t, ray_length(x0, d)), 4 + slice / 2, Lin, T);
    o_in = vec4(Lin, 1.0); o_tr = vec4(T, 1.0);
}
"""

# per-pixel passes (timing): the depth buffer is a stand-in with distances across the range
APPLY_AP_FS = HEAD + r"""
uniform sampler2D depth_tex; uniform sampler3D ap_in; uniform sampler3D ap_tr;
uniform vec2 res; uniform float ap_range; uniform float tan_half; uniform float aspect;
layout(location = 0, index = 0) out vec4 o_add;
layout(location = 0, index = 1) out vec4 o_mul;
void main()
{
    vec2 tc = gl_FragCoord.xy / res;
    float dist = texture(depth_tex, tc).r;           // view distance (m) of the surface
    vec3 uvw = vec3(tc, sqrt(clamp(dist / ap_range, 0.0, 1.0)));
    o_add = vec4(texture(ap_in, uvw).rgb, 1.0);
    o_mul = vec4(texture(ap_tr, uvw).rgb, 1.0);
}
"""

APPLY_SKY_FS = HEAD + ATMOS + r"""
uniform sampler2D sky_lut; uniform vec2 res; uniform float tan_half; uniform float aspect; uniform mat3 view_to_atmo;
uniform float sky_scale; uniform float h_obs;
out vec4 o;
void main()
{
    vec2 ndc = gl_FragCoord.xy / res * 2.0 - 1.0;
    vec3 d = view_to_atmo * normalize(vec3(ndc.x * tan_half * aspect, ndc.y * tan_half, -1.0));
    vec3 L = texture(sky_lut, sky_uv(d)).rgb * sky_scale;
    float c = dot(d, L_dir[0]);                       // the sun's disc, through the atmosphere
    float disc = smoothstep(0.99996, 0.99998, c);
    L += disc * L_E[0] * T_lut(Rg + h_obs, d.y) * 5000.0;
    o = vec4(L, 1.0);
}
"""

# today's haze pass for timing: hazeF.glsl main(), with getPositionWithDepth replaced by the stand-in
TODAY_HAZE_FS = HEAD + shader_text('class1/environment/srgbF.glsl') + shader_text('class1/windlight/atmosphericsFuncs.glsl') + r"""
uniform sampler2D depth_tex; uniform vec2 res; uniform float tan_half; uniform float aspect;
uniform vec3 sun_dir; uniform float sky_hdr_scale;
out vec4 frag_color;
void main()
{
    vec2 tc = gl_FragCoord.xy / res;
    float dist = texture(depth_tex, tc).r;
    vec2 ndc = tc * 2.0 - 1.0;
    vec3 pos = normalize(vec3(ndc.x * tan_half * aspect, ndc.y * tan_half, -1.0)) * dist;
    vec3 norm = vec3(0.0, 1.0, 0.0);
    vec3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(pos.xyz, norm.xyz, sun_dir, sunlit, amblit, additive, atten);
    float alpha = atten.r;
    vec3 color = srgb_to_linear(additive * 2.0);
    color *= sky_hdr_scale;
    frag_color = max(vec4(color.rgb, alpha), vec4(0));
}
"""

# the panorama: x = azimuth from the light's (-180..180), y = elevation (EL0..EL1 deg). Sky above the
# horizon and ground beyond FAR_M; a grey ground plane below, lit by the model's sun and sky.
EL0, EL1 = -12.0, 90.0
CAM_H = 20.0      # m above the ground plane
FAR_M = 1024.0    # m, the ground plane's end (the viewer shows the dome beyond its far clip)
ALBEDO = 0.18

PANO_TODAY_FS = HEAD + shader_text('class1/environment/srgbF.glsl') + shader_text('class1/windlight/atmosphericsFuncs.glsl') + r"""
uniform sampler2D sky_tex; uniform vec2 res; uniform vec3 light_world; uniform vec3 E_sky; uniform float sky_hdr_scale;
uniform float el0; uniform float el1; uniform float cam_h; uniform float far_m; uniform float albedo; uniform float yaw_offset;
out vec4 o;
void main()
{
    vec2 uv = gl_FragCoord.xy / res;
    float az = (uv.x * 2.0 - 1.0) * 3.14159265; float el = radians(mix(el0, el1, uv.y));
    vec3 dir = vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
    float d = el < 0.0 ? cam_h / sin(-el) : 1e9;
    if (d > far_m) { o = vec4(texture(sky_tex, uv).rgb, 1.0); return; }
    // the camera looks along this pixel's azimuth (+ yaw_offset), level: eye space = world turned about y
    float yaw = az + yaw_offset;
    vec3 fwd = vec3(cos(yaw), 0.0, sin(yaw)); vec3 right = vec3(-sin(yaw), 0.0, cos(yaw));
    vec3 pw = dir * d;
    vec3 pos = vec3(dot(pw, right), pw.y, -dot(pw, fwd));
    vec3 ld = vec3(dot(light_world, right), light_world.y, -dot(light_world, fwd));
    vec3 n_eye = vec3(0.0, 1.0, 0.0);
    vec3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(pos, n_eye, ld, sunlit, amblit, additive, atten);
    // ground: the PBR sun term of pbrBaseLight (nl * diffuse/pi * sunlit * 3), plus the sky's irradiance
    vec3 ground = albedo / 3.14159265 * 3.0 * max(light_world.y, 0.0) * sunlit + albedo * E_sky;
    // hazeF.glsl: blended dst * atten.r + srgb_to_linear(additive * 2) * sky_hdr_scale
    o = vec4(ground * atten.r + srgb_to_linear(additive * 2.0) * sky_hdr_scale, 1.0);
}
"""

PANO_PROP_FS = HEAD + ATMOS + r"""
uniform sampler2D sky_lut; uniform vec2 res; uniform vec3 E_sky; uniform vec3 sky_scale; uniform float h_obs;
uniform float el0; uniform float el1; uniform float cam_h; uniform float far_m; uniform float albedo;
out vec4 o;
void main()
{
    vec2 uv = gl_FragCoord.xy / res;
    float az = (uv.x * 2.0 - 1.0) * PI; float el = radians(mix(el0, el1, uv.y));
    vec3 dir = vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
    float d = el < 0.0 ? cam_h / sin(-el) : 1e9;
    if (d > far_m) { o = vec4(texture(sky_lut, sky_uv(dir)).rgb * sky_scale, 1.0); return; }
    vec3 x0 = vec3(0.0, Rg + h_obs, 0.0);
    vec3 xg = x0 + dir * d * 1e-3;
    // L_E is already 3 x lin(sunlight): the viewer's sun irradiance (pbrBaseLight's sunlit * 3)
    vec3 sun = L_E[0] * light_T(xg, L_dir[0]) * max(L_dir[0].y, 0.0) + L_E[1] * light_T(xg, L_dir[1]) * max(L_dir[1].y, 0.0);
    vec3 ground = albedo / PI * sun + albedo * E_sky;
    vec3 Lin, T; integrate(x0, dir, d * 1e-3, 48, Lin, T);   // what the AP volume stores, per pixel
    o = vec4(ground * T + Lin * sky_scale, 1.0);
}
"""

DISPLAY_FS = HEAD + shader_text('class1/environment/srgbF.glsl') + shader_text('class1/deferred/tonemapUtilF.glsl').replace(
    'in vec2 vary_fragcoord;', 'vec2 vary_fragcoord = vec2(0.0);') + r"""
uniform sampler2D src; uniform vec2 res; uniform float expo;
out vec4 o;
void main() { vec3 c = texture(src, gl_FragCoord.xy / res).rgb * expo; o = vec4(linear_to_srgb(toneMapACES_Hill(c)), 1.0); }
"""


# --- EEP -> physical: the mapping (study section 4.2) ------------------------------------------
# EEP's stored profiles, as LLSettingsSky's defaults write them (every known sky carries these):
EEP_RAYLEIGH = dict(width=0.0, exp_term=1.0, exp_scale=-1.0 / 8000.0, linear=0.0, constant=0.0)
EEP_MIE = dict(width=0.0, exp_term=1.0, exp_scale=-1.0 / 1200.0, linear=0.0, constant=0.0, anisotropy=0.8)
EEP_ABSORPTION = [dict(width=25000.0, exp_term=0.0, exp_scale=0.0, linear=-1.0 / 25000.0, constant=-2.0 / 3.0),
                  dict(width=0.0, exp_term=0.0, exp_scale=0.0, linear=-1.0 / 15000.0, constant=8.0 / 3.0)]
PLANET_KM, TOP_KM = 6360.0, 6420.0
OZONE = (0.000650, 0.001881, 0.000085)   # 1/km at density 1 (Bruneton & Neyret's Earth values)
H_SLAB_KM = 0.25                         # the ground haze slab's top (km): a tuning value, study 4.2
# Default.xml is the physical reference: its blue_horizon, haze_horizon and ambient map to a Rayleigh
# albedo of 1, an aerosol albedo of 0.9 and Hillaire's multiple scattering as it is; other skies act
# relative to it. sky_gain is a display-only gain on the drawn sky and its aerial perspective (not on
# what lights the scene), 1 = physical; calibrate() fits the gain that matches today's Default sky.
DEFAULT_BH = (0.4954838156700134, 0.4954838156700134, 0.6399999856948853)
DEFAULT_HH = 0.1899999976158142
DEFAULT_AMB = 1.0499999523162842
CAL = dict(sky_gain=1.0, tint=(1.0, 1.0, 1.0))


def profile_vec(layer):
    """EEP layer (metres) -> (exp_term, exp_scale /km, linear /km, constant)."""
    return (layer['exp_term'], layer['exp_scale'] * 1000.0, layer['linear'] * 1000.0, layer['constant'])


def scale_height_km(layer):
    return -1.0 / (layer['exp_scale'] * 1000.0)


def column_km(layers, top_km=TOP_KM - PLANET_KM, n=2000):
    """Vertical integral of an EEP density profile, km."""
    acc, dh = 0.0, top_km / n
    for i in range(n):
        h = (i + 0.5) * dh
        L = layers[0] if (len(layers) == 1 or h < layers[0]['width'] / 1000.0) else layers[1]
        e, s, l, c = profile_vec(L)
        acc += min(max(e * math.exp(s * h) + l * h + c, 0.0), 1.0) * dh
    return acc


OZONE_COLUMN_KM = column_km(EEP_ABSORPTION)


def legacy_glow_g(glow):
    """Mie anisotropy whose Cornette-Shanks lobe best matches the legacy glow (glow.x (1 - cos)^glow.z
    + 0.25, the sky's haze_glow) on log scale over the sphere. Cached by (glow.x, glow.z)."""
    key = (round(glow[0], 4), round(glow[2], 4))
    if key in _g_cache:
        return _g_cache[key]
    cs = [math.cos(math.pi * (i + 0.5) / 90) for i in range(90)]   # uniform in angle: the lobe near the sun counts
    leg = [math.log(max((max(1.0 - c, 0.001) * glow[0]) ** glow[2], 1e-6) + 0.25) for c in cs] if glow[0] > 0 else [0.0] * len(cs)

    def err(g):
        g2 = g * g
        ph = [math.log(3 / (8 * math.pi) * ((1 - g2) * (1 + c * c)) / ((2 + g2) * (1 + g2 - 2 * g * c) ** 1.5)) for c in cs]
        m = sum(leg[i] - ph[i] for i in range(len(cs))) / len(cs)
        return sum((leg[i] - ph[i] - m) ** 2 for i in range(len(cs)))
    lo, hi = 0.0, 0.95
    for _ in range(40):
        a, b = lo + (hi - lo) * 0.382, lo + (hi - lo) * 0.618
        if err(a) < err(b): hi = b
        else: lo = a
    g = 0.5 * (lo + hi)
    _g_cache[key] = g
    return g


_g_cache = {}


def glow_phase(glow):
    """(glow.x, glow.z, 1 / integral over the sphere) of the legacy glow lobe (glow.x (1 - cos)^glow.z + 0.25,
    the 1 - cos clamped at 0.001 as skyV.glsl); (0, 0, 0) when it is not a usable lobe."""
    gx, gz = glow[0], glow[2]
    if gx <= 0 or gz > 0.5 or gz < -3.0:
        return (0.0, 0.0, 0.0)
    acc = 0.0
    # integrate over x = 1 - cos in [0, 2], log-spaced near 0 where the lobe peaks
    xs = [2.0 * (10 ** (-7 + 7 * i / 4000)) for i in range(4001)]
    for i in range(4000):
        x0, x1 = xs[i], xs[i + 1]; xm = 0.5 * (x0 + x1)
        acc += ((max(xm, 0.001) * gx) ** gz + 0.25) * (x1 - x0)
    acc *= 2 * math.pi
    return (gx, gz, 1.0 / acc)


def map_sky(p, cal=None):
    """EEP sky (legacy haze fields + stored profiles) -> physical coefficients. Returns a dict, with
    'valid' False when the sky is outside the physical range (it then stays on today's model)."""
    cal = cal or CAL
    sc = p['sunlight_color']; bd = p['blue_density']; hd = max(p['haze_density'], 0.0)
    dm = max(p['density_multiplier'], 0.0); my = max(p['max_y'], 0.0); dmul = p['distance_multiplier']
    H_r, H_m = scale_height_km(EEP_RAYLEIGH), scale_height_km(EEP_MIE)
    # 1. the column: today's sun at the zenith in the PBR path is srgb_to_linear(sc exp(-(bd + hd/4) dm max_y));
    #    the physical sun is lin(sc) exp(-tau_lin). Match it per channel.
    tau_lin = []
    for k in range(3):
        tau = (bd[k] + 0.25 * hd) * dm * my
        a, b = s2l(sc[k]), s2l(sc[k] * math.exp(-tau))
        tau_lin.append(-math.log(b / a) if a > 1e-6 and b > 1e-9 else 2.4 * tau)
    tot = [(bd[k] + 0.25 * hd) for k in range(3)]
    tau_r = [tau_lin[k] * (bd[k] / tot[k] if tot[k] > 0 else 0.0) for k in range(3)]
    tau_m = sum(tau_lin[k] - tau_r[k] for k in range(3)) / 3.0
    # ozone (Earth's, on EEP's absorption profile) comes out of the Rayleigh column so noon stays matched
    tau_o = [OZONE[k] * OZONE_COLUMN_KM for k in range(3)]
    tau_r = [max(tau_r[k] - tau_o[k], 0.0) for k in range(3)]
    # 2. the ground: today's distance haze is exp(-(bd + hd) dm distance_multiplier d) (red channel used);
    #    what the column gives at the ground is tau_r/H_r + tau_m/H_m; the rest is a ground haze slab
    k_ap = (bd[0] + hd) * dm * max(dmul, 0.0) * 1000.0   # 1/km
    tau_m_eff = tau_m
    for _ in range(4):
        e_h = max(k_ap - (tau_r[0] / H_r + tau_m_eff / H_m), 0.0)
        tau_m_eff = max(tau_m - e_h * H_SLAB_KM * 0.875, 0.0)  # slab column (smooth top) out of the Mie column
    e_r = [tau_r[k] / H_r for k in range(3)]
    e_m = tau_m_eff / H_m
    # 3. colours of the scattered light: blue_horizon tints the Rayleigh albedo, haze_horizon the Mie albedo
    #    (the legacy x 2 in skyF makes Default's blue_horizon about 1); ambient scales multiple scattering
    # albedos stay <= 1 (energy conservation; above 1 the multiple-scattering series diverges)
    w_r = [min(max(p['blue_horizon'][k], 0.0) / DEFAULT_BH[k], 1.0) for k in range(3)]
    w_m = min(max(p['haze_horizon'], 0.0) / DEFAULT_HH * 0.9, 1.0)
    ms = [min(max(p['ambient'][k], 0.0) / DEFAULT_AMB, 4.0) for k in range(3)]
    g = legacy_glow_g(p['glow'])
    gp = glow_phase(p['glow'])
    valid = (dm > 0 and my > 0 and dmul >= 0 and p['density_multiplier'] >= 0 and min(bd) >= 0
             and max(tau_lin) < 8.0 and k_ap < 50.0)
    return dict(valid=valid, e_r=e_r, s_r=[w_r[k] * e_r[k] for k in range(3)], e_m=e_m, s_m=w_m * e_m,
                e_h=e_h, s_h=w_m * e_h, g=g, ms=ms, glow_phase=gp, tau_lin=tau_lin, k_ap=k_ap,
                E_sun=[s2l(c) for c in sc], E_moon=[s2l(c) for c in sc],   # moonlight_color = sunlight_color (getMoonlightColor)
                sky_scale=[math.sqrt(max(p['gamma'], 0.0)) * cal['sky_gain'] * cal.get('tint', (1, 1, 1))[k] for k in range(3)])


class Proposed:
    def __init__(self):
        self.p_trans = build([(VERT, FS_VERT), (FRAG, TRANS_FS)])
        self.p_ms = build([(VERT, FS_VERT), (FRAG, MS_FS)])
        self.p_sky = build([(VERT, FS_VERT), (FRAG, SKYVIEW_FS)])
        self.p_ap = build([(VERT, FS_VERT), (FRAG, AP_FS)])
        self.p_pano = build([(VERT, FS_VERT), (FRAG, PANO_PROP_FS)])
        self.trans = tex2d(256, 64, RGBA16F); self.f_trans = fbo2d(self.trans)
        self.ms = tex2d(32, 32, RGBA16F); self.f_ms = fbo2d(self.ms)
        self.sky = tex2d(192, 108, RGBA16F); self.f_sky = fbo2d(self.sky)
        self.ap_in = tex3d(32, 32, 32); self.ap_tr = tex3d(32, 32, 32)
        self.f_ap = [fbo_layer([self.ap_in, self.ap_tr], k) for k in range(32)]

    def set_atmos(self, prog, m, p):
        use(prog)
        set_u(prog, 'Rg', PLANET_KM); set_u(prog, 'Rt', TOP_KM)
        set_u(prog, 'sr', *m['s_r']); set_u(prog, 'er', *m['e_r'])
        set_u(prog, 'sm', float(m['s_m'])); set_u(prog, 'em', float(m['e_m']))
        set_u(prog, 'sh', float(m['s_h'])); set_u(prog, 'eh', float(m['e_h'])); set_u(prog, 'hh', float(H_SLAB_KM))
        set_u(prog, 'eo', *OZONE)
        set_u(prog, 'pr', *profile_vec(EEP_RAYLEIGH)); set_u(prog, 'pm', *profile_vec(EEP_MIE))
        set_u(prog, 'po0', *profile_vec(EEP_ABSORPTION[0])); set_u(prog, 'po1', *profile_vec(EEP_ABSORPTION[1]))
        set_u(prog, 'ow', EEP_ABSORPTION[0]['width'] / 1000.0)
        set_u(prog, 'gm', float(m['g'])); set_u(prog, 'ms_scale', *m['ms']); set_u(prog, 'ground_albedo', 0.2)
        set_u(prog, 'glow_phase', *m['glow_phase'])
        e = p['sun_el']; az = 0.0 if p['sun_up'] else math.pi   # frame: the light's azimuth at +x
        sun = (math.cos(e) * math.cos(az), math.sin(e), math.cos(e) * math.sin(az))
        moon = (-sun[0], -sun[1], -sun[2])   # Windlight: diametrically opposite
        mb = p['moon_brightness']
        dirs = (F * 6)(*sun, *moon)
        # the irradiance the atmosphere scatters: the viewer's sun on a surface is sunlit * 3 (pbrBaseLight)
        Es = (F * 6)(*[3.0 * c for c in m['E_sun']], *[3.0 * c * (1.0 if mb > 0 else 0.0) for c in m['E_moon']])
        u3fv(_loc_(prog, 'L_dir'), 2, C.cast(dirs, P)); u3fv(_loc_(prog, 'L_E'), 2, C.cast(Es, P))
        set_u(prog, 'trans_lut', 0); set_u(prog, 'ms_lut', 1)
        bind_unit(0, TEX2D, self.trans); bind_unit(1, TEX2D, self.ms)
        return sun, moon

    def luts(self, m, p, h_obs=CAM_H * 1e-3):
        self.set_atmos(self.p_trans, m, p); bindfbo(FB, self.f_trans); viewport(0, 0, 256, 64); draw(0x0004, 0, 3)
        self.set_atmos(self.p_ms, m, p); bindfbo(FB, self.f_ms); viewport(0, 0, 32, 32); draw(0x0004, 0, 3)
        self.set_atmos(self.p_sky, m, p); set_u(self.p_sky, 'h_obs', float(h_obs))
        bindfbo(FB, self.f_sky); viewport(0, 0, 192, 108); draw(0x0004, 0, 3)

    def ap_volume(self, m, p, h_obs=CAM_H * 1e-3, ap_range=1.024, tan_half=math.tan(math.radians(30)), aspect=16 / 9):
        self.set_atmos(self.p_ap, m, p)
        set_u(self.p_ap, 'h_obs', float(h_obs)); set_u(self.p_ap, 'ap_range', float(ap_range))
        set_u(self.p_ap, 'tan_half', float(tan_half)); set_u(self.p_ap, 'aspect', float(aspect))
        ident = (F * 9)(0, 0, 1, 0, 1, 0, -1, 0, 0)   # view -z -> +x (towards the light's azimuth)
        umat3(_loc_(self.p_ap, 'view_to_atmo'), 1, 0, C.cast(ident, P))
        viewport(0, 0, 32, 32)
        for k in range(32):
            bindfbo(FB, self.f_ap[k]); set_u(self.p_ap, 'slice', k); draw(0x0004, 0, 3)


# --- panoramas ------------------------------------------------------------------------------------
def pano_dirs(w, h):
    dirs = []
    for j in range(h):
        el = math.radians(EL0 + (EL1 - EL0) * (j + 0.5) / h)
        for i in range(w):
            az = ((i + 0.5) / w * 2.0 - 1.0) * math.pi
            dirs.append((math.cos(el) * math.cos(az), math.sin(el), math.cos(el) * math.sin(az)))
    return dirs


def sky_irradiance_from(px, w, h):
    """Cosine-weighted irradiance on an up-facing surface from a panorama's sky rows (el > 0)."""
    acc = [0.0, 0.0, 0.0]; wsum = 0.0
    for j in range(h):
        el = math.radians(EL0 + (EL1 - EL0) * (j + 0.5) / h)
        if el <= 0: continue
        wt = math.sin(el) * math.cos(el)
        for i in range(w):
            k = 4 * (j * w + i)
            for c in range(3): acc[c] += px[k + c] * wt
        wsum += wt * w
    # E = pi * mean cosine-weighted radiance over the upper hemisphere (here: integral L cos dw)
    dth = math.radians(EL1 - EL0) / h; dph = 2 * math.pi / w
    # the probes' irradiance is the cosine-convolved radiance (irradianceGenF.glsl), i.e. E / pi, which
    # pbrIbl multiplies by the diffuse colour
    return [a * dth * dph / math.pi for a in acc]


class Panorama:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.today_sky = TodaySky()
        self.prop = Proposed()
        self.p_today = build([(VERT, FS_VERT), (FRAG, PANO_TODAY_FS)])
        self.p_disp = build([(VERT, FS_VERT), (FRAG, DISPLAY_FS)])
        self.sky_tex = tex2d(w, h)
        self.out = tex2d(w, h); self.f_out = fbo2d(self.out)
        self.disp = tex2d(w, h, RGBA8); self.f_disp = fbo2d(self.disp)
        self.dirs = pano_dirs(w, h)

    def today(self, p, yaw_offset=0.0):
        sky = self.today_sky.run(p, self.dirs)
        flat = [v for c in sky for v in (c[0], c[1], c[2], 1.0)]
        # what lights the ground today is the irradiance capture's sky: ambient 0, blue_horizon and
        # blue_density reduced to their HSL lightness (llsettingsvo.cpp applySpecial, draw_color)
        q = dict(p); q['ambient'] = [0.0, 0.0, 0.0]
        for key in ('blue_horizon', 'blue_density'):
            l = (max(p[key]) + min(p[key])) / 2.0; q[key] = [l, l, l]
        cap = self.today_sky.run(q, self.dirs)
        E = sky_irradiance_from([v for c in cap for v in (c[0], c[1], c[2], 1.0)], self.w, self.h)
        upload(self.sky_tex, self.w, self.h, flat)
        pr = self.p_today; use(pr); set_today_uniforms(pr, p)
        lx, ly, lz = light_vec(p); n = math.sqrt(lx * lx + ly * ly + lz * lz)
        set_u(pr, 'light_world', lx / n, ly / n, lz / n); set_u(pr, 'E_sky', *E)
        set_u(pr, 'sky_hdr_scale', 2.0 * math.sqrt(p['gamma'])); set_u(pr, 'res', float(self.w), float(self.h))
        set_u(pr, 'el0', EL0); set_u(pr, 'el1', EL1); set_u(pr, 'cam_h', CAM_H); set_u(pr, 'far_m', FAR_M)
        set_u(pr, 'albedo', ALBEDO); set_u(pr, 'yaw_offset', float(yaw_offset)); set_u(pr, 'sky_tex', 2)
        bind_unit(2, TEX2D, self.sky_tex)
        bindfbo(FB, self.f_out); viewport(0, 0, self.w, self.h); draw(0x0004, 0, 3)
        return read_tex(self.out, self.w, self.h), E

    def proposed(self, p, m):
        P_ = self.prop
        P_.luts(m, p)
        # sky irradiance first: render the sky only (far_m = 0)
        pr = P_.p_pano
        for pass_ in (0, 1):
            P_.set_atmos(pr, m, p)
            set_u(pr, 'sky_lut', 2); bind_unit(2, TEX2D, P_.sky)
            set_u(pr, 'res', float(self.w), float(self.h)); set_u(pr, 'h_obs', CAM_H * 1e-3)
            set_u(pr, 'el0', EL0); set_u(pr, 'el1', EL1); set_u(pr, 'cam_h', CAM_H); set_u(pr, 'albedo', ALBEDO)
            set_u(pr, 'sky_scale', *[float(x) for x in m['sky_scale']])
            set_u(pr, 'far_m', 0.0 if pass_ == 0 else FAR_M)
            if pass_ == 1: set_u(pr, 'E_sky', *E)
            bindfbo(FB, self.f_out); viewport(0, 0, self.w, self.h); draw(0x0004, 0, 3)
            px = read_tex(self.out, self.w, self.h)
            if pass_ == 0: E = sky_irradiance_from(px, self.w, self.h)
        return px, E

    def display(self, px, expo=1.0):
        upload(self.out, self.w, self.h, list(px))
        use(self.p_disp); set_u(self.p_disp, 'src', 3); set_u(self.p_disp, 'res', float(self.w), float(self.h)); set_u(self.p_disp, 'expo', float(expo))
        bind_unit(3, TEX2D, self.out)
        bindfbo(FB, self.f_disp); viewport(0, 0, self.w, self.h); draw(0x0004, 0, 3)
        buf = (C.c_ubyte * (self.w * self.h * 4))(); readpix(0, 0, self.w, self.h, 0x1908, 0x1401, C.cast(buf, P))
        return bytes(buf)


def write_png(path, w, h, rgba_rows_bottom_up):
    raw = bytearray()
    for y in range(h - 1, -1, -1):
        raw.append(0)
        row = rgba_rows_bottom_up[y * w * 4:(y + 1) * w * 4]
        for x in range(w): raw += row[x * 4:x * 4 + 3]

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(png)


def stack(images, w, h, gap=4):
    """Stack bottom-up RGBA buffers vertically (first on top) with a white gap."""
    out = bytearray()
    for k, img in enumerate(reversed(images)):
        out += img
        if k < len(images) - 1: out += bytes([255] * (w * gap * 4))
    return bytes(out), h * len(images) + gap * (len(images) - 1)


# --- metrics --------------------------------------------------------------------------------------
def to_lab(rgb8):
    def f(t): return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    r, g, b = [s2l(c / 255.0) for c in rgb8]
    X = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047; Y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    Z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    return 116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z))


def delta_e(img_a, img_b, w, h, rows=None, step=3):
    """Mean CIE76 dE between two displayed images over the sky rows (el > 0) or the given rows."""
    tot, n = 0.0, 0
    for j in range(h):
        el = EL0 + (EL1 - EL0) * (j + 0.5) / h
        if rows is None and el <= 0: continue
        if rows is not None and not rows(el): continue
        for i in range(0, w, step):
            k = 4 * (j * w + i)
            a = to_lab(img_a[k:k + 3]); b = to_lab(img_b[k:k + 3])
            tot += math.sqrt(sum((a[c] - b[c]) ** 2 for c in range(3))); n += 1
    return tot / max(n, 1)


def today_sunlit(p):
    """Today's PBR-path sunlit (srgb_to_linear of calcAtmosphericVars' sunlit)."""
    lx, ly, lz = light_vec(p)
    sc = p['sunlight_color']; bd = p['blue_density']; hd = p['haze_density']
    return [s2l(sc[k] * math.exp(-(bd[k] + 0.25 * hd) * p['density_multiplier'] * p['max_y'] / max(1e-6, ly))) for k in range(3)]


def prop_sunlit(m, el_rad, h_km=CAM_H * 1e-3, n=400, E=None):
    """The proposed sun at the ground: lin(sunlight) x transmittance along the curved path (CPU, the
    renderer-free integral the C++ mirror would hold), 0 behind the planet."""
    r0 = PLANET_KM + h_km
    x = [0.0, r0, 0.0]; d = [math.cos(el_rad), math.sin(el_rad), 0.0]
    b = r0 * d[1]; disc = b * b - (r0 * r0 - PLANET_KM * PLANET_KM)
    if d[1] < 0 and disc > 0 and -b - math.sqrt(disc) > 0:
        return [0.0, 0.0, 0.0]
    t_top = -b + math.sqrt(b * b - (r0 * r0 - TOP_KM * TOP_KM))
    tau = [0.0, 0.0, 0.0]; dt = t_top / n
    er = profile_vec(EEP_RAYLEIGH); em = profile_vec(EEP_MIE)
    for i in range(n):
        t = (i + 0.5) * dt
        px, py = x[0] + d[0] * t, x[1] + d[1] * t
        h = math.hypot(px, py) - PLANET_KM
        dr = min(max(er[0] * math.exp(er[1] * h), 0.0), 1.0); dmie = min(max(em[0] * math.exp(em[1] * h), 0.0), 1.0)
        dh = 1.0 - smooth(0.75 * H_SLAB_KM, H_SLAB_KM, h)
        L = EEP_ABSORPTION[0] if h < EEP_ABSORPTION[0]['width'] / 1000.0 else EEP_ABSORPTION[1]
        e_, s_, l_, c_ = profile_vec(L); do = min(max(e_ * math.exp(s_ * h) + l_ * h + c_, 0.0), 1.0)
        for k in range(3):
            tau[k] += (m['e_r'][k] * dr + m['e_m'] * dmie + m['e_h'] * dh + OZONE[k] * do) * dt
    E = E or m['E_sun']
    return [E[k] * math.exp(-tau[k]) for k in range(3)]


def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


# --- calibration ----------------------------------------------------------------------------------
def calibrate(pano, sky):
    """The display gain on the drawn sky that matches the proposed Default sky to today's (mean squared
    log luminance over the sky above 1 degree, at the file's 66 degrees and at 30 and 10). Returns the
    gain and the rms log error per channel at that gain (the shape and colour mismatch that is left)."""
    targets = []
    for el in (None, 30.0, 10.0):
        p = dict(sky); set_elevation(p, el)
        t, _ = pano.today(p)
        m = map_sky(p, dict(sky_gain=1.0, tint=(1.0, 1.0, 1.0)))
        q, _ = pano.proposed(p, m)
        targets.append((t, q))
    num, n = 0.0, 0
    for t, q in targets:
        for j in range(pano.h):
            if EL0 + (EL1 - EL0) * (j + 0.5) / pano.h <= 1.0: continue
            for i in range(0, pano.w, 2):
                k = 4 * (j * pano.w + i)
                num += math.log(lum(t[k:k + 3]) + 1e-5) - math.log(lum(q[k:k + 3]) + 1e-5); n += 1
    gain = math.exp(num / n)
    # per channel too (a fixed tint, the same for every sky): the colour mismatch left on Default
    tint = []
    for c in range(3):
        acc, nn = 0.0, 0
        for t, q in targets:
            for j in range(pano.h):
                if EL0 + (EL1 - EL0) * (j + 0.5) / pano.h <= 1.0: continue
                for i in range(0, pano.w, 2):
                    k = 4 * (j * pano.w + i)
                    acc += math.log(t[k + c] + 1e-5) - math.log(q[k + c] * gain + 1e-5); nn += 1
        tint.append(math.exp(acc / nn))
    CAL['_tint_fit'] = tuple(tint)
    e, n = 0.0, 0
    for t, q in targets:
        for j in range(pano.h):
            if EL0 + (EL1 - EL0) * (j + 0.5) / pano.h <= 1.0: continue
            for i in range(0, pano.w, 2):
                k = 4 * (j * pano.w + i)
                for c in range(3):
                    e += (math.log(t[k + c] + 1e-5) - math.log(q[k + c] * gain + 1e-5)) ** 2; n += 1
    return gain, math.sqrt(e / n)


# --- main -----------------------------------------------------------------------------------------
STUDY_SKIES = [
    ('default', 'Default.xml', 'Default (sun at 66 deg, the file)'),
    ('sunset', 'Sunset.xml', 'Sunset (4.3 deg)'),
    ('night', 'Night.xml', 'Night (moon at the zenith)'),
    ('hazy', 'Foggy.xml', 'Foggy (sun at the zenith)'),
]


def main():
    W, H = 720, 220
    pano = Panorama(W, H)
    default = load_sky(sky_file('Default.xml'))
    gain, cal_err = calibrate(pano, default)
    CAL.update(sky_gain=gain)
    tinted = dict(sky_gain=gain, tint=CAL.pop('_tint_fit'))
    print(f"Default's remaining per-channel ratio (today / proposed at that gain): {[round(x, 2) for x in tinted['tint']]}")
    md = map_sky(default, tinted); td, _ = pano.today(default); qd, _ = pano.proposed(default, md)
    print(f"Default with that tint too: sky dE {delta_e(pano.display(td), pano.display(qd), W, H):.1f}")
    print(f"display gain matching today's Default sky: {gain:.2f} ({math.log2(gain):.2f} EV); "
          f"rms log error per channel left at that gain {cal_err:.3f}")
    phys = dict(sky_gain=1.0, tint=(1.0, 1.0, 1.0))

    # 1. study skies, side by side
    print('\n## Study skies (mean dE76 on the displayed sky above the horizon, and below it)')
    print('| Sky | File | Sun elev. (deg) | valid | sky dE | ground dE | today sunlit (lin) | proposed sunlit (lin) |')
    print('|---|---|---|---|---|---|---|---|')
    for key, fname, label in STUDY_SKIES:
        p = load_sky(sky_file(fname))
        m = map_sky(p)
        t, Et = pano.today(p)
        q, Eq = pano.proposed(p, m)
        q1, Eq1 = pano.proposed(p, map_sky(p, phys))
        a, b, b1 = pano.display(t), pano.display(q), pano.display(q1)
        if not NO_IMAGES:
            img, hh = stack([a, b1, b], W, H)
            write_png(OUT_DIR / f'{key}.png', W, hh, img)
        zen = lambda px: px[4 * ((H - 1) * W + W // 2):4 * ((H - 1) * W + W // 2) + 3]
        hor = lambda px: px[4 * (int((2.0 - EL0) / (EL1 - EL0) * H) * W + W // 4):4 * (int((2.0 - EL0) / (EL1 - EL0) * H) * W + W // 4) + 3]
        print(f"|   zenith lum today {lum(zen(t)):.3f}, physical {lum(zen(q1)):.4f}; horizon (2 deg, 90 deg from the light) "
              f"today {lum(hor(t)):.3f}, physical {lum(hor(q1)):.4f}; sky irradiance today {lum(Et):.3f}, physical {lum(Eq1):.4f} |")
        ts = today_sunlit(p)
        ps = prop_sunlit(m, p['light_el'], E=m['E_sun'] if p['sun_up'] else m['E_moon'])
        print(f"|   mapping: tau_lin {[round(x, 3) for x in m['tau_lin']]}, e_r {[round(x, 4) for x in m['e_r']]} /km, e_m {m['e_m']:.4f}, "
              f"e_h {m['e_h']:.4f} /km, k_ap {m['k_ap']:.4f} /km, g {m['g']:.2f} |")
        print(f"| {label} | `{fname}` | {math.degrees(p['sun_el']):.1f} | {m['valid']} | {delta_e(a, b, W, H):.1f} | "
              f"{delta_e(a, b, W, H, rows=lambda el: el < 0):.1f} | {ts[0]:.3f} {ts[1]:.3f} {ts[2]:.3f} | {ps[0]:.3f} {ps[1]:.3f} {ps[2]:.3f} |")

    # 2. the sunset sky through the last degrees: panoramas at 8, 3, 1, -2, -6 degrees
    p0 = load_sky(sky_file(STUDY_SKIES[1][1]))
    rows = []
    for el in (8.0, 3.0, 1.0, -2.0, -6.0):
        p = dict(p0); set_elevation(p, el); p['sun_up'] = 1 if el >= 0 else 0
        if el < 0:   # the sun below the horizon: today the light is the moon (opposite), as the viewer
            pass
        m = map_sky(p)
        t, _ = pano.today(p); q, _ = pano.proposed(p, m)
        rows.append(pano.display(t)); rows.append(pano.display(q))
        ts = today_sunlit(p) if p['sun_up'] else [0.0] * 3
        print(f"| sweep {el:+.0f} deg | today sunlit {lum(ts):.4f} | proposed sunlit {lum(prop_sunlit(m, math.radians(el))):.4f} |")
    if not NO_IMAGES:
        img, hh = stack(rows, W, H)
        write_png(OUT_DIR / 'sunset_sweep.png', W, hh, img)

    # 3. the sun's colour at the ground against elevation (Default and the sunset sky)
    print('\n## The sun at the ground (linear, PBR path), today against proposed')
    print('| Sky | Elevation (deg) | today R G B | today lum | proposed R G B | proposed lum |')
    print('|---|---|---|---|---|---|')
    for fname in ('Default.xml', STUDY_SKIES[1][1], '[TOR] SUNSET - Bangko 1.xml'):
        base = load_sky(sky_file(fname))
        m = map_sky(base)
        for el in (90, 30, 10, 5, 3, 2, 1, 0.5, 0.0, -0.5, -1.0):
            p = dict(base); set_elevation(p, el)
            ts = today_sunlit(p) if el >= 0 else [0.0] * 3
            ps = prop_sunlit(m, math.radians(el))
            print(f"| `{fname}` | {el} | {ts[0]:.4f} {ts[1]:.4f} {ts[2]:.4f} | {lum(ts):.4f} | {ps[0]:.4f} {ps[1]:.4f} {ps[2]:.4f} | {lum(ps):.4f} |")

    # 4. today's object haze depends on where the camera looks (eye-space position, world lightnorm)
    print("\n## Today's object haze: the same ground point 800 m away, camera turned ([NB] P-Haze 1200, sun at 20 deg)")
    p = load_sky(sky_file('[NB] P-Haze 1200.xml')); set_elevation(p, 20.0)
    for yaw in (0.0, 30.0, 90.0):
        t, _ = pano.today(p, yaw_offset=math.radians(yaw))
        # the ground point 800 m away towards the sun (azimuth 0)
        el = -math.degrees(math.atan2(CAM_H, 800.0)); j = int((el - EL0) / (EL1 - EL0) * H); i = W // 2
        k = 4 * (j * W + i)
        print(f"| camera yaw +{yaw:.0f} deg | {t[k]:.4f} {t[k+1]:.4f} {t[k+2]:.4f} |")

    # 5. the library: change per sky at its own sun elevation
    if not NO_LIBRARY:
        Ws, Hs = 240, 72
        small = Panorama(Ws, Hs)
        res = []
        files = sorted(SKY_DIR.iterdir())
        for f in files:
            try:
                p = load_sky(f)
            except Exception:
                continue
            m = map_sky(p)
            mt = map_sky(p, tinted)
            try:
                t, _ = small.today(p); q, _ = small.proposed(p, m); qt, _ = small.proposed(p, mt)
            except AssertionError:
                continue
            a, b, bt = small.display(t), small.display(q), small.display(qt)
            dsky = delta_e(a, b, Ws, Hs, step=2)
            dgnd = delta_e(a, b, Ws, Hs, rows=lambda el: el < 0, step=2)
            dtint = delta_e(a, bt, Ws, Hs, step=2)
            res.append((dsky, dgnd, m['valid'], p['name'], math.degrees(p['sun_el']), dtint))
            if len(res) % 100 == 0: print(f'  ... {len(res)}', flush=True)
        res.sort(reverse=True)
        valid = [r for r in res if r[2]]
        dt = sorted(r[5] for r in res)
        print(f"with Default's tint as well: sky dE median {dt[len(dt) // 2]:.1f}, p90 {dt[9 * len(dt) // 10]:.1f}; "
              f"< 10: {sum(1 for x in dt if x < 10)} of {len(dt)}")
        print(f"\n## Library: {len(res)} sky files, {len(valid)} inside the physical range")
        for name, sel in (('all', res), ('valid', valid)):
            ds = sorted(r[0] for r in sel); dg = sorted(r[1] for r in sel)
            n = len(ds)
            print(f"{name}: sky dE median {ds[n // 2]:.1f}, p90 {ds[9 * n // 10]:.1f}, max {ds[-1]:.1f}; "
                  f"ground dE median {dg[n // 2]:.1f}, p90 {dg[9 * n // 10]:.1f}")
            for lim in (5, 10, 20):
                print(f"  sky dE < {lim}: {sum(1 for x in ds if x < lim)} of {n}")
        print('| Sky file | sun elev. | sky dE | ground dE | valid |')
        print('|---|---|---|---|---|')
        for r in res[:12]:
            print(f'| `{r[3]}` | {r[4]:.0f} | {r[0]:.1f} | {r[1]:.1f} | {r[2]} |')
        print('least changed:')
        for r in res[-6:]:
            print(f'| `{r[3]}` | {r[4]:.0f} | {r[0]:.1f} | {r[1]:.1f} | {r[2]} |')
        invalid = [r for r in res if not r[2]]
        print(f'outside the physical range: {len(invalid)}')

    if not NO_TIMING:
        timing()


def timing():
    print('\n## Cost (GL_TIME_ELAPSED median of 60, after 10 warm-up)')
    P_ = Proposed()
    p = load_sky(sky_file('Default.xml')); m = map_sky(p)
    tan_half = math.tan(math.radians(30))
    t_trans = median_ms(lambda: (P_.set_atmos(P_.p_trans, m, p), bindfbo(FB, P_.f_trans), viewport(0, 0, 256, 64), draw(0x0004, 0, 3)))
    t_ms = median_ms(lambda: (P_.set_atmos(P_.p_ms, m, p), bindfbo(FB, P_.f_ms), viewport(0, 0, 32, 32), draw(0x0004, 0, 3)))
    def sky():
        P_.set_atmos(P_.p_sky, m, p); set_u(P_.p_sky, 'h_obs', 0.02); bindfbo(FB, P_.f_sky); viewport(0, 0, 192, 108); draw(0x0004, 0, 3)
    t_sky = median_ms(sky)
    t_ap = median_ms(lambda: P_.ap_volume(m, p))
    print(f'| transmittance LUT 256x64 | {t_trans:.4f} ms |')
    print(f'| multiple scattering LUT 32x32 | {t_ms:.4f} ms |')
    print(f'| sky view LUT 192x108 | {t_sky:.4f} ms |')
    print(f'| aerial perspective 32x32x32 (32 slice draws) | {t_ap:.4f} ms |')

    p_apply = build([(VERT, FS_VERT), (FRAG, APPLY_AP_FS)])
    p_sky = build([(VERT, FS_VERT), (FRAG, APPLY_SKY_FS)])
    p_haze = build([(VERT, FS_VERT), (FRAG, TODAY_HAZE_FS)])
    for (w, h) in ((1920, 1080), (2560, 1440)):
        # depth stand-in: view distances 1 m .. 1 km across the screen, a gradient with noise
        depth = tex2d(w, h, R32F)
        import random
        rnd = random.Random(7)
        data = []
        for j in range(h):
            for i in range(w):
                data += [1.0 + 1023.0 * ((i / w) ** 2) * (0.5 + 0.5 * rnd.random()), 0.0, 0.0, 0.0]
        bindtex(TEX2D, depth); arr = (F * len(data))(*data)
        # upload as RGBA float into an R32F texture
        texsub2(TEX2D, 0, 0, 0, w, h, 0x1908, FLOAT, C.cast(arr, P))
        tgt = tex2d(w, h, RGBA16F); f = fbo2d(tgt)

        def apply_ap():
            use(p_apply); set_u(p_apply, 'depth_tex', 0); set_u(p_apply, 'ap_in', 1); set_u(p_apply, 'ap_tr', 2)
            set_u(p_apply, 'res', float(w), float(h)); set_u(p_apply, 'ap_range', 1024.0)
            bind_unit(0, TEX2D, depth); bind_unit(1, TEX3D, P_.ap_in); bind_unit(2, TEX3D, P_.ap_tr)
            bindfbo(FB, f); viewport(0, 0, w, h)
            enable(0x0BE2); blendfunc(1, 0x88F9)   # ONE, SRC1_COLOR (dual-source: dst * T + inscatter)
            draw(0x0004, 0, 3); disable(0x0BE2)

        def apply_sky():
            P_.set_atmos(p_sky, m, p); set_u(p_sky, 'sky_lut', 2); bind_unit(2, TEX2D, P_.sky)
            set_u(p_sky, 'res', float(w), float(h)); set_u(p_sky, 'tan_half', tan_half); set_u(p_sky, 'aspect', w / h)
            set_u(p_sky, 'sky_scale', 1.0); set_u(p_sky, 'h_obs', 0.02)
            ident = (F * 9)(0, 0, 1, 0, 1, 0, -1, 0, 0); umat3(_loc_(p_sky, 'view_to_atmo'), 1, 0, C.cast(ident, P))
            bindfbo(FB, f); viewport(0, 0, w, h); draw(0x0004, 0, 3)

        def haze():
            use(p_haze); set_today_uniforms(p_haze, p)
            set_u(p_haze, 'depth_tex', 0); bind_unit(0, TEX2D, depth)
            set_u(p_haze, 'res', float(w), float(h)); set_u(p_haze, 'tan_half', tan_half); set_u(p_haze, 'aspect', w / h)
            set_u(p_haze, 'sun_dir', 0.0, 0.9, -0.43); set_u(p_haze, 'sky_hdr_scale', 2.0)
            bindfbo(FB, f); viewport(0, 0, w, h)
            enable(0x0BE2); blendfunc(1, 0x0302)   # ONE, SRC_ALPHA, as pipeline.cpp's haze pass
            draw(0x0004, 0, 3); disable(0x0BE2)

        ta, ts, th = median_ms(apply_ap), median_ms(apply_sky), median_ms(haze)
        print(f'| {w}x{h} | AP apply {ta:.4f} ms | sky apply (every pixel) {ts:.4f} ms | today haze pass {th:.4f} ms |')


if __name__ == '__main__':
    main()
