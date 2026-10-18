#include <cassert>
#include <optional>
#include <memory>
#include <vector>
#define VS_NATIVE_VULKAN 1
using LLUUID=int;struct LLSD{};struct LLVOAvatar{int getID()const{return 999;}};
struct VSWorldScene {struct ObjectHandle{int id;};int wearer=42;bool valid=true;struct Avatar{struct Character{int getID(){return 42;}};Character character(){return {};}}avatar;Avatar* avatarForObject(int id){return id==17?&avatar:nullptr;}bool current(ObjectHandle){return valid;}};
struct VSObjectSelection{struct Metadata{bool primary=false;};struct Node{VSWorldScene::ObjectHandle object;Metadata metadata;};struct State{std::vector<Node>nodes;bool valid=true;bool current(VSWorldScene::ObjectHandle){return valid;}};std::shared_ptr<State>mImpl=std::make_shared<State>();std::optional<VSWorldScene::ObjectHandle>primaryObject()const;std::optional<Metadata>node(VSWorldScene::ObjectHandle h){for(auto&n:mImpl->nodes)if(n.object.id==h.id)return n.metadata;return {};}};
PRIMARY
struct VSNativeSession{std::shared_ptr<VSObjectSelection>selected=std::make_shared<VSObjectSelection>();bool valid=true;static inline std::shared_ptr<VSNativeSession>owner;static auto active(){return owner;}int account(){return 8;}bool current(int){return valid;}auto retainObjectSelection(){return selected;}};
struct Context{VSWorldScene scene;VSWorldScene&world(){return scene;}}context;struct Window{Context*nativeContext(){return &context;}}window,*gViewerWindow=&window;
struct RlvActions{enum{SNC_DEFAULT};static inline bool allowed=true,enabled=true;static bool canShowName(int,int id){assert(id==42);return allowed;}static bool isRlvEnabled(){return enabled;}};
struct LLAvatarActions{static inline int sent=0,availabilityCalls=0;static void startCall(int id){sent=id;}static bool canCall(){++availabilityCalls;return true;}};
struct LLSelectMgr{static auto getInstance(){return (LLSelectMgr*)nullptr;}LLSelectMgr*getSelection(){return this;}int getPrimaryObject(){return 0;}};LLVOAvatar*find_avatar_from_object(int){return nullptr;}struct view_listener_t{};
CALL
ENABLE
int main(){auto s=std::make_shared<VSNativeSession>();VSNativeSession::owner=s;auto nodes=s->selected->mImpl;nodes->nodes={{{99},{false}},{{17},{true}}};assert(s->selected->primaryObject()->id==17);LLAvatarCall c;c.handleEvent({});assert(LLAvatarActions::sent==42);assert(enable_avatar_call());RlvActions::allowed=false;LLAvatarActions::sent=0;c.handleEvent({});assert(!LLAvatarActions::sent&&!enable_avatar_call());RlvActions::allowed=true;nodes->valid=false;assert(!s->selected->primaryObject());c.handleEvent({});assert(!LLAvatarActions::sent&&!enable_avatar_call());RlvActions::enabled=false;assert(enable_avatar_call());nodes->valid=true;s->valid=false;c.handleEvent({});assert(!LLAvatarActions::sent);s->valid=true;context.scene.valid=false;c.handleEvent({});assert(!LLAvatarActions::sent);nodes->nodes[1].metadata.primary=false;assert(!s->selected->primaryObject());}
