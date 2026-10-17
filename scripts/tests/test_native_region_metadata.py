from pathlib import Path
import subprocess,shutil,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
def method(source,signature):
 a=source.index(signature);start=source.index('{',a);depth=0
 for b in range(start,len(source)):
  if source[b]=='{':depth+=1
  elif source[b]=='}':
   depth-=1
   if not depth:return source[a:b+1]
 raise AssertionError(signature)

class MetadataTest(unittest.TestCase):
 def test_actual_metadata_parser(self):
  h=(ROOT/'indra/newview/vsnativesession.h').read_text(encoding='utf-8-sig');a=h.index('    struct RegionMetadata');b=h.index('    struct RegionSnapshot',a);metadata=h[a:b]
  source=(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig');a=source.index('std::optional<VSNativeSession::RegionMetadata> nativeRegionMetadata(');b=source.index('\n}\nbool VSNativeSession::deliver',a);parser=source[a:b]
  fixture=(HERE/'native_region_metadata_fixture.cpp').read_text().replace('METADATA',metadata).replace('PARSER',parser)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'metadata.cpp';exe=Path(tmp)/'metadata.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_current_region_values_retire_with_exact_owner(self):
  fixture=(HERE/'native_spatial_roots_fixture.cpp').read_text(encoding='utf-8-sig').split('METHODS')[0]
  fixture=fixture.replace('    std::vector<RegionSnapshot> regionSnapshots()', '    bool current(const AccountHandle& a)const{return a.generation==mGeneration && a.agent==1 && a.session==1;}\n    bool current(const RegionSnapshot&)const;\n    std::vector<RegionSnapshot> regionSnapshots()',1)
  source=(ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig');methods='\n'.join(method(source,signature) for signature in ['bool VSNativeSession::current(const RegionSnapshot&', 'std::vector<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshots()'])
  fixture+=methods+'''
int main(){VSNativeSession owner;owner.mCircuits[1]={};auto before=owner.regionSnapshots()[0];assert(owner.current(before));owner.mCircuits.at(1).metadata.productName="real-product";auto after=owner.regionSnapshots()[0];assert(before.metadata.productName=="unknown" && after.metadata.productName=="real-product");auto stale=after;stale.account.generation=6;assert(!owner.current(stale));stale=after;stale.circuit++;assert(!owner.current(stale));stale=after;stale.id={99};assert(!owner.current(stale));owner.mCircuits.at(1).acknowledged=false;assert(!owner.current(after));owner.mCircuits.at(1).acknowledged=true;owner.mCircuits.at(1).handshake=false;assert(!owner.current(after));owner.mCircuits.at(1).handshake=true;owner.mCircuits.at(1).incarnation++;assert(!owner.current(after));owner.mCircuits.clear();assert(!owner.current(after));}'''
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'region.cpp';exe=Path(tmp)/'region.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
