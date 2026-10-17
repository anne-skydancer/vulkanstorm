#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using S32=int;struct LLSD{};
int epoch=1,selectionEpoch=1,calls=0;bool selectionAlive=true;
std::function<bool()>vs_native_im_guard(){int capture=epoch;return[capture]{return epoch==capture;};}
#define VS_NATIVE_VULKAN 1
#include "vsnativeim.h"
struct View;
struct Handle{View* pointer;std::weak_ptr<int>alive;View*get()const{return alive.expired()?nullptr:pointer;}};
struct View{std::shared_ptr<int>life=std::make_shared<int>(0);Handle getHandle(){return {this,life};}};
struct Parcel{int price=10;float hours=2;int getPassPrice()const{return price;}float getPassHours()const{return hours;}}parcel;
struct Selection{Parcel*getParcel()const{return selectionAlive?&parcel:nullptr;}};
struct LLViewerParcelMgr{static LLViewerParcelMgr&instance(){static LLViewerParcelMgr p;return p;}std::function<bool()>selectionGuard(){int capture=selectionEpoch;return[capture]{return selectionEpoch==capture;};}std::shared_ptr<Selection>getParcelSelection(){return std::make_shared<Selection>();}};
struct Panel:View{
 void setGroup(int){++calls;}void callbackAvatarCBAccess(const std::vector<int>&){++calls;}void callbackAvatarCBBanned(const std::vector<int>&){++calls;}void callbackAvatarCBBanned2(const std::vector<int>&,int){++calls;}
 void exportListCallback(View*,const std::vector<std::string>&){++calls;}void importListCallback(View*,const std::vector<std::string>&){++calls;}
 auto group(){return GROUP_CLOSURE;}auto access(){return ACCESS_CLOSURE;}auto banned(){return BANNED_CLOSURE;}auto duration(){return DURATION_CLOSURE;}
 auto import(View*list){return IMPORT_CLOSURE;}auto exportFile(View*list){return EXPORT_CLOSURE;}
 static bool cbBuyPass(const LLSD&,const LLSD&){++calls;return false;}
 auto pass(){const auto selection=LLViewerParcelMgr::instance().getParcelSelection();const auto current=LLViewerParcelMgr::instance().selectionGuard();const auto account=vs_native_im_guard();const int pass_price=10;const float pass_hours=2;return PASS_CLOSURE;}
};
int main(){
 auto panel=std::make_unique<Panel>();auto list=std::make_unique<View>();
 std::function<void(int)>group=panel->group();std::function<void(const std::vector<int>&,const std::vector<std::string>&)>access=panel->access(),banned=panel->banned();std::function<void(const std::vector<int>&,int)>duration=panel->duration();
 std::function<void(const std::vector<std::string>&,int,int)>import=panel->import(list.get()),exportFile=panel->exportFile(list.get());
 auto invoke=[&]{group(3);access({1},{"one"});banned({2},{"two"});duration({3},60);import({"test.csv"},0,0);exportFile({"test.csv"},0,0);};
 invoke();assert(calls==6);++epoch;invoke();assert(calls==6);--epoch;++selectionEpoch;invoke();assert(calls==6);--selectionEpoch;
 list.reset();import({"test.csv"},0,0);exportFile({"test.csv"},0,0);assert(calls==6);
 auto pass=panel->pass();pass({},{});assert(calls==7);++epoch;pass({},{});--epoch;++selectionEpoch;pass({},{});--selectionEpoch;assert(calls==7);
 ++parcel.price;pass({},{});--parcel.price;++parcel.hours;pass({},{});--parcel.hours;selectionAlive=false;pass({},{});selectionAlive=true;assert(calls==7);
 panel.reset();invoke();assert(calls==7);
}
