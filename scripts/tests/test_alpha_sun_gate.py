# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The sun-shadow gate of the blended passes is image-exact (alpha-pass stage 1, option A), and the rigged depth
restore goes through the alpha-only variants (option B).

A CPU twin of the shaded sun term: the shadow value is read by sunShadowedDiffuse(da, shadow) (both of its
combines) and, in materialF, by the glossy specular term. The tests show that

  * alphaF and the non-glossy materialF give the same sun term with the march skipped when N.L <= 0, for every
    shadow in [0, 1] (and the soft filter's overshoot and NaN, which the SHADOW_NL_MULTIPLY combine clamps);
  * a glossy materialF is NOT gated: calcHalfVectors clamps nl and nh to 1e-6, so its back-face specular still
    depends on the shadow (the twin shows the dependence, which is why the shader gates on glossiness <= 0 only);

and the shader texts carry the gate where the twin says it is exact (and nowhere else).

    python3 scripts/tests/test_alpha_sun_gate.py"""
import math
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
SH = ROOT / 'indra/newview/app_settings/shaders'
ALPHA_F = SH / 'class2/deferred/alphaF.glsl'
MATERIAL_F = SH / 'class3/deferred/materialF.glsl'
PBR_ALPHA_F = SH / 'class2/deferred/pbralphaF.glsl'
STOCH_F = SH / 'class1/deferred/alphaStochasticF.glsl'
POOL = ROOT / 'indra/newview/lldrawpoolalpha.cpp'


def clamp(x, lo, hi):
    if math.isnan(x):   # GLSL leaves clamp(NaN) undefined; the twin propagates it
        return x
    return min(max(x, lo), hi)


def sun_shadowed_diffuse(da, shadow, multiply):
    if multiply:   # SHADOW_NL_MULTIPLY: a NaN reads as unshadowed, an overshoot is clamped
        return da * (1.0 if math.isnan(shadow) else clamp(shadow, 0.0, 1.0))
    return min(da, shadow)


def normalize(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sun_term_alpha(n, l, shadow_fn, gated, multiply, classic):
    """alphaF's sun contribution (before the sun colour), shadow_fn the march."""
    da = clamp(dot(n, l), -1.0, 1.0)
    final_da = clamp(da, 0.0, 1.0)
    shadow = 1.0
    if not gated or da > 0.0:
        shadow = shadow_fn()
    if classic:
        final_da = final_da ** 1.2
    return sun_shadowed_diffuse(final_da, shadow, multiply)


def material_sun(n, l, v, glossiness, shadow_fn, gated, multiply, classic):
    """materialF's sun diffuse + glossy specular scalar (spec colour, sun colour and the light function folded to 1)."""
    shadow = 1.0
    if not gated or glossiness > 0.0 or dot(n, l) > 0.0:
        shadow = shadow_fn()
    da = clamp(dot(n, l), 0.0, 1.0)
    if classic:
        da = da ** 1.2
    out = sun_shadowed_diffuse(da, shadow, multiply)
    if glossiness > 0.0:
        eps = 1e-6
        h = normalize(tuple(a + b for a, b in zip(l, v)))
        nh = clamp(dot(n, h), eps, 1.0)
        nl = clamp(dot(n, l), eps, 1.0)
        nv = clamp(dot(n, v), eps, 1.0)
        vh = clamp(dot(v, h), eps, 1.0)
        if nl > 0.0 and nh > 0.0:     # always true after the clamps
            lit = min(nl * 6.0, 1.0)
            gt = max(0.0, min(2 * nh * nv / vh, 2 * nh * nl / vh))
            scol = shadow * (pow(1 - vh, 5) * 0.4 + 0.5) * gt / (nh * nl)
            out += lit * scol
    return out


# The legacy combine, min(da, shadow), is exact only for shadow in [0, 1]: the march (a PCF/PCSS average of
# lit fractions) is never negative, and a NaN or an overshoot is only tolerated by the SHADOW_NL_MULTIPLY
# combine (usable() below), which clamps and reads a NaN as unshadowed. Negative and NaN shadows are tried
# for that combine only.
SHADOWS = [0.0, 0.1, 0.5, 0.999, 1.0, 1.0000001, -0.25, float('nan')]
NORMALS = [normalize(n) for n in [(0, 0, 1), (0, 0, -1), (1, 0, 0), (0.3, 0.2, -0.9), (-0.7, 0.1, 0.05),
                                   (0.001, 0, -1), (0.2, -0.5, 0.4), (1, 1, 1), (-1, -1, -1)]]
SUN = normalize((0.3, -0.2, 0.9))
# N.L exactly 0 and on either side of it by 1e-7 (the gate is da > 0.0, strict), for a sun straight overhead
SUN_Z = (0.0, 0.0, 1.0)
EDGE_NORMALS = [normalize(n) for n in [(1, 0, 0), (1, 0, 1e-7), (1, 0, -1e-7)]]
VIEW = normalize((0.1, 0.2, 1.0))


def same(a, b):
    return (math.isnan(a) and math.isnan(b)) or a == b


def usable(shadow, multiply):
    """min(da, NaN) is undefined in GLSL and the march never exceeds 1; the multiply combine clamps both."""
    return multiply or (0.0 <= shadow <= 1.0)


class SunGateTwin(unittest.TestCase):
    def test_alpha_f_gate_is_exact(self):
        for multiply in (False, True):
            for classic in (False, True):
                for n in NORMALS:
                    for s in SHADOWS:
                        if not usable(s, multiply):
                            continue
                        a = sun_term_alpha(n, SUN, lambda: s, False, multiply, classic)
                        b = sun_term_alpha(n, SUN, lambda: s, True, multiply, classic)
                        self.assertTrue(same(a, b), (n, s, multiply, classic, a, b))

    def test_gate_at_n_dot_l_zero(self):
        self.assertEqual(dot(EDGE_NORMALS[0], SUN_Z), 0.0)
        for multiply in (False, True):
            for classic in (False, True):
                for n in EDGE_NORMALS:
                    for s in SHADOWS:
                        if not usable(s, multiply):
                            continue
                        a = sun_term_alpha(n, SUN_Z, lambda: s, False, multiply, classic)
                        b = sun_term_alpha(n, SUN_Z, lambda: s, True, multiply, classic)
                        self.assertTrue(same(a, b), (n, s, multiply, classic, a, b))
                        c = material_sun(n, SUN_Z, VIEW, 0.0, lambda: s, False, multiply, classic)
                        d = material_sun(n, SUN_Z, VIEW, 0.0, lambda: s, True, multiply, classic)
                        self.assertTrue(same(c, d), (n, s, multiply, classic, c, d))
        # the march runs exactly where da > 0: not at 0 and -1e-7, at +1e-7
        for n, expected in zip(EDGE_NORMALS, (False, True, False)):
            calls = []
            sun_term_alpha(n, SUN_Z, lambda: calls.append(1) or 1.0, True, False, False)
            self.assertEqual(bool(calls), expected, n)

    def test_nan_da_skips_the_march_and_stays_nan(self):
        n = (float('nan'), 0.0, 1.0)
        calls = []
        a = sun_term_alpha(n, SUN, lambda: calls.append(1) or 0.5, True, True, False)
        self.assertFalse(calls)   # NaN > 0.0 is false
        b = sun_term_alpha(n, SUN, lambda: 0.5, False, True, False)
        self.assertTrue(same(a, b) and math.isnan(a))

    def test_gate_skips_back_faces_only(self):
        calls = []
        for n in NORMALS:
            calls.clear()
            sun_term_alpha(n, SUN, lambda: calls.append(1) or 1.0, True, False, False)
            self.assertEqual(bool(calls), dot(n, SUN) > 0.0)

    def test_material_non_glossy_gate_is_exact(self):
        for multiply in (False, True):
            for classic in (False, True):
                for n in NORMALS:
                    for s in SHADOWS:
                        if not usable(s, multiply):
                            continue
                        a = material_sun(n, SUN, VIEW, 0.0, lambda: s, False, multiply, classic)
                        b = material_sun(n, SUN, VIEW, 0.0, lambda: s, True, multiply, classic)
                        self.assertTrue(same(a, b), (n, s, a, b))

    def test_material_glossy_back_face_depends_on_the_shadow(self):
        """Why the glossy fragment is not gated: its back-face specular is shadow-dependent (1e-6 clamps)."""
        n = normalize((0.0, 0.0, -1.0))
        self.assertLess(dot(n, SUN), 0.0)
        lit = material_sun(n, SUN, VIEW, 0.5, lambda: 1.0, False, False, False)
        dark = material_sun(n, SUN, VIEW, 0.5, lambda: 0.0, False, False, False)
        self.assertNotEqual(lit, dark)
        # the shader's gate takes the march on every glossy fragment, front or back face
        for g in (0.05, 0.5, 1.0):
            for nn in NORMALS + EDGE_NORMALS:
                calls = []
                material_sun(nn, SUN, VIEW, g, lambda: calls.append(1) or 0.3, True, False, False)
                self.assertTrue(calls, (g, nn))


def text(path):
    return re.sub(r'[ \t]+', ' ', path.read_text(encoding='utf-8'))


def shadow_lines(path):
    """The code lines (comments stripped) naming the identifier `shadow`, but for the prototype."""
    return [l for l in code_lines(path)
            if re.search(r'\bshadow\b', l) and not l.startswith('float sunShadowedDiffuse(')]


def code_lines(path):
    return [re.sub(r'\s+', ' ', l.split('//')[0]).strip() for l in text(path).splitlines()]


class ShaderText(unittest.TestCase):
    def test_alpha_f_gate(self):
        lines = code_lines(ALPHA_F)
        calls = [i for i, l in enumerate(lines) if 'sampleDirectionalShadow(pos.xyz' in l]
        self.assertEqual(len(calls), 1)
        i = calls[0]
        # the call sits directly in `if (da > 0.0) {`, itself under the #if (nothing else in between)
        j = max(k for k in range(i) if lines[k] == 'if (da > 0.0)')
        self.assertTrue(all(l in ('', '{') for l in lines[j + 1:i]), lines[j:i + 1])
        pre = [l for l in lines[:j] if l.startswith('#')][-1]
        self.assertEqual(pre, '#if defined(HAS_SUN_SHADOW) && !defined(ALPHA_STOCHASTIC)')
        # after the diffuse fetch, both live discards and da
        first_discard = max(j for j, l in enumerate(lines) if l == 'discard;')
        self.assertGreater(i, first_discard)
        self.assertGreater(i, next(j for j, l in enumerate(lines) if 'float da = dot(norm.xyz, light_dir.xyz);' in l))

    def test_shadow_identifier_only_where_expected(self):
        self.assertEqual(shadow_lines(ALPHA_F), [
            'float shadow = 1.0f;',
            'shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, frag);',
            'vec3 sun_contrib = vec3(sunShadowedDiffuse(final_da, shadow));',
            'vec3 sun_contrib = sunShadowedDiffuse(final_da, shadow) * sunlit_linear;'])
        self.assertEqual(shadow_lines(MATERIAL_F), [
            'float shadow = 1.0;',
            'shadow = getShadow(pos, norm);',
            'vec3 sun_contrib = vec3(sunShadowedDiffuse(da, shadow));',
            'vec3 sun_contrib = sunShadowedDiffuse(da, shadow) * sunlit_linear;',
            'float scol = shadow*fres*texture(lightFunc, vec2(nh, glossiness)).r*gt/(nh*nl);'])

    def test_material_f_gate(self):
        t = text(MATERIAL_F)
        self.assertIn('if (glossiness > 0.0 || dot(norm.xyz, light_dir.xyz) > 0.0)', t)
        self.assertEqual(len(re.findall(r'getShadow\(pos, norm\);', t)), 1)
        self.assertRegex(t, r'#if defined\(HAS_SUN_SHADOW\) && !defined\(ALPHA_STOCHASTIC\)[^\n]*\n\s*#if \(DIFFUSE_ALPHA_MODE')
        self.assertIn('alpha_stochastic_depth_only() ||', t)

    def test_pbr_alpha_f_returns_before_shading(self):
        """pbralphaF is not gated: its alpha-only variant leaves main() before any shading, and its
        sun shadow stays unconditional in the shaded variant."""
        t = text(PBR_ALPHA_F)
        main = t.index('void main()')
        ret = t.index('gl_SampleMask[0] = alpha_stochastic_mask(', main)
        self.assertIn('#ifdef ALPHA_STOCHASTIC', t[main:ret])
        self.assertIn('return;', t[ret:ret + 120])
        self.assertLess(ret, t.index('vec3 col = vertex_color.rgb * basecolor.rgb;', main))
        lines = code_lines(PBR_ALPHA_F)
        i = next(j for j, l in enumerate(lines) if 'sampleDirectionalShadow(pos.xyz' in l)
        self.assertNotIn('if (da > 0.0)', lines[i - 3:i])

    def test_helper_mode3(self):
        t = text(STOCH_F)
        self.assertIn('bool alpha_stochastic_depth_only()', t)
        self.assertIn('return st_mode == 3;', t)

    def test_restore_uses_its_own_phase(self):
        t = text(POOL)
        self.assertIn('true, true, ALPHA_ST_RESTORE);', t)
        self.assertIn('setStochasticUniforms(3);', t)
        # the glare light-binding stays with the shading passes only
        self.assertIn('if (glare_pass && mat && current_shader', t)
        self.assertEqual(t.count('if (alpha_only_pass && target_shader->mStochasticVariant)'), 2)
        # custom-blend draws of the restore keep their full programs; the override kept
        self.assertIn('restore_alpha_only = oit_phase == ALPHA_ST_RESTORE && !custom_blend;', t)
        self.assertIn('mAlphaOnlyDepth = restore_alpha_only;', t)
        self.assertIn('current_shader->setMinimumAlpha(0.f);', t)
        # the restore sweep: mode 3 binds the rigged variants only, and only when there are rigged groups
        self.assertIn('const bool restore = mode == 3;', t)
        self.assertIn('beginRiggedAlphaGroups() != gPipeline.endRiggedAlphaGroups()', t)


if __name__ == '__main__':
    unittest.main()
