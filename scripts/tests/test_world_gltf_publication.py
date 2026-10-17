"""Run actual glTF commit/promotion paths for refused and empty replacements."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class WorldGLTFPublicationTest(unittest.TestCase):
    def test_empty_scene_is_a_publication_and_refusal_keeps_previous_provider(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        gltf=(ROOT/'indra/newview/vsworldgltf.cpp').read_text()
        scene=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
        append=method(gltf,'void VSWorldGLTF::append(')
        commit=method(append,'if (!deferred)')
        promote=method(method(scene,'std::vector<VSWorldRenderer::Packet> VSWorldScene::packets('),'if (o.pendingGLTF)')
        fixture=r'''
#include <cassert>
#include <map>
#include <memory>
#include <vector>
struct VSWorldRenderer {using Packet=int;};
struct VSWorldGLTF {
 struct Impl {bool live=true,publicationKnown=false,refused=false;
 std::map<int,int> publications;std::vector<int> publishedPackets,assetPackets;
 bool current()const{return live;}};
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 bool hasPublication()const;
 void append(std::vector<int>& output,int,int,int,int,unsigned){auto& owner=*mImpl;
 auto candidates=owner.publications;auto candidatePackets=owner.assetPackets;bool deferred=owner.refused;
COMMIT
 output.insert(output.end(),owner.publishedPackets.begin(),owner.publishedPackets.end());}
};
HAS_PUBLICATION
struct Object {std::shared_ptr<VSWorldGLTF> gltf,pendingGLTF;int scale=1;};
void promote(Object& o){int position=0,rotation=0,eye=0;unsigned viewportHeight=480;
PROMOTE
}
int main(){Object o;auto old=std::make_shared<VSWorldGLTF>();old->mImpl->publicationKnown=true;
 old->mImpl->publishedPackets={10};o.gltf=old;
 auto next=std::make_shared<VSWorldGLTF>();next->mImpl->assetPackets={20};next->mImpl->refused=true;
 o.pendingGLTF=next;promote(o);assert(o.gltf==old&&o.pendingGLTF==next&&!next->hasPublication());
 next->mImpl->refused=false;promote(o);assert(o.gltf==next&&!o.pendingGLTF&&next->hasPublication());
 auto empty=std::make_shared<VSWorldGLTF>();o.pendingGLTF=empty;promote(o);
 assert(o.gltf==empty&&!o.pendingGLTF&&empty->hasPublication()&&empty->mImpl->publishedPackets.empty());
 empty->mImpl->live=false;assert(!empty->hasPublication());
}
'''.replace('COMMIT',commit).replace('HAS_PUBLICATION',method(gltf,'bool VSWorldGLTF::hasPublication() const')).replace('PROMOTE',promote)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'gltf_publication.cpp';exe=Path(directory)/'gltf_publication.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
