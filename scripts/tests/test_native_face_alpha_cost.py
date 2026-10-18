"""Original render alpha classification shared by actual native ARC consumers."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeFaceAlphaCost(unittest.TestCase):
    def test_original_render_policy_image_modes_and_native_cost_classifier(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
        policy=(ROOT/'indra/newview/vsfacealphapolicy.h').read_text().split('#pragma once',1)[1]
        code=(ROOT/'scripts/tests/native_face_edit/alpha_classifier.cpp').read_text()
        code=code.replace('POLICY',policy).replace('NATIVE',method(source,'    static VSWorldContracts::AlphaMode classifyFaceAlpha('))
        code=code.replace('ORIGINAL',(ROOT/'scripts/tests/native_face_edit/alpha_classifier_original.cpp').read_text())
        compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'alpha.cpp';exe=path.with_suffix('.exe');path.write_text(code)
            p=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
