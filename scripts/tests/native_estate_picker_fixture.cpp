// Compile the actual estate picker continuation and real lifetime wrappers.
#include <cassert>
#include <memory>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <type_traits>
using U32=unsigned;using S32=int;using LLUUID=int;using uuid_vec_t=std::vector<int>;
namespace boost{using std::bind;}using namespace std::placeholders;
struct LLSD {int operation=1;const LLSD&operator[](const char*)const{return*this;}};
struct LLAvatarName{};
struct LLView{bool alive=true;virtual~LLView()=default;};struct LLButton:LLView{};
struct LLFloater:LLView{int dependents=0;std::string getName(){return"region_info";}void addDependentFloater(LLFloater*){++dependents;}};
struct FloaterView{LLFloater parent;LLFloater*getParentFloater(LLView*){return&parent;}}view;FloaterView*gFloaterView=&view;
unsigned account=1,region=1;bool manager=true;
std::function<bool()>vs_native_im_guard(){auto old=account;return[old]{return old==account;};}
std::function<bool()>region_info_guard(LLView*view,bool,bool){auto old=region;return[view,old]{return view->alive&&old==region&&manager;};}
WRAPPERS
struct LLEstateAccessChangeInfo{static int live;U32 mOperationFlag;LLEstateAccessChangeInfo(const LLSD&v):mOperationFlag(v.operation){++live;}~LLEstateAccessChangeInfo(){--live;}};int LLEstateAccessChangeInfo::live=0;
constexpr int ESTATE_ACCESS_ALLOWED_AGENT_ADD=1,ESTATE_ACCESS_BANNED_AGENT_ADD=2,ESTATE_ACCESS_MANAGER_ADD=4;
struct LLNotificationsUtil{static int getSelectedOption(const LLSD&,const LLSD&v){return v.operation;}};
struct LLFloaterAvatarPicker{static std::function<void(const uuid_vec_t&,std::vector<LLAvatarName>)>callback;static LLFloater picker;template<class F>static LLFloater*show(F f,bool,bool,bool,const std::string&,LLView*){callback=f;return&picker;}};
std::function<void(const uuid_vec_t&,std::vector<LLAvatarName>)>LLFloaterAvatarPicker::callback;LLFloater LLFloaterAvatarPicker::picker;
struct LLPanelEstateAccess:LLView{int selections=0;LLButton button;template<class T>T*findChild(const char*){return&button;}bool accessAddCore2(const LLSD&,const LLSD&);void accessAddCore3(const uuid_vec_t&,std::vector<LLAvatarName>,std::shared_ptr<LLEstateAccessChangeInfo>){++selections;}};
PICKER
int main(){LLPanelEstateAccess owner;LLSD notification,cancel{1},accept{0};
owner.accessAddCore2(notification,cancel);assert(!LLFloaterAvatarPicker::callback&&LLEstateAccessChangeInfo::live==0);
owner.accessAddCore2(notification,accept);assert(LLEstateAccessChangeInfo::live==1&&view.parent.dependents==1);LLFloaterAvatarPicker::callback({7},{});assert(owner.selections==1);LLFloaterAvatarPicker::callback={};assert(LLEstateAccessChangeInfo::live==0);
for(int failure=0;failure<4;++failure){owner.alive=true;manager=true;owner.accessAddCore2(notification,accept);assert(LLEstateAccessChangeInfo::live==1);if(failure==0)++account;if(failure==1)++region;if(failure==2)owner.alive=false;if(failure==3)manager=false;LLFloaterAvatarPicker::callback({7},{});assert(owner.selections==1);LLFloaterAvatarPicker::callback={};assert(LLEstateAccessChangeInfo::live==0);}
owner.alive=true;manager=true;owner.accessAddCore2(notification,accept);assert(LLEstateAccessChangeInfo::live==1);LLFloaterAvatarPicker::callback={};assert(LLEstateAccessChangeInfo::live==0); // picker canceled without ever invoking callback
}
