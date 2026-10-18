#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <random>
#include <cassert>
#include <iostream>
using F32=float;
#define LL_INFOS() std::cerr
#define LL_ENDL std::endl
constexpr int VOICE_GESTICULATION_LEVEL_OFF=-1;
struct LLVector3{float x=0,y=0,z=0;bool operator==(const LLVector3&)const=default;};
struct Settings{std::map<std::string,bool>values;}gSavedSettings;
template<class T>struct LLCachedControl{std::string name;LLCachedControl(Settings&,const char*n):name(n){}operator T()const{return gSavedSettings.values[name];}};
struct Camera{bool first=false;bool cameraMouselook(){return first;}}gAgentCamera;
struct Agent{int clear=0;void clearAFK(){++clear;}}gAgent;
struct LLGestureMgr{std::vector<std::string>events;static LLGestureMgr&instance(){static LLGestureMgr v;return v;}void triggerAndReviseString(std::string&s){events.push_back(s);}};
struct LLVoiceClient{bool speaking=false;float power=.5;static LLVoiceClient*getInstance(){static LLVoiceClient v;return&v;}bool getIsSpeaking(int){return speaking;}float getCurrentPower(int){return power;}};
struct LLVoiceVisualizer{bool enabled=false,speaking=false;float power=0;int level=0,starts=0,stops=0;LLVector3 position;inline static bool lip=true;void setVoiceEnabled(bool b){enabled=b;}int getCurrentGesticulationLevel(){return level;}bool getCurrentlySpeaking(){return speaking;}void setStartSpeaking(){speaking=true;++starts;}void setStopSpeaking(){speaking=false;power=0;++stops;}void setSpeakingAmplitude(float f){power=f;}void setPositionAgent(const LLVector3&p){position=p;}void lipSyncOohAah(float&o,float&a){o=.3f;a=.7f;}static bool getLipSyncEnabled(){return lip;}};
struct Morph{float value=0;float getMinWeight(){return .1f;}float getMaxWeight(){return .9f;}void setWeight(float f,bool){value=f;}};
struct LLCharacter{int updates=0;void updateVisualParams(){++updates;}};
struct Avatar:LLCharacter{int mID=31;bool self=false,muted=false,mLipSyncActive=false;int mCurrentGesticulationLevel=-1,mLastRezzedStatus=4,dirty=0;LLVoiceVisualizer visualizer;LLVoiceVisualizer*mVoiceVisualizer=&visualizer;Morph ooh,aah;Morph*mOohMorph=&ooh,*mAahMorph=&aah;bool isSelf(){return self;}bool isInMuteList(){return muted;}int getID(){return mID;}void dirtyMesh(){++dirty;}void frozenUpdate(bool,const LLVector3&);void frozenLip(bool);auto state()const{return std::tuple(self,muted,mLipSyncActive,mCurrentGesticulationLevel,mLastRezzedStatus,dirty,updates,ooh.value,aah.value,visualizer.enabled,visualizer.speaking,visualizer.power,visualizer.level,visualizer.starts,visualizer.stops,visualizer.position);}};
FROZEN_UPDATE
FROZEN_LIP
POLICY
int main(){std::mt19937 rng(311);for(int i=0;i<10000;++i){Avatar old,current;old.self=current.self=rng()%2;old.muted=current.muted=rng()%2;old.mLipSyncActive=current.mLipSyncActive=rng()%2;old.visualizer.speaking=current.visualizer.speaking=rng()%2;old.visualizer.level=current.visualizer.level=int(rng()%4)-1;old.mCurrentGesticulationLevel=current.mCurrentGesticulationLevel=int(rng()%4)-1;old.mLastRezzedStatus=current.mLastRezzedStatus=int(rng()%5);gAgentCamera.first=rng()%2;gSavedSettings.values["VoiceDisableMic"]=rng()%2;gSavedSettings.values["FSShowMyOwnVoiceVisualizer"]=rng()%2;gSavedSettings.values["VoiceVisualizerEnabled"]=rng()%2;LLVoiceVisualizer::lip=rng()%2;LLVoiceClient::getInstance()->speaking=rng()%2;LLVoiceClient::getInstance()->power=float(rng()%100)*.01f;bool enabled=rng()%2;LLVector3 position{1,2,3};gAgent.clear=0;LLGestureMgr::instance().events.clear();old.frozenUpdate(enabled,position);old.frozenLip(enabled);auto events=LLGestureMgr::instance().events;auto clear=gAgent.clear;LLGestureMgr::instance().events.clear();gAgent.clear=0;VSAvatarVoicePolicy::update(current,enabled,position);VSAvatarVoicePolicy::lipSync(current,enabled);assert(old.state()==current.state());assert(events==LLGestureMgr::instance().events&&clear==gAgent.clear);}}
