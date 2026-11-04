# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check the installer payload against staged files, including excluded artifacts."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'indra/newview'))
from viewer_manifest import Windows_x86_64_Manifest

class InnoPayloadTest(unittest.TestCase):
    def test_only_manifest_files_ship_with_correct_nested_destinations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'skins/ansastorm_modern').mkdir(parents=True)
            shipped = ['Vulkanstorm-Release.exe', 'skins/ansastorm_modern/colors.xml']
            for name in shipped + ['vulkanstorm-bin.pdb', 'stray-Setup.exe']:
                (root / name).write_bytes(b'fixture')
            manifest = object.__new__(Windows_x86_64_Manifest)
            manifest.get_dst_prefix = lambda: str(root)
            manifest.file_list = [('source', str(root / name)) for name in shipped + ['vulkanstorm-bin.pdb']]
            manifest.file_list += [('', str(root / 'stray-Setup.exe'))]
            commands = manifest.inno_file_commands()
            self.assertEqual(len(commands.splitlines()), 2)
            self.assertIn('DestDir: "{app}"', commands)
            self.assertIn('DestDir: "{app}\\skins\\ansastorm_modern"', commands)
            self.assertNotIn('.pdb', commands)
            self.assertNotIn('stray-Setup.exe', commands)

    def test_licence_texts_copy_beside_the_viewer(self):
        newview = Path(__file__).resolve().parents[2] / 'indra/newview'
        with tempfile.TemporaryDirectory() as directory:
            manifest = object.__new__(Windows_x86_64_Manifest)
            Windows_x86_64_Manifest.__init__(manifest, {
                'source': str(newview), 'artwork': str(newview),
                'build': str(newview), 'dest': directory})
            manifest.actions = ['copy']
            manifest.path_licence_texts()
            self.assertEqual(manifest.missing, [])
            root = Path(directory)
            self.assertEqual(sorted(p.name for p in root.iterdir()),
                             ['GPL-3.0-license.txt', 'LGPL-license.txt', 'LICENSE.txt'])
            self.assertIn('Version 3, 29 June 2007', (root / 'GPL-3.0-license.txt').read_text())
            self.assertIn('Version 2.1, February 1999', (root / 'LGPL-license.txt').read_text())
            self.assertIn('LICENSE-GPL-3.0', (root / 'LICENSE.txt').read_text())

if __name__ == '__main__':
    unittest.main()
