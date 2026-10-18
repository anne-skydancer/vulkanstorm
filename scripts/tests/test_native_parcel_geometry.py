"""Execute native parcel geometry and received-terrain lifetime against shared reference logic."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from native_source_fixture import method, ROOT, HERE

class ParcelGeometry(unittest.TestCase):
    def compile_run(self, fixture):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'parcel.cpp'
            binary = Path(temp) / 'parcel.exe'
            source.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'indra/newview'), str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_reference_segment_and_interpolation(self):
        reference = (ROOT / 'indra/newview/llglsandbox.cpp').read_text(encoding='utf-8')
        terrain = (ROOT / 'indra/newview/llsurface.cpp').read_text(encoding='utf-8')
        fixture = (HERE / 'parcel_geometry_fixture.cpp').read_text(encoding='utf-8')
        fixture = fixture.replace('SEGMENT_REFERENCE', method(reference, 'void LLViewerParcelMgr::renderOneSegment('))
        fixture = fixture.replace('TERRAIN_REFERENCE', method(terrain, 'F32 LLSurface::resolveHeightRegion('))
        self.compile_run(fixture)

    def test_actual_received_terrain_and_retirement(self):
        source = (ROOT / 'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
        fixture = (HERE / 'native_terrain_field_fixture.cpp').read_text(encoding='utf-8')
        fixture = fixture.replace('HEIGHT_LOCAL', method(source, 'float heightLocal('))
        fixture = fixture.replace('RESET_TERRAIN', method(source, 'void VSWorldScene::reset('))
        fixture = fixture.replace('TERRAIN_FIELD', '\n'.join(method(source, name) for name in (
            'std::optional<U64> VSWorldScene::terrainRevision(', 'std::optional<VSWorldScene::TerrainField> VSWorldScene::terrainField(')))
        self.compile_run(fixture)

if __name__ == '__main__':
    unittest.main()
