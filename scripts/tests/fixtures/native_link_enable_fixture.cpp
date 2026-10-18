#include <cassert>
#include <vector>
#include <functional>
#include <optional>
using S32=int;
struct Object {bool modify=false,permanent=false,attachment=false;Object* root=this;bool permModify(){return modify;}bool isPermanentEnforced(){return permanent;}bool isAttachment(){return attachment;}Object* getRootEdit(){return root;}};using LLViewerObject=Object;using VSEditObject=Object;
struct Node{Object* object=nullptr;bool mValid=true;Object* getObject(){return object;}};
struct LLSelectedObjectFunctor{virtual bool apply(Object*)=0;};
int gAgentAvatarp=0;struct RlvSelectIsSittingOn{explicit RlvSelectIsSittingOn(int){}};
struct Settings {bool parts=false;bool getBOOL(const char*)const{return parts;}}gSavedSettings;
struct rlv_handler_t{static bool enabled;static bool isEnabled(){return enabled;}};bool rlv_handler_t::enabled=false;
struct RlvActions{static bool stand;static bool canStand(){return stand;}};bool RlvActions::stand=true;
struct View {
 std::vector<Node*> nodes,roots;bool animated=true,sitting=false,current=true;
 auto begin(){return nodes.begin();}auto end(){return nodes.end();}auto root_begin(){return roots.begin();}auto root_end(){return roots.end();}
 int getRootObjectCount(){return int(roots.size());}bool checkAnimatedObjectLinkable(){return animated;}
 bool applyToRootObjects(LLSelectedObjectFunctor* fn,bool){for(auto* n:roots)if(fn->apply(n->object))return true;return false;}
 Object* getFirstEditableObject(){for(auto*n:nodes)if(n->object&&n->object->permModify())return n->object;return nullptr;}
 Node* getFirstRootNode(RlvSelectIsSittingOn*,bool){return sitting ? nodes.front():nullptr;}
 std::function<bool()> guard(){return [this]{return current;};}
};
using LLObjectSelectionHandle=View*;
struct ReferenceManager {static ReferenceManager* singleton;View view;static ReferenceManager* getInstance(){return singleton;}View* getSelection(){return &view;}bool selectGetAllRootsValid(){for(auto*n:view.roots)if(!n->mValid)return false;return true;}bool enableLinkObjects();bool enableUnlinkObjects();};ReferenceManager* ReferenceManager::singleton=nullptr;
REFERENCE
struct VSObjectSelection{View view;bool enableLinkObjects()const;bool enableUnlinkObjects()const;};
struct VSLinkManager{View selection;VSObjectSelection& source;VSLinkManager(View,VSObjectSelection& owner):selection(owner.view),source(owner){}bool selectGetAllRootsValid(){for(auto*n:selection.roots)if(!n->mValid)return false;return true;}};
bool vs_link_animated_selection(View& view){return view.animated;}bool vs_link_sitting_selection(View& view){return rlv_handler_t::enabled&&!RlvActions::stand&&view.sitting;}
NATIVE
int main(){
 for(unsigned bits=0;bits<65536;++bits){
  Object objects[3];Node nodes[3];View view;
  for(int i=0;i<3;++i){objects[i].modify=bits&(1u<<i);objects[i].permanent=bits&(1u<<(i+3));objects[i].attachment=bits&(1u<<(i+6));nodes[i].object=&objects[i];nodes[i].mValid=!(bits&(1u<<(i+9)));view.nodes.push_back(&nodes[i]);if(i<2)view.roots.push_back(&nodes[i]);}
  objects[2].root=&objects[0];view.animated=bits&(1u<<12);view.sitting=bits&(1u<<13);gSavedSettings.parts=bits&(1u<<14);rlv_handler_t::enabled=bits&(1u<<15);RlvActions::stand=false;
  ReferenceManager original;original.view=view;ReferenceManager::singleton=&original;VSObjectSelection native;native.view=view;
  assert(original.enableLinkObjects()==native.enableLinkObjects());assert(original.enableUnlinkObjects()==native.enableUnlinkObjects());
  native.view.current=false;assert(!native.enableLinkObjects()&&!native.enableUnlinkObjects());
 }
}
