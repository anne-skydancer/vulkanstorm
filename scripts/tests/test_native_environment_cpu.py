from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute production real CPU settings updates and model/region ownership guards."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest
class EnvironmentCPU(unittest.TestCase):
 def test_actual_cpu_settings_and_request_guards_preserve_legacy_publication(self):
  changes=compose();settings=changes['indra/newview/llsettingsvo.cpp'];source=changes['indra/newview/llenvironment.cpp']
  sky=method(settings,'void LLSettingsVOSky::updateSettings()');water=method(settings,'void LLSettingsVOWater::updateSettings()')
  guards='\n'.join(method(source,s) for s in ['bool vs_environment_native()', 'std::function<bool()> vs_environment_model_guard(', 'std::function<bool()> vs_environment_region_guard('])
  fixture=(HERE/'native_environment_cpu_fixture.cpp').read_text().replace('GUARDS',guards).replace('SKY_UPDATE',sky).replace('WATER_UPDATE',water)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'environment.cpp';exe=Path(tmp)/'environment.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
