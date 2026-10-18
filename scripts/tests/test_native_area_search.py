"""Original Area Search policy/protocol and current native service transactions."""
from pathlib import Path
import os,sys,unittest
R=Path(os.environ.get('VS_AREA_SEARCH_ROOT',Path(__file__).resolve().parents[2]))
O=Path(os.environ['VS_AREA_SEARCH_PROPOSED']) if 'VS_AREA_SEARCH_PROPOSED' in os.environ else None
sys.path.insert(0,str(R/'scripts/tests'))
from test_native_material_selection import body,run
def source(path):
 p=O/Path(path).name if O else R/path
 return (p if p.exists() else R/path).read_text(encoding='utf-8')
class NativeAreaSearchTests(unittest.TestCase):
 def test_actual_original_searchable_policy_all_source_and_exclusion_combinations(self):
  old=body(source('scripts/tests/native_area_search_reference.cpp'),'bool FSAreaSearch::isSearchableObject(')
  new=body(source('indra/newview/fsareasearch.cpp'),'bool FSAreaSearch::isSearchableObject(')
  run(self,r'''#include <cassert>
struct Region{};struct Probe{bool value=false;bool notNull()const{return value;}};
struct LLViewerObject{enum{LL_VO_SURFACE_PATCH=9};};struct Object{Region*region=nullptr;int code=0;bool mbCanSelect=false,avatar=false,root=false,attachment=false,rootEdit=false,physical=false,temporary=false;Probe mReflectionProbe;Region*getRegion()const{return region;}int getPCode()const{return code;}bool canSelect()const{return mbCanSelect;}bool isAvatar()const{return avatar;}bool isRoot()const{return root;}bool isRootEdit()const{return rootEdit;}bool isAttachment()const{return attachment;}bool flagUsePhysics()const{return physical;}bool flagTemporaryOnRez()const{return temporary;}bool isReflectionProbe()const{return mReflectionProbe.value;}};
struct Query{bool mExcludeChildPrims=false,mExcludeNeighborRegions=false,mExcludeAttachment=false,mExcludePhysics=false,mExcludeTemporary=false,mExcludeReflectionProbe=false;bool original(Object*objectp,Region*our_region)const OLD bool current(Object*objectp,Region*our_region)const NEW};
int main(){Region region,neighbor;for(unsigned source=0;source<1024;++source)for(unsigned flags=0;flags<64;++flags){Object o;o.region=(source&1)?&region:&neighbor;o.code=(source&2)?9:1;o.mbCanSelect=source&4;o.avatar=source&8;o.root=source&16;o.rootEdit=source&32;o.attachment=source&64;o.physical=source&128;o.temporary=source&256;o.mReflectionProbe.value=source&512;Query q;q.mExcludeChildPrims=flags&1;q.mExcludeNeighborRegions=flags&2;q.mExcludeAttachment=flags&4;q.mExcludePhysics=flags&8;q.mExcludeTemporary=flags&16;q.mExcludeReflectionProbe=flags&32;assert(q.current(&o,&region)==q.original(&o,&region));o.region=nullptr;assert(!q.current(&o,&region));}}
'''.replace('OLD',old).replace('NEW',new))
 def test_actual_encoded_select_deselect_packet_boundaries_and_expiry(self):
  original=body(source('scripts/tests/native_area_search_reference.cpp'),'void FSAreaSearch::requestObjectProperties(')
  current=body(source('indra/newview/fsareasearch.cpp'),'void FSAreaSearch::requestObjectProperties(')
  run(self,r'''#include <cassert>
#include <functional>
#include <vector>
#include <string>
#include <sstream>
using U32=unsigned;using S32=int;constexpr int MAX_OBJECTS_PER_PACKET=255;constexpr int gAgentID=9,gAgentSessionID=10;
std::ostringstream logSink;
#define LL_DEBUGS(x) logSink
#define LL_ENDL std::endl
#define _PREHASH_ObjectSelect "ObjectSelect"
#define _PREHASH_ObjectDeselect "ObjectDeselect"
#define _PREHASH_AgentData "AgentData"
#define _PREHASH_ObjectData "ObjectData"
#define _PREHASH_AgentID "AgentID"
#define _PREHASH_SessionID "SessionID"
#define _PREHASH_ObjectLocalID "ObjectLocalID"
struct Packet{std::string name;std::vector<unsigned>ids;int host=0;bool operator==(const Packet&)const=default;};
struct LLMessageSystem{Packet pending;std::vector<Packet>sent;void newMessageFast(std::string n){pending={n};}void nextBlockFast(const char*){}void addUUIDFast(std::string n,int id){assert((n=="AgentID"&&id==9)||(n=="SessionID"&&id==10));}void addU32Fast(const char*,unsigned id){pending.ids.push_back(id);}bool isSendFull(void*){return false;}void sendReliable(int host){pending.host=host;sent.push_back(pending);}}message;LLMessageSystem*gMessageSystem=&message;
struct Region{int getHost()const{return 77;}};
struct Query{std::function<bool()>mCurrent;void original(const std::vector<U32>&request_list,bool select,Region*regionp) OLD void current(const std::vector<U32>&request_list,bool select,Region*regionp) NEW};
int main(){Region region;bool live=true;Query q;q.mCurrent=[&]{return live;};for(unsigned count:{0u,1u,253u,254u,255u,256u,768u})for(bool select:{false,true}){std::vector<unsigned>ids;for(unsigned i=0;i<count;++i)ids.push_back(i+1);message.sent.clear();q.original(ids,select,&region);auto expected=message.sent;message.sent.clear();q.current(ids,select,&region);assert(message.sent==expected);std::vector<unsigned>actual;for(auto&p:message.sent){assert(p.host==77&&p.name==(select?"ObjectSelect":"ObjectDeselect"));actual.insert(actual.end(),p.ids.begin(),p.ids.end());}assert(actual==ids);}live=false;message.sent.clear();q.current({1,2,3},true,&region);assert(message.sent.empty());}
'''.replace('OLD',original).replace('NEW',current))
 def test_actual_cost_completion_requires_successful_current_publication(self):
  actual=body(source('indra/newview/vsobjectcosts.cpp'),'    void finish(const Entry& entry, const LLSD* data)')
  run(self,r'''#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <string>
using F32=float;struct Token{int id=1;};struct VSObjectCostValues{float object=7,linkset=8,physics=9,linksetPhysics=10;bool pending=true,stale=true;};struct Record{VSObjectCostValues costs;};struct Value{float v=0;float asReal()const{return v;}};struct LLSD{std::map<std::string,Value>values;Value operator[](std::string key)const{return values.at(key);}};
struct Scene{Record record;bool publish=true;int writes=0;std::optional<Record>editParameters(Token){return record;}bool setObjectCostValues(Token,const VSObjectCostValues&v){++writes;if(publish)record.costs=v;return publish;}}scene;
struct Entry{Token object;std::function<bool()>current;};struct Owner{bool active=true;int completions=0;std::function<void()>onComplete;Scene*world(){return &scene;}void completed(Token,const VSObjectCostValues&v){++completions;assert(!v.stale&&!v.pending);if(onComplete)onComplete();}void finish(const Entry&entry,const LLSD*data) ACTUAL};
int main(){bool live=true;Entry e{{1},[&]{return live;}};Owner o;LLSD data{{{"resource_cost",{1}},{"linked_set_resource_cost",{2}},{"physics_cost",{3}},{"linked_set_physics_cost",{4}}}};o.finish(e,nullptr);assert(o.completions==0&&!scene.record.costs.pending&&scene.record.costs.stale&&scene.record.costs.object==7);o.finish(e,&data);assert(o.completions==1&&scene.record.costs.linkset==2);scene.publish=false;o.finish(e,&data);assert(o.completions==1);live=false;int writes=scene.writes;o.finish(e,&data);assert(o.completions==1&&scene.writes==writes);live=true;o.active=false;o.finish(e,&data);assert(scene.writes==writes);}
'''.replace('ACTUAL',actual))
 def test_actual_editing_identity_is_captured_before_recursive_retirement(self):
  actual=body(source('indra/newview/vsareaobject.cpp'),'void VSAreaObject::derender(')
  fallback=body(source('indra/newview/lltool.cpp'),'LLUUID LLTool::getEditingObjectID(')
  composite=source('indra/newview/lltoolcomp.h')
  grab=source('indra/newview/lltoolgrab.h')
  self.assertIn('return mCur->getEditingObjectID();',composite)
  self.assertIn('return editingObjectID();',grab)
  run(self,r'''#include <cassert>
#include <memory>
#include <string>
#include <vector>
#define VS_NATIVE_VULKAN 1
struct LLUUID{int value=0;static const LLUUID null;bool operator==(const LLUUID&)const=default;};const LLUUID LLUUID::null{};const LLUUID gAgentID{9};
std::vector<int>order;bool live=true;struct Object{LLUUID getID()const{return {3};}};
struct Tool{bool stopped=false;Object object;Object*getEditingObject(){return &object;}LLUUID getEditingObjectID() FALLBACK void stopEditing(){stopped=true;order.push_back(6);}}tool;
struct LLToolMgr{static LLToolMgr&instance(){static LLToolMgr x;return x;}Tool*getCurrentTool(){return &tool;}};
struct Region{std::string getName()const{return "source";}};struct Selection{void deselectOnly(int){assert(live);order.push_back(2);}};
struct VSNativeSession{Selection selection;static std::shared_ptr<VSNativeSession>active(){static auto p=std::make_shared<VSNativeSession>();return p;}Selection*objectSelection(){return &selection;}};
struct Scene{void derender(int){order.push_back(5);live=false;}}scene;Scene*area_scene(){return &scene;}
struct LLAssetType{enum{AT_OBJECT};};struct FSAssetBlacklist{static FSAssetBlacklist&instance(){static FSAssetBlacklist x;return x;}void addNewItemToBlacklist(LLUUID,std::string,std::string,int){order.push_back(1);}};
struct ObjectList{void addDerenderedItem(LLUUID,bool){order.push_back(3);}}gObjectList;
struct VSAreaObject{Region region;bool native()const{return true;}explicit operator bool()const{return live;}LLUUID getID()const{return live?LLUUID{3}:LLUUID::null;}const Region*getRegion()const{return &region;}int token()const{return 1;}void derender(const std::string&name)const ACTUAL};
int main(){VSAreaObject object;object.derender("object");assert(tool.stopped);assert((order==std::vector<int>{1,2,3,5,6}));assert(!live);}
'''.replace('FALLBACK',fallback).replace('ACTUAL',actual))
 def test_actual_selected_property_seed_requires_exact_selected_current_source(self):
  actual=body(source('indra/newview/vsobjectselection.cpp'),'bool VSObjectSelection::seedSelectedProperties(')
  run(self,r'''#include <cassert>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
struct Token{int id=1,epoch=1;};bool sameObject(Token a,Token b){return a.id==b.id&&a.epoch==b.epoch;}
struct VSWorldScene{using ObjectHandle=Token;};struct VSObjectProperties{int id=1;bool complete=true;};
struct Session{bool live=true;std::function<bool()>accountGuard(){return [this]{return live;};}};
struct Node{Token object;std::shared_ptr<int>publication;bool pending=true;};
struct State{bool retired=false,live=true,publishable=true;int publications=0,notifications=0;std::shared_ptr<Session>owner=std::make_shared<Session>();std::weak_ptr<Session>session=owner;std::vector<Node>nodes{{{1,1},{},true}};std::function<void()>onChange;
bool current(Token t){return live&&t.epoch==1;}std::shared_ptr<int>publish(Token,const VSObjectProperties&){++publications;return publishable?std::make_shared<int>(3):nullptr;}void changed(){++notifications;if(onChange)onChange();}};
struct VSObjectSelection{std::shared_ptr<State>mImpl=std::make_shared<State>();bool seedSelectedProperties(const Token&token,const VSObjectProperties&properties) ACTUAL};
int main(){VSObjectSelection s;auto p=s.mImpl;assert(!s.seedSelectedProperties({1,2},{1,true}));assert(!s.seedSelectedProperties({1,1},{2,true}));assert(!s.seedSelectedProperties({1,1},{1,false}));assert(!s.seedSelectedProperties({2,1},{2,true}));assert(p->publications==0);assert(s.seedSelectedProperties({1,1},{1,true}));assert(p->nodes[0].publication&&!p->nodes[0].pending&&p->notifications==1);p->onChange=[&]{p->owner->live=false;p->live=false;};assert(!s.seedSelectedProperties({1,1},{1,true}));assert(p->notifications==2);p->live=true;p->retired=true;assert(!s.seedSelectedProperties({1,1},{1,true}));assert(p->notifications==2);}
'''.replace('ACTUAL',actual))
 def test_actual_callback_policy_covers_every_packaged_area_search_binding(self):
  import xml.etree.ElementTree as ET
  import json
  bindings=set()
  for path in (R/'indra/newview/skins').rglob('*.xml'):
   if 'area' not in path.name.lower() or 'search' not in path.name.lower():continue
   for element in ET.parse(path).iter():
    name=element.get('function','')
    if name.startswith('AreaSearch.'):
     bindings.add((name,element.get('parameter',element.get('userdata',''))))
  self.assertGreater(len(bindings),25)
  actual=body(source('indra/newview/fsareasearch.cpp'),'bool FSAreaSearch::isNativeCallback(')
  checks=''.join('assert(policy('+json.dumps(name)+',LLSD{'+json.dumps(value)+'}));' for name,value in sorted(bindings))
  run(self,r'''#include <cassert>
#include <string>
#include <string_view>
struct LLSD{std::string value;std::string asString()const{return value;}};
bool policy(std::string_view name,const LLSD&data) ACTUAL
int main(){CHECKS assert(!policy("AreaSearch.Action",LLSD{"unknown"}));assert(!policy("Unknown",LLSD{"edit"}));}
'''.replace('ACTUAL',actual).replace('CHECKS',checks))
if __name__=='__main__':unittest.main()
