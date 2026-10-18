#include <vector>
#include <cassert>
#include <string>
#include <cstddef>
using LLUUID=int;using LLQuaternion=int;
constexpr int _PREHASH_SitObject=1,_PREHASH_ID=2,_PREHASH_SitTransform=3,_PREHASH_AutoPilot=4,_PREHASH_SitPosition=5,_PREHASH_SitRotation=6,_PREHASH_CameraEyeOffset=7,_PREHASH_CameraAtOffset=8,_PREHASH_ForceMouselook=9;constexpr float CAMERA_POSITION_THRESHOLD_SQUARED=.0001f;
std::vector<int>events;bool valid=true,exists=true,autopilot=true,already=false,sitting=true,forced=false;int cameraDelta=1;
struct LLVector3{int value=0;LLVector3 operator+(LLVector3 b)const{return {value+b.value};}LLVector3 operator*(LLQuaternion q)const{return {value*q};}};int dist_vec_squared(LLVector3 a,LLVector3 b){int d=a.value-b.value;return d*d;}
struct LLViewerObject{LLVector3 getPositionAgent(){events.push_back(20);return {50};}int getRotation(){events.push_back(21);return 3;}LLViewerObject*getRoot(){events.push_back(22);return this;}}object,other;
struct Avatar{bool isSitting(){events.push_back(23);return sitting;}LLViewerObject*getRoot(){events.push_back(24);return already?&object:&other;}}avatar;Avatar*gAgentAvatarp=&avatar;bool isAgentAvatarValid(){events.push_back(25);return valid;}
struct Objects{LLViewerObject*findObject(int id){events.push_back(26);assert(id==777);return exists?&object:nullptr;}}gObjectList;
void near_sit_object(bool,void*){}
struct Agent{void setFlying(bool fly){events.push_back(30+fly);}LLVector3 getPosGlobalFromAgent(LLVector3 p){events.push_back(31);return {p.value+100};}void startAutoPilotGlobal(LLVector3 p,const char* behavior,const int* rotation,void(*)(bool,void*),void*,float stop){events.push_back(32);events.push_back(p.value);events.push_back(*rotation);assert(std::string(behavior)=="Sit"&&stop==.5f);}}gAgent;
struct Camera{void setSitCamera(int id,LLVector3 eye,LLVector3 focus){events.push_back(40);events.push_back(id);events.push_back(eye.value);events.push_back(focus.value);}void setForceMouselook(bool force){events.push_back(50+force);}}gAgentCamera;
struct LLMessageSystem{void getUUIDFast(int,int,int&out){events.push_back(1);out=777;}void getBOOLFast(int,int field,bool&out){events.push_back(field);out=field==_PREHASH_AutoPilot?autopilot:forced;}void getVector3Fast(int,int field,LLVector3&out){events.push_back(field);out.value=field==_PREHASH_SitPosition?2:field==_PREHASH_CameraEyeOffset?8:8+cameraDelta;}void getQuatFast(int,int,int&out){events.push_back(6);out=9;}};
struct Sink{template<class T>Sink&operator<<(T const&){return*this;}};Sink sink;
#define LL_WARNS(x) sink
#define LL_ENDL 0
PEER
SHARED
REFERENCE
struct Retired:ReferenceSitResponse{bool current()const{return false;}};
int main(){LLMessageSystem message;for(int bits=0;bits<128;++bits){valid=bits&1;exists=bits&2;autopilot=bits&4;already=bits&8;sitting=bits&16;forced=bits&32;cameraDelta=(bits&64)?1:0;events.clear();referenceResponse(&message,nullptr);auto expected=events;events.clear();sharedAvatarSitResponse(&message,ReferenceSitResponse{});assert(events==expected);events.clear();sharedAvatarSitResponse(&message,Retired{});for(int event:events)assert(event<20);}}
