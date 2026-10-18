"""Real BP preview cache is separate from authenticated texture rollback."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeFacePreview(unittest.TestCase):
    def test_actual_owner_snapshot_budget_deselection_and_account_source_retirement(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
        a=source.index('        struct LegacyMaterialPreview\n');b=source.index('        std::shared_ptr<LegacyMaterialPreview> legacyPreview;',a)
        code=(ROOT/'scripts/tests/native_face_edit/preview_owner.cpp').read_text().replace('PREVIEW',source[a:b])
        code=code.replace('METHODS','\n'.join(method(source,name)for name in [
            'bool VSWorldScene::setLegacyMaterialPreview(',
            'bool VSWorldScene::legacyMaterialPreview(',
            'LLUUID VSWorldScene::renderMaterialID(',
            'bool VSWorldScene::setSelectionState(']))
        compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'preview.cpp';exe=path.with_suffix('.exe');path.write_text(code)
            p=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
