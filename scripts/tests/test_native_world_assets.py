import pathlib, shutil, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'indra/newview/vsworldassets.cpp'
class MeshAssetBounds(unittest.TestCase):
    def test_real_section_admission(self):
        source = SOURCE.read_text()
        begin = source.index('bool section(')
        end = source.index('\n}\n}', begin) + 2
        function = source[begin:end]
        fixture = r'''
#include <map>
#include <string>
#include <cstddef>
#include <cassert>
using S32=int;
struct LLSD { int number=0; bool map=false, integer=false; std::map<std::string,LLSD> data;
 const LLSD& operator[](const char* key)const { static LLSD missing; auto i=data.find(key); return i==data.end()?missing:i->second; }
 bool has(const char* key)const{return data.count(key)!=0;} bool isMap()const{return map;} bool isInteger()const{return integer;} int asInteger()const{return number;}
};
''' + function + r'''
int main(){LLSD h; h.data["high_lod"].map=true;
 auto& s=h.data["high_lod"]; s.data["offset"].integer=true; s.data["size"].integer=true;
 size_t offset=0,length=0; s.data["size"].number=32;
 assert(section(h,"high_lod",64,96,offset,length)&&offset==64&&length==32);
 s.data["offset"].number=1; assert(!section(h,"high_lod",64,96,offset,length));
 s.data["offset"].number=-1; assert(!section(h,"high_lod",64,96,offset,length));
 s.data["offset"].number=0;s.data["size"].number=0;assert(!section(h,"high_lod",64,96,offset,length));
 s.data["size"].number=32; assert(!section(h,"high_lod",97,96,offset,length));
 assert(!section(h,"skin",64,96,offset,length));
 s.data["offset"].integer=false;assert(!section(h,"high_lod",64,96,offset,length));
 assert(meshVersionAllowed(h)); h.data["version"].number=999;assert(meshVersionAllowed(h));
 h.data["version"].number=1000;assert(!meshVersionAllowed(h));
 s.data["offset"].integer=true;assert(meshLOD(h,0,64,96,offset,length)==3);
 h.data["low_lod"]=s;assert(meshLOD(h,2,64,96,offset,length)==1);
 assert(meshLOD(h,3,64,96,offset,length)==3);
 s.data["offset"].number=2;assert(meshLOD(h,2,64,96,offset,length)==-1);
 s.data["size"].number=0;assert(meshLOD(h,3,64,96,offset,length)==1);
 assert(meshLOD(h,4,64,96,offset,length)==-1);
 h.data["low_lod"].data["size"].number=-1;assert(meshLOD(h,1,64,96,offset,length)==-1);
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp=pathlib.Path(directory)/'bounds.cpp'; cpp.write_text(fixture)
            exe=pathlib.Path(directory)/'bounds.exe'
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode,0)
    def test_requests_only_queue_and_reconcile_before_insert(self):
        s=SOURCE.read_text(); request=s[s.index('VSWorldAssets::State VSWorldAssets::request'):s.index('std::shared_ptr<const VSWorldAssets::Geometry> VSWorldAssets::geometry')]
        self.assertNotIn('pump();',request)
        self.assertLess(request.index('reset(); mImpl->owner'),request.index('entries.emplace'))
        self.assertIn('face.mNumIndices % 3 != 0',s)
    def test_publication_rechecks_account_region_and_request_identity(self):
        s=SOURCE.read_text(); resume=s[s.index('auto state = weak.lock();'):]
        for guard in ['VSNativeSession::active() != owner','owner->generation() != generation','owner->regionEpoch() != epoch','pending->second != adapter','cacheLimit - state->retained']:
            self.assertIn(guard,resume)
        self.assertLess(resume.index('owner->regionEpoch() != epoch'),resume.index('entry.geometry = decode'))
    def test_actual_shared_decoder_and_expansion_admission(self):
        s=SOURCE.read_text()
        self.assertIn('LLSDSerialize::fromBinary',s)
        self.assertIn('unpackVolumeFaces',s)
        self.assertIn('LLUZipHelper::unzip_llsd',s)
        self.assertLess(s.index('inflatedBound(bytes.data() + offset, length)'),s.index('unpackVolumeFaces'))
        self.assertIn('face.mIndices[i] >= face.mNumVertices',s)
if __name__=='__main__': unittest.main()
