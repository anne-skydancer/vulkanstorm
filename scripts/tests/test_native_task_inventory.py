"""Compile actual persistent task model/publication/incarnation lifetimes without a device."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest
ROOT=Path(os.environ.get('VS_TASK_TEST_ROOT',Path(__file__).resolve().parents[2]))
def body(source,name):
 start=source.index(name);start += re.search(r'\n[ \t]*\{',source[start:]).end()-1;end=start+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
def compile_run(test,source):
 cc=shutil.which('clang++')or shutil.which('g++');test.assertIsNotNone(cc)
 with tempfile.TemporaryDirectory()as d:
  cpp=Path(d)/'task.cpp';exe=Path(d)/'task.exe';cpp.write_text(source)
  r=subprocess.run([cc,'-std=c++20',str(cpp),'-o',str(exe)],text=True,capture_output=True);test.assertEqual(r.returncode,0,r.stdout+r.stderr)
  r=subprocess.run([str(exe)],text=True,capture_output=True,timeout=20);test.assertEqual(r.returncode,0,r.stdout+r.stderr)
class NativeTaskInventoryTest(unittest.TestCase):
 def test_persistent_atomic_publication_item_incarnation_and_reentrant_retirement(self):
  s=(ROOT/'indra/newview/vstaskinventory.cpp').read_text()
  model='struct Model '+body(s,'    struct Model')+';'
  methods='\n'.join(signature+body(s,key)for signature,key in (
   ('static bool same(const VSWorldScene::ObjectHandle&a,const VSWorldScene::ObjectHandle&b)','    static bool same('),
   ('std::shared_ptr<Model> model(const VSWorldScene::ObjectHandle&object)const','    std::shared_ptr<Model> model('),
   ('LLInventoryObject*objectIn(const Model&model,const LLUUID&id)const','    LLInventoryObject* objectIn('),
   ('bool publish(const std::shared_ptr<Model>&model)','    bool publish('),
   ('Request*find(const LLUUID&id,U64 ticket)','    Request* find('),
   ('void finish(const LLUUID&id,U64 ticket,bool success,U16 serial,LLInventoryObject::object_list_t contents={})','    void finish(')))
  fixture=r'''
#include <cassert>
#include <map>
#include <list>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
using U64=unsigned long long;using S16=short;using U16=unsigned short;using F64=double;using LLUUID=int;using LLSD=int;
struct LLInventoryObject{virtual~LLInventoryObject()=default;virtual int getUUID()const=0;using object_list_t=std::list<std::shared_ptr<LLInventoryObject>>;};
struct LLViewerInventoryItem:LLInventoryObject{int id=1,fields=1;int getUUID()const override{return id;}LLSD asLLSD()const{return fields;}};
struct VSWorldScene{struct ObjectHandle{int host=1,id=1,local=1,region=1,generation=1,account=1,accountGeneration=1;};};
struct LLFrameTimer{inline static double now=1;static double getTotalSeconds(){return now;}};
struct VSTaskInventory{
 struct Result{VSWorldScene::ObjectHandle object;U16 serial=0;bool success=false;LLInventoryObject::object_list_t contents;};
 using Callback=std::function<void(const Result&)>;
 struct Impl{
 MODEL
 struct Request{VSWorldScene::ObjectHandle object;std::function<bool()>current;std::vector<Callback>callbacks;U64 ticket=0,transfer=0;U16 serial=0;bool receiving=false;F64 deadline=10;};
 std::map<LLUUID,std::shared_ptr<Model>>models;U64 nextRevision=0;bool active=true;
 std::map<LLUUID,Request>requests;
 std::function<void(const Result&)>changed=[](const auto&){};
 METHODS
 };
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();
 std::function<bool()>guard(const VSWorldScene::ObjectHandle&token,const LLUUID&id)const GUARD
};
int main(){
 VSTaskInventory service;auto p=service.mImpl;VSWorldScene::ObjectHandle token;bool current=true;
 auto model=std::make_shared<VSTaskInventory::Impl::Model>();model->object=token;model->current=[&]{return current;};p->models[token.id]=model;
 auto make=[&](int fields){auto item=std::make_shared<LLViewerInventoryItem>();item->fields=fields;return item;};
 auto add=[&](int ticket){VSTaskInventory::Impl::Request r;r.object=token;r.current=model->current;r.ticket=ticket;p->requests[token.id]=r;};
 add(1);int deliveries=0;p->requests[1].callbacks.push_back([&](const auto&r){assert(r.success&&r.serial==7&&r.contents.size()==1);++deliveries;});
 p->finish(1,2,true,7,{make(1)});assert(!model->known&&deliveries==0&&p->requests.size()==1);
 p->finish(1,1,true,7,{make(1)});assert(model->known&&deliveries==1&&model->serial==7&&p->requests.empty());
 auto first=service.guard(token,1);assert(first());auto entry=model->contents->front();
 add(2);p->finish(1,2,true,7,*model->contents);assert(first()); // genuine 304/cache publication doesn't replace unchanged item identity
 add(3);p->finish(1,3,true,8,{make(2)});assert(!first());assert(dynamic_cast<LLViewerInventoryItem*>(entry.get())->fields==1); // retained UI snapshot remains valid
 auto second=service.guard(token,1);assert(second());dynamic_cast<LLViewerInventoryItem*>(model->contents->front().get())->fields=3;assert(!second()); // mutable caller edit caught before formalpublication
 p->publish(model);auto third=service.guard(token,1);assert(third());
 add(4);p->changed=[&](const auto&){assert(p->requests.empty());add(5);};p->finish(1,4,true,9,*model->contents);assert(p->requests.at(1).ticket==5); // synchronous observer may request next revision
 p->changed=[](const auto&){};model->expected=11;p->finish(1,5,true,10,*model->contents);assert(model->retryDue==2&&model->expected==10); // stale accepted cap gets exact original1sec retry
 auto fourth=service.guard(token,1);assert(fourth());
 add(6);p->changed=[&](const auto&){current=false;};p->requests[1].callbacks.push_back([&](const auto&){assert(false);});p->finish(1,6,true,11,*model->contents);assert(!fourth());
 current=true;p->changed=[](const auto&){};token.generation++;assert(!service.guard(token,1)());
 p->active=false;assert(!third());
}
'''
  guard=body(s,'std::function<bool()> VSTaskInventory::guard(').replace('id.notNull()','id != 0').replace('id.isNull()','id == 0')
  compile_run(self,fixture.replace('MODEL',model).replace('METHODS',methods.replace('return item;', 'return item.get();')).replace('GUARD',guard))
 def test_stale_task_upload_releases_only_original_same_account_controller(self):
  s=(ROOT/'indra/newview/llviewerassetupload.cpp').read_text()
  actual='struct TaskRetirement '+body(s,'    struct TaskRetirement')+';'
  compile_run(self,r"""
