from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles';sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Cadence(unittest.TestCase):
 def test_actual_simulator_visible_invisible_clock_skiptime_and_phase(self):
  s={p:p.read_text()for p in (ROOT/'indra/newview/llviewerpartsim.cpp',ROOT/'indra/newview/vsparticleeffects.cpp')};sim=method(s[ROOT/'indra/newview/llviewerpartsim.cpp'],'void LLViewerPartSim::updateSimulation()');start=sim.index('    count = (S32) mViewerPartGroups.size();');end=sim.index('    if (LLDrawable::getCurrentFrame()%16==0)',start);loop=sim[start:end];visible=method(s[ROOT/'indra/newview/vsparticleeffects.cpp'],'bool vs_native_particle_group_visible(');source=(HERE/'cadence_fixture.cpp').read_text().replace('VISIBLE',visible).replace('LOOP',loop);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
