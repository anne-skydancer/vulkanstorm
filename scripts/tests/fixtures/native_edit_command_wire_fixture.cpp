#include <cassert>
#include <iostream>
#define LL_ERRS() std::cerr
#define LL_ENDL std::endl
#include <string>
#include <queue>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <memory>
using U32=unsigned;using S32=int;constexpr int MAX_OBJECTS_PER_PACKET=254;
namespace VSWorldContracts{enum class OwnerKind{Object,Avatar};}
struct LLHost{int id=1;bool operator==(LLHost b)const{return id==b.id;}bool operator!=(LLHost b)const{return id!=b.id;}};
struct VSResourceBudget{struct Reservation{};};
struct VSWorldScene{struct ObjectHandle{LLHost host;int id=1;VSWorldContracts::OwnerKind kind=VSWorldContracts::OwnerKind::Object;};std::map<int,bool>roots;bool refuse=false;std::optional<bool>objectIsRootEdit(ObjectHandle h){return roots.at(h.id);}std::shared_ptr<const VSResourceBudget::Reservation>reserveSpatialBytes(std::size_t){return refuse?nullptr:std::make_shared<VSResourceBudget::Reservation>();}}world;VSWorldScene*scene(){return&world;}
bool alive=true,selected=true,retireAfterSend=false;int capacity=254;
struct Owner{struct Account{};Account account(){return{};}bool current(Account){return alive;}};
struct LLMessageSystem{struct Packet{LLHost host;std::string name;std::vector<int>ids;bool operator==(const Packet&b)const{return host==b.host&&name==b.name&&ids==b.ids;}};std::vector<Packet>sent;Packet pending;bool active=false;int headers=0,clear=0;void newMessage(const char*name){assert(!active);active=true;pending={};pending.name=name;}bool isSendFull(void*){return pending.ids.size()>=std::size_t(capacity);}void sendReliable(LLHost h){assert(active&&!pending.ids.empty());pending.host=h;sent.push_back(pending);active=false;if(retireAfterSend)alive=false;}int getCurrentSendTotal(){return int(pending.ids.size());}void clearMessage(){active=false;++clear;}}message;LLMessageSystem*gMessageSystem=&message;
struct VSObjectSelection{enum class SendOrder{RootsOnly,Individuals,RootsFirst,ChildrenFirst};struct Node{VSWorldScene::ObjectHandle object;struct{bool individual=false;}metadata;};struct State{bool retired=false,allowSelectAvatar=false;std::vector<Node>nodes;std::weak_ptr<Owner>session;bool current(VSWorldScene::ObjectHandle){return alive;}};std::shared_ptr<State>mImpl=std::make_shared<State>();int resets=0;bool resetObjectOverrides(){++resets;return true;}std::function<bool()>selectionGuard(){return[]{return selected;};}bool sendEditCommand(const char*,SendOrder,const std::function<void(LLMessageSystem&)>&,const std::function<void(LLMessageSystem&,const VSWorldScene::ObjectHandle&)>&);};
METHOD
struct LLViewerRegion{LLHost host;LLHost getHost(){return host;}};
struct LLViewerObject{LLViewerRegion*region;int id;bool root=false,avatar=false;bool isRootEdit(){return root;}bool isAvatar(){return avatar;}LLViewerRegion*getRegion(){return region;}};
struct LLSelectNode{LLViewerObject*object;bool individual=false;LLViewerObject*getObject(){return object;}};
struct LLSelectedNodeFunctor{virtual~LLSelectedNodeFunctor()=default;virtual bool apply(LLSelectNode*)=0;};
struct RefSelection{std::vector<LLSelectNode*>nodes;int getObjectCount(){return int(nodes.size());}LLViewerObject*getFirstObject(){return nodes.empty()?nullptr:nodes.front()->object;}void applyToNodes(LLSelectedNodeFunctor*f){for(auto*n:nodes)f->apply(n);}void applyToRootNodes(LLSelectedNodeFunctor*f){for(auto*n:nodes)if(n->object&&!n->individual&&n->object->root)f->apply(n);}};using LLObjectSelectionHandle=std::shared_ptr<RefSelection>;
struct LLSelectMgr{enum ESendType{SEND_ONLY_ROOTS,SEND_INDIVIDUALS,SEND_ROOTS_FIRST,SEND_CHILDREN_FIRST};bool mAllowSelectAvatar=false;int resets=0;void resetObjectOverrides(LLObjectSelectionHandle){++resets;}void sendListToRegions(LLObjectSelectionHandle,const std::string&,void(*)(void*),void(*)(LLSelectNode*,void*),void(*)(LLSelectNode*,void*),void*,ESendType);};
REFERENCE
void header(void*){++message.headers;}void body(LLSelectNode*node,void*){message.pending.ids.push_back(node->object->id);}void logNoOp(LLSelectNode*,void*){}
int main(){for(int c:{2,254})for(int mode=0;mode<4;++mode)for(int linking=0;linking<2;++linking){alive=selected=true;capacity=c;retireAfterSend=false;world.roots.clear();auto owner=std::make_shared<Owner>();VSObjectSelection native;native.mImpl->session=owner;LLSelectMgr legacy;auto ref=std::make_shared<RefSelection>();std::map<int,LLViewerRegion>regions{{1,{{1}}},{2,{{2}}},{3,{{3}}}};std::vector<LLViewerObject>objects;std::vector<LLSelectNode>nodes;objects.reserve(521);nodes.reserve(521);for(int i=0;i<521;++i){int host=i<350?1:i%2?2:3;bool root=i%3==0,individual=i%11==0;objects.push_back({&regions.at(host),i+1,root,false});nodes.push_back({&objects.back(),individual});ref->nodes.push_back(&nodes.back());native.mImpl->nodes.push_back({{{host},i+1}, {individual}});world.roots[i+1]=root;}const char*name=linking?"ObjectLink":"Undo";message={};legacy.sendListToRegions(ref,name,header,body,logNoOp,nullptr,LLSelectMgr::ESendType(mode));const auto expected=message.sent;const int headers=message.headers;message={};assert(native.sendEditCommand(name,VSObjectSelection::SendOrder(mode),[](auto&m){++m.headers;},[](auto&m,const auto&h){m.pending.ids.push_back(h.id);}));assert(message.sent==expected&&message.headers==headers&&legacy.resets==native.resets&&!message.active);}
// No mutation when scratch admission fails; account retirement between real
// packet sends cannot leak another command to the retired simulator owner.
auto owner=std::make_shared<Owner>();VSObjectSelection s;s.mImpl->session=owner;s.mImpl->nodes={{{{1},1},{}}};world.roots={{1,true}};message={};world.refuse=true;assert(!s.sendEditCommand("Undo",VSObjectSelection::SendOrder::Individuals,[](auto&){},[](auto&,const auto&){}));assert(s.resets==0&&message.sent.empty());world.refuse=false;alive=selected=true;assert(!s.sendEditCommand("Undo",VSObjectSelection::SendOrder::Individuals,[](auto&){alive=false;},[](auto&,const auto&){assert(false);}));assert(message.sent.empty()&&!message.active);}
