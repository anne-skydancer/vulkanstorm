"""Actual shared original/native sound state-machine differential regressions."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(os.environ.get('VS_SOUND_TEST_ROOT',Path(__file__).resolve().parents[2]))
def body(source,name):
 start=source.index(name);end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
class NativeSoundTests(unittest.TestCase):
 def compile_run(self,source):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as d:
   cpp=Path(d)/'sound.cpp';exe=Path(d)/'sound.exe';cpp.write_text(source)
   result=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_attached_controller_matches_frozen_original_engine_event_order(self):
  shared=(ROOT/'indra/newview/vsobjectsoundcontroller.h').read_text();shared=shared[shared.index('template<class Source,'):]
  original=(Path(__file__).with_name('native_attached_sound_reference.cpp')).read_text()
  self.compile_run(r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>
using U8=unsigned char;using F32=float;
struct LLUUID{int id=0;bool isNull()const{return id==0;}bool notNull()const{return id!=0;}bool operator==(const LLUUID&)const=default;};
struct LLVector3d{};std::vector<std::string> events;
constexpr U8 LL_SOUND_FLAG_LOOP=1,LL_SOUND_FLAG_SYNC_MASTER=2,LL_SOUND_FLAG_SYNC_SLAVE=4,LL_SOUND_FLAG_SYNC_PENDING=8,LL_SOUND_FLAG_QUEUE=16,LL_SOUND_FLAG_STOP=32;
namespace LLAssetType{constexpr int AT_SOUND=1;}
struct FSAssetBlacklist{enum eBlacklistFlag{NONE=0,WORN=1,REZZED=2};static unsigned mask;static FSAssetBlacklist&instance(){static FSAssetBlacklist b;return b;}
 bool isBlacklisted(LLUUID id,int,eBlacklistFlag f=NONE){return (id.id==2&&(mask&1))||(id.id==9&&f==WORN&&(mask&2))||(id.id==9&&f==REZZED&&(mask&4));}};unsigned FSAssetBlacklist::mask=0;
struct Agent{bool mature=true;bool canAccessMaturityAtGlobal(LLVector3d){events.push_back("maturity");return mature;}}gAgent;
struct Data{LLUUID id;LLUUID getID(){return id;}};
struct Source{bool loop=false,pending=false,done=false,muted=false,hasData=false,queue=false,master=false,slave=false;F32 gain=0;Data data{{2}};
 bool isLoop(){return loop;}bool hasPendingPreloads(){return pending;}bool isDone(){return done;}bool isMuted(){return muted;}Data*getCurrentData(){return hasData?&data:nullptr;}
 void stop(){events.push_back("stop");}void setGain(float g){events.push_back("gain");gain=std::clamp(g,0.f,1.f);}void setLoop(bool x){events.push_back("loop");loop=x;}
 void setSyncMaster(bool x){events.push_back("master");master=x;}void setSyncSlave(bool x){events.push_back("slave");slave=x;}void setQueueSounds(bool x){events.push_back("queue");queue=x;}
 void checkCutOffRadius(){events.push_back("cutoff");}void play(LLUUID id){events.push_back("play"+std::to_string(id.id));}};
struct Engine{void cleanupAudioSource(Source*p){events.push_back("cleanup");delete p;}}engine;Engine*gAudiop=&engine;
struct ReferenceObject{Source*mAudioSourcep=nullptr;F32 mAudioGain=0;bool attachment=false;bool isAttachment(){events.push_back("attachment");return attachment;}
 LLVector3d getPositionGlobal(){events.push_back("position");return{};}void getAudioSource(LLUUID){events.push_back("ensure");if(!mAudioSourcep)mAudioSourcep=new Source;}
 void setAttachedSound(const LLUUID&,const LLUUID&,F32,U8);};
'''+original+'\n'+shared+r'''
std::string fingerprint(Source*s,float gain){std::ostringstream out;out<<(s!=nullptr)<<":"<<gain;if(s)out<<":"<<s->loop<<":"<<s->master<<":"<<s->slave<<":"<<s->queue<<":"<<s->gain;for(auto&e:events)out<<"/"<<e;return out.str();}
Source* initial(unsigned bits){if(!(bits&1))return nullptr;auto*s=new Source;s->loop=bits&2;s->pending=bits&4;s->done=bits&8;s->muted=bits&16;s->hasData=bits&32;return s;}
int main(){for(unsigned bits=0;bits<256;++bits)for(unsigned flags=0;flags<64;++flags)for(int asset:{0,2,3})for(float gain:{-1.f,0.f,.5f,2.f}){
 FSAssetBlacklist::mask=(bits>>5)&7;gAgent.mature=!(bits&128);
 ReferenceObject original;original.attachment=bits&64;original.mAudioSourcep=initial(bits);events.clear();original.setAttachedSound({asset},{9},gain,U8(flags));const auto expected=fingerprint(original.mAudioSourcep,original.mAudioGain);delete original.mAudioSourcep;
 Source*actual=initial(bits);float savedGain=0;events.clear();vs_set_attached_sound(actual,savedGain,LLUUID{asset},LLUUID{9},gain,U8(flags),[&]{events.push_back("attachment");return bool(bits&64);},[]{events.push_back("position");return LLVector3d{};},[&](LLUUID){events.push_back("ensure");if(!actual)actual=new Source;});
 assert(expected==fingerprint(actual,savedGain));delete actual;}
 // No engine must not evaluate model getters or allocate a source.
 gAudiop=nullptr;events.clear();Source*s=nullptr;float gain=0;vs_set_attached_sound(s,gain,LLUUID{2},LLUUID{9},1,0,[]{assert(false);return false;},[]{assert(false);return LLVector3d{};},[](LLUUID){assert(false);});assert(events.empty()&&!s);}
''')
 def test_source_update_native_and_reference_peers_match_frozen_policy(self):
  source=(ROOT/'indra/newview/llaudiosourcevo.cpp').read_text()
  peers=source[source.index('namespace\n{'):source.index('\nLLAudioSourceVO::LLAudioSourceVO(')]
  methods=['template<class Peer>\nvoid LLAudioSourceVO::checkCutOffRadiusFor(', 'template<class Peer>\nLLVector3d LLAudioSourceVO::getPosGlobalFor(', 'bool LLAudioSourceVO::isInCutOffRadius(', 'template<class Peer>\nvoid LLAudioSourceVO::updateMuteFor(', 'template<class Peer>\nvoid LLAudioSourceVO::updateFor(', 'void LLAudioSourceVO::update()', 'void LLAudioSourceVO::checkCutOffRadius()']
  policies='\n'.join(body(source,m).replace('LLAudioSourceVO::','NativeAudio::')for m in methods)
  fixture=Path(__file__).with_name('native_audio_source_fixture.cpp').read_text()
  reference=Path(__file__).with_name('native_audio_source_reference.cpp').read_text()
  main=r'''
int main(){for(unsigned bits=0;bits<512;++bits)for(float radius:{0.f,.099999f,.1f,.100001f,1.f,2.f})for(int ear:{0,1,2}){
 LLViewerObject object,avatar,ride,agent;
 object.dead=bits&1;object.attachment=bits&2;object.hud=bits&4;object.cutoff=radius;object.position={1,0,0};object.velocity={1,2,3};
 avatar.id={3};avatar.avatar=true;avatar.position={2,0,0};avatar.velocity={4,5,6};ride.id={4};ride.position={9,9,9};ride.velocity={7,8,9};agent.id={5};
 if(bits&8)object.parent=object.attachment?&avatar:&ride;
 if(bits&16)agent.parent=(bits&32)?object.parent:&ride;
 gAgentAvatarp=(bits&64)?nullptr:&agent;gAgent.position={0,0,0};gAgentCamera.position={1,0,0};gSavedSettings.ear=ear;
 LLViewerParcelMgr::getInstance()->hear=!(bits&128);LLMuteList::getInstance()->mask=(bits>>1)&7;
 VSAudioObjectState state;state.id=object.id;state.dead=object.dead;state.attachment=object.attachment;state.hud=object.hud;state.cutoff=object.cutoff;state.position=object.position;state.velocity=object.parent?object.parent->velocity:object.velocity;
 state.root=object.getRoot()->id;state.agentRiding=isAgentAvatarValid()&&agent.parent;if(state.agentRiding)state.agentRoot=agent.getRoot()->id;
 LLViewerObject*parent=&object;while(parent&&!parent->isAvatar())parent=parent->getParent();if(parent){state.avatarRoot=parent->id;state.avatarRootPosition=parent->position;}
 OriginalAudio expected;expected.mObjectp=&object;expected.mSourceMuted=bits&256;if(bits&32)expected.mCurrentDatap=&expected.data;
 NativeAudio legacy;legacy.mObjectp=&object;legacy.mSourceMuted=expected.mSourceMuted;if(bits&32)legacy.mCurrentDatap=&legacy.data;
 NativeAudio native;native.mNativeObject=[state]{return std::optional<VSAudioObjectState>(state);};native.mSourceMuted=expected.mSourceMuted;if(bits&32)native.mCurrentDatap=&native.data;
 eventLog.clear();expected.update();const auto expectedEvents=eventLog;
 eventLog.clear();legacy.update();assert(eventLog==expectedEvents);assert(legacy.mSourceMuted==expected.mSourceMuted);assert(legacy.mPositionGlobal==expected.mPositionGlobal);assert(legacy.mVelocity==expected.mVelocity);
 eventLog.clear();native.update();assert(eventLog==expectedEvents);assert(native.mSourceMuted==expected.mSourceMuted);assert(native.mPositionGlobal==expected.mPositionGlobal);assert(native.mVelocity==expected.mVelocity);
 // Explicit cutoff check has a distinct 0.1 boundary; its current mute bit is
 // reset to isolate that actual original method rather than update's result.
 expected.mObjectp=&object;expected.mSourceMuted=false;legacy.mObjectp=&object;legacy.mSourceMuted=false;native.mSourceMuted=false;
 expected.checkCutOffRadius();legacy.checkCutOffRadius();native.checkCutOffRadius();assert(legacy.mSourceMuted==expected.mSourceMuted);assert(native.mSourceMuted==expected.mSourceMuted);
 }
 NativeAudio retired;retired.mNativeObject=[]{return std::optional<VSAudioObjectState>{};};retired.update();assert(retired.mSourceMuted);}
'''
  self.compile_run(fixture+'\n'+reference+'\n'+peers+'\n'+policies+'\n'+main)

 def test_derender_retires_actual_descendant_identity_family(self):
  method=body((ROOT/'indra/newview/vsworldscene.cpp').read_text(),'bool VSWorldScene::derender(')
  self.compile_run(r'''
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <algorithm>
using U32=unsigned;using U64=unsigned long long;
struct VSWorldScene {
 struct ObjectHandle { int host;U32 local;U64 generation; };
 struct Object {U32 parent=0;U64 generation=1,parentGeneration=0;bool parentRetired=false;};
 struct Index { std::multimap<U32,U32> edges;auto children(U32 id)const{return edges.equal_range(id);} };
 struct Impl { int host=1;std::map<U32,Object>objects;Index objectIndex;std::vector<U32>retired;
  void eraseObject(U32 id){retired.push_back(id);objects.erase(id);} };
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;
 bool current(const ObjectHandle&t){if(t.host!=mImpl->host){auto n=mNeighbours.find(t.host);return n!=mNeighbours.end()&&n->second->current(t);}auto i=mImpl->objects.find(t.local);return i!=mImpl->objects.end()&&i->second.generation==t.generation;}
 bool derender(const ObjectHandle&);
};
'''+method+r'''
int main(){VSWorldScene scene;auto&s=*scene.mImpl;
 // avatar root, attachment, attachment child, stale/recycled links and a cycle
 s.objects={{1,{0,10}},{2,{1,20,10}},{3,{2,30,20}},{4,{1,40,9}},{5,{2,50,20,true}},{6,{0,60}}};
 s.objectIndex.edges={{1,2},{2,3},{1,4},{2,5},{3,1}};
 assert(!scene.derender({1,1,9}));assert(s.retired.empty());
 assert(scene.derender({1,1,10}));assert((s.retired==std::vector<U32>{3,2,1}));
 assert(s.objects.count(4)&&s.objects.count(5)&&s.objects.count(6));
 assert(!scene.derender({1,1,10}));
 auto n=std::make_unique<VSWorldScene>();n->mImpl->host=2;n->mImpl->objects[7]={0,70};auto*neighbour=n.get();scene.mNeighbours[2]=std::move(n);
 assert(scene.derender({2,7,70}));assert(neighbour->mImpl->objects.empty());
 assert(!scene.derender({3,7,70}));}
''')
 def test_engine_incarnation_and_queued_account_retirement(self):
  source=(ROOT/'indra/newview/vsworldsound.cpp').read_text()
  live=body(source,'    static bool live(')
  tick=body(source,'void VSWorldSound::tick()')
  self.compile_run(r'''
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <memory>
using LLUUID=int;
struct AudioSource {int log=1;int getLogID()const{return log;}};
struct Engine{AudioSource*value=nullptr;AudioSource*findAudioSource(int){return value;}}engine;Engine*gAudiop=&engine;
double now=0;struct LLFrameTimer{static double getTotalSeconds(){return now;}};
struct Handle{int id=0;bool current=true;};
struct Scene{std::vector<int>events;std::map<int,Handle>objects;const Handle*object(int id){auto i=objects.find(id);return i==objects.end()?nullptr:&i->second;}bool current(Handle h){return h.current&&objects.count(h.id);}void derender(Handle h){events.push_back(h.id);objects.erase(h.id);}};
struct VSWorldSound{
 struct Impl{struct Source{Handle object;AudioSource*source=nullptr;int logID=0;};
  struct Region{bool valid=true;bool current()const{return valid;}};
  struct Pending{Region region;double deadline=0;};
  Scene*value=nullptr;bool active=true;std::set<int>pendingBlacklistRetirements;std::map<int,Source>sources;std::map<int,Pending>postponed;
  Scene*scene(){return active?value:nullptr;}int cleaned=0;void cleanup(Source&){++cleaned;}
'''+live+r'''
 };std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool retired=false;void reset(){retired=true;mImpl->postponed.clear();mImpl->sources.clear();mImpl->pendingBlacklistRetirements.clear();}void tick();};
'''+tick+r'''
int main(){using Impl=VSWorldSound::Impl;AudioSource source;Impl::Source record{{7},&source,1};engine.value=&source;
 assert(Impl::live(record));source.log=2;assert(!Impl::live(record)); // same pointer, different engine incarnation
 source.log=1;engine.value=nullptr;assert(!Impl::live(record));record.source=nullptr;assert(!Impl::live(record));
 VSWorldSound service;Scene scene;service.mImpl->value=&scene;scene.objects[7]={7};
 service.mImpl->pendingBlacklistRetirements.insert(7);
 // Blacklist publication did not invalidate the selection before ObjectDeselect.
 assert(scene.objects.count(7));scene.events.push_back(-7);
 service.mImpl->sources[7]={{7},nullptr,0};
 service.mImpl->postponed[8]={{true},15};service.mImpl->postponed[9]={{false},15};
 now=15;service.tick();assert((scene.events==std::vector<int>{-7,7}));assert(service.mImpl->sources.empty());assert(service.mImpl->cleaned==1);
 assert(service.mImpl->postponed.count(8)&&!service.mImpl->postponed.count(9));
 now=15.001;service.tick();assert(service.mImpl->postponed.empty());
 service.mImpl->active=false;service.mImpl->pendingBlacklistRetirements.insert(1);service.tick();assert(service.retired&&service.mImpl->pendingBlacklistRetirements.empty());}
''')
 def test_spatial_monitor_rebinds_real_account_model_and_radar_action_uses_actual_widget(self):
  monitor=body((ROOT/'indra/newview/lloutputmonitorctrl.cpp').read_text(),'void NearbyVoiceMonitor::draw()')
  action=body((ROOT/'indra/newview/fspanelradar.cpp').read_text(),'void FSPanelRadar::onOptionsMenuItemClicked(')
  self.compile_run(r'''
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <vector>
#include <string>
#include <memory>
using S32=int;struct LLUUID{};
struct VSNativeSession{enum class Phase{Connecting,Connected,Disconnected};Phase state=Phase::Connected;static std::shared_ptr<VSNativeSession>owner;static auto active(){return owner;}Phase phase()const{return state;}};std::shared_ptr<VSNativeSession>VSNativeSession::owner=std::make_shared<VSNativeSession>();
struct Speaker{float mSpeechVolume=0;int mStatus=0;LLUUID mID;static constexpr int STATUS_SPEAKING=1;};using LLSpeaker=Speaker;
struct LLSpeakerMgr{using speaker_list_t=std::vector<std::shared_ptr<Speaker>>;bool poisoned=false;int updates=0;void update(bool){assert(!poisoned);++updates;}void getSpeakerList(speaker_list_t*,bool){assert(!poisoned);}};
struct LLLocalSpeakerMgr:LLSpeakerMgr{static LLLocalSpeakerMgr*instance;static auto*getInstance(){return instance;}};LLLocalSpeakerMgr*LLLocalSpeakerMgr::instance=nullptr;
struct LLOutputMonitorCtrl{int draws=0;void draw(){++draws;}};
struct NearbyVoiceMonitor:LLOutputMonitorCtrl{LLSpeakerMgr*mSpeakerMgr=nullptr;bool mAutoHide=false;void setSpeakerId(LLUUID){}void draw();};
struct LLSD{std::string text;std::string asString()const{return text;}};
struct LLUICtrl{std::string name;std::string getName()const{return name;}};
int nativeOpen=0,legacyOpen=0;struct FSPanelBlockList{static void showPanelAndSelect(){++nativeOpen;}};struct LLPanelBlockedList{static void showPanelAndSelect(){++legacyOpen;}};
struct FSPanelRadar{void onOptionsMenuItemClicked(LLUICtrl*,const LLSD&);};
'''+monitor+'\n'+action+r'''
int main(){LLLocalSpeakerMgr retired,current;retired.poisoned=true;LLLocalSpeakerMgr::instance=&current;NearbyVoiceMonitor view;view.mSpeakerMgr=&retired;view.draw();assert(current.updates==1&&view.draws==1);
 VSNativeSession::owner->state=VSNativeSession::Phase::Disconnected;view.draw();assert(current.updates==1);
 FSPanelRadar radar;LLUICtrl item{"panel_block_list_sidetray"};radar.onOptionsMenuItemClicked(&item,{});assert(nativeOpen==1);
 VSNativeSession::owner.reset();radar.onOptionsMenuItemClicked(nullptr,{"panel_block_list_sidetray"});assert(legacyOpen==1);
 item.name="not_a_blocklist";radar.onOptionsMenuItemClicked(&item,{});assert(legacyOpen==1);}
''')
 def test_avatar_blacklist_uses_original_stop_and_source_skip_policy(self):
  source=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
  function=body(source,'bool VSWorldAvatar::animations(LLMessageSystem& msg)')
  self.compile_run(r'''
#include <cassert>
#include <map>
#include <vector>
#include <memory>
#include <string>
using S32=int;
struct LLUUID{int n=0;bool notNull()const{return n!=0;}friend auto operator<=>(const LLUUID&,const LLUUID&)=default;};
enum{ANIM_REQUEST_STOP=1};struct LLAssetType{enum{AT_ANIMATION=1};};
struct FSAssetBlacklist{static FSAssetBlacklist&instance(){static FSAssetBlacklist b;return b;}bool isBlacklisted(LLUUID id,int){return id.n==2;}};
std::vector<int>stops;struct Agent{void sendAnimationRequest(LLUUID id,int){stops.push_back(id.n);}}gAgent;
struct LLMessageSystem{int sender=7;std::vector<int>ids{1,2,3};
 void getUUID(const char*block,const char*,LLUUID&id,int i=0){id.n=std::string(block)=="Sender"?sender:std::string(block)=="AnimationList"?ids.at(i):100+i;}
 int getNumberOfBlocks(const char*){return int(ids.size());}void getS32(const char*,const char*,int&seq,int i){seq=i+10;}};
struct VSWorldAvatar{struct Impl{LLUUID id{7};std::multimap<LLUUID,LLUUID>animationSources;};std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<LLUUID,int>published;bool animations(LLMessageSystem&);void animations(const std::map<LLUUID,int>&next){published=next;}};
'''+function+r'''
int main(){VSWorldAvatar avatar;LLMessageSystem message;assert(avatar.animations(message));assert((stops==std::vector<int>{2}));assert(avatar.published.size()==2&&!avatar.published.count(LLUUID{2}));assert(avatar.mImpl->animationSources.size()==2);assert(!avatar.mImpl->animationSources.count(LLUUID{101}));message.sender=8;stops.clear();assert(!avatar.animations(message));assert(stops.empty());}
''')

if __name__=='__main__':unittest.main()
