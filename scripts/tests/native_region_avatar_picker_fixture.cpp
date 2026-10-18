// Actual picker registrations retain the real requesting controller and policy.
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
struct LLUUID{int value=0;bool operator==(const LLUUID&o)const{return value==o.value;}};using uuid_vec_t=std::vector<LLUUID>;struct LLAvatarName{};
struct LLView{bool alive=true;virtual~LLView()=default;struct Handle{LLView*p=nullptr;LLView*get()const{return p&&p->alive?p:nullptr;}};Handle getHandle(){return{this};}};
template<class T>using LLHandle=LLView::Handle;
struct VSRegionContext{int id=1;LLUUID owner{7};bool manager=true;bool current()const{return true;}LLUUID getOwner()const{return owner;}bool canManageEstate()const{return manager||owner.value==7;}bool operator!=(const VSRegionContext&r)const{return id!=r.id;}};VSRegionContext region;std::optional<VSRegionContext>vs_current_region_context(){return region;}
int accountEpoch=1;std::function<bool()>vs_native_im_guard(){int e=accountEpoch;return[e]{return e==accountEpoch;};}struct Agent{LLUUID getID(){return{7};}bool isGodlike(){return false;}}gAgent;
GUARD
WRAPPER
struct LLButton:LLView{};struct LLFloater:LLView{int dependents=0;std::string getName(){return"requesting-region";}void addDependentFloater(LLFloater*){++dependents;}}parent,child;struct FloaterView{LLFloater*getParentFloater(LLView*){return&parent;}}fv,*gFloaterView=&fv;
struct Placeholder{};Placeholder _1,_2;namespace boost{template<class T>auto bind(void(T::*method)(const uuid_vec_t&),T*owner,Placeholder){return[=](const uuid_vec_t&ids,const std::vector<LLAvatarName>&){(owner->*method)(ids);};}template<class T>auto bind(void(T::*method)(const uuid_vec_t&,const std::vector<LLAvatarName>),T*owner,Placeholder,Placeholder){return[=](const uuid_vec_t&ids,const std::vector<LLAvatarName>names){(owner->*method)(ids,names);};}}
struct LLFloaterAvatarPicker{static inline std::function<void(const uuid_vec_t&,const std::vector<LLAvatarName>)>pending;template<class Callback>static LLFloater*show(Callback callback,bool,bool,bool,const std::string&name,LLView*button){assert(name=="requesting-region"&&button);pending=[callback](const uuid_vec_t&ids,const std::vector<LLAvatarName>names)mutable{callback(ids,names);};return&child;}};
struct LLPanelRegionDebugInfo:LLView{int calls=0;LLButton button;template<class T>T*findChild(const char*){return &button;}void onClickChooseAvatar();void callbackAvatarID(const uuid_vec_t&,const std::vector<LLAvatarName>){++calls;}};
struct LLPanelEstateInfo:LLView{int calls=0;LLButton button;template<class T>T*findChild(const char*){return &button;}void onClickKickUser();void onKickUserCommit(const uuid_vec_t&){++calls;}};
METHODS
int main(){LLPanelRegionDebugInfo debug;LLPanelEstateInfo estate;
for(int which=0;which<2;++which){auto request=[&]{if(which)estate.onClickKickUser();else debug.onClickChooseAvatar();return LLFloaterAvatarPicker::pending;};auto count=[&]{return which?estate.calls:debug.calls;};
auto cb=request();cb({{4}},{{}});assert(count()==1);cb=request();++accountEpoch;cb({{4}},{{}});assert(count()==1);cb=request();region.id++;cb({{4}},{{}});assert(count()==1);cb=request();region.owner={9};region.manager=false;cb({{4}},{{}});assert(count()==1);region.owner={7};region.manager=true;
cb=request();if(which)estate.alive=false;else debug.alive=false;cb({{4}},{{}});assert(count()==1);estate.alive=debug.alive=true;
}
assert(parent.dependents==10);
}
