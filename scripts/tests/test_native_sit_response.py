from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class SitResponse(unittest.TestCase):
 def test_exact_original_reply_schema_pose_force_camera_fly_autopilot_and_retired_admission(self):
  s=(ROOT/'indra/newview/llviewermessage.cpp').read_text();shared=method(s,'template<class Peer>void sharedAvatarSitResponse(');gold=(HERE/'fixtures/native_sit_original_response.cpp').read_text().replace('process_avatar_sit_response(','referenceResponse(');peers=s;peer=peers[peers.index('struct ReferenceSitResponse'):peers.index('#if VS_NATIVE_VULKAN',peers.index('struct ReferenceSitResponse'))];source=(HERE/'fixtures/native_sit_response_fixture.cpp').read_text().replace('PEER',peer).replace('SHARED',shared).replace('REFERENCE',gold);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
