"""Execute explicit shared environment creation/initialization and account retirement."""
from native_source_fixture import ROOT, method
from pathlib import Path
import shutil, subprocess, tempfile, unittest

class EnvironmentLifecycle(unittest.TestCase):
 def test_explicit_simpleton_initializes_before_crossing_and_reinitializes_after_retirement(self):
  session=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
  env=(ROOT/'indra/newview/llenvironment.cpp').read_text()
  simpleton=(ROOT/'indra/llcommon/llsingleton.h').read_text()
  start=simpleton.index('template<class T>\nclass LLSimpleton')
  stop=simpleton.index('#ifdef LL_WINDOWS',start)
  definitions=simpleton[start:stop]
  connected=method(session,'void VSNativeSession::connected()')
  self.assertEqual(connected.count('initializeEnvironment();'),2)
  crossing=connected[connected.index('if (mChangingRegion)'):connected.index('    mPhase = Phase::Connected;')]
  self.assertLess(crossing.index('mChangingRegion = false;'),crossing.index('initializeEnvironment();'))
  self.assertLess(crossing.index('initializeEnvironment();'),crossing.index('requestParcel();'))
  initial=connected[connected.index('    mPhase = Phase::Connected;'):]
  self.assertLess(initial.index('mPhase = Phase::Connected;'),initial.index('initializeEnvironment();'))
  self.assertLess(initial.index('initializeEnvironment();'),initial.index('LLStartUp::initExperiences();'))
  self.assertLess(connected.index('!mCircuitAck'),connected.index('initializeEnvironment();'))
  reset=method(session,'void VSNativeSession::reset()')
  self.assertLess(reset.index('vs_native_im_reset();'),reset.index('LLEnvironment::deleteSingleton();'))
  self.assertNotIn('LLEnvironment::instance();',method(session,'void VSNativeSession::advanceStartup()'))
  fixture=(ROOT/'scripts/tests/native_environment_lifecycle_fixture.cpp').read_text()
  fixture=fixture.replace('SIMPLETON',definitions).replace('INITIALIZE',method(env,'void LLEnvironment::initSingleton()')).replace('CLEANUP',method(env,'void LLEnvironment::cleanupSingleton()')).replace('ENSURE',method(session,'void VSNativeSession::initializeEnvironment()'))
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'lifecycle.cpp';exe=Path(tmp)/'lifecycle.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
