
#include <cassert>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <cstdint>
#include <cstddef>
std::vector<std::string>trace;
struct LLVolume{};struct LLRiggedVolume{enum{DO_NOT_UPDATE_FACES=0};};
struct LLDrawable{enum{RIGGED=1};bool dead=false,rigged=false;bool isDead(){trace.push_back("dead");return dead;}int getRenderType(){trace.push_back("type");return 0;}bool isState(int){trace.push_back("rigged");return rigged;}};
struct OriginalVolume;
bool selected=true,rendered=true,building=false;
struct LLSelectMgr{static LLSelectMgr&instance(){static LLSelectMgr s;return s;}bool canSelectObject(OriginalVolume*,bool){trace.push_back("selection");return selected;}};
struct Pipeline{bool hasRenderType(int){trace.push_back("render");return rendered;}}gPipeline;
void*gFloaterTools=nullptr;struct LLFloater{static bool isVisible(void*){trace.push_back("build");return building;}};
constexpr int CLICK_ACTION_IGNORE=9;
struct Avatar{bool self=false;bool isSelf(){trace.push_back("self");return self;}};
struct OriginalVolume{bool mbCanSelect=true;LLDrawable drawable;LLDrawable*mDrawable=&drawable;int click=0;Avatar*avatar=nullptr;LLVolume actual;LLVolume*mRiggedVolume=&actual;unsigned rebuilt=0;
int getClickAction(){trace.push_back("click");return click;}LLVolume*getVolume(){return &actual;}Avatar*getAvatar(){trace.push_back("avatar");return avatar;}void updateRiggedVolume(bool,int){++rebuilt;}bool canPick(bool,bool);};
REFERENCE
POLICY
struct Adapter{OriginalVolume&o;bool canSelectFlag()const{return o.mbCanSelect;}bool dead()const{return o.mDrawable->isDead();}bool rendered()const{return gPipeline.hasRenderType(o.mDrawable->getRenderType());}bool canSelect()const{return LLSelectMgr::instance().canSelectObject(&o,true);}bool clickIgnore()const{return o.getClickAction()==CLICK_ACTION_IGNORE;}bool rigged()const{return o.mDrawable->isState(LLDrawable::RIGGED);}bool hasAvatar()const{return o.getAvatar()!=nullptr;}bool selfAvatar()const{return o.getAvatar()->isSelf();}};
int main(){Avatar avatar;for(unsigned flags=0;flags<4096;flags++){OriginalVolume o;o.mbCanSelect=flags&1;o.drawable.dead=flags&2;rendered=flags&4;selected=flags&8;o.click=flags&16?CLICK_ACTION_IGNORE:0;building=flags&32;o.drawable.rigged=flags&64;o.avatar=flags&128?&avatar:nullptr;avatar.self=flags&256;const bool rigged=flags&512,unselectable=flags&1024;
trace.clear();const bool expected=o.canPick(rigged,unselectable);const auto calls=trace;trace.clear();const bool actual=VSPickPolicy::volume(Adapter{o},rigged,unselectable,[]{return LLFloater::isVisible(gFloaterTools);});assert(expected==actual&&calls==trace);
}}
