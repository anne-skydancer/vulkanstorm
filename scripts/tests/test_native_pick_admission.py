"""Native picking admission against frozen OpenGL policy and actual CPU owners."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_pick'
def viewer(name):return (ROOT/'indra/newview'/name).read_text(encoding='utf-8-sig')
def fixture(name):return (FIXTURES/name).read_text(encoding='utf-8-sig')
def policy():return '\n'.join(line for line in viewer('vspickpolicy.h').splitlines()if not line.startswith('#'))
def actual_pick():
    source=viewer('vsworldscene.cpp');signature='std::optional<VSWorldScene::Pick> VSWorldScene::pick('
    first=source.index(signature);second=source.index(signature,first+1)
    return method(source[second:],signature)
class NativePickAdmission(unittest.TestCase):
    def run_cpp(self,text):
        compiler=shutil.which('clang++')or shutil.which('g++')
        self.assertIsNotNone(compiler,'C++ compiler is required')
        with tempfile.TemporaryDirectory()as temp:
            source=Path(temp)/'fixture.cpp';binary=source.with_suffix('.exe');source.write_text(text,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++20',str(source),'-o',str(binary)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            tested=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
            self.assertEqual(tested.returncode,0,tested.stdout+tested.stderr)
    def test_frozen_volume_decisions_and_lazy_queries(self):
        text=fixture('pick_volume_policy_fixture.cpp').replace('REFERENCE',fixture('pick_volume_gl_reference.cpp'))
        self.run_cpp(text.replace('POLICY',policy()))
    def test_frozen_legacy_alpha_materials_and_mask_query_counts(self):
        text=fixture('pick_alpha_fixture.cpp').replace('REFERENCE',fixture('pick_alpha_gl_reference.cpp'))
        self.run_cpp(text.replace('POLICY',policy()))
    def test_frozen_actual_gl_pick_masks_and_native_top_left_pixels(self):
        text=fixture('pick_mask_fixture.cpp').replace('REFERENCE',fixture('pick_mask_gl_reference.cpp'))
        self.run_cpp(text.replace('POLICY',policy()))
    def test_actual_scene_admission_before_nearest_hit_and_owner_retirement(self):
        renderer=method(viewer('vsworldrenderer.cpp'),'bool VSWorldRenderer::intersect(').replace('VSWorldRenderer::intersect(','VSWorldRenderer::exact(',1)
        text=fixture('pick_admission_owner_fixture.cpp').replace('RENDERER',renderer).replace('POLICY',policy())
        text=text.replace('PACKET_OBJECT\n',method(viewer('vsworldscene.cpp'),'std::optional<VSWorldScene::ObjectHandle> VSWorldScene::packetObject(')+'\n')
        self.run_cpp(text.replace('PICK\n',actual_pick()+'\n'))
    def test_frozen_avatar_collision_volumes_and_lazy_first_hit(self):
        text=fixture('avatar_collision_fixture.cpp').replace('SPHERE',fixture('avatar_sphere_reference.cpp'))
        text=text.replace('REFERENCE',fixture('avatar_collision_gl_reference.cpp'))
        self.run_cpp(text.replace('NATIVE',method(viewer('vsworldavatar.cpp'),
            'std::optional<VSWorldRenderer::SurfaceHit> VSWorldAvatar::collisionPick(')))
    def test_actual_reflection_probe_extra_parameter_admission(self):
        source=viewer('vsworldscene.cpp')
        helpers=method(source,'U32 littleU32(')+'\n'+method(source,'bool ownerExtras(')
        self.run_cpp(fixture('pick_probe_decode_fixture.cpp').replace('BODY',helpers))
    def test_default_pick_flags_are_deterministic(self):
        self.run_cpp(fixture('pick_constructor_fixture.cpp').replace('BODY',method(viewer('llviewerwindow.cpp'),'LLPickInfo::LLPickInfo()')))
if __name__=='__main__':unittest.main()