#include <cassert>
#include <functional>
#include <memory>
#include <string>
struct LLSD{};struct UUID{bool notNull()const{return true;}};
struct LLResourceUploadInfo{using ptr_t=std::shared_ptr<LLResourceUploadInfo>;virtual~LLResourceUploadInfo()=default;virtual void failedUpload(LLSD&,std::string&)=0;};
struct LLBufferedAssetUploadInfo:LLResourceUploadInfo{int failures=0;UUID getTaskId(){return{};}void failedUpload(LLSD&,std::string&)override{++failures;}};
struct LLTrans{static std::string getString(const char*){return "failed";}};
"""+actual+r"""
int main(){auto task=std::make_shared<LLBufferedAssetUploadInfo>();bool active=true,item=true;std::function<bool()>account=[&]{return active;},current=[&]{return item;};
 {TaskRetirement scope{task,account,current};}assert(task->failures==0);
 {TaskRetirement scope{task,account,current};item=false;}assert(task->failures==1);
 {TaskRetirement scope{task,account,current};active=false;}assert(task->failures==1);
 active=true;item=false;{TaskRetirement scope{task,account,current};item=true;}assert(task->failures==1); // only validated own publication advances guard
}
""")
 def test_real_drop_permission_policy_matches_frozen_reference(self):
  original=(Path(__file__).with_name('task_drop_willObjectAcceptInventory_reference.cpp')).read_text()
  original=original[original.index('EAcceptance '):].replace('LLToolDragAndDrop::willObjectAcceptInventory','Original::willObjectAcceptInventory')
  source=(ROOT/'indra/newview/lltooldraganddrop.cpp').read_text();start=source.index('EAcceptance LLToolDragAndDrop::willObjectAcceptInventory(const VSTaskInventoryObject&')
  native=source[start:source.index('\n}',start)+2].replace('LLToolDragAndDrop::willObjectAcceptInventory','Native::willObjectAcceptInventory')
  compile_run(self,r"""
