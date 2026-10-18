#include <cassert>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <utility>
#define VS_NATIVE_VULKAN 1
using LLUUID=int;using LLMaterialID=int;using LLMaterialPtr=std::shared_ptr<int>;
template<class...Args>struct Signal{std::vector<std::function<void(Args...)>>slots;void connect(std::function<void(Args...)>s){slots.push_back(s);}void operator()(Args...args){auto own=slots;for(auto&s:own)s(args...);}};
struct Assets{bool ready=true;std::map<int,LLMaterialPtr>values;void requestAll(){}bool loaded()const{return ready;}const auto&materials()const{return values;}LLMaterialPtr get(int id){auto i=values.find(id);return i==values.end()?nullptr:i->second;}};std::map<int,Assets>assets;
Assets*nativeRegionMaterials(int id){return &assets[id];}
struct World{int ticks=0;void tickLegacyMaterials(){++ticks;}};struct Context{World scene;auto&world(){return scene;}};struct Window{Context ctx;auto*nativeContext(){return &ctx;}}window;auto*gViewerWindow=&window;
struct LLMaterialMgr {using material_map_t=std::map<int,LLMaterialPtr>;using get_callback_t=Signal<const int&,const LLMaterialPtr>;using getall_callback_t=Signal<const int&,const material_map_t&>;struct NativeState;std::shared_ptr<NativeState>active;auto nativeState(){return active;}void nativeIdle();};
STATE
IDLE
int main(){LLMaterialMgr manager;auto make=[&]{auto s=std::make_shared<LLMaterialMgr::NativeState>();s->current=[]{return true;};manager.active=s;return s;};auto s=make();auto&row=s->regions[11];row.current=[]{return true;};assets[11].values[7]=std::make_shared<int>(42);assets[11].values[8]=std::make_shared<int>(43);int called=0;
 row.pending[7]=std::make_shared<LLMaterialMgr::get_callback_t>();row.pending[7]->connect([&](int id,LLMaterialPtr material){assert(id==7&&*material==42);++called;assert(s->regions.empty());s->retired=true;s->regions.clear();manager.active.reset();});
 row.pending[8]=std::make_shared<LLMaterialMgr::get_callback_t>();row.pending[8]->connect([&](int,LLMaterialPtr){++called;});manager.nativeIdle();assert(called==1);
 // A reentrant request for the same ID belongs to the next dispatch, not an old iterator.
 s=make();auto&next=s->regions[11];next.current=[]{return true;};next.pending[7]=std::make_shared<LLMaterialMgr::get_callback_t>();next.pending[7]->connect([&](int,LLMaterialPtr){++called;auto&r=s->regions[11];r.current=[]{return true;};r.pending[7]=std::make_shared<LLMaterialMgr::get_callback_t>();r.pending[7]->connect([&](int,LLMaterialPtr){++called;});});manager.nativeIdle();assert(called==2&&s->regions.at(11).pending.size()==1);manager.nativeIdle();assert(called==3&&s->regions.empty());
 // Successfully empty GET-all is a real result, not an indefinitely pending cache.
 s=make();auto&empty=s->regions[12];empty.current=[]{return true;};empty.all=std::make_shared<LLMaterialMgr::getall_callback_t>();empty.all->connect([&](int region,const auto&values){assert(region==12&&values.empty());++called;});manager.nativeIdle();assert(called==4&&s->regions.empty());
 s=make();auto&expired=s->regions[11];expired.current=[]{return false;};expired.pending[7]=std::make_shared<LLMaterialMgr::get_callback_t>();expired.pending[7]->connect([&](int,LLMaterialPtr){assert(false);});manager.nativeIdle();assert(s->regions.empty());
}
