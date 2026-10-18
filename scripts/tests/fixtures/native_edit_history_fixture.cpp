#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>
using LLUUID=int;using U32=unsigned;constexpr int _PREHASH_AgentData=1,_PREHASH_AgentID=2,_PREHASH_SessionID=3,_PREHASH_GroupID=4,_PREHASH_ObjectData=5,_PREHASH_ObjectID=6;
int gAgentID=17,gAgentSessionID=29;struct Agent{int getGroupID(){return 41;}}gAgent;
struct Settings{bool parts=false;bool getBOOL(const char*)const{return parts;}}gSavedSettings;
struct LLMessageSystem{std::vector<int> data;void nextBlockFast(int n){data.push_back(n);}void addUUIDFast(int n,int id){data.push_back(n);data.push_back(id);}};
struct VSWorldScene{struct ObjectHandle{int id=53;};};
struct VSObjectSelection{enum class SendOrder{RootsOnly,Individuals,RootsFirst,ChildrenFirst};SendOrder last;std::string name;LLMessageSystem encoded;bool sendEditCommand(const char*n,SendOrder order,const std::function<void(LLMessageSystem&)>&header,const std::function<void(LLMessageSystem&,const VSWorldScene::ObjectHandle&)>&body){name=n;last=order;header(encoded);body(encoded,{});return true;}void undo();void redo();};
METHODS
int main(){for(bool parts:{false,true}){gSavedSettings.parts=parts;for(bool redo:{false,true}){VSObjectSelection selection;if(redo)selection.redo();else selection.undo();assert(selection.name==(redo?"Redo":"Undo"));assert(selection.last==(parts?VSObjectSelection::SendOrder::ChildrenFirst:VSObjectSelection::SendOrder::RootsOnly));assert(selection.encoded.data==std::vector<int>({1,2,17,3,29,4,41,5,6,53}));}}}
