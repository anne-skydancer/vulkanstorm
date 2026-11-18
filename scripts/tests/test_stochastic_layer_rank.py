# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the stochastic layer rank (coincident blended faces of one object, indra/newview/
llstochasticlayerrank.h): the rank reaches the stochastic passes only, and sorted alpha (Legacy) is untouched.

- Every glPolygonOffset call in lldrawpoolalpha.cpp is in the two offset helpers; nothing else sets one.
- setStochasticOffset is called only in the stochastic passes' draw (inside `if (stochastic_pass)`) and in the
  glow draws behind `mRankedGlow`, which only the stochastic accumulation turns on.
- The rank is read by registerFace for PASS_ALPHA of a static face only, and genDrawInfo assigns it for the
  distance-sorted (alpha) list of non-rigged faces outside HUD groups. A rank breaks a batch (an equal-rank
  condition added to the batching test, nothing else changed in it): draws split, the triangles and their
  order do not, so the sorted path draws the same pixels.

    python3 scripts/tests/test_stochastic_layer_rank.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_stochastic_layer_rank.py   (another source tree)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
POOL = (ROOT / 'indra/newview/lldrawpoolalpha.cpp').read_text(encoding='utf-8', errors='replace')
VOLUME = (ROOT / 'indra/newview/llvovolume.cpp').read_text(encoding='utf-8', errors='replace')


def body(text, signature):
    """The text of the function whose definition starts with `signature`, to its closing brace at column 0."""
    start = text.index(signature)
    end = text.index('\n}\n', start)
    return text[start:end]


class LayerRank(unittest.TestCase):
    def test_polygon_offset_only_in_the_helpers(self):
        calls = [m.start() for m in re.finditer(r'glPolygonOffset\(', POOL)]
        allowed = [body(POOL, 'void LLDrawPoolAlpha::setStochasticOffset'),
                   body(POOL, 'void LLDrawPoolAlpha::resetStochasticOffset')]
        inside = 0
        for text in allowed:
            inside += len(re.findall(r'glPolygonOffset\(', text))
        self.assertEqual(len(calls), inside, 'a glPolygonOffset call outside the two helpers')

    def test_rank_offset_only_in_the_stochastic_passes(self):
        # in renderAlpha the call sits in the `if (stochastic_pass)` block
        render = body(POOL, 'void LLDrawPoolAlpha::renderAlpha(U32 mask')
        block = render[render.index('if (stochastic_pass)\n                    {'):]
        block = block[:block.index('else\n')]
        self.assertIn('setStochasticOffset(st_units, params.mStochasticRank)', block)
        self.assertEqual(len(re.findall(r'(?<!re)setStochasticOffset\(', render)), 1)
        # the glow draws take theirs only behind mRankedGlow, which only the accumulation's glow section sets
        for name in ('void LLDrawPoolAlpha::drawEmissive', 'void LLDrawPoolAlpha::renderPbrEmissives',
                     'void LLDrawPoolAlpha::renderRiggedPbrEmissives'):
            text = body(POOL, name)
            self.assertRegex(text, r'if \(mRankedGlow\)[^\n]*\n\s*\{\n\s*setStochasticOffset\(', name)
        self.assertEqual(len(re.findall(r'mRankedGlow = true', POOL)), 1)
        glow = POOL[POOL.index('mRankedGlow = true') - 600:POOL.index('mRankedGlow = true')]
        self.assertIn('if (st_glow)', glow)
        # the other states of the pass are restored behind the ranked draws
        self.assertIn('if (st_phase && mStochasticOffsetRank != 0)', render)

    def test_pass_offsets_are_the_survey_units(self):
        header = (ROOT / 'indra/newview/llstochasticlayerrank.h').read_text(encoding='utf-8')
        for name, value in (('UNITS_TOTAL_ALPHA', '-1.f'), ('UNITS_DEPTH', '-1.f'), ('UNITS_ACCUM', '-2.f'),
                            ('UNITS_GLOW', '-3.f'), ('UNITS_PER_RANK', '2.f')):
            self.assertRegex(header, name + r'\s*=\s*' + re.escape(value) + ';')
        stoch = body(POOL, 'bool LLDrawPoolAlpha::renderStochastic()')
        self.assertIn('resetStochasticOffset(LLStochasticLayerRank::UNITS_TOTAL_ALPHA)', stoch)
        self.assertIn('resetStochasticOffset(LLStochasticLayerRank::UNITS_DEPTH)', stoch)
        self.assertIn('resetStochasticOffset(LLStochasticLayerRank::UNITS_ACCUM)', stoch)

    def test_registerface_rank_is_alpha_static_only_and_batching_otherwise_unchanged(self):
        reg = body(VOLUME, 'void LLVolumeGeometryManager::registerFace')
        self.assertIn('(type == LLRenderPass::PASS_ALPHA && !rigged) ? facep->getStochasticRank() : 0', reg)
        self.assertEqual(len(re.findall(r'info->mStochasticRank == stochastic_rank', reg)), 1)
        # the batching test: the stock conditions followed by the one added
        self.assertRegex(reg, r'info->getSkinHash\(\) == facep->getSkinHash\(\) &&\n\s*info->mStochasticRank == stochastic_rank')
        self.assertEqual(len(re.findall(r'mStochasticRank', reg)), 2)   # the test, the new draw

    def test_genDrawInfo_assigns_ranks_for_static_blended_faces_only(self):
        gen = body(VOLUME, 'U32 LLVolumeGeometryManager::genDrawInfo')
        self.assertIn('faces[k]->setStochasticRank(0);', gen)
        self.assertIn('if (distance_sort && !rigged && !group->isHUDGroup() && face_count > 1)', gen)
        self.assertEqual(VOLUME.count('assign_stochastic_layer_ranks('), 2)   # definition and the one call
        # the reference is the creator's order: the texture entry index
        assign = body(VOLUME, 'static void assign_stochastic_layer_ranks')
        self.assertIn('a.mTE < b.mTE', assign)
        self.assertIn('facep->getTEOffset()', assign)

    def test_the_sorted_path_does_not_read_the_rank(self):
        # nothing in the Legacy (sorted) code path of the pool reads mStochasticRank outside the stochastic helpers
        render = body(POOL, 'void LLDrawPoolAlpha::renderAlpha(U32 mask')
        self.assertEqual(len(re.findall(r'mStochasticRank', render)), 1)
        for name in ('void LLDrawPoolAlpha::renderOITResidual', 'void LLDrawPoolAlpha::forwardRender'):
            self.assertNotIn('mStochasticRank', body(POOL, name))


if __name__ == '__main__':
    unittest.main()
