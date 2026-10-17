"""Execute shared teleport admission against the original reference preflights."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method,ROOT,HERE
class TeleportAdmission(unittest.TestCase):
    def test_original_permission_matrix_and_native_wire(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/vsteleportpolicy.cpp').read_text(encoding='utf-8')
        policy='\n'.join(method(source,name) for name in ('bool vs_teleport_location_allowed(', 'bool vs_teleport_landmark_allowed(', 'bool vs_teleport_cancel_allowed('))
        native=(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        wire='\n'.join(method(native,name) for name in ('bool VSNativeSession::teleportRequest(', 'bool VSNativeSession::teleportToLandmark(', 'bool VSNativeSession::teleportToLure(', 'bool VSNativeSession::cancelTeleport('))
        fixture=(HERE/'teleport_admission_fixture.cpp').read_text(encoding='utf-8').replace('POLICY_FUNCTIONS',policy).replace('REFERENCE_FUNCTIONS',(HERE/'teleport_admission_reference.cpp').read_text(encoding='utf-8')).replace('WIRE_FUNCTIONS',wire)
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'teleport.cpp';exe=Path(tmp)/'teleport.exe';cpp.write_text(fixture,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
