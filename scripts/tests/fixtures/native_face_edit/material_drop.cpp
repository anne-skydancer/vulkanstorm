#include <cassert>
#include <memory>
#include <functional>
#include <string>
#include <sstream>
#include <map>
using U8=unsigned char;const int PERM_COPY=1,PERM_TRANSFER=2,PERM_MODIFY=4,ACCEPT_YES_COPY_SINGLE=2,TASK_INVENTORY_ITEM_KEY=0;
#define LL_WARNS() std::ostringstream()
#define LL_ENDL ""
struct LLUUID {int id=0;bool operator<(const LLUUID& r)const{return id<r.id;}static const LLUUID null;};const LLUUID LLUUID::null{};
struct Agent {LLUUID getID(){return {1};}}gAgent;
namespace LLAssetType {enum EType{AT_TEXTURE,AT_MATERIAL,AT_OTHER};}
struct Permissions {int mask=7;bool allowOperationBy(int bit,LLUUID)const{return mask&bit;}};
struct LLInventoryItem {Permissions perms;LLAssetType::EType type=LLAssetType::AT_TEXTURE;LLUUID id{7},asset{8};auto& getPermissions(){return perms;}auto getType(){return type;}auto getUUID(){return id;}auto getAssetUUID(){return asset;}};
struct LLViewerInventoryItem:LLInventoryItem {LLViewerInventoryItem(const LLInventoryItem* i):LLInventoryItem(*i){}};
template<class T>struct LLPointer {std::unique_ptr<T> ptr;LLPointer(T* p):ptr(p){}T* operator->(){return ptr.get();}operator T*(){return ptr.get();}};
struct LLSD {std::string str;std::string& operator[](const char*){return str;}};
struct LLNotificationsUtil {static inline unsigned errors=0;static void add(const char*,const LLSD&){++errors;}};
struct LLViewerObject {bool live=true,dirty=false,listener=true,existing=false,sourceItemCurrent=true;int acceptance=2,updates=0,materials=0,requests=0,removes=0;bool isInventoryDirty(){return dirty;}bool hasInventoryListeners(){return listener;}void requestInventory(){++requests;}LLViewerInventoryItem* getInventoryItemByAsset(LLUUID,LLAssetType::EType){static LLInventoryItem item;static LLViewerInventoryItem result(&item);return existing?&result:nullptr;}void removeInventory(LLUUID){++removes;}void updateMaterialInventory(LLViewerInventoryItem*,U8,bool){++materials;}void updateInventory(LLViewerInventoryItem*,U8,bool){++updates;}void dirtyInventory(){dirty=true;}};
struct Inventory {int removed=0,notifications=0;std::function<void()> callback;void deleteObject(LLUUID){++removed;}void notifyObservers(){++notifications;if(callback)callback();}}gInventory;
struct Objects {LLViewerObject* source=nullptr;auto* findObject(LLUUID){return source;}}gObjectList;
struct VSTaskInventoryObject {LLViewerObject* p=nullptr;bool native()const{return true;}LLViewerObject* legacy()const{return p;}explicit operator bool()const{return p&&p->live;}const auto* operator->()const{return this;}auto objectGuard()const{return [p=p]{return p&&p->live;};}auto itemGuard(LLUUID)const{return [p=p]{return p&&p->live&&p->sourceItemCurrent;};}bool requiresInventoryFetch()const{return p->dirty;}void requestInventory()const{p->requestInventory();}auto* getInventoryItemByAsset(LLUUID id,LLAssetType::EType type)const{return p->getInventoryItemByAsset(id,type);}void removeInventory(LLUUID id)const{p->removeInventory(id);}void updateMaterialInventory(LLViewerInventoryItem* i,U8 key,bool v)const{p->updateMaterialInventory(i,key,v);}void updateInventory(LLViewerInventoryItem* i,U8 key,bool v)const{p->updateInventory(i,key,v);}void dirtyInventory()const{p->dirtyInventory();}static VSTaskInventoryObject find(LLUUID){return {gObjectList.source};}};
struct LLToolDragAndDrop {enum ESource {SOURCE_LIBRARY,SOURCE_AGENT,SOURCE_WORLD,SOURCE_OTHER};static int willObjectAcceptInventory(LLViewerObject* p,LLInventoryItem*){return p->acceptance;}static int willObjectAcceptInventory(const VSTaskInventoryObject& p,LLInventoryItem*){return p.p->acceptance;}static bool handleDropMaterialProtections(LLViewerObject*,LLInventoryItem*,ESource,const LLUUID&);static bool handleDropMaterialProtections(const VSTaskInventoryObject&,LLInventoryItem*,ESource,const LLUUID&);};
ORIGINAL
NATIVE
int main(){
for(int bits=0;bits<512;++bits)for(int from=0;from<4;++from)for(int type=0;type<3;++type){LLViewerObject original,native,sourceA,sourceB;original.dirty=native.dirty=bits&8;original.existing=native.existing=bits&16;original.acceptance=native.acceptance=(bits&32)?0:2;LLInventoryItem item;item.perms.mask=bits&7;item.type=LLAssetType::EType(type);gInventory={};gObjectList.source=(bits&64)?&sourceA:nullptr;LLNotificationsUtil::errors=0;bool a=LLToolDragAndDrop::handleDropMaterialProtections(&original,&item,LLToolDragAndDrop::ESource(from),{});auto deletes=gInventory.removed,notifies=gInventory.notifications;auto errors=LLNotificationsUtil::errors;
gInventory={};gObjectList.source=(bits&64)?&sourceB:nullptr;LLNotificationsUtil::errors=0;bool b=LLToolDragAndDrop::handleDropMaterialProtections(VSTaskInventoryObject{&native},&item,LLToolDragAndDrop::ESource(from),{});assert(a==b&&deletes==gInventory.removed&&notifies==gInventory.notifications&&errors==LLNotificationsUtil::errors);assert(original.updates==native.updates&&original.materials==native.materials&&original.requests==native.requests&&sourceA.removes==sourceB.removes);}
 LLViewerObject target;LLInventoryItem noCopy;noCopy.perms.mask=0;gInventory={};gInventory.callback=[&]{target.live=false;};assert(!LLToolDragAndDrop::handleDropMaterialProtections(VSTaskInventoryObject{&target},&noCopy,LLToolDragAndDrop::SOURCE_AGENT,{}));assert(gInventory.removed==1&&target.materials==0&&target.updates==0);
 target={};target.dirty=true;target.listener=false;gInventory={};assert(!LLToolDragAndDrop::handleDropMaterialProtections(VSTaskInventoryObject{&target},&noCopy,LLToolDragAndDrop::SOURCE_AGENT,{}));assert(target.requests==1&&gInventory.removed==0);
 target={};LLViewerObject source;source.sourceItemCurrent=false;gObjectList.source=&source;assert(!LLToolDragAndDrop::handleDropMaterialProtections(VSTaskInventoryObject{&target},&noCopy,LLToolDragAndDrop::SOURCE_WORLD,{}));assert(source.removes==0&&target.materials==0);
}
