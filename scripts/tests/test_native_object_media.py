"""Exercise the production ObjectMedia publication block for stable entry identity."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class ObjectMediaIdentity(unittest.TestCase):
    def test_refresh_preserves_plugin_identity_and_rejects_partial_rows(self):
        source=(ROOT/'indra/newview/vsobjectmedia.cpp').read_text()
        start=source.index('    // Validate every row')
        body=source[start:source.index('    fetchedVersion=',start)]
        body=body.replace('this->','state->').replace('remObserver(this)','remObserver(nullptr)').replace('removeNativeSurface(object)','removeNativeSurface(9)')
        fixture=r'''
#include <cassert>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
struct Row {std::string url;bool undefined=false,valid=true;bool isUndefined()const{return undefined;}bool isMap()const{return valid;}};
struct Data {std::vector<Row> rows;int size()const{return int(rows.size());}const Row& operator[](unsigned i)const{return rows.at(i);}};
namespace llsd {const std::vector<Row>& inArray(const Data& d){return d.rows;}}
struct Entry {int id;std::string url;static inline int next=1;Entry():id(next++){}const std::string& getCurrentURL()const{return url;}};
struct LLTextureEntry {
 std::unique_ptr<Entry> value;Entry* getMediaData(){return value.get();}
 void updateMediaData(const Row& row){if(row.undefined)value.reset();else{if(!value)value=std::make_unique<Entry>();value->url=row.url;}}
 static int getAgentIDFromMediaVersionString(const std::string&){return 1;}
};
struct Media {int id,removed=0,unobserved=0;std::string previous;bool fromSelf=false;void removeNativeSurface(int){++removed;}void remObserver(void*){++unobserved;}};
using viewer_media_t=std::shared_ptr<Media>;
struct LLViewerMedia {
 std::map<int,std::weak_ptr<Media>> instances;static LLViewerMedia* getInstance(){static LLViewerMedia m;return &m;}
 viewer_media_t updateMediaImpl(Entry* entry,const std::string& old,bool self){
  auto media=instances[entry->id].lock();if(!media){media=std::make_shared<Media>();media->id=entry->id;instances[entry->id]=media;}
  media->previous=old;media->fromSelf=self;return media;
 }
};
struct State {
 std::vector<std::unique_ptr<LLTextureEntry>> entries;std::vector<viewer_media_t> media;
 void release(){for(auto& m:media)if(m)m->removeNativeSurface(9);media.clear();entries.clear();}
};
struct Session{struct Account{int agent=1;};Account account(){return {};}};
int main(){
 auto state=std::make_shared<State>();auto session=std::make_shared<Session>();const std::string mediaVersion="v1";
 // Default publish() arguments: a fetched refresh of every face that updates its player.
 const std::optional<bool> updateFromSelf;const bool updatePlayer=true;const std::optional<unsigned> editedFace;const bool retired=false;auto valid=[]{return true;};
 auto apply=[&](const Data& data){BODY return true;};
 apply(Data{{Row{"first"}}});auto first=state->media[0];const auto* address=state->entries[0]->getMediaData();
 assert(first->previous.empty() && first->fromSelf);
 apply(Data{{Row{"second"}}});assert(state->media[0]==first && state->entries[0]->getMediaData()==address);
 assert(first->previous=="first" && first->removed==0 && first->unobserved==0);
 for(int i=0;i<100;++i){apply(Data{{Row{"second"}}});assert(state->media[0]==first && first->removed==0 && first->unobserved==0);}
 apply(Data{{Row{"third"},Row{"bad",false,false}}});
 assert(state->media.size()==1 && state->entries[0]->getMediaData()->url=="second");
 apply(Data{{Row{"",true}}});assert(!state->media[0] && first->removed==1 && first->unobserved==1);
 apply(Data{{Row{"new"}}});assert(state->media[0]!=first && state->media[0]->id!=first->id);
 apply(Data{});assert(state->media.empty() && state->entries.empty());
}
'''.replace('BODY',body)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'object_media.cpp';source.write_text(fixture);exe=Path(temp)/'object_media.exe'
            build=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
