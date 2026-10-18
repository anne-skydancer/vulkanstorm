#include <cassert>
#include <functional>
#include <memory>
#include <vector>
#include <utility>
struct LLRefCount{mutable int refs=0;virtual~LLRefCount()=default;void ref()const{++refs;}void unref()const{assert(refs>0);if(!--refs)delete this;}int getNumRefs()const{return refs;}};
template<class T>class LLPointer{T*p=nullptr;public:LLPointer()=default;LLPointer(T*q):p(q){if(p)p->ref();}LLPointer(const LLPointer&o):LLPointer(o.p){}~LLPointer(){if(p)p->unref();}LLPointer&operator=(T*q){if(q)q->ref();if(p)p->unref();p=q;return*this;}LLPointer&operator=(const LLPointer&o){return*this=o.p;}T*operator->(){return p;}const T*operator->()const{return p;}T*get(){return p;}operator bool()const{return p;}};
class LLHUDObject:public LLRefCount{public:bool dead=false;static std::vector<LLPointer<LLHUDObject>>sHUDObjects;void markDead(){dead=true;}static void cleanupUnreferenced();};std::vector<LLPointer<LLHUDObject>>LLHUDObject::sHUDObjects;
struct Object:LLRefCount{void clearIcon(){}};
int destroyed=0;
class LLHUDIcon:public LLHUDObject{public:std::function<void()>mNativeRetire,mNativeSource,mNativeImageQuery;std::shared_ptr<int>mNativeImage,mNativeMesh,mNativeResidency;bool mNativeQuadKnown=true;LLPointer<Object>mSourceObject;void markDead();~LLHUDIcon(){++destroyed;}};
MARK
CLEAN
int main(){LLPointer<LLHUDIcon> source(new LLHUDIcon);auto*identity=source.get();LLHUDObject::sHUDObjects.emplace_back(identity);LLPointer<LLRefCount>savedPick(identity);std::weak_ptr<int>budget;{auto reserve=std::make_shared<int>(99);budget=reserve;identity->mNativeResidency=reserve;}int retired=0;identity->mNativeRetire=[&]{++retired;source=nullptr;};identity->mNativeImage=std::make_shared<int>(1);identity->mNativeMesh=std::make_shared<int>(2);identity->markDead();assert(retired==1&&!source&&identity->dead&&!identity->mNativeImage&&!identity->mNativeMesh);LLHUDObject::cleanupUnreferenced();assert(!budget.expired()&&destroyed==0&&LLHUDObject::sHUDObjects.size()==1);identity->markDead();assert(retired==1);savedPick=nullptr;LLHUDObject::cleanupUnreferenced();assert(budget.expired()&&destroyed==1&&LLHUDObject::sHUDObjects.empty());}
