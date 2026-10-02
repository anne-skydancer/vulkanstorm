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

if __name__ == '__main__':
    unittest.main()
