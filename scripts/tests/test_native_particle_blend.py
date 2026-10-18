from pathlib import Path
import importlib.util,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
class Blend(unittest.TestCase):
 def test_all_original_factors_alpha_color_masks_and_glow(self):
  s=(ROOT/'indra/newview/vsworldrenderer.cpp').read_text(encoding='utf-8');a=s.index('        if(customBlend)\n');b=s.index('        ci.PSODesc.ResourceLayout',a);block=s[a:b]
  fixture=(HERE/'blend_fixture.cpp').read_text().replace('NATIVE',block)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'test.cpp').write_text(fixture);exe=p/'test.exe';built=subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
