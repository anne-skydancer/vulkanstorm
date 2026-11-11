# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The Stochastic shadow method (RenderShadowMethod 2): no feature-table list recommends it, the combo
offers the three methods, and its shader stays free of PCSS code and of the groundwork defines."""
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
    def test_no_list_recommends_stochastic(self):
        tables = sorted(NEWVIEW.glob('featuretable*.txt'))
        self.assertGreaterEqual(len(tables), 3)
        for table in tables:
            rows = [l.split() for l in table.read_text().splitlines() if l.startswith('RenderShadowMethod')]
            self.assertTrue(rows, table.name)
            for row in rows:
                self.assertEqual(3, len(row), (table.name, row))
                self.assertIn(row[2], ('0', '1'), (table.name, row))

    def test_combo_items(self):
        xml = (NEWVIEW / 'skins/default/xui/en/panel_preferences_graphics1.xml').read_text()
        combo = xml[xml.index('name="ShadowMethod"'):]
        combo = combo[:combo.index('</combo_box>')]
        self.assertEqual([('Legacy', '0'), ('PCSS', '1'), ('Stochastic', '2')],
                         re.findall(r'label="([^"]+)"\s+name="\d"\s+value="(\d)"', combo))


class StochasticShaderIsolation(unittest.TestCase):
    def test_util_has_no_pcss_code(self):
        code = strip_comments(UTIL.read_text())
        self.assertIsNone(re.search(r'pcss', code, re.I), 'the Stochastic util must not use PCSS code or defines')
        self.assertNotIn('SHADOW_MODERN', code)

    def test_stochastic_build_has_no_pcss_or_modern_defines(self):
        mgr = (NEWVIEW / 'llviewershadermgr.cpp').read_text()
        for start in ('attribs["SHADOW_UTIL_STOCHASTIC"]', 'mFragmentShaderObjects.erase(STOCHASTIC_SHADOW_UTIL)'):
            block = mgr[mgr.index(start):]
            block = block[:block.index('// </Vulkanstorm>')]
            for bad in ('PCSS_', 'SHADOW_GATHER', 'SHADOW_MODERN'):
                self.assertNotIn(bad, block)
            self.assertNotIn('SHADOW_MODERN', block)

    def test_common_block_is_attached_with_the_util(self):
        mgr = (ROOT / 'indra/llrender/llshadermgr.cpp').read_text()
        block = mgr[mgr.index('STOCHASTIC_SHADOW_COMMON_DEPTH_KEY'):]
        block = block[:block.index('else')]
        self.assertIn('attachFragmentObject(common)', block)
        self.assertIn('attachFragmentObject(STOCHASTIC_SHADOW_UTIL)', block)

if __name__ == '__main__':
    unittest.main()
