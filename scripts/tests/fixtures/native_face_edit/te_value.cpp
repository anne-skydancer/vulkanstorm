#include <cassert>
#include <optional>
#include <memory>
#include <vector>
#include <functional>
#include <type_traits>
#include <cmath>
#include <random>
using S32=int;using U32=unsigned;
struct Handle {int id;};struct LLViewerObject {int id;std::vector<float> values;int getID()const{return id;}int getNumTEs()const{return values.size();}};
struct LLSelectNode {LLViewerObject* object;Handle source;U32 mask=0;int last=0;LLViewerObject* getObject(){return object;}bool isTESelected(int face){return mask&(1u<<face);}int getLastSelectedTE(){return last;}};
template<class T>struct LLSelectedTEGetFunctor {virtual~LLSelectedTEGetFunctor()=default;virtual T get(LLViewerObject*,int)=0;};template<class T>struct LLCheckIdenticalFunctor {static bool same(T a,T b,T t){return std::abs(a-b)<=t;}};
struct LLObjectSelection {using iterator=std::vector<LLSelectNode*>::iterator;std::vector<LLSelectNode*> nodes;LLViewerObject* primary=nullptr;auto begin(){return nodes.begin();}auto end(){return nodes.end();}auto* getPrimaryObject(){return primary;}template<class T>bool getSelectedTEValue(LLSelectedTEGetFunctor<T>*,T&,bool=false,T=T());};
ORIGINAL
struct LLSelectMgr {LLObjectSelection* selection;auto* getSelection(){return selection;}};
struct NativeSelection {LLObjectSelection* data;struct Metadata {U32 faceMask;int lastFace;};std::optional<Handle> primaryObject(){return data->primary?std::optional<Handle>({data->primary->id}):std::nullopt;}std::optional<Metadata> node(Handle h){for(auto* n:data->nodes)if(n->source.id==h.id)return Metadata{n->mask,n->last};return{};}};
struct NativeSelector {LLObjectSelection* data;std::shared_ptr<NativeSelection> owner;auto nativeSelection(){return owner;}auto begin(){return data->begin();}auto end(){return data->end();}};
KERNEL
int main(){std::mt19937 random(1971);for(unsigned iteration=0;iteration<50000;++iteration){LLViewerObject objects[5];LLSelectNode nodes[5];LLObjectSelection original;const unsigned count=random()%6;for(unsigned i=0;i<count;++i){objects[i].id=i;const unsigned faces=random()%8;for(unsigned f=0;f<faces;++f)objects[i].values.push_back(float(random()%100)/10);nodes[i]={&objects[i],{int(i)},random()&255u,int(random()%8)};original.nodes.push_back(&nodes[i]);}if(count&&random()%2)original.primary=&objects[random()%count];NativeSelector native{&original,std::make_shared<NativeSelection>(NativeSelection{&original})};LLSelectMgr legacy{&original};
std::vector<int> originalCalls,nativeCalls,legacyCalls;struct Getter:LLSelectedTEGetFunctor<float>{std::vector<int>& calls;Getter(std::vector<int>& c):calls(c){}float get(LLViewerObject* o,int f)override{calls.push_back(o->id*100+f);return o->values[f];}} getter(originalCalls);
float a=-999,b=-999,c=-999;const bool tolerance=random()%2;const float amount=float(random()%20)/10;bool ar=original.getSelectedTEValue(&getter,a,tolerance,amount);bool br=vs_build_te_value(&native,[&](auto* o,int f){nativeCalls.push_back(o->id*100+f);return o->values[f];},b,tolerance,amount);bool cr=vs_build_te_value(&legacy,[&](auto* o,int f){legacyCalls.push_back(o->id*100+f);return o->values[f];},c,tolerance,amount);assert(ar==br&&ar==cr&&a==b&&a==c&&originalCalls==nativeCalls&&originalCalls==legacyCalls);}}
