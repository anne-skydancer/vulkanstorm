#include <cassert>
#include <memory>
#include <optional>
using LLUUID=int;
constexpr int RLV_BHVR_UNSIT=1;
#define VS_NATIVE_VULKAN 1
struct Avatar {bool sitting=false;bool isSitting()const{return sitting;}}legacy,*gAgentAvatarp=&legacy;
bool legacyValid=false;
bool isAgentAvatarValid(){return legacyValid;}
struct Handler {
 bool unsit=false;int overrideID=7;
 bool hasBehaviour(int)const{return unsit;}
 bool hasBehaviourExcept(int,int id)const{return unsit&&id!=overrideID;}
}gRlvHandler;
struct NativeAvatar {bool seated=false;bool sitting()const{return seated;}}nativeAvatar;
struct Token {int account=42;unsigned accountGeneration=2;};
struct World {
 std::optional<Token> token=Token{};NativeAvatar* avatar=&nativeAvatar;
 auto object(int id)const{return id==42?token:std::nullopt;}auto selfAvatar()const{return avatar;}
};
struct Context {World scene;World& world(){return scene;}}context;
struct Window {bool native=false;Context* context=nullptr;bool isNativeVulkan()const{return native;}
 auto nativeContext()const{return context;}}window,*gViewerWindow=&window;
struct VSNativeSession {
 enum class Phase{Connected,Disconnected};Phase state=Phase::Connected;bool accountCurrent=true;
 struct Account {int agent=42;unsigned generation=2;};
 static inline std::shared_ptr<VSNativeSession> instance;
 static auto active(){return instance;}Phase phase()const{return state;}
 Account account()const{return {};}bool current(const Account&)const{return accountCurrent;}
};
struct RlvActions {static bool canStand();static bool canStand(const LLUUID&);};
struct Reference {static bool canStand();static bool canStand(const LLUUID&);};
REFERENCE
CURRENT
int main(){
 for(bool valid:{false,true})for(bool sitting:{false,true})for(bool restricted:{false,true}){
  legacyValid=valid;legacy.sitting=sitting;gRlvHandler.unsit=restricted;
  assert(RlvActions::canStand()==Reference::canStand());
  for(int except:{7,9})assert(RlvActions::canStand(except)==Reference::canStand(except));
 }
 window.native=true;window.context=&context;
 auto owner=std::make_shared<VSNativeSession>();VSNativeSession::instance=owner;
 legacyValid=true;legacy.sitting=false;gRlvHandler.unsit=true;
 assert(RlvActions::canStand());nativeAvatar.seated=true;assert(!RlvActions::canStand());
 assert(RlvActions::canStand(7)&&!RlvActions::canStand(9));nativeAvatar.seated=false;
 context.scene.avatar=nullptr;assert(!RlvActions::canStand());context.scene.avatar=&nativeAvatar;
 context.scene.token.reset();assert(!RlvActions::canStand());context.scene.token=Token{};
 context.scene.token->accountGeneration=1;assert(!RlvActions::canStand());context.scene.token=Token{};
 context.scene.token->account=99;assert(!RlvActions::canStand());context.scene.token=Token{};
 owner->accountCurrent=false;assert(!RlvActions::canStand());owner->accountCurrent=true;
 owner->state=VSNativeSession::Phase::Disconnected;assert(!RlvActions::canStand());owner->state=VSNativeSession::Phase::Connected;
 window.context=nullptr;assert(!RlvActions::canStand());window.context=&context;
 VSNativeSession::instance.reset();assert(!RlvActions::canStand());
 gRlvHandler.unsit=false;assert(RlvActions::canStand());
}
