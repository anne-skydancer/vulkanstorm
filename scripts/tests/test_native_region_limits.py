from pathlib import Path
import shutil,subprocess,tempfile,unittest,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class RegionLimits(unittest.TestCase):
 def test_actual_transposed_defaults_setters_and_grid_reset(self):
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  header=(ROOT/'indra/newview/vsregionlimits.h').read_text(encoding='utf-8-sig');header=re.sub(r'^#(?:include|pragma).*$', '',header,flags=re.M)
  methods=(ROOT/'indra/newview/vsregionlimits.cpp').read_text(encoding='utf-8-sig');a=methods.index('VSRegionLimits::VSRegionLimits()');b=methods.index('VSRegionLimits& vs_region_limits()',a);methods=methods[a:b]
  files=['indra/llmath/xform.h','indra/llprimitive/llprimitive.cpp','indra/newview/llselectmgr.h'];constants=[]
  needed=set(re.findall(r'\b(?:SL_|OS_|MAX_CHILDREN_PER_)[A-Z_]+\b',methods))
  for f in files:
   for line in (ROOT/f).read_text(encoding='utf-8-sig').splitlines():
    if re.match(r'(?:constexpr|const)\s+(?:F32|S32)\s+',line) and any(re.search(r'\b'+name+r'\b',line) for name in needed):constants.append(line)
  fixture=(HERE/'native_region_limits_fixture.cpp').read_text(encoding='utf-8-sig').replace('CONSTANTS','\n'.join(constants)).replace('HEADER',header).replace('METHODS',methods)
  for opensim in [False,True]:
   with self.subTest(opensim=opensim),tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'limits.cpp';exe=Path(tmp)/'limits.exe';cpp.write_text(fixture)
    command=[compiler,'-std=c++17',str(cpp),'-o',str(exe)]+(['-DOPENSIM=1'] if opensim else [])
    result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