#include <cassert>
#include <string>
#include <vector>
using U64=unsigned long long;using EAcceptance=int;using EDragAndDropType=int;
constexpr int ACCEPT_NO=0,ACCEPT_NO_LOCKED=1,ACCEPT_YES_MULTI=5,DAD_NONE=0,DAD_CATEGORY=1,PERM_TRANSFER=1,PERM_ITEM_UNRESTRICTED=7,LL_PCODE_VOLUME=9;
namespace LLAssetType{constexpr int AT_OBJECT=1,AT_BODYPART=2,AT_CLOTHING=3,AT_CALLINGCARD=4;}
struct LLPermissions{int owner=1,mask=7;bool transfer=true;int getOwner()const{return owner;}int getMaskBase()const{return mask;}bool allowOperationBy(int,int)const{return transfer;}};
struct LLInventoryItem{int type=1,parent=2;LLPermissions perm;int getType(){return type;}int getUUID(){return 8;}int getParentUUID(){return parent;}const LLPermissions&getPermissions(){return perm;}virtual~LLInventoryItem()=default;};
struct LLViewerInventoryItem:LLInventoryItem{bool complete=true,link=false;bool isFinished(){return complete;}bool getIsLinkType(){return link;}int getUUID(){return 8;}};
struct Agent{int getID(){return 1;}int getGroupID(){return 3;}}gAgent;
struct LLViewerObject;struct Avatar{bool valid=true,worn=false,sitting=false;LLViewerObject*root=nullptr;bool isWearingAttachment(int){return worn;}bool isSitting(){return sitting;}LLViewerObject*getRoot(){return root;}}avatar;using LLVOAvatarSelf=Avatar;auto*gAgentAvatarp=&avatar;bool isAgentAvatarValid(){return avatar.valid;}
struct Wearables{bool worn=false;bool isWearingItem(int){return worn;}}gAgentWearables;
struct LLViewerObject{int id=1,pcode=9;bool modify=true,inventory=false,owner=true,attached=false,locked=false;int getID(){return id;}bool permModify(){return modify;}bool flagAllowInventoryAdd(){return inventory;}bool permYouOwner(){return owner;}int getPCode(){return pcode;}bool isAttachment(){return attached;}LLViewerObject*getRootEdit(){return this;}bool attachmentLocked(){return locked;}bool agentSittingOnRoot(){return avatar.valid&&avatar.sitting&&avatar.root==this;}bool wearing(int id){return avatar.valid&&avatar.isWearingAttachment(id);}};
using VSTaskInventoryObject=LLViewerObject*;
struct Rlv{inline static bool enabled=true;static bool isEnabled(){return enabled;}};using rlv_handler_t=Rlv;
constexpr int RLV_BHVR_UNSIT=1,RLV_BHVR_SITTP=2;
struct Locks{bool isLockedAttachment(const LLViewerObject*p){return p->locked;}}gRlvAttachmentLocks;
struct Handler{bool unsit=false,sittp=false;bool hasBehaviour(int x){return x==1?unsit:sittp;}}gRlvHandler;
struct Original{static int willObjectAcceptInventory(LLViewerObject*,LLInventoryItem*,EDragAndDropType);};
struct Native{static int willObjectAcceptInventory(const VSTaskInventoryObject&,LLInventoryItem*,EDragAndDropType);};
"""+original+'\n'+native+r"""
int main(){for(unsigned bits=0;bits<4096;++bits)for(int type=1;type<=5;++type)for(int category=0;category<2;++category){LLViewerObject obj;LLViewerInventoryItem item;avatar.valid=true;avatar.root=&obj;avatar.sitting=bits&1;avatar.worn=bits&2;gAgentWearables.worn=bits&4;obj.modify=bits&8;obj.inventory=bits&16;obj.owner=bits&32;obj.attached=bits&64;obj.locked=bits&128;gRlvHandler.unsit=bits&256;gRlvHandler.sittp=bits&512;item.perm.transfer=bits&1024;item.perm.mask=bits&2048?7:0;item.type=type;item.perm.owner=bits&1?1:2;item.link=bits%23==0;item.complete=bits%17!=0;obj.pcode=bits%11?9:3;item.parent=bits%13?2:1;VSTaskInventoryObject native=&obj;assert(Original::willObjectAcceptInventory(&obj,&item,category)==Native::willObjectAcceptInventory(native,&item,category));}
}
""")
 def test_actual_request_overloads_cache_pending_error_and_category_policy(self):
  source=(ROOT/'indra/newview/vstaskinventoryobject.cpp').read_text()
  methods='\n'.join(signature+body(source,key)for signature,key in (
   ('void VSTaskInventoryObject::requestInventory()const','void VSTaskInventoryObject::requestInventory() const'),
   ('bool VSTaskInventoryObject::requestInventory(VSTaskInventory::Callback callback)const','bool VSTaskInventoryObject::requestInventory(VSTaskInventory::Callback callback)'),
   ('void VSTaskInventoryObject::getInventoryContents(LLInventoryObject::object_list_t&contents)const','void VSTaskInventoryObject::getInventoryContents('),
   ('LLInventoryObject*VSTaskInventoryObject::getInventoryRoot()const','LLInventoryObject* VSTaskInventoryObject::getInventoryRoot(')))
  compile_run(self,r"""
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <optional>
#include <functional>
#include <memory>
#include <list>
#include <vector>
namespace LLAssetType{constexpr int AT_CATEGORY=1;}
struct LLInventoryObject{int type;int getType(){return type;}using object_list_t=std::list<std::shared_ptr<LLInventoryObject>>;};
struct VSTaskInventory{
 struct Result{bool success=true;LLInventoryObject::object_list_t contents;};
 using Callback=std::function<void(const Result&)>;
 struct State{bool pending=false,dirty=false;};
 State status;std::optional<Result>cache;int requests=0,notifications=0;std::vector<Callback>callbacks;
 State state(int)const{return status;}auto snapshot(int)const{return cache;}
 void notify(int){++notifications;}bool request(int,Callback callback){++requests;callbacks.push_back(callback);return true;}
};
struct Legacy{void requestInventory(){assert(false);}void getInventoryContents(LLInventoryObject::object_list_t&){assert(false);}LLInventoryObject*getInventoryRoot(){assert(false);return nullptr;}};
struct LegacyPtr{Legacy*get()const{return nullptr;}};
struct VSTaskInventoryObject{bool valid=true,mNative=true;int mObject=1;LegacyPtr mLegacy;VSTaskInventory*inventory;
 explicit operator bool()const{return valid;}VSTaskInventory*nativeInventory()const{return inventory;}
 void requestInventory()const;bool requestInventory(VSTaskInventory::Callback)const;void getInventoryContents(LLInventoryObject::object_list_t&)const;LLInventoryObject*getInventoryRoot()const;
};
"""+methods+r"""
int main(){VSTaskInventory inventory;VSTaskInventoryObject object;object.inventory=&inventory;
 auto category=std::make_shared<LLInventoryObject>();category->type=1;auto item=std::make_shared<LLInventoryObject>();item->type=2;
 inventory.cache=VSTaskInventory::Result{true,{item,category}};
 LLInventoryObject::object_list_t contents;object.getInventoryContents(contents);assert(contents.size()==1&&contents.front()==item);assert(object.getInventoryRoot()==category.get());
 object.requestInventory();assert(inventory.requests==0&&inventory.notifications==1);
 inventory.status.pending=true;for(int i=0;i<100;++i)object.requestInventory();assert(inventory.requests==0&&inventory.callbacks.empty());
 bool failure=false;assert(object.requestInventory([&](const auto&r){failure=!r.success;}));assert(inventory.requests==1&&inventory.callbacks.size()==1);inventory.callbacks.front()(VSTaskInventory::Result{false,{}});assert(failure);
 inventory.status.pending=false;bool cached=false;assert(object.requestInventory([&](const auto&r){cached=r.success;}));assert(cached&&inventory.requests==1);
 inventory.status.dirty=true;object.requestInventory();assert(inventory.requests==2);
 object.valid=false;assert(!object.requestInventory([](const auto&){assert(false);}));
}
""")
 def test_actual_reset_retires_models_and_observers_before_abort_reentry(self):
  source=(ROOT/'indra/newview/vstaskinventory.cpp').read_text()
  reset=body(source,'void VSTaskInventory::reset()')
  compile_run(self,r"""
