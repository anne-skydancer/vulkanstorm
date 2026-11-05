"""GLOD stays retired and the mesh uploader's LOD selectors stay consistent.

Port of the earlier scripts/validate_mesh_physics.ps1 GLOD checks. The LOD
source combo boxes are read by index (LLModelPreview::eLoDMode), so the
English item order must match the enum, and translations may only relabel
items the English file defines.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
XUI = ROOT / 'indra/newview/skins/default/xui'
ENGLISH = XUI / 'en/floater_model_preview.xml'
LODS = ('high', 'medium', 'low', 'lowest')

# eLoDMode enumerator -> English combo item name
MODE_ITEMS = {
    'LOD_FROM_FILE': 'Load from file',
    'MESH_OPTIMIZER_AUTO': 'MeshOpt Auto',
    'MESH_OPTIMIZER_PRECISE': 'MeshOptCombine',
    'MESH_OPTIMIZER_SLOPPY': 'MeshOptSloppy',
    'USE_LOD_ABOVE': 'Use LoD above',
}

ACTIVE_PATHS = (
    'autobuild.xml',
    'indra/cmake',
    'indra/newview/CMakeLists.txt',
    'indra/newview/viewer_manifest.py',
    'indra/newview/llmodelpreview.cpp',
    'indra/newview/llmodelpreview.h',
    'indra/newview/llfloatermodelpreview.cpp',
    'indra/newview/llfloatermodelpreview.h',
    'indra/newview/licenses-win32.txt',
    'indra/newview/licenses-linux.txt',
    'indra/newview/licenses-mac.txt',
    'indra/newview/app_settings',
    'indra/newview/skins',
)
OBSOLETE = re.compile(r'glod|mesh_upload_default_to_reliable', re.I)


def parse(path):
    return ET.parse(path).getroot()


def combo_items(root, name):
    combo = next((c for c in root.iter('combo_box') if c.get('name') == name), None)
    return None if combo is None else [i.get('name') for i in combo.findall('item')]


def lod_mode_enum():
    header = (ROOT / 'indra/newview/llmodelpreview.h').read_text(encoding='utf-8')
    match = re.search(r'typedef enum\s*\{(.*?)\}\s*eLoDMode;', header, re.S)
    assert match, 'eLoDMode not found'
    body = re.sub(r'//[^\n]*', '', match.group(1))
    names = []
    for entry in filter(None, (e.strip() for e in body.split(','))):
        name, _, value = entry.partition('=')
        if value.strip():
            # Only the first enumerator may carry an explicit value, and it must be 0.
            assert not names and value.strip() == '0', entry
        names.append(name.strip())
    return names


class GlodRetiredTest(unittest.TestCase):
    def test_no_obsolete_controls_in_any_language(self):
        files = sorted(XUI.glob('*/floater_model_preview.xml'))
        self.assertGreater(len(files), 1)
        for path in files:
            for element in parse(path).iter():
                with self.subTest(path=path.parent.name, tag=element.tag):
                    self.assertNotEqual(element.get('name'), 'Generate')
                    self.assertNotEqual(element.get('name'), 'mesh_upload_default_to_reliable')
                    self.assertNotEqual(element.get('control_name'), 'FSMeshUploadUseGLODAsDefault')

    def test_translations_only_relabel_english_items(self):
        english = parse(ENGLISH)
        for path in sorted(XUI.glob('*/floater_model_preview.xml')):
            root = parse(path)
            for combo in root.iter('combo_box'):
                name = combo.get('name') or ''
                if not name.startswith('lod_source_'):
                    continue
                base = combo_items(english, name)
                self.assertIsNotNone(base, f'{path}: {name} not in English')
                for item in combo.findall('item'):
                    with self.subTest(path=path.parent.name, combo=name, item=item.get('name')):
                        self.assertIn(item.get('name'), base)

    def test_english_item_order_matches_lod_mode_enum(self):
        modes = lod_mode_enum()
        self.assertEqual(set(modes), set(MODE_ITEMS), 'eLoDMode gained or lost a mode')
        expected = [MODE_ITEMS[m] for m in modes]
        english = parse(ENGLISH)
        for lod in LODS:
            with self.subTest(lod=lod):
                want = [i for i in expected if lod != 'high' or i != 'Use LoD above']
                self.assertEqual(combo_items(english, f'lod_source_{lod}'), want)

    def test_lod_selector_indices(self):
        english = parse(ENGLISH)
        base = ['Load from file', 'MeshOpt Auto', 'MeshOptCombine', 'MeshOptSloppy']
        for lod in LODS:
            with self.subTest(lod=lod):
                want = base if lod == 'high' else base + ['Use LoD above']
                self.assertEqual(combo_items(english, f'lod_source_{lod}'), want)

    def test_settings_and_package_xml_parse(self):
        for rel in ('autobuild.xml', 'indra/newview/app_settings/settings.xml',
                    'indra/newview/skins/default/xui/en/floater_about.xml',
                    'indra/newview/skins/default/xui/ja/floater_about.xml'):
            with self.subTest(path=rel):
                parse(ROOT / rel)

    def test_glod_reference_audit(self):
        self.assertFalse((ROOT / 'indra/cmake/GLOD.cmake').exists())
        hits = []
        for rel in ACTIVE_PATHS:
            target = ROOT / rel
            files = [target] if target.is_file() else sorted(p for p in target.rglob('*') if p.is_file())
            for path in files:
                text = path.read_bytes().decode('utf-8', errors='replace')
                for number, line in enumerate(text.splitlines(), 1):
                    if OBSOLETE.search(line):
                        hits.append(f'{path.relative_to(ROOT).as_posix()}:{number}: {line.strip()[:100]}')
        self.assertEqual(hits, [])

    @unittest.skipUnless(shutil.which('git') and (ROOT / '.git').exists(), 'needs a git checkout')
    def test_diff_whitespace(self):
        # CI checks out the PR merge commit and sets GLOD_DIFF_BASE to its first
        # parent (the base tip), so the PR's own changes are checked; a clean
        # checkout's working-tree diff would always be empty. Locally, without
        # the variable, the uncommitted changes are checked.
        base = os.environ.get('GLOD_DIFF_BASE')
        command = ['git', '-C', str(ROOT), 'diff', '--check']
        if base:
            command += [base, 'HEAD']
        command += ['--', *ACTIVE_PATHS, 'scripts/tests/test_glod_retired.py']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
