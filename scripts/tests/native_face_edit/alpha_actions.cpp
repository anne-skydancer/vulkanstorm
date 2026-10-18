#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <vector>
using F32=float;using U8=unsigned char;using S32=int;constexpr int VALPHA=3;
template<class T>T llmin(T a,T b){return std::min(a,b);}
struct LLColor4{float mV[4]{.1f,.2f,.3f,1};bool operator==(const LLColor4&)const=default;};
struct LLTextureEntry{LLColor4 color;LLColor4 getColor()const{return color;}};
struct LLViewerObject{bool modify=true;unsigned mask=7;int sends=0;std::array<LLTextureEntry,3>entries;std::function<void()>hook;bool permModify()const{return modify;}LLTextureEntry&getTEref(int side){return entries.at(side);}void setTEColor(int side,LLColor4 v){entries.at(side).color=v;if(hook)hook();}void sendTEUpdate(){++sends;}};
struct LLSelectedTEFunctor{virtual~LLSelectedTEFunctor()=default;virtual bool apply(LLViewerObject*,S32)=0;};
struct LLSelectMgrSendFunctor{bool apply(LLViewerObject*o){o->sendTEUpdate();return true;}};
struct LLSelectMgr{std::vector<LLViewerObject*>objects;LLSelectMgr*getSelection(){return this;}template<class F>void applyToTEs(F*f){for(auto*o:objects)for(int side=0;side<3;++side)if(o->mask&(1u<<side))f->apply(o,side);}template<class F>void applyToObjects(F*f){for(auto*o:objects)f->apply(o);}void selectionSetAlphaOnly(F32);};
std::map<int,LLViewerObject*>owners;int epoch=1;
struct VSMaterialObject{LLViewerObject*object=nullptr;static VSMaterialObject find(int id){return {owners.count(id)?owners.at(id):nullptr};}explicit operator bool()const{return object!=nullptr;}int getNumTEs()const{return 3;}int getNumFaces()const{return 3;}bool permModify()const{return object->modify;}const LLTextureEntry*getTE(int side)const{return &object->entries.at(side);}void setTEColor(U8 side,LLColor4 v){object->setTEColor(side,v);}void sendTEUpdate(){if(object)object->sendTEUpdate();}};
struct VSEditSelection{struct Node{int source=0;};struct Selection{bool node(int id)const{return owners.count(id);}bool contains(int id,int side)const{return owners.at(id)->mask&(1u<<side);}};std::vector<std::shared_ptr<Node>>mStorage;std::shared_ptr<Selection>mSelection=std::make_shared<Selection>();std::function<bool()>guard()const{return [captured=epoch]{return epoch==captured;};}void selectionSetAlphaOnly(F32);};
ORIGINAL
NATIVE
int main()
{
    std::mt19937 rng(774);for(unsigned i=0;i<2048;++i)
    {
        std::array<LLViewerObject,4>a,b;owners.clear();LLSelectMgr reference;VSEditSelection native;
        const unsigned count=1+rng()%4;const F32 alpha=F32(rng()%100)/100;
        for(unsigned index=0;index<count;++index)
        {
            a[index].mask=rng()%8;a[index].modify=rng()%2;
            for(auto&te:a[index].entries)for(auto&channel:te.color.mV)channel=F32(rng()%100)/100;
            b[index]=a[index];reference.objects.push_back(&a[index]);owners[int(index)]=&b[index];native.mStorage.push_back(std::make_shared<VSEditSelection::Node>(VSEditSelection::Node{int(index)}));
        }
        reference.selectionSetAlphaOnly(alpha);native.selectionSetAlphaOnly(alpha);
        for(unsigned index=0;index<count;++index){assert(a[index].sends==b[index].sends&&b[index].sends==1);for(unsigned side=0;side<3;++side)assert(a[index].entries[side].color==b[index].entries[side].color);}
    }
    LLViewerObject object;owners={{7,&object}};VSEditSelection selected;selected.mStorage.push_back(std::make_shared<VSEditSelection::Node>(VSEditSelection::Node{7}));object.hook=[]{++epoch;};selected.selectionSetAlphaOnly(.5f);assert(object.entries[0].color.mV[VALPHA]==.5f&&object.entries[1].color.mV[VALPHA]==1&&object.sends==0);
}
