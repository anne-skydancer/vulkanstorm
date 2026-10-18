"""Original texture drop channels plus actual prepare/publish source lifetime."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeFaceTextureDrop(unittest.TestCase):
    def test_original_channels_preflight_and_reentrant_source_retirement(self):
        source=(ROOT/'indra/newview/vsfacedrop.cpp').read_text()
        policy=(ROOT/'indra/newview/vsfacedroppolicy.h').read_text().split('#pragma once',1)[1]
        a=source.index('struct FaceTextureDrop\n');b=source.index('\nbool prepareFaceTextureDrop',a)
        code=(ROOT/'scripts/tests/native_face_edit/texture_drop.cpp').read_text()
        code=code.replace('POLICY',policy).replace('PLAN',source[a:b])
        code=code.replace('PREPARE',method(source,'bool prepareFaceTextureDrop(')).replace('PUBLISH',method(source,'bool publishFaceTextureDrops('))
        compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'drop.cpp';exe=path.with_suffix('.exe');path.write_text(code)
            p=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
