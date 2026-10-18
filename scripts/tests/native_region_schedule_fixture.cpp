#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <cstdio>
#include <algorithm>
using U64=unsigned long long;using S32=int;
struct LLSD{int n=0;int asInteger()const{return n;}};
template<class T>T llclamp(T v,T l,T h){return std::clamp(v,l,h);}std::string llformat(const char*fmt,int v){char b[32];std::snprintf(b,sizeof b,fmt,v);return b;}
struct LLView{bool live=true;virtual~LLView()=default;};
template<class T>struct LLHandle{T*p=nullptr;T*get()const{return p&&p->live?p:nullptr;}};
struct LLFloater:LLView{bool closed=false;void onClose(bool){closed=true;}LLHandle<LLFloater>getHandle(){return{this};}};
struct Field{bool visible=true,enabled=false;std::string text;bool getVisible()const{return visible;}void setText(std::string v){text=v;}void setEnabled(bool v){enabled=v;}};
unsigned account=1,regionID=1;bool manage=true;
std::function<bool()>vs_native_im_guard(){auto old=account;return[old]{return old==account;};}
struct Region{unsigned id;bool current()const{return id==regionID;}bool operator==(const Region&v)const{return id==v.id;}bool canManageEstate()const{return manage;}};
std::optional<Region>vs_current_region_context(){return Region{regionID};}
struct LLFloaterRegionRestartSchedule:LLFloater{LLView owner;LLHandle<LLView>mOwnerHandle{&owner};U64 mRequestEpoch=1;Field hours,minutes,am,save;Field*mHoursLineEditor=&hours,*mMinutesLineEditor=&minutes,*mPMAMButton=&am,*mSaveButton=&save;std::function<bool()>requestGuard(bool=false);void onClose(bool);void onCommitHours(const LLSD&);void onCommitMinutes(const LLSD&);};
METHODS
int main(){LLFloaterRegionRestartSchedule panel;auto read=panel.requestGuard(),write=panel.requestGuard(true);assert(read()&&write());manage=false;assert(read()&&!write());manage=true;
++account;assert(!read()&&!write());auto pending=panel.requestGuard();++regionID;assert(!pending());pending=panel.requestGuard();++panel.mRequestEpoch;assert(!pending());pending=panel.requestGuard();panel.owner.live=false;assert(!pending());panel.owner.live=true;pending=panel.requestGuard();panel.onClose(false);assert(panel.closed&&!pending());
panel.onCommitHours({0});assert(panel.hours.text=="12");panel.onCommitHours({19});assert(panel.hours.text=="12");panel.onCommitHours({-1});assert(panel.hours.text=="01");panel.am.visible=false;panel.onCommitHours({25});assert(panel.hours.text=="23");panel.onCommitHours({-1});assert(panel.hours.text=="00");panel.onCommitMinutes({70});assert(panel.minutes.text=="59");panel.onCommitMinutes({-5});assert(panel.minutes.text=="00"&&panel.save.enabled);
}
