#include <memory>
#include <functional>
#include <cassert>
#include <optional>
#include <string>
using U64=unsigned long long;using LLVector3=int;using LLQuaternion=int;
int sent=0;void near_sit_object(bool success,void* data){assert(success&&!data);++sent;}
struct VSNativeSession{U64 epoch=1;bool account=true;std::shared_ptr<int>life=std::make_shared<int>(1);std::function<bool()>accountGuard(){return [this,keep=life]{return account;};}U64 regionEpoch()const{return epoch;}};
struct World{bool current=true;std::shared_ptr<int>life=std::make_shared<int>(1);std::function<bool()>guard(int){return [this,keep=life]{return current;};}};
struct Agent{void(*callback)(bool,void*)=nullptr;void*data=nullptr;bool immediate=false;int getPosGlobalFromAgent(int p){return p+100;}void startAutoPilotGlobal(int p,const char*behavior,const int*rotation,void(*cb)(bool,void*),void*raw,float stop){assert(p==102&&*rotation==7&&std::string(behavior)=="Sit"&&stop==.5f);if(immediate)cb(false,raw);else {callback=cb;data=raw;}}void finish(bool success){auto cb=callback;auto raw=data;callback=nullptr;data=nullptr;assert(cb);cb(success,raw);}}gAgent;
struct NativeSitResponse{struct Pose{int object=3;};using Object=std::optional<Pose>;std::shared_ptr<VSNativeSession>owner;U64 circuit;World&world;
METHOD
};
int main(){for(int mode=0;mode<7;++mode){World world;auto owner=std::make_shared<VSNativeSession>();auto accountLife=owner->life;auto objectLife=world.life;NativeSitResponse response{owner,owner->epoch,world};gAgent.immediate=mode==6;int before=sent;response.autopilot(NativeSitResponse::Pose{},2,7);if(mode==6){assert(accountLife.use_count()==2&&objectLife.use_count()==2);continue;}assert(accountLife.use_count()==3&&objectLife.use_count()==3);if(mode==1)owner->account=false;if(mode==2)world.current=false;if(mode==3)++owner->epoch;if(mode==4){response.owner.reset();owner.reset();}gAgent.finish(mode!=5);assert(sent==before+(mode==0));assert(objectLife.use_count()==2);assert(accountLife.use_count()==(mode==4?1:2));}}
