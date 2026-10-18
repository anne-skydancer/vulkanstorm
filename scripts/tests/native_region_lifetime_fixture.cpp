#include <cassert>
#include <memory>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <optional>
using U64=unsigned long long;using U8=unsigned char;using S32=int;using LLExtStat=int;
struct LLUUID{int value=0;bool operator==(LLUUID v)const{return value==v.value;}bool operator!=(LLUUID v)const{return !(*this==v);}};
template<class T>struct LLHandle;
struct LLView{bool alive=true;virtual~LLView()=default;LLHandle<LLView>getHandle();};
template<class T>struct LLHandle{T*value=nullptr;T*get()const{return value&&value->alive?value:nullptr;}};
LLHandle<LLView>LLView::getHandle(){return {this};}
struct VSRegionContext{int id=1;LLUUID owner{7};bool live=true,manager=true;bool current()const{return live;}LLUUID getOwner()const{return owner;}bool canManageEstate()const;bool operator!=(const VSRegionContext&v)const{return id!=v.id;}};
VSRegionContext region;
struct Agent{bool god=false;LLUUID getID(){return {7};}bool isGodlike(){return god;}}gAgent;
bool VSRegionContext::canManageEstate()const{return region.manager||region.owner==gAgent.getID()||gAgent.god;}
std::optional<VSRegionContext>vs_current_region_context(){return region;}
unsigned accountEpoch=1;std::function<bool()>vs_native_im_guard(){unsigned old=accountEpoch;return[old]{return old==accountEpoch;};}
struct View:LLView{auto getHandle(){return LLHandle<LLView>{this};}};
GUARD
REQUEST
namespace LLAssetType{enum EType{AT_NOTECARD};}
constexpr int LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE=-1,LL_ERR_FILE_EMPTY=-2,LL_ERR_INSUFFICIENT_PERMISSIONS=-3;
std::ostringstream logs;
#define LL_INFOS(...) logs
#define LL_WARNS(...) logs
#define LL_ENDL ""
struct LLTrans{static std::string getString(const char*s){return s;}};
struct LLNotificationsUtil{static void add(const char*){}};
struct Item{LLUUID asset{4};LLUUID getAssetUUID()const{return asset;}};
struct Inventory{bool exists=true;Item item;Item*getItem(LLUUID){return exists?&item:nullptr;}}gInventory;
struct LLFileSystem{enum Mode{READ};LLFileSystem(LLUUID,LLAssetType::EType,Mode){}int getSize(){return 25;}void read(U8*out,int bytes){std::memcpy(out,"Linden text version 2\n{}\n",bytes);}};
struct Editor{int imports=0;bool importBuffer(const char*,int){++imports;return true;}};
struct LLPanelEstateCovenant:View{unsigned mCovenantRequestEpoch=1;Editor editor;Editor*mEditor=&editor;int mAssetStatus=0,writes=0;LLUUID covenant;enum{ASSET_LOADED=2};void sendChangeCovenantID(LLUUID){++writes;}void setCovenantID(LLUUID value){covenant=value;}static void onLoadComplete(const LLUUID&,LLAssetType::EType,void*,S32,LLExtStat);};
CALLBACK
int main(){LLPanelEstateCovenant panel;
auto request=[&]{return new RegionCovenantRequest{panel.getHandle(),region_info_guard(&panel,false,true),panel.mCovenantRequestEpoch,{3},{4}};};
auto call=[&](RegionCovenantRequest*value,int asset=4){LLPanelEstateCovenant::onLoadComplete({asset},LLAssetType::AT_NOTECARD,value,0,0);};
call(request());assert(panel.writes==1&&panel.editor.imports==1&&panel.covenant.value==4&&panel.mAssetStatus==2);
auto* pending=request();++accountEpoch;call(pending);assert(panel.writes==1);
pending=request();++panel.mCovenantRequestEpoch;call(pending);assert(panel.writes==1);
pending=request();region.id++;call(pending);assert(panel.writes==1);
pending=request();gInventory.item.asset={9};call(pending);assert(panel.writes==1);gInventory.item.asset={4};
pending=request();region.owner={9};region.manager=false;call(pending);assert(panel.writes==1);region.owner={7};region.manager=true;
pending=request();panel.alive=false;call(pending);assert(panel.writes==1);panel.alive=true;
call(request(),8);assert(panel.writes==1);
pending=request();gInventory.exists=false;call(pending);assert(panel.writes==1);
View owner;auto read=region_info_guard(&owner);auto manage=region_info_guard(&owner,false,true);auto own=region_info_guard(&owner,true,true);
region.owner={9};region.manager=true;assert(read()&&manage()&&!own());region.manager=false;assert(read()&&!manage());gAgent.god=true;assert(manage()&&own());
}