#include <cassert>
#include <map>
#include <memory>
#include <vector>
#include <functional>
using U64=unsigned long long;
struct Xfer{std::function<void(U64)>callback;void abortRequestById(U64 id,int){callback(id);}}xfer;auto*gXferManager=&xfer;
struct Signal{bool disconnected=false;void disconnect_all_slots(){disconnected=true;}};
struct VSTaskInventory{struct Impl{bool active=true;std::map<int,int>models;Signal changed;struct Request{U64 transfer;};std::map<int,Request>requests;};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();void reset() RESET};
int main(){VSTaskInventory inventory;auto owner=inventory.mImpl;owner->models[1]=7;owner->requests[1]={99};int aborted=0;
 xfer.callback=[&](U64 id){assert(id==99);assert(!owner->active&&owner->models.empty()&&owner->requests.empty()&&owner->changed.disconnected);++aborted;inventory.reset();};
 inventory.reset();assert(aborted==1);inventory.reset();assert(aborted==1);
}
""".replace('RESET',reset))
 def test_original_controllers_preserve_exact_branch_getter_and_wire_order(self):
  s=(ROOT/'indra/newview/vstaskinventorycontroller.h').read_text()
  # This independently verifies the transposition against retained original
  # implementation witnesses, including original packet field order.
  # The send witnesses carry the null-region guards of inventory bug 33
  # (9981a614ff, LLViewerObject); the shared templates test Peer::hasRegion().
  replacements=[('!mRegionp','!peer.hasRegion()'),('LLSelectMgr::getInstance()->findObjectPermissions(this)','peer.objectPermissions()'),('mRegionp->getHost()','peer.host()'),('mExpectedInventorySerialNum','peer.expectedSerial()'),('mInventory','peer.inventory()'),('mLocalID','peer.localID()'),('getInventoryItemByAsset(','peer.getInventoryItemByAsset('),('getInventoryObject(','peer.getInventoryObject('),('deleteInventoryItem(','peer.deleteInventoryItem('),('doInventoryCallback()','peer.doInventoryCallback()'),('permYouOwner()','peer.permYouOwner()'),('doUpdateInventory(','peer.doUpdateInventory(')]
  for old,new in [('doUpdateInventory','vs_do_update_task_inventory'),('updateInventory','vs_update_task_inventory'),('saveScript','vs_save_task_script'),('moveInventory','vs_move_task_inventory'),('removeInventory','vs_remove_task_inventory'),('updateInventoryLocal','vs_update_task_inventory_local'),('updateViewerInventoryAsset','vs_update_task_inventory_asset')]:
   with self.subTest(controller=old):
    witness=(Path(__file__).with_name('task_inventory_'+old+'_reference.cpp')).read_text();expected=body(witness,'void LLViewerObject::'+old+'(')
    for before,after in replacements:expected=expected.replace(before,after)
    expected=re.sub(r'\bmID\b','peer.id()',expected)
    self.assertEqual(body(s,'void '+new+'('),expected)
if __name__=='__main__':unittest.main()
