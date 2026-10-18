"""Actual image source-format metadata, separate from normalized upload pixels."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class FaceImageMetadata(unittest.TestCase):
 def test_actual_primary_format_preserves_source_alpha_and_unknown(self):
  source=(ROOT/'indra/newview/vsfaceimagemetadata.cpp').read_text()
  code=r'''#include <cassert>
using LLGLenum=int;
enum{GL_LUMINANCE=1,GL_LUMINANCE_ALPHA,GL_RGB,GL_RGBA,GL_ALPHA,GL_BGRA};
struct Image{int format;int getPrimaryFormat()const{return format;}};
struct VSFaceImageMetadata{Image* mLegacy=nullptr;int mSourceFormat=0,mComponents=0;int getPrimaryFormat()const;};
METHOD
int main(){for(int components=0;components<8;++components)for(int format=0;format<8;++format){
 VSFaceImageMetadata image;image.mComponents=components;image.mSourceFormat=format;
 const int originalComponents[]={0,GL_LUMINANCE,GL_LUMINANCE_ALPHA,GL_RGB,GL_RGBA,0,0,0};
 assert(image.getPrimaryFormat()==(format?format:originalComponents[components]));
 Image legacy{99};image.mLegacy=&legacy;assert(image.getPrimaryFormat()==99);
 } }
'''.replace('METHOD',method(source,'LLGLenum VSFaceImageMetadata::getPrimaryFormat('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as d:
   src=Path(d)/'source.cpp';exe=src.with_suffix('.exe');src.write_text(code)
   p=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
