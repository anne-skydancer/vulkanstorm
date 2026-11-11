# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The shadow method values: no feature-table list recommends Stochastic (2) -- it is selectable
only -- and the header, the combo and the settings comment agree on the three methods."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
NEWVIEW = ROOT / 'indra/newview'


class ShadowMethodTables(unittest.TestCase):
    def test_no_list_recommends_stochastic(self):
        tables = sorted(NEWVIEW.glob('featuretable*.txt'))
        self.assertEqual(3, len(tables))
        for table in tables:
            rows = [l.split() for l in table.read_text().splitlines() if l.startswith('RenderShadowMethod')]
            self.assertEqual(10, len(rows), table.name)
            for row in rows:
                self.assertEqual(3, len(row), (table.name, row))
                self.assertIn(row[2], ('0', '1'), (table.name, row))

    def test_header_constants(self):
        header = (NEWVIEW / 'llshadowmethod.h').read_text()
        values = {m.group(1): int(m.group(2)) for m in
                  re.finditer(r'constexpr std::uint32_t (LEGACY|PCSS|STOCHASTIC)\s*=\s*(\d+);', header)}
        self.assertEqual({'LEGACY': 0, 'PCSS': 1, 'STOCHASTIC': 2}, values)

    def test_combo_items(self):
        xml = (NEWVIEW / 'skins/default/xui/en/panel_preferences_graphics1.xml').read_text()
        combo = xml[xml.index('name="ShadowMethod"'):]
        combo = combo[:combo.index('</combo_box>')]
        self.assertEqual([('Legacy', '0'), ('PCSS', '1'), ('Stochastic', '2')],
                         re.findall(r'label="([^"]+)"\s+name="\d"\s+value="(\d)"', combo))


if __name__ == '__main__':
    unittest.main()
