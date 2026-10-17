"""Compile actual immutable parcel overlay and authenticated consumer from proposal."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent

def method(source,signature):
    begin=source.index(signature);start=source.index('{',begin);depth=0
    for end in range(start,len(source)):
        if source[end]=='{':depth+=1
        elif source[end]=='}':
            depth-=1
            if not depth:return source[begin:end+1]
    raise AssertionError(signature)

class MapModelTest(unittest.TestCase):
    def test_immutable_sparse_overlay(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'overlay.exe'
            command=[compiler,'-std=c++17','-I',str(HERE),'-I',str(ROOT/'indra/newview'),str(HERE/'native_map_models_fixture.cpp'),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_authenticated_parcel_owner(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        fixture=(HERE/'native_parcel_consumer_fixture.cpp').read_text(encoding='utf-8-sig').replace('CONSUMER',method((ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig'),'bool VSNativeSession::receiveParcelOverlay('))
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'consumer.cpp';exe=Path(tmp)/'consumer.exe';cpp.write_text(fixture)
            command=[compiler,'-std=c++17','-I',str(HERE),'-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_current_immutable_object_metadata(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        fixture=(HERE/'native_object_metadata_fixture.cpp').read_text(encoding='utf-8-sig').replace('METHODS','\n'.join(method((ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8-sig'),signature) for signature in ['std::shared_ptr<const VSResourceBudget::Reservation> VSWorldScene::reserveSpatialBytes(', 'std::optional<VSWorldScene::ObjectSnapshot> VSWorldScene::objectSnapshot(', 'std::vector<VSWorldScene::ObjectSnapshot> VSWorldScene::objectSnapshots()']))
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'metadata.cpp';exe=Path(tmp)/'metadata.exe';cpp.write_text(fixture)
            command=[compiler,'-std=c++17','-I',str(HERE),'-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
