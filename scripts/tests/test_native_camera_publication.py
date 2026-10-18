from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent

def method(source,signature):
    begin=source.index(signature);start=source.index('{',begin);depth=0
    for end in range(start,len(source)):
        if source[end]=='{':depth+=1
        elif source[end]=='}':
            depth-=1
            if not depth:return source[begin:end+1]
    raise AssertionError(signature)

class CameraPublication(unittest.TestCase):
    def test_exact_cpu_frame_and_retirement(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        fixture=(HERE/'native_camera_publication_fixture.cpp').read_text(encoding='utf-8-sig').replace('METHOD',method((ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig'),'bool VSNativeSession::publishCamera()')).replace('FOCUS_BODY',method((ROOT/'indra/newview/llagentcamera.cpp').read_text(encoding='utf-8-sig'),'void LLAgentCamera::publishNativeFocus('))
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'camera.cpp';exe=Path(tmp)/'camera.exe';cpp.write_text(fixture)
            result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
