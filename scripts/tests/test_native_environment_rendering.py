"""Reference sky geometry and actual current-setting publication; CPU evidence only."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,HERE,method
class EnvironmentRendering(unittest.TestCase):
 def execute(self,fixture):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'environment.cpp';exe=Path(tmp)/'environment.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_actual_dome_kernel_matches_original_positions_uvs_and_indices(self):
  kernel=(ROOT/'indra/newview/vsskygeometry.h').read_text()
  kernel='\n'.join(line for line in kernel.splitlines()if not line.startswith('#include')and not line.startswith('#pragma'))
  fixture=(HERE/'native_sky_geometry_fixture.cpp').read_text().replace('SHARED_KERNEL',kernel)
  self.execute(fixture)
 def test_actual_current_uniform_publication_override_retirement_and_relogin(self):
  gl=(ROOT/'indra/llrender/llglslshader.h').read_text();a=gl.index('class LLShaderUniforms\n{');b=gl.index('\nclass LLGLSLShader\n{',a)
  fixture=(HERE/'native_environment_snapshot_fixture.cpp').read_text().replace('SHARED_UNIFORMS',gl[a:b])
  env=method((ROOT/'indra/newview/llenvironment.cpp').read_text(),'void LLEnvironment::updateGLVariablesForSettings(')
  fixture=fixture.replace('ENV_PUBLISH',env)
  header=(ROOT/'indra/newview/vsenvironmentstate.h').read_text().replace('#pragma once','').replace('#include <array>','')
  source='\n'.join(line for line in (ROOT/'indra/newview/vsenvironmentstate.cpp').read_text().splitlines()if not line.startswith('#include'))
  fixture=fixture.replace('STATE_HEADER',header).replace('STATE_SOURCE',source)
  self.execute(fixture)
 def test_real_sky_publication_never_crosses_geometry_domains_on_refusal(self):
  source=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
  a=source.index('        const auto activeSky=',source.index('    void surfaces('));b=source.index('        if (sky)',a)
  fixture=(HERE/'native_sky_publication_fixture.cpp').read_text().replace('BUILD',source[a:b])
  kernel=(ROOT/'indra/newview/vsskygeometry.h').read_text()
  kernel='\n'.join(line for line in kernel.splitlines()if not line.startswith('#include')and not line.startswith('#pragma'))
  fixture=fixture.replace('SHARED_KERNEL',kernel).replace('CURRENT',method(source,'bool VSWorldScene::skyCurrent()'))
  self.execute(fixture)
if __name__=='__main__':unittest.main()
