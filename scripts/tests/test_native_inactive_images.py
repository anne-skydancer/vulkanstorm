"""Execute the real inactive-view image rebinding and headless window APIs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]


class InactiveImages(unittest.TestCase):
    def compile(self,source):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'inactive.cpp';binary=path.with_suffix('.exe');path.write_text(source)
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],
                                 capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_headless_window_minimizes_and_restores_through_ordinary_api(self):
        header=(ROOT/'indra/llwindow/llwindowheadless.h').read_text()
        methods='\n'.join(method(header,signature) for signature in
                          ('bool getMinimized()','void minimize()','void restore()'))
        self.compile('''
#include <cassert>
struct Base {virtual bool getMinimized()=0;virtual void minimize()=0;virtual void restore()=0;};
struct Window:Base {bool mMinimized=false;METHODS};
int main(){Window window;Base& api=window;assert(!api.getMinimized());api.minimize();
 assert(api.getMinimized());api.minimize();assert(api.getMinimized());api.restore();assert(!api.getMinimized());}
'''.replace('METHODS',methods))

    def test_imported_rebinding_retains_geometry_and_refused_publications(self):
        source=(ROOT/'indra/newview/vsworldgltf.cpp').read_text()
        body=method(source,'void VSWorldGLTF::refreshInactiveImages()')
        self.compile(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>
using Image=std::shared_ptr<int>;
struct Packet {Image image;std::array<Image,4> channels;int mesh=73;};
struct Resources {std::map<std::string,Image> images;Image image(const std::string& key){return images[key];}};
struct State {
 bool active=true,ready=true;std::string error;Resources resources;
 struct Update{unsigned target=1024,frameDemand=0;};std::map<unsigned,Update> imageUpdates;
 std::map<unsigned,std::string> images;std::vector<Packet> publishedPackets;
 Image next;unsigned calls=0;bool current(){return active;}
 Image image(unsigned index,unsigned demand){assert(index==0 && demand==1024);++calls;
  if(next)resources.images["image"]=next;return next;}
};
struct VSWorldGLTF {std::shared_ptr<State> mImpl=std::make_shared<State>();void refreshInactiveImages();};
BODY
int main(){VSWorldGLTF scene;auto& s=*scene.mImpl;s.images[0]="image";
 auto old=std::make_shared<int>(1),other=std::make_shared<int>(2);s.resources.images["image"]=old;
 Packet packet;packet.image=old;packet.channels[0]=old;packet.channels[1]=other;
 s.publishedPackets.push_back(packet);packet={};
 scene.refreshInactiveImages();assert(s.calls==1 && s.publishedPackets[0].image==old);
 s.next=std::make_shared<int>(3);auto queued=old;std::weak_ptr<int> retired=old;
 scene.refreshInactiveImages();assert(s.publishedPackets[0].image==s.next && s.publishedPackets[0].channels[0]==s.next);
 assert(s.publishedPackets[0].channels[1]==other && s.publishedPackets[0].mesh==73);
 old.reset();assert(!retired.expired());queued.reset();assert(retired.expired());
 s.active=false;scene.refreshInactiveImages();assert(s.calls==2);
 s.active=true;s.error="failed";scene.refreshInactiveImages();assert(s.calls==2);
 s.error.clear();s.ready=false;scene.refreshInactiveImages();assert(s.calls==2);
}
'''.replace('BODY',body))


if __name__=='__main__':unittest.main()
