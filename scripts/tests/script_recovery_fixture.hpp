// Isolated adapters for compiling the actual recovery transaction implementation.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
struct LLUUID { int id=0; LLUUID(int value=0):id(value){} bool notNull()const{return id!=0;} bool operator==(const LLUUID& b)const{return id==b.id;} bool operator!=(const LLUUID& b)const{return id!=b.id;} };
struct LLSD {
 std::string text; int number=0; std::map<std::string,LLSD> data; std::vector<LLSD> array;
 using array_const_iterator=std::vector<LLSD>::const_iterator;
 LLSD()=default; LLSD(const char* s):text(s){} LLSD(std::string s):text(std::move(s)){} LLSD(LLUUID id):number(id.id){}
 LLSD& operator[](const std::string& key){return data[key];}
 const LLSD& operator[](const std::string& key)const{static LLSD missing;auto i=data.find(key);return i==data.end()?missing:i->second;}
 std::string asString()const{return text;} LLUUID asUUID()const{return {number};} bool has(const std::string& k)const{return data.count(k)!=0;}
 array_const_iterator beginArray()const{return array.begin();}array_const_iterator endArray()const{return array.end();}
};
LLUUID gAgentID{1},gAgentSessionID{2};bool nativeCurrent=true;
std::function<bool()> vs_native_im_guard(){return []{return nativeCurrent;};}
struct LLInventoryCallback{virtual ~LLInventoryCallback()=default;virtual void fire(const LLUUID&)=0;};
template<class T> struct LLPointer{T* p=nullptr;LLPointer(T* v=nullptr):p(v){} bool notNull()const{return p!=nullptr;}T* operator->()const{return p;}operator T*()const{return p;}};
namespace LLAssetType{enum EType{AT_LSL_TEXT};}namespace LLInventoryType{const int IT_LSL=1;}namespace LLFolderType{const int FT_LOST_AND_FOUND=7,FT_TRASH=8;}
std::function<void()> afterUpdateServer,afterUpdateItem,afterNotify;
struct LLViewerInventoryItem{std::string name; LLViewerInventoryItem()=default;LLViewerInventoryItem(LLPointer<LLViewerInventoryItem> v):name(v->name){} LLAssetType::EType getType()const{return LLAssetType::AT_LSL_TEXT;} void rename(const std::string& n){name=n;}void updateServer(bool){if(afterUpdateServer)afterUpdateServer();}};
struct Inventory{std::map<int,LLViewerInventoryItem> items;int trashed=0;LLUUID findCategoryUUIDForType(int type){return {type};}LLViewerInventoryItem* getItem(LLUUID id){auto i=items.find(id.id);return i==items.end()?nullptr:&i->second;}
 void updateItem(LLPointer<LLViewerInventoryItem>){if(afterUpdateItem)afterUpdateItem();}void notifyObservers(){if(afterNotify)afterNotify();}void changeItemParent(LLViewerInventoryItem*,LLUUID,bool){++trashed;}}gInventory;
struct LLFolderViewFolder{enum{RECURSE_UP};virtual ~LLFolderViewFolder()=default;void setOpenArrangeRecursively(bool,int){}};
struct LLInventoryPanel{static LLInventoryPanel* getActiveInventoryPanel(bool){return nullptr;}LLFolderViewFolder* getItemByID(LLUUID){return nullptr;}void setSelection(LLUUID,bool){}};
struct LLViewerAssetType{static void generateDescriptionFor(LLAssetType::EType,std::string&){}};
struct Agent{LLUUID getID(){return gAgentID;}LLUUID getSessionID(){return gAgentSessionID;}std::string cap="upload";std::string getRegionCapability(const char*){return cap;}}gAgent;
struct LLTransactionID{static int tnull;};int LLTransactionID::tnull=0;const int NO_INV_SUBTYPE=0,PERM_MOVE=1,PERM_TRANSFER=2;
using LLFILE=FILE;struct llstat{std::int64_t st_size=0;};std::map<std::string,std::string> files;bool shortRead=false;std::vector<std::string> removed;
struct LLFile{static bool isfile(const LLSD& path){return files.count(path.asString());}static int stat(const std::string& path,llstat* s){if(!files.count(path))return -1;s->st_size=files[path].size();return 0;}
 static FILE* fopen(const std::string& path,const char*){auto i=files.find(path);if(i==files.end())return nullptr;FILE* f=tmpfile();auto size=i->second.size();if(shortRead&&size)--size;fwrite(i->second.data(),1,size,f);rewind(f);return f;}static void close(FILE* f){fclose(f);}static void remove(const std::string& path){removed.push_back(path);files.erase(path);}};
namespace LLCore{struct HttpStatus{bool good=false;operator bool()const{return good;}};}
namespace LLCoreHttpUtil{struct HttpCoroutineAdapter{inline static std::string HTTP_RESULTS="http_result";static LLCore::HttpStatus getStatusFromLLSD(const LLSD& s){return {s.number==200};}};}
struct LLBufferedAssetUploadInfo{using invnUploadFinish_f=std::function<void(LLUUID,LLUUID,LLUUID,LLSD)>;using uploadFailed_f=std::function<bool(LLUUID,LLUUID,LLSD,std::string)>;};
struct LLResourceUploadInfo{using ptr_t=std::shared_ptr<LLResourceUploadInfo>;virtual ~LLResourceUploadInfo()=default;};
struct LLScriptAssetUpload:LLResourceUploadInfo{LLUUID item;std::string buffer;LLBufferedAssetUploadInfo::invnUploadFinish_f done;LLBufferedAssetUploadInfo::uploadFailed_f failure;LLScriptAssetUpload(LLUUID i,std::string b,LLBufferedAssetUploadInfo::invnUploadFinish_f d,LLBufferedAssetUploadInfo::uploadFailed_f f):item(i),buffer(std::move(b)),done(d),failure(f){}};
std::vector<std::shared_ptr<LLScriptAssetUpload>> uploads;
struct LLViewerAssetUpload{static void EnqueueInventoryUpload(const std::string&,LLResourceUploadInfo::ptr_t upload){uploads.push_back(std::static_pointer_cast<LLScriptAssetUpload>(upload));}};
std::vector<std::unique_ptr<LLInventoryCallback>> creates;
void create_inventory_item(LLUUID,LLUUID,LLUUID,int,const std::string&,const std::string&,LLAssetType::EType,int,int,int,LLInventoryCallback* cb){creates.emplace_back(cb);}
#define LL_WARNS() std::cerr
#define LL_INFOS() std::cerr
#define LL_ENDL "\n"
const std::string NEW_LSL_NAME="New Script";
std::string fixNewScriptDefaultName(const std::string& name){return name==NEW_LSL_NAME?"New_Script":name;}
