from pathlib import Path
import subprocess,tempfile,shutil,unittest,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent

def method(source,signature):
 a=source.index(signature);start=source.index('{',a);depth=0
 for b in range(start,len(source)):
  if source[b]=='{':depth+=1
  elif source[b]=='}':
   depth-=1
   if not depth:return source[a:b+1]
 raise AssertionError(signature)

class RegionContextTest(unittest.TestCase):
 def test_actual_region_values_geometry_guards_and_no_dummy_world(self):
  header=(ROOT/'indra/newview/vsregioncontext.h').read_text(encoding='utf-8-sig');header=re.sub(r'^#(?:include|pragma).*$', '',header,flags=re.M)
  methods=(ROOT/'indra/newview/vsregioncontext.cpp').read_text(encoding='utf-8-sig');methods=re.sub(r'^#include.*$', '',methods,flags=re.M)
  h=(ROOT/'indra/newview/vsnativesession.h').read_text(encoding='utf-8-sig');a=h.index('    struct RegionMetadata');b=h.index('    struct RegionSnapshot',a);meta=h[a:b];snapshot=method(h,'    struct RegionSnapshot')+';'
  source=(ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig');spatial='\n'.join(method(source,s) for s in ['VSParcelOverlay::Snapshot VSNativeSession::parcelOverlay(', 'std::optional<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshot(', 'std::vector<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshots(', 'bool VSNativeSession::current(const RegionSnapshot&'])
  fixture=(HERE/'native_region_context_fixture.cpp').read_text().replace('METADATA',meta).replace('SNAPSHOT_METHODS',spatial).replace('SNAPSHOT',snapshot).replace('CONTEXT_HEADER',header).replace('CONTEXT_METHODS',methods)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'region.cpp';exe=Path(tmp)/'region.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
