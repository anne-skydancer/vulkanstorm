#include <cassert>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>
using U8=unsigned char;using U32=unsigned;using U64=unsigned long long;using LLHost=int;using LLMaterialID=int;
struct LLMaterial{int value=0;};using LLMaterialPtr=std::shared_ptr<LLMaterial>;
namespace LLCoreHttpUtil{struct HttpCoroutineAdapter{using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;int cancellations=0;void cancelSuspendedOperation(){++cancellations;}};}
struct VSWorldScene{struct ObjectHandle{int host=1;U32 local=7;int incarnation=1;};bool alive=true,budget=true;int epoch=1,leases=0,reservations=0;bool current(ObjectHandle t)const{return alive&&t.incarnation==epoch;}int textureEntryCount(ObjectHandle)const{return 3;}std::function<bool()>guard(ObjectHandle t){return[this,t]{return current(t);};}std::shared_ptr<const void>reserveSpatialBytes(std::size_t){++reservations;if(!budget)return{};++leases;return{new int(1),[this](const void*p){--leases;delete static_cast<const int*>(p);}};}};
struct Context{VSWorldScene scene;auto&world(){return scene;}};struct Window{Context context;auto*nativeContext(){return&context;}}window;auto*gViewerWindow=&window;
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>{static inline std::shared_ptr<VSNativeSession>instance;static auto active(){return instance;}bool alive=true;int generation=3;std::string cap="RenderMaterials";std::function<bool()>accountGuard(){return[weak=weak_from_this()]{auto p=weak.lock();return p&&p->alive;};}int circuitGeneration(LLHost)const{return generation;}std::string capability(LLHost,const std::string&)const{return cap;}};
struct VSMaterialAssets{struct Impl;struct PreparedPut;std::shared_ptr<Impl>mImpl;explicit VSMaterialAssets(LLHost);void reset();std::shared_ptr<PreparedPut>preparePut(const VSWorldScene::ObjectHandle&,U8,const LLMaterial&);bool put(const std::shared_ptr<PreparedPut>&);bool put(const VSWorldScene::ObjectHandle&,U8,const LLMaterial&);};
@@STRUCTS@@
VSMaterialAssets::VSMaterialAssets(LLHost host):mImpl(std::make_shared<Impl>(host)){}
@@RESET@@
@@PREPARE@@
@@PUT_PREPARED@@
@@PUT@@
int main(){VSNativeSession::instance=std::make_shared<VSNativeSession>();auto session=VSNativeSession::instance;auto&world=window.context.scene;VSMaterialAssets assets(1);VSWorldScene::ObjectHandle token;
 world.budget=false;assert(!assets.preparePut(token,1,{11})&&assets.mImpl->pending.empty()&&world.leases==0);
 world.budget=true;auto prepared=assets.preparePut(token,1,{11});assert(prepared&&assets.mImpl->pending.empty()&&world.leases==1);auto reservations=world.reservations;world.budget=false;assert(assets.put(prepared)&&world.reservations==reservations&&assets.mImpl->pending.size()==1);
 auto request=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>();auto write=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>();assets.mImpl->request=request;assets.mImpl->writes[1]=write;assets.reset();assert(request->cancellations==1&&write->cancellations==1&&assets.mImpl->pending.empty());assert(!assets.put(prepared));prepared.reset();assert(world.leases==0);
 world.budget=true;prepared=assets.preparePut(token,1,{12});assert(prepared);++session->generation;assert(!assets.put(prepared));prepared.reset();assert(world.leases==0);
 prepared=assets.preparePut(token,1,{13});assert(prepared);++world.epoch;assert(!assets.put(prepared));prepared.reset();token.incarnation=world.epoch;
 prepared=assets.preparePut(token,1,{14});assert(prepared);session->alive=false;assert(!assets.put(prepared));prepared.reset();assert(!assets.preparePut(token,1,{15})&&world.leases==0);session->alive=true;
 VSMaterialAssets different(1);prepared=assets.preparePut(token,1,{16});assert(prepared&&!different.put(prepared));prepared.reset();
 session->cap.clear();assert(!assets.preparePut(token,1,{17}));session->cap="cap";assert(!assets.preparePut(token,3,{18}));token.host=2;assert(!assets.preparePut(token,1,{19}));assert(world.leases==0);
}
