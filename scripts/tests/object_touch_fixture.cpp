#include <cassert>
#include <array>
#include <cstdint>
#include <initializer_list>
struct LLUUID {int value=0;bool notNull()const{return value!=0;}static const LLUUID null;};
const LLUUID LLUUID::null{};
struct LLVector3 {float x=0;static const LLVector3 zero;};
const LLVector3 LLVector3::zero{};
struct LLVector3d {
 double x=0;LLVector3d()=default;LLVector3d(double v):x(v){};LLVector3d(LLVector3 v):x(v.x){}
 LLVector3d operator+(LLVector3d b)const{return x+b.x;}
};
double dist_vec_squared(LLVector3d a,LLVector3d b){return (a.x-b.x)*(a.x-b.x);}
enum {
 RLV_BHVR_TOUCHALL,RLV_BHVR_TOUCHTHIS,RLV_BHVR_TOUCHWORLD,RLV_BHVR_FARTOUCH,
 RLV_BHVR_TOUCHATTACH,RLV_BHVR_TOUCHATTACHOTHER,RLV_BHVR_TOUCHATTACHSELF,
 RLV_BHVR_TOUCHHUD,RLV_BHVR_TOUCHME,RLV_MODIFIER_FARTOUCHDIST
};
enum class ERlvExceptionCheck {Permissive};
struct RlvHandler {
 unsigned behaviours=0,exceptions=0;bool overrideRoot=false;
 static RlvHandler& instance();
 bool hasBehaviour(int b)const{return behaviours&(1u<<b);}
 bool isException(int b,LLUUID id,ERlvExceptionCheck=ERlvExceptionCheck::Permissive)const {
  // Root object, wearer and distinct picked child exercise independent exceptions.
  unsigned index=0;
  switch(b){
   case RLV_BHVR_TOUCHTHIS:index=0;break;
   case RLV_BHVR_TOUCHWORLD:index=1;break;
   case RLV_BHVR_TOUCHATTACH:index=id.value==2?2:3;break;
   case RLV_BHVR_TOUCHATTACHOTHER:index=4;break;
   case RLV_BHVR_TOUCHHUD:index=5;break;
   default:return false;
  }
  return (exceptions&(1u<<index))!=0;
 }
 bool hasBehaviourRoot(LLUUID id,int)const{return id.value==2&&overrideRoot;}
} handler;
RlvHandler& RlvHandler::instance(){return handler;}
template<class T>struct RlvCachedBehaviourModifier {explicit RlvCachedBehaviourModifier(int){};operator T()const{return T(2);}};
struct Agent {LLVector3d getPositionGlobal()const{return {};}} gAgent;
std::array<unsigned,7> accesses{};
struct LLViewerObject {
 LLUUID id;LLViewerObject* rootEdit=nullptr;LLViewerObject* root=nullptr;
 LLVector3d position;bool attached=false,hud=false,owned=false;
 LLViewerObject* getRootEdit()const{++accesses[0];return rootEdit;}
 LLViewerObject* getRoot()const{++accesses[1];return root;}
 const LLUUID& getID()const{++accesses[2];return id;}
 bool isHUDAttachment()const{++accesses[3];return hud;}
 bool isAttachment()const{++accesses[4];return attached;}
 bool permYouOwner()const{++accesses[5];return owned;}
 LLVector3d getPositionGlobal()const{++accesses[6];return position;}
};
struct VSObjectContext {
 LLUUID rootEditID,rootID;LLVector3d position;bool attachment=false,hud=false,youOwner=false;
};
struct RlvActions {
 static bool hasBehaviour(int b){return handler.hasBehaviour(b);}
 static bool referenceTouch(const LLViewerObject*,const LLVector3& = LLVector3::zero);
 static bool canTouch(const LLViewerObject*,const LLVector3& = LLVector3::zero);
 static bool canTouch(const VSObjectContext&,const LLVector3& = LLVector3::zero);
};
REFERENCE
CURRENT
int main(){
 LLViewerObject edit{{2}},avatar{{3}},child{{1},&edit,&avatar};
 // Include null roots and @touchme overrides; null object must never dereference.
 for(unsigned behaviours=0;behaviours<(1u<<9);++behaviours){
  handler.behaviours=behaviours;
  for(unsigned exceptions=0;exceptions<64;++exceptions){
   handler.exceptions=exceptions;
   for(bool overrideRoot:{false,true}){
    handler.overrideRoot=overrideRoot;
    assert(RlvActions::canTouch(nullptr)==RlvActions::referenceTouch(nullptr));
    for(bool attached:{false,true})for(bool owned:{false,true})for(bool hud:{false,true})
     for(double distance:{0.,1.,2.,3.})for(float offset:{-1.f,0.f,1.f}){
      child.attached=attached;child.owned=owned;child.hud=hud;child.position=distance;
      const LLVector3 picked{offset};
      VSObjectContext native{edit.id,avatar.id,child.position,attached,hud,owned};
      accesses={};
      const bool expected=RlvActions::referenceTouch(&child,picked);
      const auto referenceAccesses=accesses;accesses={};
      assert(RlvActions::canTouch(&child,picked)==expected);
      assert(accesses==referenceAccesses);accesses={};
      assert(RlvActions::canTouch(native,picked)==expected);
      assert((accesses==std::array<unsigned,7>{}));
     }
   }
  }
 }
}
