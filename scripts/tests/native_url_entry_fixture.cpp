#include <cassert>
#include <functional>
#include <memory>
#include <string>
using S32=int;using U64=unsigned long long;
template<class T>struct Handle{T*value=nullptr;std::weak_ptr<int>life;T*get()const{return life.expired()?nullptr:value;}};
struct LLSD{std::string value;std::string asString()const{return value;}};
struct Control{std::string value="https://parcel.test/media";int changes=0;LLSD getValue(){return {value};}void setVisible(bool){++changes;}};
struct Window{int busy=0;void decBusyCount(){--busy;}}window;
int changes=0,draws=0;bool account=true,selection=true;
struct LLPanel{std::shared_ptr<int>life=std::make_shared<int>(0);virtual~LLPanel()=default;Handle<LLPanel>getHandle(){return {this,life};}};
struct LLPanelLandMedia:LLPanel{std::function<void()>onChange;void setMediaType(const std::string&){++changes;}void setMediaURL(const std::string&){++changes;if(onChange)onChange();}};
struct LLFloater:LLPanel{bool visible=true;virtual void onClose(bool){}virtual void draw(){++draws;}void closeFloater(){visible=false;onClose(false);}Handle<LLFloater>getHandle(){return {this,life};}Window*getWindow(){return &window;}};
struct LLFloaterURLEntry:LLFloater{
 Handle<LLPanel>mPanelLandMediaHandle;std::function<bool()>mAccountCurrent=[] {return account;},mSelectionCurrent=[] {return selection;};U64 mRequest=0;bool mBusy=false;
 Control control;Control*mMediaURLEdit=&control;Control*getChildView(const char*){return &control;}
 void headerFetchComplete(S32,const std::string&);void onClose(bool)override;void draw()override;
 auto request(){auto*self=this;REQUEST_GUARD return current;}
};
METHODS
int main(){
 auto panel=std::make_unique<LLPanelLandMedia>();auto owner=std::make_unique<LLFloaterURLEntry>();owner->mPanelLandMediaHandle=panel->getHandle();
 owner->draw();assert(draws==1&&owner->visible);owner->mBusy=true;window.busy=1;auto first=owner->request();assert(first());
 auto latest=owner->request();assert(!first()&&latest());account=false;assert(!latest());account=true;selection=false;assert(!latest());selection=true;
 owner->headerFetchComplete(200,"text/html");assert(changes==2&&!owner->visible&&!owner->mBusy&&window.busy==0&&!latest());owner->onClose(false);assert(window.busy==0);
 owner->visible=true;owner->mBusy=true;window.busy=1;account=false;owner->headerFetchComplete(200,"bad");assert(changes==2&&!owner->visible&&!owner->mBusy&&window.busy==0);account=true;
 owner->visible=true;owner->mBusy=true;window.busy=1;selection=false;owner->headerFetchComplete(200,"bad");assert(changes==2&&!owner->visible&&!owner->mBusy&&window.busy==0);selection=true;
 owner->visible=true;panel.reset();owner->draw();assert(!owner->visible&&draws==1);
 panel=std::make_unique<LLPanelLandMedia>();owner->mPanelLandMediaHandle=panel->getHandle();auto pending=owner->request();owner.reset();assert(!pending());
 owner=std::make_unique<LLFloaterURLEntry>();owner->mPanelLandMediaHandle=panel->getHandle();panel->onChange=[&] {owner.reset();};auto* raw=owner.get();raw->headerFetchComplete(200,"reentrant");assert(!owner&&changes==4);
}
