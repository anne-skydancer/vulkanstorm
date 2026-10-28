"""Configure the production NDOF module with an isolated package fixture."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NdofCmakeTest(unittest.TestCase):
    def configure(self, present, multiple=False):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / 'modules').mkdir()
            (source / 'modules/Prebuilt.cmake').write_text('''
function(use_prebuilt_binary package)
  if(NOT package STREQUAL "open-libndofdev")
    message(FATAL_ERROR "Unexpected Linux NDOF package: ${package}")
  endif()
endfunction()
''')
            archive = source / 'package with spaces/lib/release/libndofdev.a'
            archive.parent.mkdir(parents=True)
            if present:
                archive.write_bytes(b'fixture')
            directories = archive.parent.as_posix()
            if multiple:
                fallback = source / 'fallback/libndofdev.a'
                fallback.parent.mkdir()
                fallback.write_bytes(b'fallback fixture')
                directories = f'{source.as_posix()}/missing;{directories};{fallback.parent.as_posix()}'
            (source / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.16)
project(NdofFixture NONE)
set(LINUX TRUE)
set(WINDOWS FALSE)
set(DARWIN FALSE)
set(CMAKE_MODULE_PATH "{source.as_posix()}/modules")
set(ARCH_PREBUILT_DIRS_RELEASE "{directories}")
include("{ROOT.as_posix()}/indra/cmake/NDOF.cmake")
get_target_property(linked ll::ndof INTERFACE_LINK_LIBRARIES)
if(NOT linked STREQUAL "{archive.as_posix()}")
  message(FATAL_ERROR "NDOF target links unexpected payload: ${{linked}}")
endif()
get_target_property(definitions ll::ndof INTERFACE_COMPILE_DEFINITIONS)
if(NOT definitions STREQUAL "LIB_NDOF=1")
  message(FATAL_ERROR "NDOF target lacks its feature definition")
endif()
''')
            return subprocess.run(['cmake', '-G', 'Ninja' if sys.platform == 'win32' else 'Unix Makefiles',
                                   '-S', str(source), '-B', str(source / 'build')],
                                  capture_output=True, text=True)

    def test_links_installed_linux_archive(self):
        result = self.configure(True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_directory_list_skips_missing_and_uses_first_existing_archive(self):
        result = self.configure(True, multiple=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_archive_fails_configuration(self):
        result = self.configure(False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Could not find NDOF library:', result.stderr)


if __name__ == '__main__':
    unittest.main()
