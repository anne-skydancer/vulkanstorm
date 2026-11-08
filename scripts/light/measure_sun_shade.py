# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage L1 of the light study (doc/light/light-model-study.md, D6): today's sun-to-shade ratio and
the colours on a white and a grey PBR panel, half in sun and half in shadow, under three EEP skies.

No viewer launch. The viewer's own shader code is compiled in a hidden OpenGL context and run on
one panel pixel at a time:
  * `calcAtmosphericVarsLinear` (atmosphericsFuncs.glsl, unmodified) gives sunlit and amblit;
  * softenLightF.glsl's PBR branch is repeated line for line around `pbrBaseLight`, `pbrPunctual`
    and `pbrIbl` from deferredUtil.glsl (unmodified): `sunlit *= 1.35` in classic mode, the
    irradiance, `pbrBaseLight`, `final_scale` 1.1 in classic mode;
  * the panel is a horizontal PBR surface (metallic 0, roughness 1, ao 1), seen from 35 degrees
    above its plane. "In sun" is scol = 1, "in shadow" scol = 0.

What the shade is lit with depends on the sky:
  * a classic sky (every sky file shipped with the viewer: no reflection probe ambiance) takes the
    sky's `amblit` (reflectionProbeF.glsl doProbeSample: `if (classic_mode == 0)` samples the
    probes). Its `pbrBaseLight` branch is the classic one.
  * a PBR sky (reflection_probe_ambiance 1) takes the sky probe's irradiance,
    mix(amblit, E * ambiance, min(ambiance, 1)). E is computed here as the viewer's irradiance
    capture sees the sky: the sky dome (skyV.glsl haze colour, skyF.glsl x2 and clamp 5, decoded
    to linear and scaled by sky_hdr_scale = 2 sqrt(gamma)) with the capture's ambient uniform at 0
    and blue_horizon / blue_density reduced to their HSL lightness (RenderDesaturateIrradiance),
    cosine-convolved over the upper hemisphere (irradianceGenF.glsl: cosine-weighted, so E is
    radiance-like, the mean cosine-weighted radiance). Only the sky is in E: the ground and the
    scene that the real capture also holds are not, and no sun disc, rainbow or halo.
Also given for a PBR sky: the fallback without a probe, E replaced by `amblit`.

    python scripts/light/measure_sun_shade.py [--markdown]

