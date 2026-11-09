# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Local-lights study bench (doc/light/local-lights-study.md): today's prim point lights and
projectors against the proposed physical model, on the GPU with the viewer's own shader code.

No viewer launch. A hidden OpenGL context (scripts/perf/gl_test_context.py, through atmosphere_gl)
compiles, unmodified:
  * class1/deferred/deferredUtil.glsl: calcLegacyDistanceAttenuation, pbrPunctual,
    clipProjectedLightVars, getProjectedLightDiffuseColor, getProjectedLightAmbiance;
  * class1/environment/srgbF.glsl and class1/deferred/tonemapUtilF.glsl (toneMapACES_Hill).
"Today" repeats the PBR branches of class3/deferred/pointLightF.glsl and spotLightF.glsl line for
line around those functions (shadow = 1, classic_mode 0). "Proposed" is the study's model, written
here as GLSL (prop_point / prop_projector below); it is not in the viewer.

Three outputs:
  1. tables: the illuminance E (in the renderer's units, U) on a surface facing the light, and the
     displayed value of a white panel (albedo 0.9, roughness 1) after ACES at exposure 1, for the
     study's worked examples at a list of distances;
  2. side-by-side renders (today | proposed) of the worked examples in a small analytic scene
     (floor, walls, a head-sized sphere), written as PNG to the folder given by --out (default:
     doc/light/local-lights/). No shadows in either half: point lights cast none today, and the
     proposed shadows are a later stage;
  3. the same formulas in Python, for the tables that sweep many lights: kappa per falloff, the
     flux check, the prim cap dial, the worn-light caps, flicker depth and the cap's size floor.

The unit U: a white Lambertian surface lit with illuminance E has radiance albedo/pi * E. The
clear PBR noon sun of L1 (doc/light/light-reference-numbers.md, sky 2 at 66 degrees) gives
E = 0.95 U on a horizontal panel; the classic Default sky gives about 3.2 U (L1, sky 1).

    python scripts/light/bench_local_lights.py [--out DIR] [--no-images]

Needs Windows and a GPU.
"""
import ctypes as C
import math
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403
from atmosphere_gl import _loc  # noqa: E402

OUT_DIR = ROOT / 'doc' / 'light' / 'local-lights'
for k, a in enumerate(sys.argv):
    if a == '--out' and k + 1 < len(sys.argv):
        OUT_DIR = Path(sys.argv[k + 1])
NO_IMAGES = '--no-images' in sys.argv

# The proposed constants (study §3 and §4).
E_CAP_PRIM = 6.5     # U, the highest illuminance any prim light may give (today's own maximum, 3.25 x 2)
E_CAP_ATT = 1.0      # U, the same for a worn light (attachment)
R_CAP_ATT = 6.0      # m, the reach of a worn light (radius 4 m x 1.5)
SUN_PBR = 0.95       # U, L1 clear PBR noon sun on a horizontal panel
SUN_CLASSIC = 3.2    # U, L1 classic Default sky, same panel

uniform_matrix4 = fn('glUniformMatrix4fv', None, I, I, C.c_ubyte, P)
uniform_1fv = fn('glUniform1fv', None, I, I, P)
u4f = fn('glUniform4f', None, I, F, F, F, F)
u2f = fn('glUniform2f', None, I, F, F)
gen_mip = fn('glGenerateMipmap', None, U)
RGBA8 = 0x8058

DEFERRED_STUBS = """
vec4 decodeNormal(vec4 n) { return n; }
vec3 atmosFragLightingLinear(vec3 light, vec3 additive, vec3 atten) { return light; }
"""

MAIN = r"""
uniform int mode;             // 0: panel facing the light at distance dist_list[i]; 1: image
uniform float dist_list[32];
uniform vec2 img_res;
uniform vec3 cam_pos; uniform vec3 cam_fwd; uniform vec3 cam_up; uniform float cam_tanfov;
uniform vec3 room;            // half width (x), back wall y, ceiling z (0: no ceiling)
uniform vec4 head;            // sphere: centre, radius (radius 0: none)
uniform float amb;            // uniform ambient irradiance (diffuse only), U
uniform float exposure_u;

uniform float falloff;        // today's falloff uniform (pointLightF.glsl / spotLightF.glsl)
uniform int is_proj;
uniform vec3 lp;              // light (prim) centre
uniform vec3 l_axis;          // projector: direction it shines (-Z of the prim)
uniform vec3 apex;            // projector: projection origin (setupSpotLight's `origin`)
uniform float l_falloff;      // the prim's falloff (0..2), not yet x 0.5
uniform float s_phys;         // the light's own size (m)
uniform float e_cap;          // U
uniform float r_reach;        // proposed reach (m), 1.5 x radius, capped for worn lights
uniform float tan_half_fov;   // projector, vertical
uniform float aspect;         // projector, width / height
uniform float focus_m;        // projector focus (m)
uniform float ambiance_v;     // projector ambiance
uniform float tex_size;

layout(location = 0) out vec4 out_today;
layout(location = 1) out vec4 out_prop;
layout(location = 2) out vec4 out_e;

vec3 toneMapACES_Hill(vec3 color);
vec3 linear_to_srgb(vec3 c);

// ---------------------------------------------------------------- today
// class3/deferred/pointLightF.glsl, PBR branch (size = 1.5 r, falloff = 0.5 falloff)
vec3 today_point(vec3 pos, vec3 n, vec3 v, vec3 diffuseColor, vec3 specularColor, float rough, float metallic, out float E)
{
    E = 0.0;
    vec3 lv = lp - pos;
    float lightDist = length(lv);
    if (lightDist >= size) return vec3(0);
    float dist = lightDist / size;
    float dist_atten = calcLegacyDistanceAttenuation(dist, falloff);
    vec3 intensity = dist_atten * color * 3.25;
    E = dist_atten * 3.25;
    float nl = 0; vec3 diffPunc = vec3(0); vec3 specPunc = vec3(0);
    pbrPunctual(diffuseColor, specularColor, rough, metallic, n, v, normalize(lv), nl, diffPunc, specPunc);
    return intensity * clamp(nl * (diffPunc + specPunc), vec3(0), vec3(10));
}

// class3/deferred/spotLightF.glsl, PBR branch, shadow = 1 (positions in world space here)
vec3 today_projector(vec3 pos, vec3 n, vec3 v, vec3 diffuseColor, vec3 specularColor, float rough, float metallic, out float E)
{
    E = 0.0;
    vec3 final_color = vec3(0);
    vec4 proj_tc; float dist, l_dist; vec3 lv;
    if (clipProjectedLightVars(lp, pos, dist, l_dist, lv, proj_tc)) return vec3(0);
    float shadow = 1.0;
    float dist_atten = calcLegacyDistanceAttenuation(dist, falloff);
    if (dist_atten <= 0.0) return vec3(0);
    lv = apex - pos;
    vec3 h, l; float nh, nl, nv, vh, lightDist;
    calcHalfVectors(lv, n, v, h, l, nh, nl, nv, vh, lightDist);
    vec3 diffPunc = vec3(0); vec3 specPunc = vec3(0);
    if (proj_tc.x > 0.0 && proj_tc.x < 1.0 && proj_tc.y > 0.0 && proj_tc.y < 1.0)
    {
        float lit = 0.0; float amb_da = 0.0;
        lv = normalize(lv);
        vec3 dlit = vec3(0);
        if (nl > 0.0)
        {
            amb_da += (nl*0.5 + 0.5) * proj_ambiance;
            dlit = getProjectedLightDiffuseColor(l_dist, proj_tc.xy);
            vec3 intensity = dist_atten * dlit * 3.25 * shadow;
            E = dist_atten * 3.25 * dot(dlit, vec3(0.2126, 0.7152, 0.0722)) / max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-6);
            pbrPunctual(diffuseColor, specularColor, rough, metallic, n, v, normalize(lv), nl, diffPunc, specPunc);
            final_color += intensity * clamp(nl * (diffPunc + specPunc), vec3(0), vec3(10));
        }
        vec3 amb_rgb = getProjectedLightAmbiance(amb_da, dist_atten, lit, nl, 1.0, proj_tc.xy) * 3.25;
        pbrPunctual(diffuseColor, specularColor, rough, metallic, n, v, normalize(lv), nl, diffPunc, specPunc);
        final_color += amb_rgb * clamp(nl * (diffPunc + specPunc), vec3(0), vec3(10));
    }
    return max(final_color, vec3(0.0));
}

