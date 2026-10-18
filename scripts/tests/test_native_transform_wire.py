from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Wire(unittest.TestCase):
 def test_original_complete_multiple_update_bytes_field_order_and_lazy_getters(self):
  original=(HERE/'fixtures/native_transform_original_pack.cpp').read_text();kernel=(ROOT/'indra/newview/vsmultipleupdate.h').read_text().replace('#pragma once','').replace('#include "message.h"','');source=(HERE/'fixtures/native_transform_wire_fixture.cpp').read_text().replace('ORIGINAL',original).replace('KERNEL',kernel);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