Needs Windows and a GPU. Numbers are linear HDR before exposure and tone mapping (the value
softenLightF.glsl writes), so they are what the lighting pass produces and not what the screen shows.
"""
import math
import sys
from pathlib import Path
from urllib.parse import unquote

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atmosphere_gl import *  # noqa: E402,F401,F403

SKY_DIR = SHADERS.parent / 'windlight' / 'skies'

PANEL_MAIN = """
uniform float albedo;
uniform float scol;
uniform int use_probe;
uniform vec3 probe_irradiance;
uniform float probe_ambiance;
uniform vec3 light_dir_u;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_light;
void main() {
    vec3 n = vec3(0.0, 0.0, 1.0);
    vec3 light_dir = normalize(light_dir_u);
    vec3 v = normalize(vec3(-0.819152, 0.0, 0.573576));
    vec3 sunlit, amblit, additive, atten;
    calcAtmosphericVarsLinear(vec3(0.0, 0.0, -100.0), n, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    vec3 sunlit_linear = sunlit;
    vec3 amblit_linear = amblit;
    vec3 irradiance = amblit_linear;
    if (use_probe > 0)
        irradiance = mix(amblit_linear, probe_irradiance * probe_ambiance, min(probe_ambiance, 1.0));
    vec3 diffuseColor, specularColor;
    calcDiffuseSpecular(vec3(albedo), 0.0, diffuseColor, specularColor);
    vec3 color = pbrBaseLight(diffuseColor, specularColor, 0.0, v, n, 1.0, light_dir, sunlit_linear, scol,
                              vec3(0.0), irradiance, vec3(0.0), 1.0, additive, atten);
    float final_scale = classic_mode > 0 ? 1.1 : 1.0;
    out_color = vec4(color * final_scale, 1.0);
    out_light = vec4(sunlit_linear, irradiance.r); // for the table: the light terms alone
}
"""

DEFERRED_STUBS = """
vec4 decodeNormal(vec4 n) { return n; }
vec3 atmosFragLightingLinear(vec3 light, vec3 additive, vec3 atten) { return light; }
"""

SKY_MAIN = """
uniform int n_az;
uniform int n_el;
uniform float sky_hdr_scale;
layout(location = 0) out vec4 out_sky;
void main() {
    int i = int(gl_FragCoord.x);
    int a = i % n_az, e = i / n_az;
    float phi = (float(a) + 0.5) / float(n_az) * 6.28318530718;
    float th = (float(e) + 0.5) / float(n_el) * 1.57079632679;
    vec3 dir = vec3(sin(th) * cos(phi), cos(th), sin(th) * sin(phi));
    // skyV.glsl main(), for the view direction `dir` (y up)
    vec3 rel_pos = dir;
    if (rel_pos.y > 0.) rel_pos *= (max_y / rel_pos.y);
    vec3 rel_pos_norm = normalize(rel_pos);
    float rel_pos_len = length(rel_pos);
    float rel_pos_lightnorm_dot = dot(rel_pos_norm, lightnorm.xyz);
    vec3 sunlight = (sun_up_factor == 1) ? sunlight_color : moonlight_color * 0.7;
    vec3 light_atten = (blue_density + vec3(haze_density * 0.25)) * (density_multiplier * max_y);
    vec3 combined_haze = max(abs(blue_density) + vec3(abs(haze_density)), vec3(1e-6));
    vec3 blue_weight = blue_density / combined_haze;
    vec3 haze_weight = haze_density / combined_haze;
    float off_axis = 1.0 / max(1e-6, max(0., rel_pos_norm.y) + lightnorm.y);
    sunlight *= exp(-light_atten * off_axis);
    float density_dist = rel_pos_len * density_multiplier;
    combined_haze = exp(-combined_haze * density_dist);
    float haze_glow = 1.0 - rel_pos_lightnorm_dot;
    haze_glow = max(haze_glow, .001);
    haze_glow *= glow.x;
    haze_glow = pow(haze_glow, glow.z);
    haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (sun_moon_glow_factor * (haze_glow + 0.25));
    vec3 color = (blue_horizon * blue_weight * (sunlight + ambient_color)
               + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient_color));
    color *= (1. - combined_haze);
    vec3 ambient = ambient_color + max(vec3(0), (1. - ambient_color)) * cloud_shadow * 0.5;
    sunlight *= max(0.0, (1. - cloud_shadow));
    vec3 add_below_cloud = (blue_horizon * blue_weight * (sunlight + ambient)
                         + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient));
    combined_haze = sqrt(combined_haze);
    color += (add_below_cloud - color) * (1. - sqrt(combined_haze));
    // skyF.glsl main(): x2, clamp, then softenLightF.glsl: to linear, x sky_hdr_scale
    color *= 2.;
    color = clamp(color, vec3(0), vec3(5));
    out_sky = vec4(srgb_to_linear(color) * sky_hdr_scale, th);
}
"""


def hsl_lightness(c):
    return (max(c) + min(c)) / 2.0


def sky_params(file_name, **over):
    p = load_legacy_sky(SKY_DIR / file_name)
    p.update(sky_sunlight_scale=1.0, sky_ambient_scale=1.0, classic_mode=1, moonlight_color=p['sunlight_color'],
             sun_moon_glow_factor=1.0)
    p.update(over)
    return p


def sky_irradiance(p, n_az=256, n_el=96):
    """Cosine-weighted mean sky radiance on an up-facing surface, as the irradiance capture sees the sky."""
    q = dict(p)
    q['ambient'] = [0.0, 0.0, 0.0]
    q['blue_horizon'] = [hsl_lightness(p['blue_horizon'])] * 3
    q['blue_density'] = [hsl_lightness(p['blue_density'])] * 3
    prog = atmosphere_program(SKY_MAIN)
    tgt = Target(n_az * n_el)
    tgt.bind(); use(prog); set_atmosphere_uniforms(prog, q)
    uniform(prog, 'n_az', n_az); uniform(prog, 'n_el', n_el)
    uniform(prog, 'sky_hdr_scale', 2.0 * math.sqrt(p['gamma']))
    draw(0x0004, 0, 3)
    px = tgt.read(0)
    acc = [0.0, 0.0, 0.0]
    dth, dphi = (math.pi / 2) / n_el, 2 * math.pi / n_az
    for r, g, b, th in px:
        w = math.cos(th) * math.sin(th) * dth * dphi / math.pi
        acc[0] += r * w; acc[1] += g * w; acc[2] += b * w
    return acc


class Panel:
    def __init__(self):
        self.prog = atmosphere_program(PANEL_MAIN, extra=shader_text('class1/deferred/deferredUtil.glsl').replace(
            'uniform int classic_mode;', '// classic_mode: declared by atmosphericsFuncs.glsl') + DEFERRED_STUBS)
        self.tgt = Target(1, 2)
        self.lut = linear_texture((1.0, 0.0))
        use(self.prog)
        uniform(self.prog, 'brdfLut', 0)
        active(0x84C0); bindtex(TEX2D, self.lut)

    def shade(self, p, albedo, scol, probe=None):
        self.tgt.bind(); use(self.prog); set_atmosphere_uniforms(self.prog, p)
        ly = p['light_y']
        uniform(self.prog, 'light_dir_u', math.sqrt(max(0.0, 1 - ly * ly)), 0.0, ly)
        uniform(self.prog, 'albedo', albedo); uniform(self.prog, 'scol', scol)
        uniform(self.prog, 'use_probe', 1 if probe is not None else 0)
        irr = probe or (0.0, 0.0, 0.0)
        uniform(self.prog, 'probe_irradiance', *irr); uniform(self.prog, 'probe_ambiance', float(p.get('probe_ambiance', 0.0)))
        active(0x84C0); bindtex(TEX2D, self.lut)
        draw(0x0004, 0, 3)
        return self.tgt.read(0)[0][:3], self.tgt.read(1)[0][:3]


def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


SKIES = [
    ('1 Default (classic)', 'Default.xml', {}),
    ('2 Clear PBR sky', 'Default.xml', dict(cloud_shadow=0.0, classic_mode=0, probe_ambiance=1.0)),
    ('3 Overcast creator sky', "Nacon's Natural Noon.xml", {}),
    ('3b Same, as a PBR sky', "Nacon's Natural Noon.xml", dict(classic_mode=0, probe_ambiance=1.0)),
]


def main():
    panel = Panel()
    rows = []
    for name, file_name, over in SKIES:
        fname = file_name.replace("'", '%27').replace(' ', '%20')
        if not (SKY_DIR / fname).exists():
            fname = [f for f in __import__('os').listdir(SKY_DIR) if unquote(f) == file_name][0]
        base = sky_params(fname, **over)
        pbr = base['classic_mode'] == 0
        irr = sky_irradiance(base) if pbr else None
        for elev in (None, 60.0, 30.0):
            p = dict(base)
            if elev is not None:
                p['light_y'] = math.sin(math.radians(elev))
            label = f"{math.degrees(math.asin(p['light_y'])):.0f}" + (' (sky file)' if elev is None else '')
            for panel_name, albedo in (('white 0.9', 0.9), ('grey 0.18', 0.18)):
                lit, lterms = panel.shade(p, albedo, 1.0, irr)
                shade, sterms = panel.shade(p, albedo, 0.0, irr)
                row = dict(sky=name, elev=label, panel=panel_name, lit=lit, shade=shade, sunlit=lterms[0], irr=sterms[0:3])
                row['ratio'] = lum(lit) / max(lum(shade), 1e-9)
                row['sun_only'] = (lum(lit) - lum(shade)) / max(lum(shade), 1e-9)
                row['shade_bluish'] = shade[2] / max(shade[0], 1e-9)
                row['lit_bluish'] = lit[2] / max(lit[0], 1e-9)
                if pbr:
                    nolit, _ = panel.shade(p, albedo, 1.0, None)
                    noshade, _ = panel.shade(p, albedo, 0.0, None)
                    row['fallback_ratio'] = lum(nolit) / max(lum(noshade), 1e-9)
                rows.append(row)
    print()
    print('| Sky | Sun elevation (deg) | Panel | Lit R G B | Shade R G B | Lit / shade (luminance) | Sun only / shade | Shade B/R | Lit B/R | PBR fallback lit / shade |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for r in rows:
        print(f"| {r['sky']} | {r['elev']} | {r['panel']} | {r['lit'][0]:.3f} {r['lit'][1]:.3f} {r['lit'][2]:.3f} | "
              f"{r['shade'][0]:.3f} {r['shade'][1]:.3f} {r['shade'][2]:.3f} | {r['ratio']:.2f} | {r['sun_only']:.2f} | "
              f"{r['shade_bluish']:.2f} | {r['lit_bluish']:.2f} | {r.get('fallback_ratio', float('nan')):.2f} |")


main()