// ---------------------------------------------------------------- proposed (study only)
// Window exponent from the falloff, and the constant that keeps today's luminous flux.
float prop_n(float f) { return 4.0 / (1.0 + f); }
float prop_kappa(float f)
{
    float n = prop_n(f);
    float today_flux = 3.25 * 2.0 / (30.0 * (1.0 + 0.5 * f) * (1.0 + 0.5 * f)); // integral of x^2 E_today(x)
    float prop_flux = 1.0 - 2.0 / (n + 1.0) + 1.0 / (2.0 * n + 1.0);             // integral of (1 - x^n)^2
    return today_flux / prop_flux;
}
// E at distance d (windowed inverse square, disk-source near field, cap as a size floor)
float prop_E(float d, float I, out float s_eff)
{
    float se2 = max(s_phys * s_phys, I / e_cap);
    s_eff = sqrt(se2);
    float x = d / r_reach;
    if (x >= 1.0) return 0.0;
    float w = clamp(1.0 - pow(x, prop_n(l_falloff)), 0.0, 1.0);
    return I * w * w / (d * d + se2);
}
vec3 prop_shade(vec3 pos, vec3 n, vec3 v, vec3 dC, vec3 sC, float rough, float metallic, vec3 lsrc, float E, float s_eff)
{
    vec3 lv = lsrc - pos; float d = length(lv);
    // the source's angular size widens the specular lobe (sphere-light roughness widening)
    float a = rough * rough;
    float a2 = min(1.0, a + s_eff / (2.0 * max(d, 1e-3)));
    float nl = 0; vec3 diffPunc = vec3(0); vec3 specPunc = vec3(0);
    pbrPunctual(dC, sC, sqrt(a2), metallic, n, v, normalize(lv), nl, diffPunc, specPunc);
    return E * color * clamp(nl * (diffPunc + specPunc), vec3(0), vec3(10));
}
vec3 prop_point(vec3 pos, vec3 n, vec3 v, vec3 dC, vec3 sC, float rough, float metallic, out float E)
{
    float I = prop_kappa(l_falloff) * r_reach * r_reach;
    float s_eff;
    E = prop_E(length(lp - pos), I, s_eff);
    if (E <= 0.0) return vec3(0);
    return prop_shade(pos, n, v, dC, sC, rough, metallic, lp, E, s_eff);
}
vec3 prop_projector(vec3 pos, vec3 n, vec3 v, vec3 dC, vec3 sC, float rough, float metallic, out float E)
{
    E = 0.0;
    vec4 tc = proj_mat * vec4(pos, 1.0);
    if (tc.z < 0.0) return vec3(0);
    tc.xyz /= tc.w;
    float d_c = length(lp - pos);                    // window from the prim, as today
    float a_off = length(lp - apex);                 // apex behind the prim
    float l = max(dot(pos - apex, l_axis) - a_off, 0.0);   // distance in front of the prim along the axis
    // inverse square from the apex, normalised to a point light at R/3 on the axis
    float r3 = r_reach / 3.0;
    float g = (r3 + a_off) * (r3 + a_off) / (r3 * r3);
    float I = prop_kappa(l_falloff) * r_reach * r_reach * g;
    float s_eff;
    float d_a = length(pos - apex);
    float se2 = max(s_phys * s_phys, I / (e_cap * g));
    s_eff = sqrt(se2);
    float x = d_c / r_reach;
    if (x >= 1.0) return vec3(0);
    float w = clamp(1.0 - pow(x, prop_n(l_falloff)), 0.0, 1.0); w *= w;
    float Ebeam = I * w / (d_a * d_a + se2 * g);
    // defocus: circle of confusion of the aperture (diameter 2 s_phys) focused at focus_m
    float f = max(focus_m, 0.05);
    float coc = 2.0 * s_phys * abs(l - f) / f;                      // metres on the receiver
    float width = 2.0 * (l + a_off) * tan_half_fov;                 // frustum height there
    float coc_uv = coc / max(width, 1e-3);
    float lod = clamp(log2(max(coc_uv * tex_size, 1.0)), 0.0, proj_lod);
    // frustum edge: as wide as the blur, no wider
    vec2 e = min(tc.xy, 1.0 - tc.xy);
    float edge_w = max(0.5 * coc_uv, 1.0 / tex_size);
    float edge = clamp(min(e.x, e.y * aspect) / edge_w + 0.5, 0.0, 1.0);
    if (edge <= 0.0) return vec3(0);
    vec4 t = textureLod(projectionMap, tc.xy, lod);
    vec3 trgb = srgb_to_linear(t.rgb) * t.a * edge;
    E = Ebeam * dot(trgb, vec3(0.2126, 0.7152, 0.0722));
    // ambiance: shadow fill only (no shadows in this bench, so it adds nothing here)
    vec3 lv = apex - pos;
    float a = rough * rough;
    float a2 = min(1.0, a + s_eff / (2.0 * max(d_a, 1e-3)));
    float nl = 0; vec3 diffPunc = vec3(0); vec3 specPunc = vec3(0);
    pbrPunctual(dC, sC, sqrt(a2), metallic, n, v, normalize(lv), nl, diffPunc, specPunc);
    return Ebeam * trgb * color * clamp(nl * (diffPunc + specPunc), vec3(0), vec3(10));
}

