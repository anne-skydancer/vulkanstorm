# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stochastic shadow AA S1: the semi-transparent shadow casters keep the stipple only in their Legacy branch
and the coverage threshold only in their Stochastic branch (SHADOW_STOCH_ALPHA).

With the define absent (the Legacy permutation) each shader must equal the stock text stored in
scripts/shadows/legacy_reference/NAME.glsl.txt (comments and blank lines ignored). Every program in
llviewershadermgr.cpp that links one of them must call add_stochastic_shadow_alpha_permutations in the same
block, and the draw code must set the coverage uniforms on every bound caster program."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / 'indra/newview/app_settings/shaders/class1/deferred'
REFERENCE = ROOT / 'scripts/shadows/legacy_reference'
MGR = ROOT / 'indra/newview/llviewershadermgr.cpp'
PIPELINE = ROOT / 'indra/newview/pipeline.cpp'
AVATAR_POOL = ROOT / 'indra/newview/lldrawpoolavatar.cpp'
FILES = ['shadowAlphaMaskF', 'pbrShadowAlphaBlendF', 'avatarAlphaShadowF', 'avatarAlphaMaskShadowF']
BLEND_FILES = ['shadowAlphaMaskF', 'pbrShadowAlphaBlendF', 'avatarAlphaShadowF']
DEFINE = 'SHADOW_STOCH_ALPHA'
OURS = re.compile(r'#if\s+(!?)defined\(' + DEFINE + r'\)$')


def significant(lines):
    out = []
    for line in lines:
        t = re.sub(r'\s+', ' ', line.strip())
        if t and not t.startswith('//'):
            out.append(t)
    return out


def evaluate(text, defined):
    """Evaluate the conditionals on DEFINE as if it were defined or not; every other conditional is kept
    (and, inside a branch that is not taken, dropped with it)."""
    out, stack = [], []   # entries: [kind, taken]; kind 'ours' (a conditional on DEFINE) or 'other'
    for line in text.splitlines():
        t = line.strip()
        m = OURS.match(t)
        if m:
            stack.append(['ours', defined != bool(m.group(1))])
            continue
        if t.startswith('#if'):
            stack.append(['other', None])
        elif t == '#else' and stack and stack[-1][0] == 'ours':
            stack[-1][1] = not stack[-1][1]
            continue
        elif t == '#endif':
            kind, _ = stack.pop()
            if kind == 'ours':
                continue
        if all(taken for kind, taken in stack if kind == 'ours'):
            out.append(line)
    assert not stack, 'unbalanced conditionals'
    return '\n'.join(out)


def function_body(text, name):
    """The text of `float NAME()` from its signature to the closing brace at column 0."""
    start = text.index('float %s()' % name)
    return text[start:text.index('\n}\n', start) + 3]


