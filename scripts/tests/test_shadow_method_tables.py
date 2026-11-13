# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The shadow methods (RenderShadowMethod 0 Legacy, 2 Stochastic; 1 was PCSS and is unused): the feature-table
lists recommend 0 or 2 and never 1, the combo offers the two methods, and the Stochastic shader stays free of
removed PCSS code and of the groundwork defines."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
NEWVIEW = ROOT / 'indra/newview'
UTIL = NEWVIEW / 'app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class ShadowMethodTables(unittest.TestCase):
    def test_lists_recommend_legacy_or_stochastic_never_pcss(self):
        tables = sorted(NEWVIEW.glob('featuretable*.txt'))
        self.assertGreaterEqual(len(tables), 3)
        for table in tables:
            rows = [l.split() for l in table.read_text().splitlines() if l.startswith('RenderShadowMethod')]
            self.assertTrue(rows, table.name)
            for row in rows:
                self.assertEqual(3, len(row), (table.name, row))
                self.assertIn(row[2], ('0', '2'), (table.name, row))

    def test_gl3_list_stores_legacy(self):
        """Below OpenGL 4.0 Stochastic does not run: the GL3 list stores Legacy for the method."""
        for table in sorted(NEWVIEW.glob('featuretable*.txt')):
            text = table.read_text()
            block = text[text.index('list GL3'):]
            end = block.find('\nlist ', 1)
            block = block if end < 0 else block[:end]
            rows = [l.split() for l in block.splitlines() if l.startswith('RenderShadowMethod')]
            self.assertEqual([['RenderShadowMethod', '1', '0']], rows, table.name)

    def test_combo_items(self):
        xml = (NEWVIEW / 'skins/default/xui/en/panel_preferences_graphics1.xml').read_text()
        combo = xml[xml.index('name="ShadowMethod"'):]
        combo = combo[:combo.index('</combo_box>')]
        self.assertEqual([('Legacy', '0'), ('Stochastic', '2')],
                         re.findall(r'label="([^"]+)"\s+name="\d"\s+value="(\d)"', combo))


class PcssIsRemoved(unittest.TestCase):
    """PCSS (RenderShadowMethod 1) is gone from the shaders and the shader manager; archive/pcss/2026-11-11 holds it."""
    def test_shaders_and_manager_have_no_pcss_build(self):
        deferred = NEWVIEW / 'app_settings/shaders/class1/deferred'
        code = strip_comments((deferred / 'shadowUtil.glsl').read_text())
        for gone in ('PCSS', 'pcss', 'SHADOW_MODERN', 'SHADOW_GATHER', 'shadowDepth0', 'shadowDepth1', 'shadowDepth4', 'shadowDepth5'):
            self.assertNotIn(gone, code)
        for path in ('indra/newview/llviewershadermgr.cpp', 'indra/llrender/llshadermgr.cpp', 'indra/llrender/llshadermgr.h'):
            self.assertNotIn('PCSS_SHADOW', (ROOT / path).read_text(), path)

    def test_method_one_is_unused(self):
        header = (NEWVIEW / 'llshadowmethod.h').read_text()
        self.assertIn('RETIRED_PCSS = 1', header)
        self.assertIn('DEFAULT = STOCHASTIC', header)


class StochasticShaderIsolation(unittest.TestCase):
    def test_util_has_no_pcss_code(self):
        code = strip_comments(UTIL.read_text())
        self.assertIsNone(re.search(r'pcss', code, re.I), 'the Stochastic util must not use PCSS code or defines')
        self.assertNotIn('SHADOW_MODERN', code)

    def test_stochastic_build_has_no_pcss_or_modern_defines(self):
        mgr = (NEWVIEW / 'llviewershadermgr.cpp').read_text()
        block = mgr[mgr.index('mFragmentShaderObjects.erase(STOCHASTIC_SHADOW_UTIL)'):]
        block = block[:block.index('// </Vulkanstorm>')]
        for bad in ('PCSS_', 'SHADOW_GATHER', 'SHADOW_MODERN'):
            self.assertNotIn(bad, block)
        # nothing of the method is a global define: the hash takes it instead
        basic = mgr[mgr.index('std::string LLViewerShaderMgr::loadBasicShaders'):mgr.index('bool LLViewerShaderMgr::loadShadersWater')]
        for name in ('SHADOW_COMMON', 'SHADOW_MODERN', 'SHADOW_FOOTPRINT_M', 'SHADOW_BIAS_TEXELS', 'SHADOW_GATHER'):
            self.assertNotIn('  attribs["%s"]' % name, basic)   # the global defines (the groundwork's go through addShadowCommonDefines on a copy)
        self.assertIn('sShadowUtilVariant', mgr)

    def test_common_block_is_attached_with_the_util(self):
        mgr = (ROOT / 'indra/llrender/llshadermgr.cpp').read_text()
        block = mgr[mgr.index('STOCHASTIC_SHADOW_COMMON_DEPTH_KEY'):]
        block = block[:block.index('else')]
        self.assertIn('attachFragmentObject(common)', block)
        self.assertIn('attachFragmentObject(STOCHASTIC_SHADOW_UTIL)', block)

class StochasticResolveUniforms(unittest.TestCase):
    """The viewer sets every uniform the resolve shaders declare before it runs the resolve: the compute path
    once missed inv_proj, which only a vertex buffer draw uploads, and ran without history."""
    DECLARED = {
        # uniform: what resolveShadowStochastic (pipeline.cpp) has to contain for it
        'inv_proj': 'INVERSE_PROJECTION_MATRIX',
        'depthMap': 'DEFERRED_DEPTH',
        'shadow_st_raw': 'SHADOW_ST_RAW',
        'shadow_st_history': 'SHADOW_ST_HISTORY',
        'shadow_st_channel_valid': 'sChannelValid',
        'stoch_weight': 'setResolveUniforms',
        'stoch_reproj': 'setResolveUniforms',
        'stoch_history_valid': 'setResolveUniforms',
    }

    def test_every_uniform_is_set(self):
        deferred = NEWVIEW / 'app_settings/shaders/class1/deferred'
        found = set()
        for name in ('shadowStochasticResolveUtilF.glsl', 'shadowStochasticResolveF.glsl', 'shadowStochasticResolveC.glsl', 'stochasticResolveUtilF.glsl'):
            for m in re.finditer(r'^\s*(?:layout\([^)]*\)\s*)?uniform\s+(?:writeonly\s+)?\w+\s+(\w+)', strip_comments((deferred / name).read_text()), re.M):
                found.add(m.group(1))
        found.discard('shadow_st_out')   # the history image: bound by bindWriteImage
        self.assertEqual(set(self.DECLARED), found, 'a uniform of the resolve shaders is not in the viewer check list')
        pipeline = (NEWVIEW / 'pipeline.cpp').read_text()
        body = pipeline[pipeline.index('void LLPipeline::resolveShadowStochastic'):]
        body = body[:body.index('void LLPipeline::composeShadowStochastic')]
        for uniform, setter in self.DECLARED.items():
            self.assertIn(setter, body, f'{uniform} is not set by resolveShadowStochastic ({setter})')
        self.assertIn('bindWriteImage', body)
        # both paths are bound the same way: the dispatch and the draw come after one uniform setup
        self.assertLess(body.index('INVERSE_PROJECTION_MATRIX'), body.index('glDispatchCompute'))
        self.assertLess(body.index('INVERSE_PROJECTION_MATRIX'), body.index('drawArrays'))


if __name__ == '__main__':
    unittest.main()