// ---------------------------------------------------------------- scene
bool hit_plane(vec3 ro, vec3 rd, vec3 pn, float pd, inout float t, inout vec3 n)
{
    float den = dot(rd, pn);
    if (abs(den) < 1e-6) return false;
    float tt = -(dot(ro, pn) + pd) / den;
    if (tt > 1e-4 && tt < t) { t = tt; n = pn; return true; }
    return false;
}

void surface(vec3 pos, vec3 n, vec3 v, vec3 albedo, float rough, out vec3 c_today, out vec3 c_prop, out float e_t, out float e_p)
{
    vec3 dC, sC;
    calcDiffuseSpecular(albedo, 0.0, dC, sC);
    if (is_proj > 0)
    {
        c_today = today_projector(pos, n, v, dC, sC, rough, 0.0, e_t);
        c_prop = prop_projector(pos, n, v, dC, sC, rough, 0.0, e_p);
    }
    else
    {
        c_today = today_point(pos, n, v, dC, sC, rough, 0.0, e_t);
        c_prop = prop_point(pos, n, v, dC, sC, rough, 0.0, e_p);
    }
    vec3 a = amb * dC;
    c_today += a; c_prop += a;
}

vec3 display(vec3 c) { return linear_to_srgb(toneMapACES_Hill(c * exposure_u)); }

