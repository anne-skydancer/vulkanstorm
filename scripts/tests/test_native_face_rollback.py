"""Actual native Face rollback ownership and authenticated texture capture."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeFaceRollback(unittest.TestCase):
    def test_account_source_permissions_budget_and_reentry(self):
        source=(ROOT/'indra/newview/vsobjectselection.cpp').read_text()
        functions='\n'.join(method(source,name) for name in [
          'bool VSObjectSelection::refreshSavedTextures(',
          'bool VSObjectSelection::saveColors(',
          'std::shared_ptr<const VSObjectSelection::SavedColors> VSObjectSelection::savedColors(',
          'bool VSObjectSelection::captureSavedMaterials('])
        code=(ROOT/'scripts/tests/native_face_edit/rollback_owner.cpp').read_text().replace('CURRENT',functions)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'owner.cpp';exe=src.with_suffix('.exe');src.write_text(code)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            tested=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(tested.returncode,0,tested.stdout+tested.stderr)
if __name__=='__main__':unittest.main()
