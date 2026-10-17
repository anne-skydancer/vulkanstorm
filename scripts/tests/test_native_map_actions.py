"""Compile actual map tracking consumer and assert admission/lifetime boundaries."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent
def body(source,sig):
    a=source.index('{',source.index(sig));n=0
    for b in range(a,len(source)):
        if source[b]=='{':n+=1
        elif source[b]=='}':
            n-=1
            if not n:return source[a:b+1]
    raise AssertionError(sig)
class MapActionTest(unittest.TestCase):
    def test_authenticated_real_tracker(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/llcallingcard.cpp').read_text(encoding='utf-8-sig')
        fixture=(HERE/'native_map_actions_fixture.cpp').read_text(encoding='utf-8-sig').replace('FOUND',body(source,'void LLAvatarTracker::processAgentFound('))
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'case.cpp';exe=Path(tmp)/'case.exe';cpp.write_text(fixture)
            result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_reentrant_remote_parcel_observers(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/llremoteparcelrequest.cpp').read_text(encoding='utf-8-sig')
        a=source.index('    // A notification may destroy another observer')
        b=source.index('    LLUrlEntryParcel::LLParcelData url_data;',a)
        fixture=(HERE/'native_map_observers_fixture.cpp').read_text(encoding='utf-8-sig').replace('OBSERVERS',source[a:b])
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'case.cpp';exe=Path(tmp)/'case.exe';cpp.write_text(fixture)
            result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_no_admission_without_endpoints(self):
        source=lambda name:(ROOT/'indra/newview'/name).read_text(encoding='utf-8-sig')
        session=source('vsnativesession.cpp');self.assertIn('"FindAgent",',session);self.assertIn('LLTracker::cleanupInstance();',session)
        tracker=source('lltracker.cpp')
        for name in ['clearAvatarSelection','clearLandmarkSelection','clearLocationSelection']:
            self.assertIn('if(gFloaterWorldMap)gFloaterWorldMap->'+name,tracker)
        self.assertIn('vs_avatar_precise_location(mAvatarID,position)',source('llcallingcard.cpp'))
        self.assertNotIn('vs_avatar_locations(',source('llcallingcard.cpp'))
if __name__=='__main__':unittest.main()
