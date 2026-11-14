# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The semi-transparent shadow casters keep Firestorm 7.2.5's stipple in their Legacy permutation and use a
hashed per-texel dither only under the Stochastic shadow method (define STOCHASTIC_SHADOW_ALPHA).

With the define absent each of the four shaders must equal the stock text stored in
scripts/shadows/legacy_reference/NAME.glsl.txt (comments and blank lines ignored). With it present, the stipple
is gone and the dither text is the same in all four. Every program in llviewershadermgr.cpp that links one of
them adds the define inside `if (use_stochastic_shadows())`, and nowhere else.

    python3 scripts/tests/test_shadow_alpha_permutation.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_shadow_alpha_permutation.py   (another source tree, e.g. a
        `git archive` of another line, to see whether the test would hold there)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
SHADERS = ROOT / 'indra/newview/app_settings/shaders/class1/deferred'
REFERENCE = ROOT / 'scripts/shadows/legacy_reference'
MGR = ROOT / 'indra/newview/llviewershadermgr.cpp'
PIPELINE = ROOT / 'indra/newview/pipeline.cpp'
AVATAR_POOL = ROOT / 'indra/newview/lldrawpoolavatar.cpp'
FILES = ['shadowAlphaMaskF', 'pbrShadowAlphaBlendF', 'avatarAlphaShadowF', 'avatarAlphaMaskShadowF']
DEFINE = 'STOCHASTIC_SHADOW_ALPHA'
OURS = re.compile(r'#\s*if\s+(!?)\s*defined\(' + DEFINE + r'\)$')
DITHER = 'float ign = fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x + 0.00583715 * gl_FragCoord.y));'


def significant(lines):
    out = []
    for line in lines:
        t = re.sub(r'\s+', ' ', line.strip())
        if t and not t.startswith('//'):
            out.append(t)
    return out


def evaluate(text, defined):
    """The text as the GLSL preprocessor sees it for the conditionals on DEFINE, with the define present or
    absent. Every other conditional is kept, and is dropped with a branch that is not taken."""
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


def dither_text(text):
    """The significant lines of the define-present branch from the dither line to the end of its
    `if (ign >= alpha)` block."""
    lines = significant(evaluate(text, True).splitlines())
    start = next(i for i, l in enumerate(lines) if l == DITHER)
    end = next(i for i in range(start, len(lines)) if lines[i] == '}')
    return lines[start:end + 1]


