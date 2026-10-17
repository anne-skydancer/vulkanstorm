"""Execute the production batch publication transaction with a refusing uploader."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeImageBatchTests(unittest.TestCase):
    def test_partial_upload_refusal_preserves_every_published_channel(self):
        text = (ROOT / 'indra/newview/vsuiresources.cpp').read_text()
        start = text.index('std::vector<LLUIImagePtr> VSUIResources::publishBatch(')
        opening = text.index('{', start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        source = r'''
#include <cassert>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using LLUIImagePtr=std::shared_ptr<int>;
struct Prepared {int role=0;};
namespace VSTextureUpload {bool valid(const Prepared&){return true;}}
void check(bool value,const char* message){if(!value) throw std::runtime_error(message);}
int imageRole(const std::string&){return 0;}
struct VSUIResources {
 using PreparedImage=std::shared_ptr<const Prepared>;
 struct Impl {
  struct Entry {std::shared_ptr<int> asset;LLUIImagePtr facade;};
  std::map<std::string,Entry> assets;
  int calls=0,refuse=0;
  Entry prepareEntry(const std::shared_ptr<Impl>&,const std::string&,const PreparedImage&) {
   if(++calls==refuse) return {};
   auto value=std::make_shared<int>(calls);return {value,value};
  }
 };
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 std::vector<LLUIImagePtr> publishBatch(const std::vector<std::pair<std::string,PreparedImage>>&);
};
BODY
int main(){
 VSUIResources resources;auto first=std::make_shared<int>(91),second=std::make_shared<int>(92);
 resources.mImpl->assets={{"base",{first,first}},{"normal",{second,second}}};
 auto prepared=std::make_shared<Prepared>();
 resources.mImpl->refuse=2;
 assert(resources.publishBatch({{"base",prepared},{"normal",prepared}}).empty());
 assert(resources.mImpl->calls==2 && resources.mImpl->assets.size()==2);
 assert(resources.mImpl->assets.at("base").asset==first && resources.mImpl->assets.at("normal").asset==second);
 resources.mImpl->calls=0;resources.mImpl->refuse=0;
 auto published=resources.publishBatch({{"base",prepared},{"normal",prepared},{"emissive",prepared}});
 assert(published.size()==3 && resources.mImpl->assets.size()==3);
 assert(resources.mImpl->assets.at("base").asset==published[0] && resources.mImpl->assets.at("normal").asset==published[1]);
 assert(*first==91 && *second==92);
 bool rejected=false;try{resources.publishBatch({{"base",prepared},{"base",prepared}});}catch(const std::runtime_error&){rejected=true;}
 assert(rejected && resources.mImpl->assets.at("base").asset==published[0]);
 auto wrong=std::make_shared<Prepared>();wrong->role=1;rejected=false;
 try{resources.publishBatch({{"base",prepared},{"normal",wrong}});}catch(const std::runtime_error&){rejected=true;}
 assert(rejected && resources.mImpl->assets.at("base").asset==published[0] && resources.mImpl->assets.at("normal").asset==published[1]);
}
'''.replace('BODY', text[start:end])
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cpp = directory / 'batch.cpp'
            cpp.write_text(source)
            result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp),
                '-o', str(directory / 'batch.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(directory / 'batch.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