void main()
{
    vec3 c_today = vec3(0), c_prop = vec3(0); float e_t = 0.0, e_p = 0.0;
    if (mode == 0)
    {
        int i = int(gl_FragCoord.x);
        vec3 ax = (is_proj > 0) ? l_axis : vec3(0, 0, -1);
        vec3 pos = lp + ax * dist_list[i];
        vec3 n = -ax;
        surface(pos, n, n, vec3(0.9), 1.0, c_today, c_prop, e_t, e_p);
        c_today -= amb * vec3(0.9 * 0.96); c_prop -= amb * vec3(0.9 * 0.96);
        out_today = vec4(display(c_today), dot(c_today, vec3(0.2126, 0.7152, 0.0722)));
        out_prop = vec4(display(c_prop), dot(c_prop, vec3(0.2126, 0.7152, 0.0722)));
        out_e = vec4(e_t, e_p, 0, 0);
        return;
    }
    vec2 uv = (gl_FragCoord.xy / img_res) * 2.0 - 1.0;
    vec3 right = normalize(cross(cam_fwd, cam_up));
    vec3 up = cross(right, cam_fwd);
    vec3 rd = normalize(cam_fwd + (uv.x * right * img_res.x / img_res.y + uv.y * up) * cam_tanfov);
    vec3 ro = cam_pos;
    float t = 1e9; vec3 n = vec3(0); int id = -1;
    if (hit_plane(ro, rd, vec3(0, 0, 1), 0.0, t, n)) id = 0;                         // floor
    if (hit_plane(ro, rd, vec3(0, -1, 0), room.y, t, n)) id = 1;                     // back wall
    if (hit_plane(ro, rd, vec3(1, 0, 0), room.x, t, n)) id = 1;                      // left wall
    if (hit_plane(ro, rd, vec3(-1, 0, 0), room.x, t, n)) id = 1;                     // right wall
    if (room.z > 0.0 && hit_plane(ro, rd, vec3(0, 0, -1), room.z, t, n)) id = 2;     // ceiling
    if (head.w > 0.0)
    {
        vec3 oc = ro - head.xyz; float b = dot(oc, rd); float c = dot(oc, oc) - head.w * head.w;
        float h = b * b - c;
        if (h > 0.0) { float tt = -b - sqrt(h); if (tt > 1e-4 && tt < t) { t = tt; n = normalize(ro + rd * tt - head.xyz); id = 3; } }
    }
    // the light's own position, drawn as a small bright dot
    { vec3 oc = ro - lp; float b = dot(oc, rd); float c = dot(oc, oc) - 0.03 * 0.03; float h = b * b - c;
      if (h > 0.0 && -b - sqrt(h) < t) { out_today = vec4(1); out_prop = vec4(1); out_e = vec4(0); return; } }
    if (id < 0) { out_today = vec4(0, 0, 0, 1); out_prop = out_today; out_e = vec4(0); return; }
    vec3 pos = ro + rd * t;
    vec3 albedo = (id == 0) ? vec3(0.35) : (id == 3) ? vec3(0.55, 0.42, 0.36) : vec3(0.6);
    float rough = (id == 0) ? 0.45 : (id == 3) ? 0.55 : 0.9;
    surface(pos, n, -rd, albedo, rough, c_today, c_prop, e_t, e_p);
    out_today = vec4(display(c_today), 1.0);
    out_prop = vec4(display(c_prop), 1.0);
    out_e = vec4(e_t, e_p, 0, 0);
}
"""


# ------------------------------------------------------------------ small vector helpers
def sub(a, b): return [a[i] - b[i] for i in range(3)]
def add(a, b): return [a[i] + b[i] for i in range(3)]
def mul(a, s): return [a[i] * s for i in range(3)]
def dot(a, b): return sum(a[i] * b[i] for i in range(3))
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a):
    l = math.sqrt(dot(a, a)); return [x / l for x in a]


def mat_mul(a, b):   # row-major 4x4
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def setup_projector(pos, axis, up_hint, scale, fov, radius):
    """LLPipeline::setupSpotLight (pipeline.cpp) with an identity modelview (world = 'screen')."""
    at = norm(axis)                       # the prim's -Z
    zax = mul(at, -1.0)
    xax = norm(cross(up_hint, zax)); yax = cross(zax, xax)
    np_ = add(pos, mul(at, scale[2] * 0.5))
    dist = (scale[1] * 0.5) / math.tan(fov * 0.5)
    origin = sub(np_, mul(at, dist))
    light_to_world = [[xax[0], yax[0], zax[0], origin[0]], [xax[1], yax[1], zax[1], origin[1]],
                      [xax[2], yax[2], zax[2], origin[2]], [0, 0, 0, 1]]
    rt = [[xax[0], xax[1], xax[2], -dot(xax, origin)], [yax[0], yax[1], yax[2], -dot(yax, origin)],
          [zax[0], zax[1], zax[2], -dot(zax, origin)], [0, 0, 0, 1]]
    s = radius * 1.5
    near, far = dist, s + dist - scale[2]
    aspect = scale[0] / scale[1]
    f = 1.0 / math.tan(fov * 0.5)
    persp = [[f / aspect, 0, 0, 0], [0, f, 0, 0], [0, 0, (far + near) / (near - far), 2 * far * near / (near - far)], [0, 0, -1, 0]]
    trans = [[0.5, 0, 0, 0.5], [0, 0.5, 0, 0.5], [0, 0, 0.5, 0.5], [0, 0, 0, 1]]
    proj = mat_mul(trans, mat_mul(persp, rt))
    p1 = add(origin, mul(at, near + 0.01)); p2 = add(origin, mul(at, near + 1.0))
    return dict(proj=proj, proj_p=p1, proj_n=norm(sub(p2, p1)), origin=origin, near=near, far=far,
                range=far - near, aspect=aspect, tan_half=math.tan(fov * 0.5), at=at)


def gobo_texture(n=256):
    """A white disc gobo with three dark bars, sRGB RGBA8, mipmapped (the viewer samples with lod)."""
    px = bytearray()
    for j in range(n):
        for i in range(n):
            x, y = (i + 0.5) / n - 0.5, (j + 0.5) / n - 0.5
            r = math.hypot(x, y)
            a = math.atan2(y, x)
            inside = r < 0.42 and not (r > 0.08 and abs(math.sin(a * 1.5)) < 0.12)
            v = 255 if inside else 0
            px += bytes((v, v, v, 255))
    t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
    levels = int(math.log2(n)) + 1
    texstorage2(TEX2D, levels, RGBA8, n, n)
    buf = (C.c_ubyte * len(px)).from_buffer(px)
    texsub2(TEX2D, 0, 0, 0, n, n, 0x1908, 0x1401, C.cast(buf, P))
    gen_mip(TEX2D)
    texparam(TEX2D, 0x2801, 0x2703); texparam(TEX2D, 0x2800, 0x2601)
    texparam(TEX2D, 0x2802, 0x812D); texparam(TEX2D, 0x2803, 0x812D)  # clamp to border (black)
    return t.value, n


def set_mat4(prog, name, m):
    l = _loc(prog, name.encode())
    if l < 0:
        return
    flat = (F * 16)(*[m[i][j] for i in range(4) for j in range(4)])
    uniform_matrix4(l, 1, 1, C.cast(flat, P))   # row-major -> transpose


def compile_bench():
    du = shader_text('class1/deferred/deferredUtil.glsl').replace('uniform int classic_mode;', '// classic_mode: declared by atmosphericsFuncs.glsl')
    tm = shader_text('class1/deferred/tonemapUtilF.glsl').replace('uniform vec2 screen_res;', '// screen_res: declared by deferredUtil.glsl')
    tm = tm.replace('in vec2 vary_fragcoord;', 'vec2 vary_fragcoord = vec2(0.0);')
    return atmosphere_program(MAIN, extra=du + DEFERRED_STUBS + tm)


class Bench:
    def __init__(self):
        self.prog = compile_bench()
        self.lut = linear_texture((1.0, 0.0))
        self.gobo, self.gobo_n = gobo_texture()
        use(self.prog)
        uniform(self.prog, 'brdfLut', 0); uniform(self.prog, 'projectionMap', 1)

    def set_light(self, ex):
        p = self.prog
        use(p)
        worn = ex.get('worn', False)
        r = ex['radius']; f = ex['falloff']
        uniform(p, 'color', *ex['color'])
        uniform(p, 'size', r * 1.5)                      # pipeline.cpp: getLightRadius() * 1.5
        uniform(p, 'falloff', f * 0.5)                   # DEFERRED_LIGHT_FALLOFF
        uniform(p, 'l_falloff', f)
        uniform(p, 'lp', *ex['pos'])
        uniform(p, 's_phys', ex['size'])
        uniform(p, 'e_cap', E_CAP_ATT if worn else E_CAP_PRIM)
        uniform(p, 'r_reach', min(r * 1.5, R_CAP_ATT) if worn else r * 1.5)
        uniform(p, 'classic_mode', 0)
        uniform(p, 'is_proj', 1 if ex.get('proj') else 0)
        if ex.get('proj'):
            sp = setup_projector(ex['pos'], ex['axis'], ex.get('up', [0, 0, 1]), ex['scale'], ex['fov'], r)
            ex['_sp'] = sp
            set_mat4(p, 'proj_mat', sp['proj'])
            uniform(p, 'proj_p', *sp['proj_p']); uniform(p, 'proj_n', *sp['proj_n'])
            uniform(p, 'proj_focus', float(ex['focus'])); uniform(p, 'proj_range', sp['range'])
            uniform(p, 'proj_ambiance', float(ex['ambiance'])); uniform(p, 'proj_lod', math.log2(self.gobo_n))
            uniform(p, 'apex', *sp['origin']); uniform(p, 'l_axis', *sp['at'])
            uniform(p, 'tan_half_fov', sp['tan_half']); uniform(p, 'aspect', sp['aspect'])
            uniform(p, 'focus_m', float(ex['focus'])); uniform(p, 'ambiance_v', float(ex['ambiance']))
            uniform(p, 'tex_size', float(self.gobo_n))
        active(0x84C0); bindtex(TEX2D, self.lut)
        active(0x84C1); bindtex(TEX2D, self.gobo)
        active(0x84C0)

    def curve(self, ex, dists, exposure=1.0):
        self.set_light(ex)
        p = self.prog
        tgt = Target(len(dists), 3); tgt.bind(); use(p)
        uniform(p, 'mode', 0); uniform(p, 'amb', 0.0); uniform(p, 'exposure_u', float(exposure))
        arr = (F * 32)(*(list(dists) + [0.0] * (32 - len(dists))))
        uniform_1fv(_loc(p, b'dist_list'), 32, C.cast(arr, P))
        draw(0x0004, 0, 3)
        return tgt.read(0), tgt.read(1), tgt.read(2)

    def image(self, ex, w, h, exposure=1.0):
        self.set_light(ex)
        p = self.prog
        tgt = Target2D(w, h, 3); tgt.bind(); use(p)
        uniform(p, 'mode', 1); u2f(_loc(p, b'img_res'), float(w), float(h))
        uniform(p, 'cam_pos', *ex['cam'][0]); uniform(p, 'cam_fwd', *norm(sub(ex['cam'][1], ex['cam'][0])))
        uniform(p, 'cam_up', 0.0, 0.0, 1.0); uniform(p, 'cam_tanfov', math.tan(math.radians(ex.get('cam_fov', 60)) * 0.5))
        uniform(p, 'room', *ex['room']); u4f(_loc(p, b'head'), *[float(x) for x in ex.get('head', [0, 0, 0, 0])])
        uniform(p, 'amb', float(ex.get('amb', 0.02))); uniform(p, 'exposure_u', float(exposure))
        draw(0x0004, 0, 3)
        return tgt.read(0), tgt.read(1), tgt.read(2)


class Target2D(Target):
    def __init__(self, w, h, count=1):
        self.n, self.w, self.h, self.count = w * h, w, h, count
        self.tex = []
        self.fbo = U(); genfbo(1, C.byref(self.fbo)); bindfbo(0x8D40, self.fbo.value)
        for i in range(count):
            t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value)
            texstorage2(TEX2D, 1, RGBA32F, w, h)
            texparam(TEX2D, 0x2801, 0x2600); texparam(TEX2D, 0x2800, 0x2600)
            attach2(0x8D40, 0x8CE0 + i, TEX2D, t.value, 0)
            self.tex.append(t.value)
        bufs = (U * count)(*[0x8CE0 + i for i in range(count)]); drawbuffers(count, bufs)

    def bind(self):
        bindfbo(0x8D40, self.fbo.value); viewport(0, 0, self.w, self.h)


def write_png(path, w, h, rows_rgb):
    raw = bytearray()
    for y in range(h - 1, -1, -1):
        raw.append(0)
        for x in range(w):
            r, g, b = rows_rgb[y * w + x]
            raw += bytes((max(0, min(255, int(r * 255 + 0.5))), max(0, min(255, int(g * 255 + 0.5))), max(0, min(255, int(b * 255 + 0.5)))))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    Path(path).write_bytes(png)


def lin(c):  # sRGB swatch -> linear, as LLLightParams::setSRGBColor
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


# ------------------------------------------------------------------ the worked examples (study §3.4)
EXAMPLES = [
    dict(key='room_lamp', name='Room lamp (prim point light)', radius=6.0, falloff=0.75, intensity=1.0,
         color=lin([1.0, 0.85, 0.65]), pos=[0.0, 1.2, 1.6], size=0.10,
         cam=([0.0, -3.4, 1.6], [0.0, 1.0, 1.0]), room=[2.5, 2.5, 2.8], head=[0.8, 2.0, 1.3, 0.12], amb=0.02),
    dict(key='candle', name='Candle, flicker script at its brightest (intensity 1.0)', radius=3.0, falloff=1.5, intensity=1.0,
         color=lin([1.0, 0.65, 0.3]), pos=[0.0, 0.8, 0.85], size=0.02,
         cam=([0.0, -2.2, 1.3], [0.0, 0.8, 0.6]), room=[2.5, 2.5, 2.8], head=[0.45, 1.4, 1.05, 0.12], amb=0.01),
    dict(key='candle_dim', name='Candle, flicker script at its dimmest (intensity 0.6)', radius=3.0, falloff=1.5, intensity=0.6,
         color=lin([1.0, 0.65, 0.3]), pos=[0.0, 0.8, 0.85], size=0.02,
         cam=([0.0, -2.2, 1.3], [0.0, 0.8, 0.6]), room=[2.5, 2.5, 2.8], head=[0.45, 1.4, 1.05, 0.12], amb=0.01),
    dict(key='club_spot', name='Club spotlight (projector, 8 m up, FOV 0.4, focus 8 m)', radius=20.0, falloff=0.0, intensity=1.0,
         color=[1.0, 1.0, 1.0], pos=[0.0, 2.0, 8.0], size=0.15, proj=True, axis=norm([0.0, 0.35, -1.0]), up=[0, 1, 0],
         scale=[0.3, 0.3, 0.3], fov=0.4, focus=8.0, ambiance=0.2,
         cam=([2.2, 0.0, 1.8], [0.0, 4.8, 0.6]), cam_fov=60, room=[10.0, 8.0, 0.0], head=[0.5, 4.4, 1.6, 0.12], amb=0.02),
    dict(key='face_light', name='Face light (worn, 0.4 m in front of the face)', radius=3.0, falloff=0.5, intensity=1.0,
         color=[1.0, 1.0, 1.0], pos=[0.0, -0.4, 1.72], size=0.02, worn=True,
         cam=([0.9, -1.4, 1.75], [0.0, 0.0, 1.6]), cam_fov=40, room=[2.5, 2.5, 2.8], head=[0.0, 0.0, 1.7, 0.12], amb=0.03),
]

DISTS = [0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 15.0, 20.0, 25.0]


def main():
    b = Bench()
    print()
    print('E on a surface facing the light, in U (1 U ~ clear PBR noon sun on the ground, %.2f U; classic Default sky ~%.1f U),' % (SUN_PBR, SUN_CLASSIC))
    print('and the displayed sRGB value (0-255) of a white panel (albedo 0.9, roughness 1) facing the light, ACES at exposure 1.')
    print('Projector rows are on the beam axis. Light colour is left out of E (white).')
    print()
    for ex in EXAMPLES:
        ex = dict(ex)
        ex['color'] = [c * ex['intensity'] for c in ex['color']]
        col = ex['color']
        white = dict(ex); white['color'] = [ex['intensity']] * 3
        today, prop, es = b.curve(white, DISTS)
        print(f"### {ex['name']}")
        print()
        print(f"radius {ex['radius']} m, falloff {ex['falloff']}, intensity {ex['intensity']}, light size {ex['size']} m" +
              (f", FOV {ex['fov']}, focus {ex['focus']}, ambiance {ex['ambiance']}" if ex.get('proj') else '') +
              (", worn (attachment caps)" if ex.get('worn') else ''))
        print()
        print('| d (m) | ' + ' | '.join(f'{d:g}' for d in DISTS) + ' |')
        print('|---|' + '---|' * len(DISTS))
        k = ex['intensity']
        print('| E today (U) | ' + ' | '.join(f'{e[0] * k:.2f}' for e in es) + ' |')
        print('| E proposed (U) | ' + ' | '.join(f'{e[1] * k:.2f}' for e in es) + ' |')
        print('| white panel today | ' + ' | '.join(f'{t[0] * 255:.0f}' for t in today) + ' |')
        print('| white panel proposed | ' + ' | '.join(f'{t[0] * 255:.0f}' for t in prop) + ' |')
        print()
        if not NO_IMAGES:
            OUT_DIR.mkdir(parents=True, exist_ok=True)
            w, h = 480, 300
            t_img, p_img, _ = b.image(ex, w, h)
            rows = []
            gap = 6
            W = w * 2 + gap
            pix = [(0, 0, 0)] * (W * h)
            for y in range(h):
                for x in range(w):
                    pix[y * W + x] = t_img[y * w + x][:3]
                    pix[y * W + x + w + gap] = p_img[y * w + x][:3]
                for x in range(w, w + gap):
                    pix[y * W + x] = (0.5, 0.5, 0.5)
            write_png(OUT_DIR / f"{ex['key']}.png", W, h, pix)
    if not NO_IMAGES:
        print(f'Images (left: today, right: proposed): {OUT_DIR}')


# ------------------------------------------------------------------ the same formulas in Python (study §3.5, §4)
def today_E(d, r, f, i=1.0):
    """calcLegacyDistanceAttenuation x 3.25, size 1.5 r, falloff x 0.5 (pointLightF.glsl)."""
    x = d / (1.5 * r)
    if x >= 1.0:
        return 0.0
    fp = 0.5 * f
    a = 1.0 - min(max((x + fp) / (1.0 + fp), 0.0), 1.0)
    return 3.25 * 2.0 * a * a * i


def prop_E_py(d, r, f, i=1.0, s=0.02, cap=E_CAP_PRIM, reach_cap=None):
    """prop_point's E (the GLSL above), for the tables that need many configurations."""
    R = 1.5 * r if reach_cap is None else min(1.5 * r, reach_cap)
    x = d / R
    if x >= 1.0:
        return 0.0
    n = 4.0 / (1.0 + f)
    kappa = 3.25 * 2.0 / (30.0 * (1.0 + 0.5 * f) ** 2) / (1.0 - 2.0 / (n + 1.0) + 1.0 / (2.0 * n + 1.0))
    I = kappa * R * R * i
    se2 = max(s * s, I / cap)
    return I * (1.0 - x ** n) ** 2 / (d * d + se2)