class ShadowAlphaPermutation(unittest.TestCase):
    def read(self, name):
        return (SHADERS / (name + '.glsl')).read_text(encoding='utf-8').replace('\r\n', '\n')

    def test_evaluate_handles_nested_conditionals(self):
        text = '\n'.join(['a', '#if defined(%s)' % DEFINE, 'b', '#if X', 'c', '#endif', '#else', 'd', '#if Y',
                          'e', '#endif', '#endif', 'f'])
        self.assertEqual(evaluate(text, True).split('\n'), ['a', 'b', '#if X', 'c', '#endif', 'f'])
        self.assertEqual(evaluate(text, False).split('\n'), ['a', 'd', '#if Y', 'e', '#endif', 'f'])
        neg = '\n'.join(['#if !defined(%s)' % DEFINE, 'x', '#else', 'y', '#endif'])
        self.assertEqual(evaluate(neg, False), 'x')
        self.assertEqual(evaluate(neg, True), 'y')
        # an unrelated #else is not ours to flip
        other = '\n'.join(['#if X', 'p', '#else', 'q', '#endif'])
        self.assertEqual(evaluate(other, True), other)

    def test_legacy_permutation_is_stock_text(self):
        for name in FILES:
            stock = (REFERENCE / (name + '.glsl.txt')).read_text(encoding='utf-8')
            self.assertEqual(significant(evaluate(self.read(name), False).splitlines()),
                             significant(stock.splitlines()), name)

    def test_stochastic_branch_has_the_dither_and_no_stipple(self):
        texts = {}
        for name in FILES:
            on = evaluate(self.read(name), True)
            self.assertNotIn('floor(target_pos_x', on, name)   # the stipple's use
            body = dither_text(self.read(name))
            self.assertIn('if (ign >= alpha)', body, name)
            on_lines = significant(evaluate(self.read(name), True).splitlines())
            at = on_lines.index(DITHER)
            self.assertTrue(any('minimum_alpha' in l for l in on_lines[:at]), name + ': dither outside the minimum_alpha branch')
            texts[name] = body
        self.assertEqual(len({tuple(b) for b in texts.values()}), 1, 'the dither differs between the shaders')

    def test_every_program_gets_the_permutation_under_the_stochastic_test(self):
        src = MGR.read_text(encoding='utf-8').replace('\r\n', '\n')
        found = set()
        for name in FILES:
            for m in re.finditer(r'(g\w+)\.mShaderFiles\.push_back\(make_pair\("deferred/' + name + r'\.glsl"', src):
                prog = m.group(1)
                block = src[m.end():src.index('createShader', m.end())]
                self.assertRegex(block, r'if \(use_stochastic_shadows\(\)\)\s*\{[^{}]*\b' + prog +
                                 r'\.addPermutation\("' + DEFINE + r'", "1"\);',
                                 '%s links %s' % (prog, name))
                # make_rigged_variant copies mDefines when it is called, so the define must come first
                rig = re.search(r'make_rigged_variant\(' + prog + r',', block)
                if rig:
                    self.assertLess(block.index('addPermutation("' + DEFINE), rig.start(),
                                    '%s: the define is added after make_rigged_variant' % prog)
                found.add(prog)
        self.assertEqual(found, {'gDeferredShadowFullbrightAlphaMaskProgram', 'gDeferredShadowAlphaMaskProgram',
                                 'gDeferredShadowGLTFAlphaBlendProgram', 'gDeferredAvatarAlphaShadowProgram',
                                 'gDeferredAvatarAlphaMaskShadowProgram'})

    def test_define_is_added_only_there(self):
        src = MGR.read_text(encoding='utf-8').replace('\r\n', '\n')
        guarded = re.findall(r'if \(use_stochastic_shadows\(\)\)\s*\{[^{}]*\.addPermutation\("' + DEFINE + r'", "1"\);', src)
        self.assertEqual(len(re.findall(r'addPermutation\("' + DEFINE + '"', src)), len(guarded), 'the define is added outside the stochastic test')
        start = src.index('add_common_permutations(LLGLSLShader')
        common = src[start:src.index('\n}\n', start)]
        self.assertNotIn(DEFINE, common)

    def test_solid_threshold_is_098_for_blend_and_zero_for_mask(self):
        # Stochastic: the dither runs below shadow_solid_alpha. The glTF blend shader hard-codes 0.98; the shared
        # mask/blend shader reads the uniform, which the pipeline sets to 0.98 after each blend bind and to 0 after
        # each mask bind (so mask faces follow the creator's cutoff and never dither).
        blend = evaluate(self.read('pbrShadowAlphaBlendF'), True)
        self.assertIn('if (alpha < 0.98)', blend)
        mask = evaluate(self.read('shadowAlphaMaskF'), True)
        self.assertIn('uniform float shadow_solid_alpha;', mask)
        self.assertIn('if (alpha < shadow_solid_alpha)', mask)
        self.assertNotIn('shadow_solid_alpha', evaluate(self.read('shadowAlphaMaskF'), False))
        pipe = PIPELINE.read_text(encoding='utf-8').replace('\r\n', '\n')
        self.assertRegex(pipe, r'SHADOW_BLEND_SOLID_ALPHA = 0\.98f;')
        self.assertRegex(pipe, r'SHADOW_MASK_SOLID_ALPHA = 0\.f;')
        call = r'sCurBoundShaderPtr->uniform1f\(LLShaderMgr::DEFERRED_SHADOW_SOLID_ALPHA, (\w+)\);'
        # every bind of the two shared programs is followed by the uniform, with the right value
        for m in re.finditer(r'(gDeferredShadow(?:Fullbright)?AlphaMaskProgram)\.bind\(rigged\);', pipe):
            tail = pipe[m.end():m.end() + 900]
            nxt = re.search(r'\.bind\(', tail)
            tail = tail[:nxt.start()] if nxt else tail
            got = re.search(call, tail)
            self.assertIsNotNone(got, m.group(0) + ' is not followed by the solid-alpha uniform')
            draw = re.search(r'pushBatch|renderMaskedObjects|renderFullbrightMaskedObjects', tail)
            self.assertIsNotNone(draw, m.group(0))
            self.assertLess(got.start(), draw.start(), m.group(0) + ': the uniform is set after the draw')
            want = 'SHADOW_BLEND_SOLID_ALPHA' if draw.group(0) == 'pushBatch' else 'SHADOW_MASK_SOLID_ALPHA'
            self.assertEqual(got.group(1), want, m.group(0))
        self.assertEqual(len(re.findall(call, pipe)), 4)
        self.assertEqual(len(re.findall(r'gDeferredShadow(?:Fullbright)?AlphaMaskProgram\.bind\(rigged\);', pipe)), 4)

    def test_avatar_shadow_pass_never_sets_minimum_alpha(self):
        # The avatar alpha-shadow branch (the stock stipple, and the dither under Stochastic) is inert: it runs only
        # for texels below minimum_alpha, which stays 0 in the shadow pass, as in stock. Making it live is a separate
        # change that needs an in-world check; this test marks the day that changes.
        src = AVATAR_POOL.read_text(encoding='utf-8').replace('\r\n', '\n')
        start = src.index('void LLDrawPoolAvatar::beginShadowPass')
        end = src.index('void LLDrawPoolAvatar::endShadowPass')
        self.assertNotIn('setMinimumAlpha', src[start:end])
        start = src.index('void LLDrawPoolAvatar::renderShadow')
        end = src.index('\n}\n', start)
        self.assertNotIn('setMinimumAlpha', src[start:end])


if __name__ == '__main__':
    unittest.main()
