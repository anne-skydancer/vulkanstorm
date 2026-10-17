from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute actual estate parsing and coroutine ownership without sending requests."""
from pathlib import Path
import re,subprocess,tempfile,shutil,unittest
HERE=Path(__file__).resolve().parent
class EstateModel(unittest.TestCase):
 def test_exact_parser_and_scoped_http_commit(self):
  changes=compose();source=changes['indra/newview/llestateinfomodel.cpp']
  getters='\n'.join(method(source,'bool LLEstateInfoModel::'+name+'(') for name in ['getUseFixedSun','getIsExternallyVisible','getAllowDirectTeleport','getDenyAnonymous','getDenyAgeUnverified','getDenyScriptedAgents','getAllowVoiceChat','getAllowAccessOverride','getAllowEnvironmentOverride'])
  flags=sorted(set(re.findall(r'REGION_FLAGS_\w+',getters)));constants='\n'.join(f'constexpr unsigned long long {flag}=1ULL<<{n};' for n,flag in enumerate(flags))
  methods='\n'.join(method(source,signature) for signature in ['void LLEstateInfoModel::update(', 'void LLEstateInfoModel::reset()', 'bool LLEstateInfoModel::commitEstateInfoCaps()', 'void LLEstateInfoModel::commitEstateInfoCapsCoro('])
  owner=method(changes['indra/newview/vsnativecircuits.cpp'],'bool VSNativeSession::publishRegionOwner(')
  fixture=(HERE/'native_estate_model_fixture.cpp').read_text().replace('CONSTANTS',constants).replace('GETTERS',getters).replace('METHODS',methods).replace('OWNER_METHOD',owner)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'estate.cpp';exe=Path(directory)/'estate.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