def python_tables():
    print('### kappa(falloff) and window exponent n')
    print()
    print('| falloff | ' + ' | '.join(str(f) for f in (0, 0.5, 0.75, 1, 1.5, 2)) + ' |')
    print('|---|' + '---|' * 6)
    ks, ns = [], []
    for f in (0, 0.5, 0.75, 1, 1.5, 2):
        n = 4.0 / (1.0 + f)
        ns.append(n); ks.append(3.25 * 2.0 / (30.0 * (1.0 + 0.5 * f) ** 2) / (1.0 - 2.0 / (n + 1.0) + 1.0 / (2.0 * n + 1.0)))
    print('| kappa | ' + ' | '.join(f'{k:.3f}' for k in ks) + ' |')
    print('| n | ' + ' | '.join(f'{n:.2f}' for n in ns) + ' |')
    print()
    print('### Flux check: integral of E d^2 over the reach, proposed / today (s -> 0, no cap)')
    print()
    for f in (0, 0.75, 2):
        t = sum(today_E((k + 0.5) * 0.001, 10, f) * ((k + 0.5) * 0.001) ** 2 for k in range(15000))
        q = sum(prop_E_py((k + 0.5) * 0.001, 10, f, s=0.0, cap=1e30) * ((k + 0.5) * 0.001) ** 2 for k in range(15000))
        print(f'- falloff {f}: {q / t:.4f}')
    print()
    print('### Prim cap dial: room lamp (r 6, falloff 0.75, size 0.1 m), E in U')
    print()
    print('| E_cap | 0.25 m | 1 m | 2 m | 4 m | E(1 m)/E(2 m) |')
    print('|---|---|---|---|---|---|')
    t = [today_E(d, 6, 0.75) for d in (0.25, 1, 2, 4)]
    print('| today | ' + ' | '.join(f'{e:.2f}' for e in t) + f' | {t[1] / t[2]:.2f} |')
    for cap in (6.5, 13.0, 26.0, 1e30):
        e = [prop_E_py(d, 6, 0.75, s=0.1, cap=cap) for d in (0.25, 1, 2, 4)]
        name = 'none' if cap > 1e20 else f'{cap:g}'
        print(f'| {name} | ' + ' | '.join(f'{x:.2f}' for x in e) + f' | {e[1] / e[2]:.2f} |')
    print()
    print('### Worn lights: E today -> proposed (cap %.1f U, reach %.0f m), U' % (E_CAP_ATT, R_CAP_ATT))
    print()
    ds = (0.3, 0.5, 1, 2, 4, 8)
    print('| light | ' + ' | '.join(f'{d:g} m' for d in ds) + ' |')
    print('|---|' + '---|' * len(ds))
    for r, f in ((1, 0), (3, 0.5), (10, 0.75), (20, 0)):
        print(f'| r {r}, falloff {f} | ' + ' | '.join(
            f'{today_E(d, r, f):.2f} -> {prop_E_py(d, r, f, cap=E_CAP_ATT, reach_cap=R_CAP_ATT):.2f}' for d in ds) + ' |')
    print()
    print('### Candle flicker with radius jitter: bright (r 3.5, i 1.0) / dim (r 2.5, i 0.6)')
    print()
    for d in (0.5, 1.0, 2.0):
        t = today_E(d, 3.5, 1.5, 1.0) / today_E(d, 2.5, 1.5, 0.6)
        q = prop_E_py(d, 3.5, 1.5, 1.0) / prop_E_py(d, 2.5, 1.5, 0.6)
        print(f'- {d:g} m: today {t:.2f}, proposed {q:.2f}')
    print()
    print('### Cap size floor s_eff = sqrt(I / E_cap), falloff 0, intensity 1')
    print()
    for r in (1, 5, 10, 20):
        R = 1.5 * r
        I = 3.25 * 2.0 / 30.0 / (1.0 - 2.0 / 5.0 + 1.0 / 9.0) * R * R
        print(f'- r {r}: {math.sqrt(I / E_CAP_PRIM):.2f} m')
    print()


main()
python_tables()
