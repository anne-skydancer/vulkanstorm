"""Check shipped currency overlays against real controller fields and XUI nesting."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SKINS = ROOT / 'indra/newview/skins'

class ConnectedUtilityXUI(unittest.TestCase):
    def test_all_land_and_currency_host_string_field_and_substitution_contracts(self):
        # The reusable currency controller is hosted by both real floaters.
        # Their direct root strings are separate from LLTrans global strings.
        for kind, files in (
            ('buy_land', ('llfloaterbuyland.cpp', 'llcurrencyuimanager.cpp')),
            ('sell_land', ('llfloatersellland.cpp',)),
            ('buy_currency', ('llfloaterbuycurrency.cpp', 'llcurrencyuimanager.cpp')),
        ):
            base=ET.parse(SKINS/f'default/xui/en/floater_{kind}.xml').getroot()
            names={node.get('name') for node in base.iter() if node.get('name')}
            strings={node.get('name') for node in base if node.tag in ('floater.string','string')}
            texts={node.get('name'):''.join(node.itertext()) for node in base.iter() if node.get('name')}
            for file in files:
                source=(ROOT/'indra/newview'/file).read_text(encoding='utf-8')
                # Ignore comments, retaining string literals and actual LLTrans qualification.
                source=re.sub(r'/\*[\s\S]*?\*/|//[^\n]*','',source)
                fields=re.findall(r'getChild(?:View|<[^>]+>)\(\s*"([^"]+)"',source)
                root_strings=re.findall(r'(?<!::)\bgetString\(\s*"([^"]+)"',source)
                substitutions=re.findall(r'getChild<[^>]+>\("([^"]+)"\)->setTextArg\("([^"]+)"',source)
                for field in fields:
                    with self.subTest(kind=kind,file=file,field=field):self.assertIn(field,names)
                for string in root_strings:
                    with self.subTest(kind=kind,file=file,string=string):self.assertIn(string,strings)
                for field,parameter in substitutions:
                    with self.subTest(kind=kind,file=file,field=field,parameter=parameter):
                        self.assertIn(parameter,texts[field])
            # Every localization that overrides an estimate keeps the same binding.
            for path in SKINS.rglob(f'floater_{kind}.xml'):
                for node in ET.parse(path).getroot().iter():
                    if node.get('name')=='currency_est' and ''.join(node.itertext()).strip():
                        with self.subTest(path=str(path.relative_to(SKINS))):
                            self.assertIn('[LOCALAMOUNT]',''.join(node.itertext()))

    def test_land_purchase_controller_fields_and_every_shipped_overlay(self):
        for kind in ('buy', 'sell'):
            base_path=SKINS/f'default/xui/en/floater_{kind}_land.xml'
            base=ET.parse(base_path).getroot()
            names={node.get('name') for node in base.iter() if node.get('name')}
            source=(ROOT/f'indra/newview/llfloater{kind}land.cpp').read_text(encoding='utf-8')
            fields=re.findall(r'getChild(?:View|<[^>]+>)\(\s*"([^"]+)"',source)
            for field in fields:
                with self.subTest(kind=kind,field=field):self.assertIn(field,names)
            def compare(expected,actual,path):
                self.assertEqual((expected.tag,expected.get('name')),(actual.tag,actual.get('name')),str(path))
                for child in actual:
                    candidates=[node for node in expected if (node.tag,node.get('name'))==(child.tag,child.get('name'))]
                    self.assertEqual(len(candidates),1,f'{path}: orphan/misnested {child.tag} {child.get("name")}')
                    compare(candidates[0],child,path)
            for path in sorted(SKINS.rglob(f'floater_{kind}_land.xml')):
                if path==base_path:continue  # base widget fields were checked above
                with self.subTest(path=str(path.relative_to(SKINS))):compare(base,ET.parse(path).getroot(),path)

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