class ShadowAlphaPermutation(unittest.TestCase):
    def read(self, name):
        return (SHADERS / (name + '.glsl')).read_text(encoding='utf-8').replace('\r\n', '\n')

    def test_evaluate_handles_nested_conditionals(self):
        text = '\n'.join(['a', '#if defined(%s)' % DEFINE, 'b', '#if X', 'c', '#endif', '#else', 'd', '#if Y',
                          'e', '#endif', '#endif', 'f'])
        self.assertEqual(evaluate(text, True).split('\n'), ['a', 'b', '#if X', 'c', '#endif', 'f'])
        self.assertEqual(evaluate(text, False).split('\n'), ['a', 'd', '#if Y', 'e', '#endif', 'f'])

    def test_legacy_permutation_is_stock_text(self):
        for name in FILES:
            stock = (REFERENCE / (name + '.glsl.txt')).read_text(encoding='utf-8')
            self.assertEqual(significant(evaluate(self.read(name), False).splitlines()),
                             significant(stock.splitlines()), name)

    def test_stochastic_branch_has_no_stipple(self):
        for name in FILES:
            self.assertNotIn('floor(target_pos_x', evaluate(self.read(name), True), name)   # the stipple's use

    def test_every_program_gets_the_permutation(self):
        src = MGR.read_text(encoding='utf-8')
        found = 0
        for name in FILES:
            for m in re.finditer(r'(g\w+)\.mShaderFiles\.push_back\(make_pair\("deferred/' + name + r'\.glsl"', src):
                prog = m.group(1)
                block = src[m.end():src.find('llassert(success);', m.end())]
                self.assertIn('add_stochastic_shadow_alpha_permutations(&%s)' % prog, block,
                              '%s links %s' % (prog, name))
                found += 1
        self.assertGreaterEqual(found, 5)   # mask, fullbright mask, glTF blend, avatar alpha, avatar mask

    def test_coverage_only_under_stochastic(self):
        tokens = ('shadowCasterThreshold', 'shadow_st_caster_phase', 'shadow_st_layer', 'shadow_st_coverage',
                  'gl_FragCoord')
        for name in BLEND_FILES:
            text = self.read(name)
            self.assertIn('shadowCasterThreshold', evaluate(text, True), name)
            legacy = ' '.join(significant(evaluate(text, False).splitlines()))   # comments may name the uniforms
            for token in tokens:
                self.assertNotIn(token, legacy, name)
        # Masking stays binary, and the shared program serves mask and blend through the uniform
        self.assertNotIn('shadowCasterThreshold', self.read('avatarAlphaMaskShadowF'))
        self.assertIn('shadow_st_coverage', evaluate(self.read('shadowAlphaMaskF'), True))

    def test_threshold_bodies_are_identical(self):
        # the loader has no include: the three copies must not drift apart
        bodies = {name: function_body(self.read(name), 'shadowCasterThreshold') for name in BLEND_FILES}
        self.assertEqual(len(set(bodies.values())), 1, 'the threshold differs between %s' % BLEND_FILES)

    def test_masking_keeps_the_stock_texture_alpha_cutoff(self):
        stochastic = evaluate(self.read('shadowAlphaMaskF'), True)
        self.assertIn('shadow_st_coverage < 0.5 && alpha < minimum_alpha', stochastic)
        # the cutoff comes before the vertex alpha is multiplied in (the stock order)
        self.assertLess(stochastic.index('alpha < minimum_alpha'), stochastic.index('alpha *= vertex_color.a'))

    def test_avatar_mask_tests_minimum_alpha_when_stochastic(self):
        self.assertIn('alpha < minimum_alpha', evaluate(self.read('avatarAlphaMaskShadowF'), True))

    def test_callers_set_coverage(self):
        pipe = PIPELINE.read_text(encoding='utf-8').replace('\r\n', '\n')
        start = pipe.index('void LLPipeline::renderAlphaObjects')
        end = pipe.index('void LLPipeline::renderMaskedObjects')
        blend = pipe[start:end]
        binds = len(re.findall(r'(?:gDeferredShadowAlphaMaskProgram|gDeferredShadowGLTFAlphaBlendProgram)\.bind\(', blend))
        self.assertEqual(binds, 4)   # mask and glTF, rigged and not
        self.assertEqual(blend.count('setShadowCasterCoverage(true,'), binds)
        # the masking passes of renderShadowImpl: the alpha mask and the fullbright mask programs
        masks = len(re.findall(r'(?:gDeferredShadowAlphaMaskProgram|gDeferredShadowFullbrightAlphaMaskProgram)\.bind\(', pipe[end:]))
        self.assertGreaterEqual(masks, 2)
        self.assertEqual(pipe.count('setShadowCasterCoverage(false)'), masks)
        avatar = AVATAR_POOL.read_text(encoding='utf-8')
        self.assertIn('setShadowCasterCoverage(true,', avatar)
        self.assertNotIn('use_stochastic_shadows', avatar)   # the flag, not the setting, gates the mask cutoff
        self.assertIn('sShadowStochAlpha', avatar)


if __name__ == '__main__':
    unittest.main()
