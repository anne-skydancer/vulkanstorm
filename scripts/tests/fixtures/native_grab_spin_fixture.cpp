#include <vector>
#include <tuple>
#include <cassert>
using LLUUID=int;using LLQuaternion=int;
enum{GRAB_ACTIVE_CENTER,GRAB_NONPHYSICAL,GRAB_LOCKED,GRAB_NOOBJECT,GRAB_INACTIVE};
constexpr int _PREHASH_ObjectSpinStart=10,_PREHASH_ObjectSpinStop=20,_PREHASH_AgentData=30,_PREHASH_AgentID=40,_PREHASH_SessionID=50,_PREHASH_ObjectData=60,_PREHASH_ObjectID=70;
struct Message{std::vector<std::tuple<int,int,int>>entries;void newMessageFast(int x){entries.emplace_back(0,x,0);}void nextBlockFast(int x){entries.emplace_back(1,x,0);}void addUUIDFast(int x,int y){entries.emplace_back(2,x,y);}void sendMessage(int h){entries.emplace_back(3,h,0);}}message;using LLMessageSystem=Message;Message*gMessageSystem=&message;
struct Agent{int getID(){return 101;}int getSessionID(){return 202;}}gAgent;
struct Region{int getHost(){return 303;}};
struct LLViewerObject{Region region;int id=404,rotation=505;LLViewerObject*getRoot(){return this;}int getRotation(){return rotation;}int getID(){return id;}Region*getRegion(){return &region;}}source;
struct Pick{int mObjectID=404;LLViewerObject*getObject(){return &source;}};
struct Peer{bool current=true;explicit operator bool()const{return current;}int rootRotation()const{return source.rotation;}int id()const{return source.id;}int host()const{return source.region.getHost();}};
bool alive=true;Peer grabObject(const Pick&,int){return {alive};}
struct Reference{Pick mGrabPick;bool mSpinGrabbing=false;int mSpinRotation=0,mMode=GRAB_INACTIVE;void startSpin();void stopSpin();};
struct Native{Pick mGrabPick;int mNativeGrab=0;bool mSpinGrabbing=false;int mSpinRotation=0,mMode=GRAB_INACTIVE;void startSpin();void stopSpin();};
REFERENCE
NATIVE
int main(){for(int mode=-1;mode<=6;++mode){Reference r;Native n;r.mMode=n.mMode=mode;message.entries.clear();r.startSpin();r.stopSpin();auto expected=message.entries;message.entries.clear();n.startSpin();n.stopSpin();assert(message.entries==expected);assert(r.mSpinGrabbing==n.mSpinGrabbing);assert(r.mSpinRotation==n.mSpinRotation);}
alive=false;Native n;n.mMode=GRAB_ACTIVE_CENTER;message.entries.clear();n.startSpin();n.stopSpin();assert(message.entries.empty()&&!n.mSpinGrabbing);}
