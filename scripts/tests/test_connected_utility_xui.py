"""Check shipped currency overlays against real controller fields and XUI nesting."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SKINS = ROOT / 'indra/newview/skins'

class ConnectedUtilityXUI(unittest.TestCase):
    def test_currency_controller_uses_existing_fields(self):
        base = ET.parse(SKINS / 'default/xui/en/floater_buy_currency.xml').getroot()
        names = {node.get('name') for node in base.iter() if node.get('name')}
        for file in ('llfloaterbuycurrency.cpp', 'llcurrencyuimanager.cpp'):
            source = (ROOT / 'indra/newview' / file).read_text(encoding='utf-8')
            fields = re.findall(r'getChild(?:View|<[^>]+>)\(\s*"([^"]+)"', source)
            self.assertTrue(fields)
            for field in fields:
                with self.subTest(file=file, field=field):
                    self.assertIn(field, names)

    def test_every_shipped_currency_overlay_matches_current_layout(self):
        base_path = SKINS / 'default/xui/en/floater_buy_currency.xml'
        base = ET.parse(base_path).getroot()
        overlays = sorted(SKINS.rglob('floater_buy_currency.xml'))
        self.assertGreater(len(overlays), 1)
        def compare(expected, actual, path):
            self.assertEqual((expected.tag, expected.get('name')), (actual.tag, actual.get('name')), str(path))
            for child in actual:
                candidates = [node for node in expected if node.tag == child.tag and node.get('name') == child.get('name')]
                self.assertEqual(len(candidates), 1, f'{path}: orphan or misnested {child.tag} {child.get("name")}')
                compare(candidates[0], child, path)
        for path in overlays:
            with self.subTest(path=str(path.relative_to(SKINS))):
                compare(base, ET.parse(path).getroot(), path)

    def test_trash_panel_uses_supported_inventory_parameters(self):
        for path in SKINS.rglob('floater_preview_trash.xml'):
            with self.subTest(path=str(path.relative_to(SKINS))):
                root = ET.parse(path).getroot()
                for node in root.iter():
                    self.assertNotIn('allow_wear', node.attrib)

if __name__ == '__main__':
    unittest.main()
