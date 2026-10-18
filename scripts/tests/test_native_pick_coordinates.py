"""Frozen OpenGL differential and native immutable picking-owner regressions.

Numeric fixture types are independent lightweight test doubles. The original
reference bodies stay frozen; the production shared kernels/owners are extracted
from current source. A separate viewer build checks actual LL math bindings.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method
ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / 'fixtures/native_pick'

def viewer(name):
    return (ROOT / 'indra/newview' / name).read_text(encoding='utf-8-sig')

def fixture(name):
    return (FIXTURES / name).read_text(encoding='utf-8-sig')

def surface_math():
    return '\n'.join(line for line in viewer('vssurfacecoordinates.h').splitlines()
                     if not line.startswith('#'))

class NativePickCoordinates(unittest.TestCase):
    def run_cpp(self, text):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'C++ compiler is required')
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'fixture.cpp'
            binary = source.with_suffix('.exe')
            source.write_text(text, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++20', str(source), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_frozen_focus_and_exact_lazy_getter_order(self):
        source = viewer('llagentcamera.cpp')
        begin = source.index('struct ReferenceFocusObject')
        end = source.index('LLVector3 LLAgentCamera::calcFocusOffset(', begin)
        shared = source[begin:end].rstrip()
        self.assertTrue(shared.endswith('}'))  # anonymous namespace
        text = fixture('focus_fixture.cpp').replace('REFERENCE\n', fixture('focus_reference.cpp') + '\n')
        text = text.replace('CURRENT\n', shared[:-1] + '\n')
        text = text.replace('REFERENCE_BOUNDS', fixture('media_bounds_reference.cpp'))
        bounds = method(viewer('vssurfacecoordinates.h'), 'std::array<LLVector3,8> volumeBounds(')
        text = text.replace('CURRENT_BOUNDS', 'namespace VSSurfaceCoordinates{template<class VolumePositionToAgent>\n' + bounds + '}\n')
        self.run_cpp(text)

    def test_frozen_complete_surface_and_lazy_getter_order(self):
        text = fixture('surface_fixture.cpp')
        text = text.replace('REFERENCE_MATH\n', fixture('surface_math_reference.cpp') + '\n')
        text = text.replace('REFERENCE\n', fixture('surface_reference.cpp') + '\n')
        self.run_cpp(text.replace('CURRENT\n', surface_math() + '\n'))

    def test_actual_focus_owner_frames_and_retirement(self):
        text = fixture('focus_owner_fixture.cpp')
        header = '\n'.join(line for line in viewer('vscamerafocusobject.h').splitlines()
                           if not line.startswith('#'))
        text = text.replace('FOCUS_FRAME', header)
        text = text.replace('BOUND_KERNEL', method(viewer('vssurfacecoordinates.h'), 'std::array<LLVector3,8> volumeBounds('))
        text = text.replace('MEDIA_QUERY', method(viewer('vsworldscene.cpp'),
            'std::optional<std::array<LLVector3,8>> VSWorldScene::mediaFaceBounds('))
        self.run_cpp(text.replace('QUERY', method(viewer('vsworldscene.cpp'),
            'std::optional<VSCameraFocusObject> VSWorldScene::focusObject(')))

    def test_actual_native_pick_consumer_and_requested_metadata(self):
        self.run_cpp(fixture('native_pick_fixture.cpp').replace('BODY',
            method(viewer('vsviewerwindow.cpp'), 'bool LLViewerWindow::nativePick(')))

    def test_actual_publication_refusal_and_retained_source_budget(self):
        scene = viewer('vsworldscene.cpp')
        begin = scene.index('    struct PickSurface')
        end = scene.index('    struct Face', begin)
        text = fixture('publication_fixture.cpp').replace('METADATA', scene[begin:end])
        begin = scene.index('                auto pickSurface=std::make_shared<Impl::PickSurface>();')
        end = scene.index('                std::vector<unsigned> indices', begin)
        text = text.replace('ADMISSION', scene[begin:end])
        begin = scene.index('                if (!mImpl->renderer.publishMesh(face.mesh, vertices, indices,', end)
        end = scene.index('\n            }', begin)
        text = text.replace('PUBLISH', scene[begin:end])
        begin = scene.index('        if (!uploadDeferred)\n')
        end = scene.index('\n        for (std::size_t f', begin)
        text = text.replace('ATOMIC', scene[begin:end]).replace('SURFACE_MATH', surface_math())
        text = text.replace('RESOURCE_BUDGET', str(ROOT / 'indra/newview/vsresourcebudget.h').replace('\\', '/'))
        self.run_cpp(text)

if __name__ == '__main__':
    unittest.main()
