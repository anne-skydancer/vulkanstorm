#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Reference values for the AT2 qualification's upstream twin (scripts/light/at2_qual.py --anchor): UPSTREAM Firestorm's sky pass run on the GPU.

Upstream's class1/deferred/skyF.glsl (the whole sky; its skyV.glsl only passes the position on) is read with `git show upstream/master:...`
into a scratch folder (the temp folder, never the repository tree), compiled with upstream's srgbF.glsl and drawn one pixel per
direction with the uniforms llsettingsvo.cpp (upstream) sets: lightnorm clamped at y -0.1, sun_up_factor, sun_moon_glow_factor, the
moonlight colour (= the sunlight colour) and the sky's own values. The moisture and ice levels are 0 (the library skies carry none), so the
rainbow and the halo add nothing and the two lookup textures need no content. The output is frag_data[0], the sky colour before the lighting
pass (x2, clamp 5, still sRGB-encoded); the lighting pass's own factors (srgb_to_linear, sky_hdr_scale, final_scale 1.1, clamp 11.2) are
plain arithmetic and are applied by the twin and checked by reading, not here.

Needs Windows and a GPU; run only when no viewer or build is running.

    python scripts/light/at2_qual_anchor_gpu.py            # prints the ANCHOR table for scripts/light/at2_qual.py
"""
import math
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
argv, sys.argv = sys.argv, sys.argv[:1]
import bench_atmosphere as b  # noqa: E402

UPSTREAM = 'upstream/master'
SKY_PATHS = ['indra/newview/app_settings/shaders/class1/deferred/skyF.glsl',
             'indra/newview/app_settings/shaders/class1/environment/srgbF.glsl']
# library skies (file names) and, per sky, the sample directions: (label, elevation, azimuth from the light), where the light is the
# sun by day and the moon by night. 'sun' entries are relative: elevation = the light's elevation + the offset.
SKIES = ['Default.xml', 'Sunset.xml', 'Foggy.xml', '[TOR] MIDDAY - Salmon.xml', '[TOR] MIDDAY - Pleasantrie.xml',
         'Alchemy Immortalis - Fog Lifting.xml', '[TOR] NIGHT - That spells moon 1.xml']
SAMPLES = [('zenith', 90.0, 0.0), ('horizon', 2.0, 90.0), ('mid', 45.0, 135.0), ('near-light', None, 14.75), ('light-core', None, 0.0)]
LIGHT_CORE_OFFSET = 0.4   # degrees above the light, in the flat zone of haze_glow (acos(0.999) = 2.56 degrees)

VS = """#version 400 compatibility
uniform vec3 dir;
out vec3 pos;
void main() { pos = dir * 15000.0; gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
"""


def upstream_fs(tmp):
    texts = []
    for p in SKY_PATHS:
        t = subprocess.run(['git', 'show', f'{UPSTREAM}:{p}'], capture_output=True, text=True, check=True,
                           cwd=b.ROOT, encoding='utf8').stdout
        (tmp / Path(p).name).write_text(t, encoding='utf8')
        texts.append(t)
    # GBUFFER_FLAG_SKIP_ATMOS is a define the shader manager injects (llshadermgr.cpp)
    return '#version 400 compatibility\n#define GBUFFER_FLAG_SKIP_ATMOS 0.0\n' + texts[1] + texts[0]


def light_of(p):
    """The light as upstream's LLSettingsVOSky::applySpecial sends it: the sun when it is up, else the moon (opposite the sun in a
    Windlight sky). Returns (elevation radians, sun_up)."""
    return p['light_el'], p['sun_up']


def main():
    with tempfile.TemporaryDirectory(prefix='at2_qual_up_') as tmp:
        fs = upstream_fs(Path(tmp))
    prog = b.build([(b.VERT, VS), (b.FRAG, fs)])
    tgt = b.Target(1, 1)
    for name in SKIES:
        p = b.load_sky(b.sky_file(name))
        le, sun_up = light_of(p)
        ln_y = max(math.sin(le), -0.1)
        ln = (math.cos(le), ln_y, 0.0)
        nl = math.sqrt(sum(x * x for x in ln))
        ln = tuple(x / nl for x in ln)
        gf = 1.0 if sun_up else p['moon_brightness'] * 0.25
        for label, el, az in SAMPLES:
            if el is None:
                el = math.degrees(le) + (LIGHT_CORE_OFFSET if label == 'light-core' else 0.0)
            if label == 'near-light':
                el = math.degrees(le)
            tgt.bind()
            b.use(prog)
            for u, v in (('lightnorm', ln), ('sunlight_color', p['sunlight_color']), ('moonlight_color', p['sunlight_color']),
                         ('ambient_color', p['ambient']), ('blue_horizon', p['blue_horizon']), ('blue_density', p['blue_density']),
                         ('glow', p['glow']), ('camPosLocal', (0.0, 0.0, 0.0))):
                b.uniform(prog, u, *v)
            for u, v in (('haze_horizon', p['haze_horizon']), ('haze_density', p['haze_density']), ('cloud_shadow', p['cloud_shadow']),
                         ('density_multiplier', p['density_multiplier']), ('distance_multiplier', p['distance_multiplier']),
                         ('max_y', p['max_y']), ('sun_moon_glow_factor', gf), ('moisture_level', 0.0), ('droplet_radius', 0.0),
                         ('ice_level', 0.0)):
                b.uniform(prog, u, float(v))
            b.uniform(prog, 'sun_up_factor', int(sun_up))
            e, a = math.radians(el), math.radians(az)
            b.uniform(prog, 'dir', math.cos(e) * math.cos(a), math.sin(e), math.cos(e) * math.sin(a))
            b.draw(0, 0, 1)
            r = tgt.read(0)[0]
            print(f'{name}\t{label}\tlight_el {math.degrees(le):.3f}\tel {el:.3f}\taz {az:.2f}\tfrag {r[0]:.6f} {r[1]:.6f} {r[2]:.6f}')


if __name__ == '__main__':
    main()
