"""Exercise actual shared lock policy against backend-neutral owner snapshots."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class AvatarLockPolicy(unittest.TestCase):
    def test_item_point_folder_and_type_locks(self):
        source=(ROOT/'indra/newview/rlvlocks.cpp').read_text()
        bodies='\n'.join(method(source,signature) for signature in [
            'bool RlvAttachmentLocks::canAttach() const',
            'ERlvWearMask RlvAttachmentLocks::canAttach(const LLInventoryItem*',
            'bool RlvAttachmentLocks::canDetach(const LLInventoryItem*',
            'bool RlvWearableLocks::canRemove(const LLInventoryItem*',
            'bool RlvWearableLocks::canRemove(LLWearableType::EType',
            'bool RlvWearableLocks::hasLockedWearable(LLWearableType::EType'])
        fixture=r"""
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <vector>
using S32=int;
struct LLUUID {int id=0;LLUUID()=default;LLUUID(int n):id(n){}bool notNull()const{return id!=0;}
 bool operator==(const LLUUID&)const=default;bool operator<(const LLUUID& r)const{return id<r.id;}};
namespace LLWearableType {enum EType{WT_SHAPE,WT_SHIRT};}
enum ERlvWearMask{RLV_WEAR_LOCKED=0,RLV_WEAR_ADD=1,RLV_WEAR_REPLACE=2,RLV_WEAR=3};
enum ERlvLockMask{RLV_LOCK_ADD=1,RLV_LOCK_REMOVE=2};
struct LLInventoryItem {
 LLUUID id,parent;int point=0;LLWearableType::EType type=LLWearableType::WT_SHIRT;
 LLUUID getLinkedUUID()const{return id;}LLUUID getUUID()const{return id;}LLUUID getParentUUID()const{return parent;}
 LLWearableType::EType getWearableType()const{return type;}
};
struct LLViewerJointAttachment{};
struct RlvAttachPtLookup {static LLViewerJointAttachment*getAttachPoint(const LLInventoryItem*){return nullptr;}
 static S32 getAttachPointIndex(const LLInventoryItem* item,bool){return item?item->point:0;}};
struct RlvFolderLocks {
 std::set<LLUUID> folders,attachments,wearables;
 static RlvFolderLocks& instance(){static RlvFolderLocks f;return f;}
 bool isLockedFolder(LLUUID id,ERlvLockMask)const{return folders.count(id);}
 bool isLockedAttachment(LLUUID id)const{return attachments.count(id);}
 bool isLockedWearable(LLUUID id)const{return wearables.count(id);}
};
struct Attachment{unsigned point;LLUUID object,item;};
std::vector<std::pair<unsigned,std::string>>points{{1,"Chest"},{2,"Skull"}};
std::vector<Attachment>attachments;
std::vector<LLUUID>worn;
auto vs_avatar_attachment_points(){return points;}auto vs_avatar_attachments(){return attachments;}
auto vs_avatar_wearable_items(unsigned){return worn;}
struct Inventory{std::map<LLUUID,LLInventoryItem>items;const LLInventoryItem*getItem(LLUUID id)const{
 auto found=items.find(id);return found==items.end()?nullptr:&found->second;}}gInventory;
bool get_is_item_worn(LLUUID id){return std::find(worn.begin(),worn.end(),id)!=worn.end();}
struct RlvAttachmentLocks {
 std::map<int,int>pointLocks;std::map<LLUUID,int>m_AttachObjRem;
 bool isLockedAttachmentPoint(S32 point,ERlvLockMask mask)const{auto f=pointLocks.find(point);return f!=pointLocks.end()&&(f->second&mask);}
 bool canAttach()const;ERlvWearMask canAttach(const LLInventoryItem*,LLViewerJointAttachment**)const;
 bool canDetach(const LLInventoryItem*)const;
};
struct RlvWearableLocks {
 bool typeLocked=false;bool isLockedWearableType(LLWearableType::EType,ERlvLockMask)const{return typeLocked;}
 bool canRemove(const LLInventoryItem*)const;bool canRemove(LLWearableType::EType)const;
 bool hasLockedWearable(LLWearableType::EType)const;
};
PRODUCTION
int main(){
 RlvAttachmentLocks locks;LLInventoryItem item{{10},{20},1};LLViewerJointAttachment*point=nullptr;
 assert(locks.canAttach()&&locks.canAttach(&item,&point)==RLV_WEAR&&point==nullptr);
 locks.pointLocks[1]=RLV_LOCK_ADD;assert(locks.canAttach()&&locks.canAttach(&item,nullptr)==RLV_WEAR_LOCKED);
 locks.pointLocks[2]=RLV_LOCK_ADD;assert(!locks.canAttach());locks.pointLocks.clear();
 attachments={{1,{30},{10}}};locks.pointLocks[1]=RLV_LOCK_REMOVE;
 assert(locks.canAttach(&item,nullptr)==RLV_WEAR_ADD&&!locks.canDetach(&item));locks.pointLocks.clear();
 locks.m_AttachObjRem[{30}]=1;assert(locks.canAttach(&item,nullptr)==RLV_WEAR_ADD&&!locks.canDetach(&item));locks.m_AttachObjRem.clear();
 auto& folders=RlvFolderLocks::instance();folders.attachments.insert({10});
 assert(locks.canAttach(&item,nullptr)==RLV_WEAR_ADD&&!locks.canDetach(&item));folders.attachments.clear();
 folders.folders.insert({20});assert(locks.canAttach(&item,nullptr)==RLV_WEAR_LOCKED);folders.folders.clear();
 assert(locks.canDetach(&item));attachments.clear();assert(locks.canDetach(nullptr));
 RlvWearableLocks clothing;gInventory.items[{10}]=item;worn={{10}};
 assert(clothing.canRemove(&item)&&clothing.canRemove(LLWearableType::WT_SHIRT)&&!clothing.hasLockedWearable(LLWearableType::WT_SHIRT));
 clothing.typeLocked=true;assert(!clothing.canRemove(&item)&&clothing.hasLockedWearable(LLWearableType::WT_SHIRT));clothing.typeLocked=false;
 folders.wearables.insert({10});assert(!clothing.canRemove(&item)&&!clothing.canRemove(LLWearableType::WT_SHIRT));
 gInventory.items[{11}]={{11},{20},0};worn.push_back({11});assert(clothing.canRemove(LLWearableType::WT_SHIRT));
 assert(clothing.hasLockedWearable(LLWearableType::WT_SHIRT));worn.clear();assert(!clothing.canRemove(&item));
}
""".replace('PRODUCTION',bodies)
        fixture='#include <algorithm>\n'+fixture
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'locks.cpp';binary=path.with_suffix('.exe');path.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
