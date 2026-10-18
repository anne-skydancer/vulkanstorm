"""Frozen original LLFace projection/alignment versus actual native kernels."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativePlanarFace(unittest.TestCase):
    def test_original_projection_alignment_maps_and_degenerate_refusal(self):
        original=(ROOT/'indra/newview/llface.cpp').read_text()
        current=(ROOT/'indra/newview/vsplanarface.cpp').read_text()
        code=(ROOT/'scripts/tests/native_face_edit/planar.cpp').read_text()
        for marker, source, signature in [
          ('REFERENCE_FRAME',original,'void LLFace::getPlanarProjectedParams('),
          ('REFERENCE_ALIGN',original,'bool LLFace::calcAlignedPlanarTE('),
          ('CURRENT_FRAME',current,'std::optional<VSPlanarFaceFrame> vs_planar_face_frame('),
          ('CURRENT_ALIGN',current,'bool vs_align_planar_texture(')]:
            code=code.replace(marker,method(source,signature))
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'planar.cpp';exe=source.with_suffix('.exe');source.write_text(code)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
