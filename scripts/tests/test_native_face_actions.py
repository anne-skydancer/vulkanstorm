"""Shared original PBR policy and retained native alpha/TE actions."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeFaceActions(unittest.TestCase):
    def compile_run(self,code):
        compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'actions.cpp';exe=path.with_suffix('.exe');path.write_text(code)
            p=subprocess.run([compiler,'-std=c++20','-DVS_NATIVE_VULKAN=1','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
    def test_original_pbr_transforms_permissions_and_refusal(self):
        source=(ROOT/'indra/newview/llselectmgr.cpp').read_text();source=source[source.index('bool LLSelectMgr::selectionSetGLTFMaterial('):]
        kernel=(ROOT/'indra/newview/vspbrfacepolicy.h').read_text();kernel=kernel[kernel.index('template<class Action'):]
        code=(ROOT/'scripts/tests/native_face_edit/pbr_policy.cpp').read_text().replace('ORIGINAL',method(source,'bool apply(LLViewerObject* objectp, S32 te)')).replace('KERNEL',kernel)
        self.compile_run(code)
    def test_original_alpha_preserves_rgb_selection_permission_and_retirement(self):
        original=(ROOT/'indra/newview/llselectmgr.cpp').read_text();native=(ROOT/'indra/newview/vsfaceselection.cpp').read_text()
        code=(ROOT/'scripts/tests/native_face_edit/alpha_actions.cpp').read_text().replace('ORIGINAL',method(original,'void LLSelectMgr::selectionSetAlphaOnly(')).replace('NATIVE',method(native,'void VSEditSelection::selectionSetAlphaOnly('))
        self.compile_run(code)
    def test_actual_material_peer_refusal_stops_before_preview_and_wire(self):
        source=(ROOT/'indra/newview/vsmaterialselection.cpp').read_text()
        code=(ROOT/'scripts/tests/native_face_edit/material_mutation.cpp').read_text()
        code=code.replace('METHODS','\n'.join(method(source,name)for name in [
            'bool VSMaterialObject::queueApply(',
            'bool VSMaterialObject::setGLTFMaterialOverride(',
            'bool VSMaterialObject::setRenderMaterialID(',
            'bool VSMaterialObject::clearTEWaterExclusion(']))
        self.compile_run(code)
if __name__=='__main__':unittest.main()
