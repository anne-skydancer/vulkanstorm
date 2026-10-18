"""Actual shared baked attachment policy and original lazy GL call order."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeBakedTexture(unittest.TestCase):
    def test_original_policy_and_lazy_source_order(self):
        fixture=ROOT/'scripts/tests/native_face_edit'
        code=(fixture/'baked_policy.cpp').read_text()
        original=(fixture/'baked_policy_original.cpp').read_text()
        kernel=(ROOT/'indra/newview/vsbakedtexturepolicy.h').read_text()
        kernel=kernel[kernel.index('template<class Object'):]
        code=code.replace('ORIGINAL',original).replace('KERNEL',kernel)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'baked.cpp';exe=source.with_suffix('.exe');source.write_text(code)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
    def test_actual_native_avatar_ancestry_generation_media_and_retirement(self):
        scene=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
        avatar=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
        kernel=(ROOT/'indra/newview/vsbakedtexturepolicy.h').read_text()
        kernel=kernel[kernel.index('template<class Object'):]
        start=scene.index('    struct TextureSource\n');brace=scene.index('{',start);level=1;end=brace+1
        while level:
            if scene[end]=='{':level+=1
            elif scene[end]=='}':level-=1
            end+=1
        source=scene[start:end+1]
        cost_start=scene.index("    struct CostTexture\n");cost_end=scene.index("    U32 renderCost(",cost_start)
        cost_source=scene[cost_start:cost_end]
        kernel += "\ninline bool vs_image_alpha_mask"+(ROOT/"indra/llrender/vsimagealphamask.h").read_text().split("inline bool vs_image_alpha_mask",1)[1]
        code=(ROOT/'scripts/tests/native_face_edit/baked_owner.cpp').read_text()
        code=code.replace('EDITING',method(avatar,'bool isEditingAppearance() const override'))
        code=code.replace('\nAVATAR\n',method(avatar,'VSWorldAvatar::BakedTexture VSWorldAvatar::bakedTexture(')+'\n')
        code=code.replace('KERNEL',kernel).replace('TEXTURESOURCE',source)
        code=code.replace('COSTTEXTURE',cost_source).replace('COSTFUNCTION',method(scene,'    F32 textureCost('))
        code=code.replace('\nPUBLIC\n','\n'.join(method(scene,name) for name in [
            'VSWorldScene::ResolvedTexture VSWorldScene::resolveTexture(',
            'VSWorldScene::ResolvedTexture VSWorldScene::diffuseTexture('])+'\n')
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'owner.cpp';exe=path.with_suffix('.exe');path.write_text(code)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
