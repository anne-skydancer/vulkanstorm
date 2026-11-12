# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stochastic shadow AA S1: the four semi-transparent shadow casters carry the stipple only in their Legacy
branch, and the coverage threshold only in their Stochastic branch. With the SHADOW_SOLID_ALPHA branches stripped (the Legacy permutation) each shader must equal the
stock text stored in scripts/shadows/legacy_reference/NAME.glsl.txt (comments and blank lines ignored), and
every program in llviewershadermgr.cpp that links one of them must call
add_stochastic_shadow_alpha_permutations in the same block."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / 'indra/newview/app_settings/shaders/class1/deferred'
REFERENCE = ROOT / 'scripts/shadows/legacy_reference'
MGR = ROOT / 'indra/newview/llviewershadermgr.cpp'
FILES = ['shadowAlphaMaskF', 'pbrShadowAlphaBlendF', 'avatarAlphaShadowF', 'avatarAlphaMaskShadowF']
DEFINE = 'SHADOW_SOLID_ALPHA'


def significant(lines):
    out = []
    for line in lines:
        t = re.sub(r'\s+', ' ', line.strip())
        if t and not t.startswith('//'):
            out.append(t)
    return out


def legacy_permutation(text):
    """Evaluate the #if/#else/#endif of SHADOW_SOLID_ALPHA as undefined; other conditionals are kept."""
    out, stack = [], []   # stack entries: True = this conditional is ours and its active branch is kept
    for line in text.splitlines():
        t = line.strip()
        m = re.match(r'#if\s+(!?)defined\(' + DEFINE + r'\)$', t)
        if m:
            stack.append(('ours', bool(m.group(1))))   # active when the define is absent: only the "!" form
            continue
        if re.match(r'#if', t):
            stack.append(('other', True))
            out.append(line)
            continue
        if t == '#else':
            kind, active = stack[-1]
            if kind == 'ours':
                stack[-1] = (kind, not active)
                continue
            out.append(line)
            continue
        if t == '#endif':
            kind, _ = stack.pop()
            if kind == 'other':
                out.append(line)
            continue
        if all(kind != 'ours' or active for kind, active in stack):
            out.append(line)
    return '\n'.join(out)


class ShadowAlphaPermutation(unittest.TestCase):
    def test_legacy_permutation_is_stock_text(self):
        for name in FILES:
            current = (SHADERS / (name + '.glsl')).read_text(encoding='utf-8')
            stock = (REFERENCE / (name + '.glsl.txt')).read_text(encoding='utf-8')
            self.assertEqual(significant(legacy_permutation(current).splitlines()),
                             significant(stock.splitlines()), name)

    def test_solid_branch_has_no_stipple(self):
        for name in FILES:
            text = (SHADERS / (name + '.glsl')).read_text(encoding='utf-8')
            solid = []
            depth_solid = None
            for line in text.splitlines():
                t = line.strip()
                if t == '#if defined(%s)' % DEFINE:
                    depth_solid = True
                elif t == '#if !defined(%s)' % DEFINE or (t == '#else' and depth_solid):
                    depth_solid = False
                elif t == '#else' and depth_solid is False:
                    depth_solid = True
                elif t == '#endif':
                    depth_solid = None
                elif depth_solid:
                    solid.append(line)
            self.assertNotIn('target_pos_x', '\n'.join(solid), name)

    def test_every_program_gets_the_permutation(self):
        src = MGR.read_text(encoding='utf-8')
        found = 0
        for name in FILES:
            for m in re.finditer(r'(g\w+)\.mShaderFiles\.push_back\(make_pair\("deferred/' + name + r'\.glsl"', src):
                prog = m.group(1)
                block_end = src.find('llassert(success);', m.end())
                block = src[m.end():block_end]
                self.assertIn('add_stochastic_shadow_alpha_permutations(&%s)' % prog, block,
                              '%s links %s' % (prog, name))
                found += 1
        self.assertGreaterEqual(found, 5)   # mask, fullbright mask, glTF blend, avatar alpha, avatar mask

    def test_coverage_only_under_stochastic(self):
        # the coverage threshold and its uniforms exist in the SOLID branch of the three Blending casters only
        # (the Legacy permutation, tested above, equals the stock text, so none of it can leak into Legacy)
        for name in ['shadowAlphaMaskF', 'pbrShadowAlphaBlendF', 'avatarAlphaShadowF']:
            text = (SHADERS / (name + '.glsl')).read_text(encoding='utf-8')
            self.assertIn('shadowCasterThreshold', text, name)
            self.assertIn('shadow_st_caster_frame', text, name)
            legacy = '\n'.join(significant(legacy_permutation(text).splitlines()))
            for token in ('shadowCasterThreshold', 'shadow_st_caster_frame', 'shadow_st_coverage', 'gl_FragCoord'):
                self.assertNotIn(token, legacy, name)
        text = (SHADERS / 'avatarAlphaMaskShadowF.glsl').read_text(encoding='utf-8')
        self.assertNotIn('shadowCasterThreshold', text)   # Masking stays binary
        mask = (SHADERS / 'shadowAlphaMaskF.glsl').read_text(encoding='utf-8')
        self.assertIn('shadow_st_coverage', mask)         # the shared program serves mask and blend

    def test_avatar_mask_tests_minimum_alpha_when_solid(self):
        text = (SHADERS / 'avatarAlphaMaskShadowF.glsl').read_text(encoding='utf-8')
        solid = text.split('#if defined(%s)' % DEFINE)[2].split('#else')[0]   # second one: the cutoff block
        self.assertIn('alpha < minimum_alpha', solid)
        self.assertNotIn('target_pos_x', solid)

    def test_callers_set_coverage(self):
        pipe = (ROOT / 'indra/newview/pipeline.cpp').read_text(encoding='utf-8')
        a = pipe.index('void LLPipeline::renderAlphaObjects')
        b = pipe.index('void LLPipeline::renderMaskedObjects')
        blend = pipe[a:b]
        self.assertEqual(blend.count('setShadowCasterCoverage(true)'), blend.count('setMinimumAlpha(ALPHA_BLEND_CUTOFF)'))
        self.assertGreaterEqual(blend.count('setShadowCasterCoverage(true)'), 4)
        self.assertGreaterEqual(pipe.count('setShadowCasterCoverage(false)'), 2)   # mask and fullbright mask
        avatar = (ROOT / 'indra/newview/lldrawpoolavatar.cpp').read_text(encoding='utf-8')
        self.assertIn('setShadowCasterCoverage(true)', avatar)


if __name__ == '__main__':
    unittest.main()
