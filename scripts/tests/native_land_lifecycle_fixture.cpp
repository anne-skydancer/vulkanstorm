#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <type_traits>
using S32=int;
struct LLUUID { int value=1;bool operator!=(const LLUUID& other)const{return value!=other.value;} };
struct LLSD { std::string value;LLSD(const char* s=""):value(s){}LLSD(int n):value(std::to_string(n)){}int asInteger()const{return std::stoi(value);}bool operator!=(const LLSD& o)const{return value!=o.value;} };
template<class T>struct LLHandle { T* raw;std::weak_ptr<int> alive;T* get()const{return alive.expired()?nullptr:raw;} };
struct LLView {std::shared_ptr<int> life=std::make_shared<int>(0);virtual~LLView()=default;LLHandle<LLView> getHandle(){return {this,life};}};
bool account=true,selection=true;
int purchases=0,sales=0;
struct Agent {LLUUID group;LLUUID getGroupID()const{return group;}}gAgent;
#define VS_NATIVE_VULKAN 1
#include "vsnativeim.h"
std::function<bool()> vs_native_im_guard(){return []{return account;};}
struct LLConfirmationManager {
 struct ListenerBase {virtual~ListenerBase()=default;virtual void confirmed(const std::string&)=0;};
 inline static std::unique_ptr<ListenerBase> pending;
 static void confirm(const std::string&,const std::string&,ListenerBase* p){pending.reset(p);}
};
struct Control {LLSD value;int index=0;LLSD getValue()const{return value;}int getCurrentIndex()const{return index;}};
using LLComboBox=Control;using LLUICtrl=Control;
struct Currency {int amount=10;std::string estimate="cost";int getAmount()const{return amount;}std::string getLocalEstimate()const{return estimate;}};
struct LLFloaterBuyLandUI:LLView {
 std::function<bool()>mAccountCurrent=vs_native_im_guard(),mSelectionCurrent=[] {return selection;};
 S32 mParcelPrice=100,mParcelBillableArea=512;Currency mCurrency;
 bool mIsForGroup=false,mCanBuy=true,mSiteValid=true;void* mTransaction=nullptr;std::string mSiteConfirm="click";
 Control plan;
 template<class T>T* getChild(const char*){return &plan;}
 void startBuyPostConfirm(const std::string&){++purchases;}
 void ask(){std::string action;BUY_CALLBACK}
};
struct LLFloaterSellLandUI:LLView {
 std::function<bool()>mAccountCurrent=vs_native_im_guard(),mSelectionCurrent=[] {return selection;};
 LLUUID mAuthorizedBuyer;std::map<std::string,Control>controls;
 LLFloaterSellLandUI(){controls["price"].value=100;controls["sell_to"].value="user";controls["sell_objects"].value="yes";}
 template<class T>T* getChild(const char* n){return &controls[n];}
 bool onConfirmSale(const LLSD&,const LLSD&){++sales;return false;}
 auto ask(){auto* self=this;S32 sale_price=100;return SELL_CALLBACK;}
};
int main(){
 auto confirm=[] {LLConfirmationManager::pending->confirmed("secret");LLConfirmationManager::pending.reset();};
 {
  LLFloaterBuyLandUI owner;owner.ask();confirm();assert(purchases==1);
  owner.ask();account=false;confirm();account=true;assert(purchases==1);
  owner.ask();selection=false;confirm();selection=true;assert(purchases==1);
  owner.ask();++owner.mParcelPrice;confirm();--owner.mParcelPrice;assert(purchases==1);
  owner.ask();++owner.mCurrency.amount;confirm();--owner.mCurrency.amount;assert(purchases==1);
  owner.ask();owner.mCurrency.estimate="changed";confirm();owner.mCurrency.estimate="cost";assert(purchases==1);
  owner.ask();++owner.plan.index;confirm();--owner.plan.index;assert(purchases==1);
  owner.mIsForGroup=true;owner.ask();++gAgent.group.value;confirm();assert(purchases==1);
  owner.mIsForGroup=false;owner.ask();owner.mTransaction=&owner;confirm();owner.mTransaction=nullptr;assert(purchases==1);
 }
 {auto owner=std::make_unique<LLFloaterBuyLandUI>();owner->ask();owner.reset();confirm();assert(purchases==1);}
 auto sell=std::make_unique<LLFloaterSellLandUI>();
 std::function<bool(const LLSD&,const LLSD&)> sale=sell->ask();sale({},{});assert(sales==1);
 sale=sell->ask();account=false;sale({},{});account=true;assert(sales==1);
 sale=sell->ask();selection=false;sale({},{});selection=true;assert(sales==1);
 sale=sell->ask();sell->controls["price"].value=200;sale({},{});sell->controls["price"].value=100;assert(sales==1);
 sale=sell->ask();++sell->mAuthorizedBuyer.value;sale({},{});assert(sales==1);
 sale=sell->ask();sell->controls["sell_objects"].value="no";sale({},{});assert(sales==1);
 sale=sell->ask();sell.reset();sale({},{});assert(sales==1);
}
