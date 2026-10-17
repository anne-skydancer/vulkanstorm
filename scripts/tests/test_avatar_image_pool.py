"""Check actual image-role routing and capacity for shipped bake transactions."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class AvatarImagePoolTest(unittest.TestCase):
    def test_bakes_do_not_consume_ui_capacity_and_three_generations_fit(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        renderer=(ROOT/'indra/newview/vsuirenderer.cpp').read_text()
        resources=(ROOT/'indra/newview/vsuiresources.cpp').read_text()
        core=re.search(r'VSResourceBudget\s+coreImages\{.*?;',renderer,re.S).group()
        composite=re.search(r'VSResourceBudget\s+compositeImages\{.*?;',renderer,re.S).group()
        publication=method(resources,'LLUIImagePtr VSUIResources::publish(')
        begin=publication.index('const auto role')
        end=publication.index(';',begin)+1
        classification=publication[begin:end]
        layers=ET.parse(ROOT/'indra/newview/character/avatar_lad.xml').getroot().iter('layer_set')
        maximum=sum(int(row.attrib['width'])*int(row.attrib['height'])*8 for row in layers)
        fixture=r'''
#include "vsresourcebudget.h"
#include "vsworldcontracts.h"
#include <cassert>
#include <string>
struct Pool {
CORE
COMPOSITE
LOOKUP
};
ROLE_FUNCTION
VSWorldContracts::ImageRole classify(const std::string& key){CLASSIFY return role;}
int main(){using Role=VSWorldContracts::ImageRole;Pool p;
 assert(classify("skin:button")==Role::CoreUI&&classify("control:picture")==Role::CoreUI);
 assert(classify("remote:uuid")==Role::RemoteAsset&&classify("gltf-scene-image:uuid")==Role::RemoteAsset);
 assert(classify("avatar-composite:uuid")==Role::AvatarComposite);
 auto ui=p.imageBudget(Role::CoreUI).reserve(p.coreImages.limit());
 auto remote=p.imageBudget(Role::RemoteAsset).reserve(p.remoteImages.limit());assert(ui&&remote);
 auto before=p.imageBudget(Role::AvatarComposite).reserve(MAXIMUM);
 auto candidate=p.imageBudget(Role::AvatarComposite).reserve(MAXIMUM);
 auto retiring=p.imageBudget(Role::AvatarComposite).reserve(MAXIMUM);assert(before&&candidate&&retiring);
 assert(p.compositeImages.used()==3*MAXIMUM&&p.coreImages.used()==p.coreImages.limit());
 auto refused=p.compositeImages.reserve(p.compositeImages.limit());assert(!refused);
 before.reset();candidate.reset();retiring.reset();assert(!p.compositeImages.used());
}
'''.replace('CORE',core).replace('COMPOSITE',composite).replace('LOOKUP',method(renderer,'VSResourceBudget& imageBudget(')).replace('ROLE_FUNCTION',method(resources,'VSWorldContracts::ImageRole imageRole(')).replace('CLASSIFY',classification).replace('MAXIMUM',str(maximum)+'ULL')
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'avatar_image_pool.cpp';exe=Path(directory)/'avatar_image_pool.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
