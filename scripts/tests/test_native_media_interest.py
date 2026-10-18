"""Compare native media source ordering, mute and proximity with the GL peer."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method

ROOT = Path(__file__).resolve().parents[2]


class NativeMediaInterest(unittest.TestCase):
    def test_visibility_expiry_mute_proximity_and_owner_retirement(self):
        source = (ROOT / 'indra/newview/llviewermedia.cpp').read_text()
        start = source.index('    if (!mNativeSurfaces.empty())')
        bridge = source[start:source.index('#endif', start)]
        methods = '\n'.join(method(source, name) for name in (
            'void LLViewerMediaImpl::setNativeSurface(',
            'void LLViewerMediaImpl::removeNativeSurface(',
            'std::optional<LLVector3d> LLViewerMediaImpl::nativeMediaSpatialPosition()const'))
        directory = Path(__file__).parent
        fixture = (directory / 'media_interest_fixture.cpp').read_text()
        reference = (directory / 'media_interest_gl_reference.cpp').read_text()
        fixture = fixture.replace('BRIDGE', bridge).replace('METHODS', methods).replace('REFERENCE', reference)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            cpp = Path(temp) / 'interest.cpp'
            exe = Path(temp) / 'interest.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
