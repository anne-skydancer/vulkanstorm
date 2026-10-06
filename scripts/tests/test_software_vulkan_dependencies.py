"""Resolve relocated libraries only inside the pinned runtime installation."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_software_vulkan import installed_driver, main, stage_manifest


class ManifestStagingTests(unittest.TestCase):
    def test_validation_only_runtime_leaves_driver_discovery_to_the_machine(self):
        source = self.root / 'source/validation/scripts'
        source.mkdir(parents=True)
        (source / 'known_good.json').write_text('{"repos": []}')
        layer = self.root / 'validation/share/VkLayer_khronos_validation.json'
        layer.parent.mkdir(parents=True)
        library = self.root / 'validation/lib/validation.so'
        library.parent.mkdir(parents=True); library.write_bytes(b'layer')
        layer.write_text(json.dumps({'layer': {'library_path': str(library)}}))
        with patch('build_software_vulkan.checkout') as checkout, patch('build_software_vulkan.run'), \
             patch('build_software_vulkan.platform.system', return_value='Linux'), \
             patch.object(sys, 'argv', ['builder', '--work-dir', str(self.root), '--driver', 'installed']), \
             patch('builtins.print'):
            main()
        runtime = json.loads((self.root / 'runtime.json').read_text())
        self.assertIsNone(runtime['icd'])
        self.assertEqual(runtime['driver_discovery'], 'system')
        self.assertIsNone(runtime['vendor'])
        self.assertEqual(checkout.call_count, 1)
        self.assertFalse((self.root/'staged/icd').exists())

    def test_installed_driver_records_exact_files_without_copying_vendor_stack(self):
        import hashlib
        self.manifest.write_text(json.dumps({'ICD': {'library_path': str(self.library)}}))
        identity = installed_driver(self.manifest)
        self.assertEqual(identity['library'], str(self.library.resolve()))
        self.assertEqual(identity['library_sha256'], hashlib.sha256(b'pinned library').hexdigest())

    def test_installed_soname_requires_explicit_library_and_rejects_wrong_override(self):
        self.manifest.write_text(json.dumps({'ICD': {'library_path': 'validation.so'}}))
        with self.assertRaisesRegex(RuntimeError, 'Provide --icd-library'):
            installed_driver(self.manifest)
        self.assertEqual(installed_driver(self.manifest, self.library)['library'], str(self.library.resolve()))
        with self.assertRaisesRegex(RuntimeError, 'basename differs'):
            installed_driver(self.manifest, self.root / 'decoy.so')

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
