"""Run the actual avatar publication commit against topology changes/refusal."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class AvatarPublicationTest(unittest.TestCase):
    def test_failed_generation_keeps_complete_body_and_retry_retires_skirt(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
        append=method(source,'void VSWorldAvatar::append(')
        commit=method(append,'if (ready)')
        fixture=r'''
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <vector>
using S32=int;
struct Publication {int image=0;};
struct Avatar {std::map<int,std::shared_ptr<int>> meshes;
 std::map<int,Publication> publications;std::vector<int> publishedParts;};
struct Replacement {std::shared_ptr<int> mesh;Publication publication;};
struct Selection {int index,image;};
void publish(Avatar& a,bool ready,std::map<int,Replacement> replacements,const std::vector<Selection>& selected){
COMMIT
}
int main(){Avatar a;
 auto head=std::make_shared<int>(10),torso=std::make_shared<int>(20),skirt=std::make_shared<int>(30);
 a.meshes={{0,head},{1,torso},{2,skirt}};a.publications={{0,{1}},{1,{2}},{2,{3}}};a.publishedParts={0,1,2};
 std::weak_ptr<int> oldSkirt=skirt;skirt.reset();
 // A new shape removes the skirt, but one of the two replacement uploads fails.
 auto newHead=std::make_shared<int>(100),newTorso=std::make_shared<int>(200);
 publish(a,false,{{0,{newHead,{4}}}},{{0,4},{1,5}});
 assert((a.publishedParts==std::vector<int>{0,1,2}));assert(a.meshes[0]==head&&a.meshes[1]==torso);
 assert(!oldSkirt.expired()&&a.publications[2].image==3);
 // A successful complete generation switches every part and prunes retired topology.
 publish(a,true,{{0,{newHead,{4}}},{1,{newTorso,{5}}}},{{0,4},{1,5}});
 assert((a.publishedParts==std::vector<int>{0,1}));assert(a.meshes[0]==newHead&&a.meshes[1]==newTorso);
 assert(oldSkirt.expired()&&!a.meshes.count(2)&&!a.publications.count(2));
 publish(a,true,{},{});assert(a.meshes.empty()&&a.publications.empty()&&a.publishedParts.empty());
}
'''.replace('COMMIT',commit)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'avatar_publication.cpp';exe=Path(directory)/'avatar_publication.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
