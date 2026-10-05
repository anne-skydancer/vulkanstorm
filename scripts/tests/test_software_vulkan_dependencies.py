"""Resolve relocated libraries only inside the pinned runtime installation."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_software_vulkan import stage_manifest


class ManifestStagingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.install = self.root / 'install'
        self.manifest = self.install / 'share/vulkan/explicit_layer.d/layer.json'
        self.manifest.parent.mkdir(parents=True)
        self.manifest.write_text(json.dumps({'layer': {'library_path': 'validation.so'}}))
        self.library = self.install / 'lib/validation.so'
        self.library.parent.mkdir()
        self.library.write_bytes(b'pinned library')

    def test_installed_basename_is_resolved_and_relocated(self):
        staged = stage_manifest(self.manifest, self.root / 'stage', 'layer', self.install)
        self.assertEqual(json.loads(staged.read_text())['layer']['library_path'], '.' + os.sep + 'validation.so')
        self.assertEqual((staged.parent / 'validation.so').read_bytes(), b'pinned library')

    def test_ambiguous_library_is_rejected(self):
        decoy = self.install / 'lib64/validation.so'
        decoy.parent.mkdir()
        decoy.write_bytes(b'decoy')
        with self.assertRaisesRegex(RuntimeError, 'Expected one installed'):
            stage_manifest(self.manifest, self.root / 'stage', 'layer', self.install)

    def test_missing_library_does_not_search_ambient_paths(self):
        self.library.unlink()
        (self.root / 'validation.so').write_bytes(b'ambient')
        with self.assertRaisesRegex(RuntimeError, 'Expected one installed'):
            stage_manifest(self.manifest, self.root / 'stage', 'layer', self.install)


if __name__ == '__main__':
    unittest.main()
