# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Compile check (no link, no viewer) of the four local light pass fragment shaders of unified lighting U1, with and without
the LOCAL_LIGHT_RATIO permutation (the Stochastic method's / the Legacy method's build), against a stub of the G-buffer
declarations deferredUtil.glsl and gbufferUtil.glsl provide. A syntax or type error in the passes shows here."""
import sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
DEEP = root / 'indra/newview/app_settings/shaders/class3/deferred'
STUB = '''
struct GBufferInfo { vec3 normal; vec4 albedo; vec4 specular; vec4 emissive; float envIntensity; int gbufferFlag; };
#define GBUFFER_FLAG_HAS_PBR 1
#define GET_GBUFFER_FLAG(f, b) ((f & b) != 0)
'''
cases = [('pointLightF.glsl', []), ('multiPointLightF.glsl', ['LIGHT_COUNT=4']), ('spotLightF.glsl', []), ('spotLightF.glsl', ['MULTI_SPOTLIGHT'])]
bad = 0
for name, extra in cases:
    for ratio in (True, False):
        defines = ['FRAGMENT_SHADER'] + extra + (['LOCAL_LIGHT_RATIO'] if ratio else [])
        head = '#version 430 core\n' + ''.join('#define ' + (d.replace('=', ' ') if '=' in d else d + ' 1') + '\n' for d in defines) + STUB
        text = head + (DEEP / name).read_text().replace('/*[EXTRA_CODE_HERE]*/', '')
        shader = create_shader(0x8B30); source = C.c_char_p(text.encode()); source_shader(shader, 1, C.byref(source), None); compile_shader(shader)
        status = I(); shader_iv(shader, 0x8B81, C.byref(status))
        log = C.create_string_buffer(8192); shader_log(shader, len(log), None, log)
        label = f'{name} {" ".join(extra)} {"with" if ratio else "without"} LOCAL_LIGHT_RATIO'
        print(('ok   ' if status.value else 'FAIL ') + label)
        if not status.value:
            print(log.value.decode()); bad += 1
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
sys.exit(1 if bad else 0)
