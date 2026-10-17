"""Public native rendering headers must coexist with X11's None macro."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADERS = '\n'.join(f'#include "{name}"' for name in (
    'vsworldcontracts.h', 'vsuirenderer.h', 'vsmaterial.h',
    'vsworldrenderer.h', 'vstextureupload.h'))
X11 = r'''
#if defined(__linux__) && __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#endif
#ifndef None
#define None 0L
#endif
'''
CHECKS = r'''
#if !defined(None) || None != 0L
#error Native headers must preserve the platform macro
#endif
using VSWorldContracts::ImageCompression;
static_assert(static_cast<int>(ImageCompression::Uncompressed) == 0);
static_assert(static_cast<int>(ImageCompression::BC3) == 1);
static_assert(VSWorldContracts::Image{}.compression == ImageCompression::Uncompressed);
static_assert(VSWorldContracts::Geometry{}.attributes ==
    (VSWorldContracts::Position | VSWorldContracts::Normal | VSWorldContracts::UV0));
'''

class NativePlatformHeaders(unittest.TestCase):
    def test_x11_before_and_after_native_headers(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            for order, source in (
                ('platform-first', X11 + HEADERS + CHECKS),
                ('native-first', HEADERS + X11 + CHECKS),
                ('codec-implementation', '#define STB_DXT_IMPLEMENTATION\n#include \"vstextureupload.h\"\n' + X11 + CHECKS),
            ):
                with self.subTest(order=order):
                    path = Path(temporary) / (order + '.cpp')
                    path.write_text(source)
                    result = subprocess.run([
                        compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsyntax-only', '-I', str(ROOT / 'indra/newview'), str(path),
                    ], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
