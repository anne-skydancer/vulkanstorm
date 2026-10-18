"""Original snapshot animation/draw stream and native immutable image lifecycle."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_snapshot_ui'
class NativeSnapshotUI(unittest.TestCase):
 def run_cpp(self,source):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'snapshot.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(source)
   built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_original_frozen_preview_shine_fall_uv_tint_and_parent_transform(self):
  painter=(ROOT/'indra/newview/vssnapshotpainter.h').read_text();painter='\n'.join(l for l in painter.splitlines()if not l.startswith('#'))
  current=method((ROOT/'indra/newview/llsnapshotlivepreview.cpp').read_text(),'void LLSnapshotLivePreview::draw()')
  source=(FIXTURES/'draw_fixture.cpp').read_text().replace('PAINTER',painter).replace('REFERENCE',(FIXTURES/'draw_gl_reference.cpp').read_text()).replace('CURRENT',current)
  self.run_cpp(source)
 def test_actual_image_publication_refusal_retry_generation_and_retirement(self):
  source=(ROOT/'indra/newview/llsnapshotlivepreview.cpp').read_text()
  fixture=(FIXTURES/'publication_fixture.cpp').read_text()
  for key,signature in [('PUBLISH','bool LLSnapshotLivePreview::publishNativeImage('),('RETRY','bool LLSnapshotLivePreview::retryNativeImage('),('CLEAR','void LLSnapshotLivePreview::clearNativeImage(')]:fixture=fixture.replace(key,method(source,signature))
  self.run_cpp(fixture)
 def test_actual_flickr_retired_reply_and_reentrant_account_owner(self):
  source=(ROOT/'indra/newview/exoflickrauth.cpp').read_text()
  header=(ROOT/'indra/newview/exoflickrauth.h').read_text()
  header='\n'.join(l for l in header.splitlines()if not l.startswith('#')).replace('private:','public:')
  core=source[source.index('exoFlickrAuth::exoFlickrAuth('):source.index('void exoFlickrAuth::checkAuthorisation()')]
  core='std::shared_ptr<exoFlickrAuth> exoFlickrAuth::sOwner;\n'+core
  fixture=(FIXTURES/'auth_fixture.cpp').read_text().replace('AUTH_HEADER',header).replace('AUTH_CORE',core).replace('AUTH_REPLY',method(source,'void exoFlickrAuth::gotAccessToken('))
  self.run_cpp(fixture)
if __name__=='__main__':unittest.main()
