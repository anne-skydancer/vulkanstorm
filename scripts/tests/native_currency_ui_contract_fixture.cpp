#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <utility>
struct Widget{
 virtual~Widget()=default;
 bool visible=true,enabled=true,prepared=false;std::string original,text,label;void*userdata=nullptr;
 void setVisible(bool value){visible=value;}bool getVisible()const{return visible;}
 bool getEnabled()const{return enabled;}
 void setTextArg(const std::string&name,const std::string&value){assert(original.find(name)!=std::string::npos);text=original;text.replace(text.find(name),name.size(),value);}
};
using LLUICtrl=Widget;
struct LLLineEditor:Widget{
 void setLabel(std::string value){label=std::move(value);}
 void setText(std::string value){text=std::move(value);}void selectAll(){}
 template<class F>void setPrevalidate(F){prepared=true;}
 template<class F>void setKeystrokeCallback(F,void*owner){userdata=owner;}
};
struct LLPanel{
 std::map<std::string,std::unique_ptr<Widget>>children;std::map<std::string,std::string>strings;
 void add(const std::string&name,const std::string&text){
  std::unique_ptr<Widget>child=name=="currency_amt"?std::make_unique<LLLineEditor>():std::make_unique<Widget>();
  child->original=child->text=text;children[name]=std::move(child);
 }
 template<class T>T*getChild(const std::string&name){auto*result=dynamic_cast<T*>(children.at(name).get());assert(result);return result;}
 Widget*getChildView(const std::string&name){return getChild<Widget>(name);}
 Widget*findChildView(const std::string&name){auto i=children.find(name);return i==children.end()?nullptr:i->second.get();}
 std::string getString(const std::string&name){return strings.at(name);}
};
struct LLTextValidate{static bool validateNonNegativeS32(int){return true;}};
struct LLStringUtil{static inline const std::string null;};
std::string llformat(const char*,int value){return std::to_string(value);}
struct LLCurrencyUIManager{
 struct Impl{
  LLPanel&mPanel;bool mHidden=false,mUserEnteredCurrencyBuy=false,mSupportsInternationalBilling=false,mCurrencyChanged=false,estimated=true;
  int mUserCurrencyBuy=2000;std::string mZeroMessage="None required";
  explicit Impl(LLPanel&panel):mPanel(panel){}
  void prepare();void updateUI();static void onCurrencyKey(LLLineEditor*,void*){}
  bool hasEstimate()const{return estimated;}std::string getLocalEstimate(){return "USD 12.34";}
 };
 Impl&impl;void setAmount(int,bool);
};
METHODS
int main(){
 LLPanel panel;
HOST_SCHEMA
 LLCurrencyUIManager::Impl impl(panel);LLCurrencyUIManager manager{impl};
 impl.prepare();assert(panel.getChild<LLLineEditor>("currency_amt")->prepared);
 assert(panel.getChild<LLLineEditor>("currency_amt")->userdata==&impl);
 manager.setAmount(0,true);assert(!impl.mCurrencyChanged);
 assert(panel.getChild<LLLineEditor>("currency_amt")->text.empty());
 assert(panel.getChild<Widget>("currency_est")->text.find(panel.getString("estimated_zero"))!=std::string::npos);
 manager.setAmount(42,false);assert(impl.mCurrencyChanged && panel.getChild<LLLineEditor>("currency_amt")->text=="42");
 assert(panel.getChild<Widget>("currency_est")->text.find("USD 12.34")!=std::string::npos);
 impl.mSupportsInternationalBilling=true;impl.updateUI();
 assert(panel.getChildView("currency_links")->getVisible() && panel.getChildView("exchange_rate_note")->getVisible());
 panel.getChildView("getting_data")->setVisible(true);impl.mHidden=true;impl.updateUI();
 for(const auto*name:{"currency_action","currency_amt","currency_est","getting_data","currency_links","exchange_rate_note"})
  assert(!panel.getChildView(name)->getVisible());
 impl.mHidden=false;impl.mSupportsInternationalBilling=false;impl.updateUI();
 assert(panel.getChildView("currency_amt")->getVisible());
 assert(!panel.getChildView("currency_links")->getVisible() && !panel.getChildView("exchange_rate_note")->getVisible());
}
