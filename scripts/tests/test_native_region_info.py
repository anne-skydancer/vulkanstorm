from pathlib import Path
import subprocess,shutil,tempfile,unittest,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent

def method(source,signature):
 a=source.index(signature);start=source.index('{',a);depth=0
 for b in range(start,len(source)):
  if source[b]=='{':depth+=1
  elif source[b]=='}':
   depth-=1
   if not depth:return source[a:b+1]
 raise AssertionError(signature)

class RegionInfoTest(unittest.TestCase):
 def test_shared_parser_and_authenticated_native_owner(self):
  h=(ROOT/'indra/newview/llregioninfomodel.h').read_text(encoding='utf-8-sig');a=h.index('    U8          mSimAccess;');b=h.index('protected:',a);fields=h[a:b]
  source=(ROOT/'indra/newview/llregioninfomodel.cpp').read_text(encoding='utf-8-sig');methods='\n'.join(method(source,s) for s in ['void LLRegionInfoModel::reset()', 'void LLRegionInfoModel::update('])
  names=sorted(set(re.findall(r'_PREHASH_\w+',methods)));prehash='\n'.join('constexpr const char* '+name+'="'+name[len('_PREHASH_'):]+'";' for name in names)
  apply=method((ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig'),'bool VSNativeSession::applyRegionInfo(');fixture=(HERE/'native_region_info_fixture.cpp').read_text().replace('PREHASH',prehash).replace('FIELDS',fields).replace('MODEL_METHODS',methods).replace('APPLY',apply)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'info.cpp';exe=Path(tmp)/'info.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
