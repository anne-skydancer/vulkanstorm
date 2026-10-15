"""Exercise real cached compilation and invalidation, plus safe tool extraction."""
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ci_compiler_cache import ROOT, unpack_binary


class ToolTests(unittest.TestCase):
    def test_checksum_and_single_executable_are_required(self):
        for windows in (True, False):
            with self.subTest(windows=windows):
                data = io.BytesIO()
                name = 'sccache.exe' if windows else 'sccache'
                if windows:
                    with zipfile.ZipFile(data, 'w') as archive:
                        archive.writestr('release/' + name, b'cache tool')
                        archive.writestr('../../outside', b'never extracted')
                else:
                    with tarfile.open(fileobj=data, mode='w:gz') as archive:
                        member = tarfile.TarInfo('release/' + name)
                        member.size = len(b'cache tool')
                        archive.addfile(member, io.BytesIO(b'cache tool'))
                payload = data.getvalue()
                pin = dict(sha256=hashlib.sha256(payload).hexdigest())
                self.assertEqual(unpack_binary(payload, pin, windows), b'cache tool')
                with self.assertRaisesRegex(RuntimeError, 'checksum'):
                    unpack_binary(payload + b'corrupted', pin, windows)
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as archive:
            archive.writestr('one/sccache.exe', b'a')
            archive.writestr('two/sccache.exe', b'b')
        payload = data.getvalue()
        with self.assertRaisesRegex(RuntimeError, 'Expected one'):
            unpack_binary(payload, dict(sha256=hashlib.sha256(payload).hexdigest()), True)

    def test_both_workflows_use_cache_before_build_and_save_before_tests(self):
        # Catch the tempting but ineffective MSBuild + launcher combination.
        for filename, configuration in [('software_vulkan.yml', 'RelWithDebInfo'), ('build.yaml', 'Release')]:
            text = (ROOT / '.github/workflows' / filename).read_text()
            self.assertIn("-G 'Ninja Multi-Config'", text)
            self.assertIn('-C scripts/ci_compiler_cache.cmake', text)
            self.assertIn('restore-keys: viewer-objects-', text)
            self.assertIn(f'-{configuration}-${{{{ hashFiles', text)
            self.assertLess(text.index('Restore compiled objects'), text.index('-C scripts/ci_compiler_cache.cmake'))
            self.assertIn('if: always() && inputs.clean_compiler_cache', text)
            self.assertIn('compiler-cache.txt', text if filename == 'build.yaml' else
                          (ROOT / 'scripts/ci_compiler_cache.py').read_text())
        qualification = (ROOT / '.github/workflows/software_vulkan.yml').read_text()
        key = next(line for line in qualification.splitlines() if 'key: software-vulkan-' in line)
        self.assertNotIn('software_vulkan.yml', key)


class ActualCompilationTests(unittest.TestCase):
    def test_unchanged_code_hits_and_header_and_flag_changes_miss(self):
        tool = shutil.which('sccache')
        if not tool:
            if os.environ.get('VS_REQUIRE_COMPILER_CACHE') == '1':
                self.fail('Pinned compiler cache is not installed')
            self.skipTest('Actual compiler-cache test runs after tool setup in each platform build')
        compiler = shutil.which('cl') if platform.system() == 'Windows' else shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # Private server/cache; do not disturb a developer's compiler cache.
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            env = dict(os.environ, SCCACHE_DIR=str(root / 'cache'), SCCACHE_SERVER_PORT=str(port),
                       SCCACHE_DIRECT='false')
            env.pop('SCCACHE_GHA_ENABLED', None)

            def run(*args):
                result = subprocess.run([str(arg) for arg in args], env=env, cwd=root,
                                        text=True, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout

            def stats():
                return json.loads(run(tool, '--show-stats', '--stats-format', 'json'))['stats']

            def count(data, key):
                return sum(data[key]['counts'].values())

            source = root / 'source'
            source.mkdir()
            (source / 'value.h').write_text('#define VALUE 7\n')
            (source / 'main.cpp').write_text('#include "value.h"\n#ifndef OFFSET\n#define OFFSET 0\n#endif\n'
                '#if __has_include("optional.h")\n#include "optional.h"\n#else\n#define BONUS 0\n#endif\n'
                'int main(){return VALUE+OFFSET+BONUS;}\n')
            (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(CacheProbe LANGUAGES CXX)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
add_executable(probe main.cpp)
target_precompile_headers(probe PRIVATE value.h)
target_compile_definitions(probe PRIVATE OFFSET=${PROBE_OFFSET})
''')
            build = root / 'build'
            generator = 'Ninja Multi-Config' if platform.system() == 'Windows' else 'Ninja'
            try:
                run('cmake', '-C', ROOT / 'scripts/ci_compiler_cache.cmake', '-S', source, '-B', build,
                    '-G', generator, '-DCMAKE_CXX_COMPILER=' + compiler, '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                    '-DPROBE_OFFSET=0')
                run('cmake', '--build', build, '--config', 'RelWithDebInfo')
                before = stats()
                self.assertGreater(count(before, 'cache_misses'), 0)
                run('cmake', '--build', build, '--config', 'RelWithDebInfo', '--target', 'clean')
                run('cmake', '--build', build, '--config', 'RelWithDebInfo')
                repeated = stats()
                self.assertGreater(count(repeated, 'cache_hits'), count(before, 'cache_hits'))
                (source / 'value.h').write_text('#define VALUE 9\n')
                run('cmake', '--build', build, '--config', 'RelWithDebInfo')
                changed = stats()
                self.assertGreater(count(changed, 'cache_misses'), count(repeated, 'cache_misses'))
                executable = build / ('RelWithDebInfo/probe.exe' if platform.system() == 'Windows' else 'probe')
                self.assertEqual(subprocess.run([str(executable)]).returncode, 9)
                run('cmake', '-S', source, '-B', build, '-DPROBE_OFFSET=2')
                run('cmake', '--build', build, '--config', 'RelWithDebInfo')
                self.assertGreater(count(stats(), 'cache_misses'), count(changed, 'cache_misses'))
                self.assertEqual(subprocess.run([str(executable)]).returncode, 11)
                # A previously missing optional include must not produce a stale
                # direct-mode hit. Force a build as a fresh CI checkout would.
                (source / 'optional.h').write_text('#define BONUS 4\n')
                run('cmake', '--build', build, '--config', 'RelWithDebInfo', '--target', 'clean')
                run('cmake', '--build', build, '--config', 'RelWithDebInfo')
                self.assertEqual(subprocess.run([str(executable)]).returncode, 15)
                cpp = source / 'main.cpp'
                cpp.write_text(cpp.read_text().replace('return VALUE+', 'return 1+VALUE+'))
                run('cmake', '--build', build, '--config', 'RelWithDebInfo')
                self.assertEqual(subprocess.run([str(executable)]).returncode, 16)
            finally:
                subprocess.run([tool, '--stop-server'], env=env, capture_output=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
