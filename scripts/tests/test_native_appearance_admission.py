"""Exercise the real native appearance admission against pending COF operations."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]

class NativeAppearanceAdmission(unittest.TestCase):
    def test_current_pending_replacement_and_attachment_limits(self):
        code=(ROOT/'indra/newview/vsnativeappearance.cpp').read_text()
        body=method(code,'bool VSNativeAppearance::canWear(').replace('VSNativeAppearance::','Appearance::')
        fixture=r"""
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <vector>
using S32=int;using U64=unsigned long long;
struct LLUUID {
 int id=0;LLUUID()=default;LLUUID(int n):id(n){};
 bool isNull()const{return !id;}bool operator<(const LLUUID& r)const{return id<r.id;}
 bool operator==(const LLUUID&)const=default;
};
namespace LLAssetType {enum EType{AT_BODYPART,AT_CLOTHING,AT_OBJECT,AT_GESTURE};}
struct LLWearableType {
 enum EType{WT_SHAPE,WT_SHIRT,WT_PHYSICS,WT_COUNT};
 static LLWearableType* getInstance(){static LLWearableType t;return &t;}
 LLAssetType::EType getAssetType(EType t)const{return t==WT_SHAPE?LLAssetType::AT_BODYPART:LLAssetType::AT_CLOTHING;}
 bool getAllowMultiwear(EType t)const{return t==WT_SHIRT;}
};
struct Item {
 LLUUID id;LLAssetType::EType type;LLWearableType::EType wearable=LLWearableType::WT_SHIRT;
 LLUUID getUUID()const{return id;}LLUUID getLinkedUUID()const{return id;}
 LLAssetType::EType getType()const{return type;}LLWearableType::EType getWearableType()const{return wearable;}
 bool isWearableType()const{return type==LLAssetType::AT_CLOTHING||type==LLAssetType::AT_BODYPART;}
};
struct LLInventoryModel {
 using cat_array_t=std::vector<int>;using item_array_t=std::vector<std::shared_ptr<Item>>;
 static constexpr int EXCLUDE_TRASH=1;
 bool complete=true;std::map<LLUUID,std::shared_ptr<Item>> items;std::set<LLUUID> links;
 LLUUID getLinkedItemID(LLUUID id)const{return id;}
 const Item* getItem(LLUUID id)const{auto f=items.find(id);return f==items.end()?nullptr:f->second.get();}
 bool isCategoryComplete(LLUUID)const{return complete;}
 void collectDescendents(LLUUID,cat_array_t&,item_array_t& result,int)const{
  for(auto id:links)result.push_back(items.at(id));}
} gInventory;
struct LLAppearanceMgr {
 static LLAppearanceMgr& instance(){static LLAppearanceMgr m;return m;}
 LLUUID getCOF()const{return {1000};}bool isLinkedInCOF(LLUUID id)const{return gInventory.links.count(id);}
};
struct LLWearableData{static constexpr unsigned MAX_CLOTHING_LAYERS=60;};
struct Attachment{LLUUID item;struct{LLUUID id;}object;};struct World{std::vector<Attachment> objects;const auto& attachments()const{return objects;}};
struct Context{World scene;World& world(){return scene;}};
struct Window{Context context;Context* nativeContext(){return &context;}}window;
Window* gViewerWindow=&window;
struct LLAgentBenefitsMgr{static LLAgentBenefitsMgr& current(){static LLAgentBenefitsMgr b;return b;}int getAttachmentLimit()const{return 4;}};
struct Session{bool ready=true;bool regionReady()const{return ready;}};
struct Impl {
 std::weak_ptr<Session>session;bool alive=true;bool current()const{return alive;}
 unsigned pendingOrder=0;bool orderRecovery=false;
 struct Pending{LLWearableType::EType type;bool failed=false,completed=false;};std::map<LLUUID,Pending>pendingLinks;
 struct Request{bool attach=true;};std::map<LLUUID,Request>attachments;
};
struct Appearance{std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool canWear(const std::vector<LLUUID>&,bool,bool)const;};
BODY
void add(int id,LLAssetType::EType type=LLAssetType::AT_CLOTHING,LLWearableType::EType wearable=LLWearableType::WT_SHIRT){
 gInventory.items[{id}]=std::make_shared<Item>(Item{{id},type,wearable});}
int main(){
 Appearance appearance;auto session=std::make_shared<Session>();appearance.mImpl->session=session;
 for(int id=1;id<=60;++id){add(id);gInventory.links.insert({id});}add(61);
 assert(!appearance.canWear({{61}},false,false));assert(appearance.canWear({{61}},true,false));
 for(int id=56;id<=60;++id)gInventory.links.erase({id});
 for(int id=56;id<=60;++id)appearance.mImpl->pendingLinks[{id}]={LLWearableType::WT_SHIRT};
 assert(!appearance.canWear({{61}},false,false));
 assert(!appearance.canWear({{56}},false,false));
 assert(!appearance.canWear({{61}},true,false));
 assert(!appearance.canWear({{61}},false,true));
 for(auto& [id,pending]:appearance.mImpl->pendingLinks)pending.completed=true;
 assert(appearance.canWear({{61}},false,false));
 appearance.mImpl->pendingLinks.clear();assert(appearance.canWear({{61}},false,true));
 add(70,LLAssetType::AT_BODYPART,LLWearableType::WT_SHAPE);
 add(71,LLAssetType::AT_BODYPART,LLWearableType::WT_SHAPE);
 assert(!appearance.canWear({{70},{71}},false,true));
 add(80,LLAssetType::AT_CLOTHING,LLWearableType::WT_PHYSICS);
 add(81,LLAssetType::AT_CLOTHING,LLWearableType::WT_PHYSICS);
 assert(!appearance.canWear({{80},{81}},false,true));
 for(int id=100;id<=104;++id)add(id,LLAssetType::AT_OBJECT);
 window.context.scene.objects={{{100},{{200}}},{{101},{{201}}},{{102},{{202}}}};appearance.mImpl->attachments[{103}]={true};
 assert(!appearance.canWear({{104}},false,false));assert(appearance.canWear({{100}},false,false));
 // Temporary attachments are separate physical owners; a null item cannot
 // collapse multiple objects into a single attachment slot.
 window.context.scene.objects={{{},{{200}}},{{},{{201}}},{{},{{202}}},{{},{{203}}}};
 appearance.mImpl->attachments.clear();assert(!appearance.canWear({{104}},false,false));
 window.context.scene.objects.clear();
 // Gesture requests reserve tracking but never enter the wearable type table.
 appearance.mImpl->pendingLinks[{120}]={LLWearableType::WT_COUNT};
 assert(appearance.canWear({},false,false));
 gInventory.complete=false;assert(!appearance.canWear({},false,false));gInventory.complete=true;
 gViewerWindow=nullptr;assert(!appearance.canWear({},false,false));gViewerWindow=&window;
 session->ready=false;assert(!appearance.canWear({},false,false));session->ready=true;
 appearance.mImpl->alive=false;assert(!appearance.canWear({},false,false));
}
""".replace('\nBODY\n','\n'+body+'\n')
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'appearance.cpp';binary=path.with_suffix('.exe');path.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__':unittest.main()
