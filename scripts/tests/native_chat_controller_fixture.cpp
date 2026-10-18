// Frozen reference versus actual shared receive controller. LGPL-2.1.
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
using U8=unsigned char; using U32=unsigned; using F32=float;
struct LLUUID {int value=0; bool isNull()const{return !value;} bool notNull()const{return value;} bool operator==(LLUUID b)const{return value==b.value;} bool operator!=(LLUUID b)const{return !(*this==b);} };
struct LLVector3 {int value=0;};
struct LLColor4 {LLColor4(float,float,float,float){} void setVec(float,float,float,float){} };
enum EChatSourceType {CHAT_SOURCE_SYSTEM,CHAT_SOURCE_AGENT,CHAT_SOURCE_OBJECT,CHAT_SOURCE_REGION};
enum EChatType {CHAT_TYPE_WHISPER,CHAT_TYPE_NORMAL,CHAT_TYPE_SHOUT,CHAT_TYPE_START,CHAT_TYPE_STOP,CHAT_TYPE_DEBUG_MSG,CHAT_TYPE_REGION,CHAT_TYPE_OWNER,CHAT_TYPE_DIRECT,CHAT_TYPE_RADAR};
enum EChatAudible {CHAT_AUDIBLE_NOT,CHAT_AUDIBLE_FULLY};
enum {CHAT_STYLE_NORMAL,CHAT_STYLE_IRC};
struct LLChat {LLUUID mFromID,mOwnerID; std::string mFromName,mText,mURL; EChatSourceType mSourceType=CHAT_SOURCE_AGENT; EChatType mChatType=CHAT_TYPE_NORMAL; EChatAudible mAudible=CHAT_AUDIBLE_NOT; LLVector3 mPosAgent; float mTime=0; int mChatStyle=0; bool mMuted=false,mRlvNamesFiltered=false;};
struct LLSD {std::string value; std::map<std::string,LLSD> map; static LLSD emptyMap(){return {};} LLSD&with(const char*k,const std::string&v){map[k].value=v;return *this;} LLSD()=default;LLSD(const char*s):value(s){} LLSD(std::string s):value(s){} template<class T> LLSD(T):value("value"){} LLSD&operator[](const char*k){return map[k];} LLSD&operator=(LLUUID id){value=std::to_string(id.value);return *this;} };
std::vector<std::string> events;
void event(std::string s){events.push_back(std::move(s));}
unsigned mode=0;bool alive=true;bool gNonInteractive=false;LLUUID gAgentID{1};
bool flag(unsigned n){return (mode&(1u<<n))!=0;}
struct Settings{bool getBOOL(const char*){return flag(3);}}gSavedSettings;
template<class T> struct LLCachedControl {std::string key;LLCachedControl(Settings&,const char*k,bool=false):key(k){} operator T()const{if(key=="UseAntiSpam")return flag(0);if(key=="FSUseAntiSpamMine")return flag(1);if(key=="FSMarkObjects")return flag(2);if(key=="EffectScriptChatParticles")return flag(3);if(key=="OmnifilterEnabled")return flag(4);if(key=="TranslateChat")return flag(5);return false;} };
struct SettingsAccess {bool getBOOL(const char*){return flag(3);}};
struct LLTrans {static std::string getString(const char*k,const LLSD& ={}){return k;}};
struct LLFrameTimer {static float getElapsedSeconds(){event("time");return 7;}};
struct LLAvatarName {std::string getCompleteName()const{return "resolved";}};
struct LLAvatarNameCache {static bool get(LLUUID,LLAvatarName*){event("name");return flag(6);}};
struct LLCacheName {static std::string cleanFullName(const std::string&s){event("clean");return s;}};
struct LLMute {enum{flagTextChat=1};};
struct LLMuteList {static LLMuteList*getInstance(){static LLMuteList v;return &v;}bool isMuted(LLUUID id,const std::string&,int){event("mute-name:"+std::to_string(id.value));return flag(7);}bool isMuted(LLUUID id,int){event("mute-owner:"+std::to_string(id.value));return flag(8);}static bool isLinden(const std::string&s){event("linden");return s=="Linden";}};
struct Agent {bool isDoNotDisturb(){event("dnd");return flag(9);}void heardChat(LLUUID){event("heard");}}gAgent;
enum {ANTISPAM_QUEUE_CHAT,ANTISPAM_SOURCE_OBJECT,ANTISPAM_SOURCE_AGENT};
struct NACLAntiSpamRegistry {static NACLAntiSpamRegistry&instance(){static NACLAntiSpamRegistry v;return v;}bool checkQueue(int,LLUUID id,int kind){event("queue:"+std::to_string(id.value)+":"+std::to_string(kind));return flag(10);}bool checkNewlineFlood(int,LLUUID id,const std::string&s){event("newline:"+std::to_string(id.value)+":"+s);return flag(11);}};
struct VSReceivedChatContext {struct Source{bool attachment=false,owned=false;};std::function<Source()>source;std::function<std::optional<std::string>()>location;};
bool vs_filter_received_chat(LLChat&,std::string&s,const LLUUID&,const VSReceivedChatContext&c){event("receive-policy");if(flag(12)){c.source();c.location();}return s!="filtered";}
enum class VSOwnerChatResult{NotCommand,Consumed,Display};
VSOwnerChatResult vs_process_owner_chat(LLChat&chat,std::string&text,const std::function<bool()>&source,const std::function<bool()>&current={}){event("owner-policy");if(text!="@command"||!source())return VSOwnerChatResult::NotCommand;if(flag(13)){alive=false;return VSOwnerChatResult::Consumed;}if(flag(14))return VSOwnerChatResult::Consumed;chat.mText="executes: ";text="command";return VSOwnerChatResult::Display;}
struct FSCommon{static bool is_irc_me_prefix(const std::string&s){return s.rfind("/me ",0)==0;}};
struct rlv_handler_t{static bool isEnabled(){return flag(12);}};
struct RlvSettings{static bool getEnableTemporaryAttachments(){event("temporary");return flag(15);}};
struct FSLSLBridge{static FSLSLBridge&instance(){static FSLSLBridge v;return v;}bool lslToViewer(const std::string&s,LLUUID,LLUUID){event("bridge");return s=="bridge";}};
bool cmdline_packager(const std::string&s,LLUUID,LLUUID){event("packager");return s=="packager";}
struct VSChatReceiveObject {virtual ~VSChatReceiveObject()=default;virtual bool permYouOwner()const=0;virtual bool isAttachment()const=0;virtual bool isTempAttachment()const=0;virtual bool isAvatar()const=0;virtual LLVector3 getPositionAgent()const=0;virtual void startTyping()=0;virtual void stopTyping()=0;virtual void addChat(const LLChat&)=0;virtual void chatParticles(const LLColor4&,const LLUUID&)=0;};
using VSChatDelivery=std::function<void(const LLChat&,const LLSD&)>;
struct VSChatReceiveServices{std::function<std::shared_ptr<VSChatReceiveObject>(const LLUUID&)>findObject;std::function<EChatAudible()>audible;std::function<std::string()>message;std::function<std::optional<std::string>(const LLVector3&)>location;VSChatDelivery deliver;std::function<bool()>current;};
struct LLViewerObject:VSChatReceiveObject {
 bool permYouOwner()const override{event("owned");return flag(16);}bool isAttachment()const override{event("attachment");return flag(17);}bool isTempAttachment()const override{event("temp-attachment");return flag(18);}bool isAvatar()const override{event("avatar");return flag(19);}LLVector3 getPositionAgent()const override{event("position");return {8};}void startTyping()override{event("typing-start");}void stopTyping()override{event("typing-stop");}void addChat(const LLChat&c)override{event("bubble:"+c.mText);}void chatParticles(const LLColor4&,const LLUUID&owner)override{getPositionAgent();event("particle-source");event("particle-color");event("particle-owner:"+std::to_string(owner.value));event("particles");}
};
struct LLVOAvatar:LLViewerObject{};LLVOAvatar object;
struct ObjectList{LLViewerObject*findObject(LLUUID){event("find");return flag(20)?&object:nullptr;}}gObjectList;
template<class T>struct LLPointer{T*p;LLPointer(T*v):p(v){}T*operator->()const{return p;}operator T*()const{return p;}};
struct LLViewerPartSourceChat {LLViewerPartSourceChat(LLVector3){}void setSourceObject(LLViewerObject*){event("particle-source");}void setColor(LLColor4){event("particle-color");}void setOwnerUUID(LLUUID id){event("particle-owner:"+std::to_string(id.value));}};
struct LLViewerPartSim{static LLViewerPartSim*getInstance(){static LLViewerPartSim v;return &v;}void addPartSource(LLPointer<LLViewerPartSourceChat>p){event("particles");delete p.p;}};
struct LLLocalSpeakerMgr{static LLLocalSpeakerMgr*getInstance(){static LLLocalSpeakerMgr v;return &v;}void setSpeakerTyping(LLUUID,bool on){event(on?"speaker-start":"speaker-stop");}};
struct Region{std::string getName()const{return "region";}};
struct LLWorld{static LLWorld*getInstance(){static LLWorld v;return &v;}Region*getRegionFromPosAgent(LLVector3){event("region");static Region v;return &v;}};
struct LLSLURL{LLSLURL(std::string,LLVector3){}std::string getLocationString(){return "location";}};
struct LLViewerChat{static void formatChatMsg(const LLChat&c,std::string&s){event("format");s="formatted:"+c.mText;}};
struct OmnifilterEngine{enum class eType{NearbyChat,ObjectChat};struct Haystack{std::string mContent,mSenderName;LLUUID mOwnerID;eType mType{};};struct Needle{std::string mChatReplace;};static OmnifilterEngine*getInstance(){static OmnifilterEngine v;return &v;}const Needle*match(const Haystack&h){event("omni:"+h.mContent+":"+h.mSenderName);static Needle v;v.mChatReplace=flag(22)?"replacement":"";return flag(21)?&v:nullptr;}};
struct FSKeywords{static FSKeywords*getInstance(){static FSKeywords v;return &v;}bool chatContainsKeyword(const LLChat&c,bool){event("keywords:"+c.mText);return flag(23);}static void notify(const LLChat&){event("keyword-notify");}};
namespace LLNotificationsUI{struct LLNotificationManager{static LLNotificationManager&instance(){static LLNotificationManager v;return v;}void onChat(const LLChat&c,const LLSD&){event("deliver:"+c.mFromName+":"+c.mText+":"+std::to_string(c.mMuted)+":"+std::to_string(c.mChatStyle));}};}
struct LLTranslate{static LLTranslate&instance(){static LLTranslate v;return v;}void logCharsSeen(std::size_t n){event("seen:"+std::to_string(n));}void logCharsSent(std::size_t n){event("sent:"+std::to_string(n));}void logSuccess(int){event("translation-success");}void logFailure(int){event("translation-failure");}static std::string getTranslateLanguage(){return "to";}static std::string removeNoTranslateTags(std::string s){return s;}template<class A,class B>static void translateMessage(std::string,std::string,std::string s,A success,B failure){event("translate:"+s);if(flag(24))failure(500,"failure");else success("translated","detected");}};
struct LLStringUtil{static int compareInsensitive(std::string a,std::string b){return a.compare(b);}static void replaceString(std::string&,const char*,const char*){} };
namespace boost{using regex=std::regex;using std::regex_search;using std::bind;}
using namespace std::placeholders;
std::ostringstream ignored;
#define LL_DEBUGS(x) ignored
#define LL_WARNS(x) ignored
#define LL_ENDL ""
void make_ui_sound(const char*){event("sound");}void on_new_message(const LLSD&){event("new-message");}
TRANSLATION
void translateSuccess(LLChat c,LLSD a,std::string m,std::string l,std::string t,std::string d){translateSuccess(c,a,m,l,t,d,[](const LLChat&c,const LLSD&a){LLNotificationsUI::LLNotificationManager::instance().onChat(c,a);});}
void translateFailure(LLChat c,LLSD a,int status,std::string e){translateFailure(c,a,status,e,[](const LLChat&c,const LLSD&a){LLNotificationsUI::LLNotificationManager::instance().onChat(c,a);});}
struct LLMessageSystem{EChatAudible aud;std::string text;void getU8Fast(const char*,const char*,U8&out){event("read-audible");out=aud;}void getStringFast(const char*,const char*,std::string&out){event("read-text");out=text;}};
const char*_PREHASH_ChatData="data",*_PREHASH_Audible="audible",*_PREHASH_Message="message";
REFERENCE
CURRENT
int main(){
 const std::string texts[]={"text","/me waves","@command","bridge","packager","filtered",""};
 const EChatType types[]={CHAT_TYPE_NORMAL,CHAT_TYPE_OWNER,CHAT_TYPE_START,CHAT_TYPE_STOP,CHAT_TYPE_WHISPER,CHAT_TYPE_SHOUT,CHAT_TYPE_DEBUG_MSG,CHAT_TYPE_DIRECT,CHAT_TYPE_REGION};
 unsigned seed=0x12345678;unsigned count=0;
 for(unsigned i=0;i<40000;++i){seed=seed*1664525u+1013904223u;mode=seed&((1u<<25)-1);LLChat chat;chat.mFromID={int(1+(seed%3))};chat.mSourceType=static_cast<EChatSourceType>((seed>>4)%4);chat.mChatType=types[(seed>>7)%9];LLUUID owner{int((seed>>12)%4)};std::string name=(seed&8)?"name":((seed&16)?"  ":"Linden");LLMessageSystem msg{(seed&32)?CHAT_AUDIBLE_FULLY:CHAT_AUDIBLE_NOT,texts[(seed>>17)%7]};
  alive=true;events.clear();reference_receive(chat,name,owner,&msg);const auto expected=events;
  alive=true;events.clear();VSChatReceiveServices services;
  services.findObject=[](LLUUID id)->std::shared_ptr<VSChatReceiveObject>{auto*p=gObjectList.findObject(id);return p?std::shared_ptr<VSChatReceiveObject>(p,[](auto*){}):nullptr;};
  services.audible=[&]{U8 v;msg.getU8Fast("data","audible",v);return static_cast<EChatAudible>(v);};services.message=[&]{std::string s;msg.getStringFast("data","message",s);return s;};
  services.location=[](LLVector3 p)->std::optional<std::string>{auto*r=LLWorld::getInstance()->getRegionFromPosAgent(p);return LLSLURL(r->getName(),p).getLocationString();};
  services.deliver=[](const LLChat&c,const LLSD&a){LLNotificationsUI::LLNotificationManager::instance().onChat(c,a);};
  vs_receive_chat(chat,name,owner,services);
  if(events!=expected){std::cerr<<"mismatch case "<<i<<" mode "<<mode<<" type "<<chat.mChatType<<"\n";for(auto&s:expected)std::cerr<<"R "<<s<<"\n";for(auto&s:events)std::cerr<<"C "<<s<<"\n";return 1;}++count;
 }
 std::cout<<count<<" frozen reference/controller traces agree\n";
}
