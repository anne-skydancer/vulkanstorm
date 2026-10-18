struct Calls{int valid=0,height=0,pelvis=0,air=0,position=0;}calls;
bool valid=true;
struct Avatar{bool mInAir=false;float getPelvisToFoot(){++calls.pelvis;return .8f;}LLVector3d getPositionGlobal(){++calls.position;return {4,8,3};}}avatar,*gAgentAvatarp=&avatar;
bool isAgentAvatarValid(){++calls.valid;return valid;}
struct LLViewerObject{LLVector3d getPositionGlobal()const{return {8,12,3};}};
struct ObjectList{LLViewerObject object;LLViewerObject*findObject(LLUUID id){return id.isNull()?nullptr:&object;}}gObjectList;
struct LLWorld{static auto*getInstance(){static LLWorld w;return &w;}float resolveStepHeightGlobal(void*,const LLVector3d&a,const LLVector3d&b,LLVector3d&intersection,LLVector3&,LLViewerObject**){++calls.height;intersection=a;intersection.mdV[2]=1;return float(a.mdV[2]-1)/float(dist_vec(a,b));}};
struct LLNotificationsUtil{static void add(const char*){}};
float gFPSClamped=30;
struct Frame{LLVector3 getAtAxis()const{return {1,0,0};}};
struct LLAgent{
 bool mAutoPilot=false,mAutoPilotFlyOnStop=false,mAutoPilotAllowFlying=true,mAutoPilotUseRotation=false,mNativeAutoPilotRetiring=false,flying=false;
 U64 mNativeAutoPilotIntent=0;std::function<bool()>mNativeAutoPilotAccount;
 LLVector3d mAutoPilotTargetGlobal,position;float mAutoPilotStopDistance=1,mAutoPilotTargetDist=0,mAutoPilotRotationThreshold=0;
 LLVector3 mAutoPilotTargetFacing;int mAutoPilotNoProgressFrameCount=0;std::string mAutoPilotBehaviorName;
 void(*mAutoPilotFinishedCallback)(bool,void*)=nullptr;void*mAutoPilotCallbackData=nullptr;LLUUID mLeaderID;Frame mFrameAgent;U32 controls=0;
 LLVector3d getPositionGlobal(){return position;}LLVector3 getPositionAgent(){return LLVector3(position);}LLVector3 getPosAgentFromGlobal(LLVector3d p){return LLVector3(p);}
 bool getFlying(){return flying;}void setFlying(bool v){flying=v;}void setControlFlags(U32 f){controls|=f;}void clearControlFlags(U32 f){controls&=~f;}void resetAxes(LLVector3){}
 void startAutoPilotGlobal(const LLVector3d&,const std::string&,const LLQuaternion*,void(*)(bool,void*),void*,F32,F32,bool);
 void setAutoPilotTargetGlobal(const LLVector3d&);void startFollowPilot(const LLUUID&,bool,F32);void autoPilot(F32*);void stopAutoPilot(bool=false);void retireNativeAutoPilot();
}gAgent;
