"""Resolve relocated libraries only inside the pinned runtime installation."""
import json
import os
import shutil
import subprocess
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



class PrebuiltDownloadRetryTests(unittest.TestCase):
    def test_actual_cmake_retries_only_server_errors_in_software_ci(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        cmake = shutil.which('cmake')
        self.assertIsNotNone(compiler)
        self.assertIsNotNone(cmake)
        fixture = r"""
#include <cstdlib>
#include <fstream>
#include <string>
#include <iostream>
int main(int argc,char** argv) {
    const std::string counter=std::getenv("PREBUILT_TEST_COUNTER");
    if(argc>3 && std::string(argv[1])=="-E") {
        std::ofstream(counter+".sleep",std::ios::app)<<argv[3]<<"\n";return 0;
    }
    int count=0;std::ifstream(counter)>>count;std::ofstream(counter)<<++count;
    const std::string mode=std::getenv("PREBUILT_TEST_MODE");
    if(mode=="success" || ((mode=="transient" || mode=="xxhash") && count==2)) return 0;
    if(mode=="auth") std::cerr<<"error: HTTP Error 401: Unauthorized\n";
    else std::cerr<<"error: HTTP Error 503: Service unavailable\n";
    if(mode=="xxhash") std::cerr<<"ERROR: Failed to download package xxhash from https://github.com/secondlife/3p-xxhash/xxhash.tar.zst\n";
    if(mode=="checksum") std::cerr<<"ERROR: checksum mismatch\n";
    if(mode=="unpack") std::cerr<<"ERROR: failed to unpack package\n";
    return 1;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            cpp = base / 'autobuild_fixture.cpp'
            executable = base / 'autobuild_fixture.exe'
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            cases = [('success', True, True, True, 1, []),
                     ('transient', True, True, True, 2, ['10']),
                     ('persistent', True, True, False, 3, ['10', '20']),
                     ('xxhash', True, True, True, 2, ['10']),
                     ('auth', True, True, False, 1, []),
                     ('checksum', True, True, False, 1, []),
                     ('unpack', True, True, False, 1, []),
                     ('transient', True, False, False, 1, []),
                     ('transient', False, True, False, 1, [])]
            for index, (mode, diagnostics, ci, passed, attempts, delays) in enumerate(cases):
                with self.subTest(mode=mode, diagnostics=diagnostics, ci=ci):
                    directory = base / str(index)
                    source = directory / 'source'
                    modules = source / 'modules'
                    modules.mkdir(parents=True)
                    (modules / 'FindAutobuild.cmake').write_text('')
                    (directory / 'autobuild.xml').write_text('fixture')
                    script = directory / 'test.cmake'
                    script.write_text('\n'.join([
                        f'set(CMAKE_SOURCE_DIR "{source.as_posix()}")',
                        f'set(CMAKE_MODULE_PATH "{modules.as_posix()}")',
                        f'set(AUTOBUILD_INSTALL_DIR "{(directory / "installed").as_posix()}")',
                        f'set(AUTOBUILD_EXECUTABLE "{executable.as_posix()}")',
                        f'set(CMAKE_COMMAND "{executable.as_posix()}")',
                        f'set(VS_VULKAN_DIAGNOSTICS {"ON" if diagnostics else "OFF"})',
                        f'include("{(root / "indra/cmake/Prebuilt.cmake").as_posix()}")',
                        'use_prebuilt_binary(fixture)']), encoding='utf-8')
                    counter = directory / 'attempts'
                    env = dict(os.environ, GITHUB_ACTIONS='true' if ci else 'false',
                               PREBUILT_TEST_COUNTER=str(counter), PREBUILT_TEST_MODE=mode)
                    result = subprocess.run([cmake, '-P', str(script)], env=env,
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode == 0, passed, result.stdout + result.stderr)
                    self.assertEqual(int(counter.read_text()), attempts)
                    delay_file = Path(str(counter) + '.sleep')
                    self.assertEqual(delay_file.read_text().splitlines() if delay_file.exists() else [], delays)
                    status = directory / 'installed/cmake_tracking/fixture_installed'
                    self.assertEqual(status.read_text(), '0' if passed else '1')

if __name__ == '__main__':
    unittest.main()
