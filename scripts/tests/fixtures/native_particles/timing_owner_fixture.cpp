#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
using U32=unsigned;using U64=unsigned long long;using LLUUID=int;
struct LLDrawable{static U32 frame;static U32 getCurrentFrame(){return frame;}};U32 LLDrawable::frame=1;
struct Character{U64 id=1;U64 getAnimationInstanceId(){return id;}};
struct VSWorldAvatar{Character value;float measured=-1;Character&character(){return value;}void gpuRenderTime(float v){measured=v;}};
struct VSWorldRenderer{struct AvatarTiming{std::function<bool()>current;std::function<void(float)>publish;std::shared_ptr<const void>residency;};};
struct VSWorldScene{struct ObjectHandle{int host;U32 local;int epoch;};struct Object{LLUUID id=0;U32 parent=0;U64 generation=1,parentGeneration=0;bool parentRetired=false;std::shared_ptr<VSWorldAvatar>controlAvatar;std::shared_ptr<const VSWorldRenderer::AvatarTiming>avatarTiming;std::optional<U32>avatarTimingAttempt;};struct Impl{int host=1,epoch=1;bool live=true;std::map<U32,Object>objects;std::map<LLUUID,std::unique_ptr<VSWorldAvatar>>avatars;bool linksCurrent(const Object&o){return !o.parentRetired;}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool budget=true;int allocations=0;bool current(ObjectHandle t){return mImpl->live&&t.epoch==mImpl->epoch&&t.host==mImpl->host&&mImpl->objects.count(t.local);}std::function<bool()>guard(ObjectHandle token){std::weak_ptr<Impl>w=mImpl;return[w,token]{auto p=w.lock();return p&&p->live&&p->epoch==token.epoch&&p->objects.count(token.local);};}std::optional<ObjectHandle>object(int host,U32 local){if(mImpl->objects.count(local))return ObjectHandle{host,local,mImpl->epoch};return{};}std::shared_ptr<const int>reserveSpatialBytes(std::size_t){++allocations;return budget?std::make_shared<int>(0):nullptr;}std::shared_ptr<const VSWorldRenderer::AvatarTiming>avatarTiming(const ObjectHandle&);};
METHOD
int main(){VSWorldScene scene;scene.mImpl->objects[1].id=100;scene.mImpl->avatars[100]=std::make_unique<VSWorldAvatar>();scene.mImpl->objects[2].id=101;scene.mImpl->objects[2].parent=1;scene.mImpl->objects[2].parentGeneration=1;auto root=*scene.object(1,1),child=*scene.object(1,2);auto timing=scene.avatarTiming(root);assert(timing&&timing->current());assert(scene.avatarTiming(child)==timing);timing->publish(2.5f);assert(scene.mImpl->avatars[100]->measured==2.5f);scene.mImpl->objects[2].parentRetired=true;assert(!scene.avatarTiming(child));scene.mImpl->objects[2].parentRetired=false;
 // Same object/UUID/local memory can acquire a different character incarnation.
 scene.mImpl->avatars[100]->value.id=2;assert(!timing->current());timing->publish(99);assert(scene.mImpl->avatars[100]->measured==2.5f);assert(!scene.avatarTiming(root));++LLDrawable::frame;auto next=scene.avatarTiming(root);assert(next&&next!=timing);next->publish(3);assert(scene.mImpl->avatars[100]->measured==3);
 // Exact account/circuit incarnation retirement rejects delayed GPU completion.
 ++scene.mImpl->epoch;assert(!next->current());next->publish(99);assert(scene.mImpl->avatars[100]->measured==3);root=*scene.object(1,1);++LLDrawable::frame;scene.budget=false;int allocations=scene.allocations;assert(!scene.avatarTiming(root));assert(scene.allocations==allocations+1);scene.budget=true;child=*scene.object(1,2);assert(!scene.avatarTiming(child));assert(scene.allocations==allocations+1);++LLDrawable::frame;assert(scene.avatarTiming(child));scene.mImpl->live=false;assert(!scene.avatarTiming(root));
}
