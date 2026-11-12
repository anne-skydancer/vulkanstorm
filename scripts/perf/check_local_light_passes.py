# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Compile check (no viewer) of the four local light pass fragment shaders of unified lighting U1, with and without the
LOCAL_LIGHT_RATIO permutation (the Stochastic method's / the Legacy method's build), against a stub of the G-buffer
declarations deferredUtil.glsl and gbufferUtil.glsl provide. A syntax or type error in the passes shows here.

Then the same four passes are linked with the real objects the viewer attaches to them (the vertex shader, deferredUtil, gbufferUtil,
srgbF, atmosphericsVarsF, and with the Stochastic permutation the shadow util with the common block and the noise), and the sampler
uniforms the linked program keeps are counted: the question is whether the added `local_light_ratio` sampler keeps a pass within
16 texture image units (the minimum of GL 4.x; the limit the driver reports is printed). Stub definitions stand for what the
viewer's other attachments define (the atmospherics' functions, the probes)."""
import re, sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
DEEP = root / 'indra/newview/app_settings/shaders/class3/deferred'
STUB = ('struct GBufferInfo { vec4 albedo; vec4 specular; vec3 normal; vec4 emissive; float gbufferFlag; float envIntensity; };' + chr(10) +   # as LLShaderMgr::loadShaderFile adds to every shader
        '#define GBUFFER_FLAG_SKIP_ATMOS 0.0' + chr(10) + '#define GBUFFER_FLAG_HAS_ATMOS 0.34' + chr(10) + '#define GBUFFER_FLAG_HAS_PBR 0.67' + chr(10) +
        '#define GBUFFER_FLAG_HAS_HDRI 1.0' + chr(10) + '#define GET_GBUFFER_FLAG(data, flag) (abs(data-flag)< 0.1)' + chr(10))
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

# --- link the passes with the objects the viewer attaches, and count the samplers -----------------------
SH = root / 'indra/newview/app_settings/shaders'
get_integer = fn('glGetIntegerv', None, U, C.POINTER(I))
units = I(); get_integer(0x8872, C.byref(units))   # GL_MAX_TEXTURE_IMAGE_UNITS
print(f'this driver reports {units.value} fragment texture image units; the minimum to stay within is 16')
get_active = fn('glGetActiveUniform', None, U, U, I, C.POINTER(I), C.POINTER(I), C.POINTER(U), C.c_char_p)
S2 = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON', 'SHADOW_COMMON_NO_DEPTH']
consts = {m.group(1): m.group(2) for m in re.finditer(r'constexpr float (SHADOW_\w+) = ([0-9.]+)f;', (root / 'indra/newview/llshadowcascadefit.h').read_text())}
S2 += [f'{k}={v}' for k, v in consts.items()]
def stage(text, defines):
    head = '#version 430 core' + chr(10) + '#define FRAGMENT_SHADER 1' + chr(10) + ''.join('#define ' + (d.replace('=', ' ') if '=' in d else d + ' 1') + chr(10) for d in defines)
    return head + STUB + text.replace('/*[EXTRA_CODE_HERE]*/', '')
def src(rel, defines): return stage((SH / rel).read_text(), defines)
def sampler_count(prog):
    n, count, names = I(), 0, []
    program_iv(prog, 0x8B86, C.byref(n))
    for i in range(n.value):
        size, kind, name = I(), U(), C.create_string_buffer(128)
        get_active(prog, i, 128, None, C.byref(size), C.byref(kind), name)
        if 0x8B5E <= kind.value <= 0x8B62 or 0x8DC0 <= kind.value <= 0x8DD8 or kind.value in (0x8B5F, 0x8B60):   # sampler types
            count += size.value; names.append(name.value.decode())
    return count, names
LINK_CASES = [('pointLightF.glsl', 'pointLightV.glsl', []), ('multiPointLightF.glsl', 'multiPointLightV.glsl', ['LIGHT_COUNT=4']),
              ('spotLightF.glsl', 'pointLightV.glsl', []), ('spotLightF.glsl', 'multiPointLightV.glsl', ['MULTI_SPOTLIGHT'])]
for name, vertex, extra in LINK_CASES:
    for ratio in (False, True):
        defines = extra + (['LOCAL_LIGHT_RATIO'] if ratio else [])
        stages = [(0x8B31, src('class3/deferred/' + vertex, extra)), (0x8B30, src('class3/deferred/' + name, defines + extra)),
                  (0x8B30, src('class1/deferred/globalF.glsl', defines)), (0x8B30, src('class1/deferred/deferredUtil.glsl', defines)), (0x8B30, src('class1/deferred/gbufferUtil.glsl', defines)),
                  (0x8B30, src('class1/environment/srgbF.glsl', defines)), (0x8B30, src('class1/windlight/atmosphericsVarsF.glsl', defines))]
        if ratio:   # the Stochastic build: the shadow util with the common block (no depth) and the noise
            stages += [(0x8B30, stage((SH / 'class1/deferred/shadowUtil.glsl').read_text(), ['SHADOW_COMMON_ONLY'] + S2)),
                       (0x8B30, stage((SH / 'class1/deferred/shadowStochasticUtil.glsl').read_text(), S2)),
                       (0x8B30, stage((SH / 'class1/deferred/stochasticNoiseUtil.glsl').read_text(), []))]
        label = f'{name} {" ".join(extra)} {"Stochastic (with local_light_ratio)" if ratio else "Legacy"}'
        try:
            prog = program(stages)
            count, names = sampler_count(prog)
            ok = count <= 16
            print(('ok   ' if ok else 'FAIL ') + f'linked {label}: {count} sampler units (limit 16 for the minimum device)')
            if not ok:
                bad += 1
            fn('glDeleteProgram', None, U)(prog)
        except AssertionError as e:
            print('info could not link ' + label + ' with these attachments: ' + str(e).strip().splitlines()[-1][:160])
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
sys.exit(1 if bad else 0)
