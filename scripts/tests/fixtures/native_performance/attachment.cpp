#include <cassert>
#include <map>
#include <optional>
#include <memory>
#include <functional>
#include <array>
#include <algorithm>
#include <cmath>
using U32=unsigned;using U8=unsigned char;constexpr unsigned UUID_BYTES=16;
struct LLUUID {unsigned char mData[16]{};};struct LLDrawable {static U32 frame;static U32 getCurrentFrame(){return frame;}};U32 LLDrawable::frame=1;
struct VSWorldRenderer {struct AvatarTiming {std::shared_ptr<int> residency;std::array<unsigned char,16> identity{};std::function<bool()> current;std::function<void(float)> publish;};};
class VSWorldScene {public:
 struct ObjectHandle {U32 host=1,local=1,epoch=1;LLUUID id;};struct Attachment {ObjectHandle object;unsigned inventoryItem=1,point=5;};
 struct Impl {struct Object {unsigned parent=9,attachment=5,inventoryItem=1;std::shared_ptr<const VSWorldRenderer::AvatarTiming> attachmentTiming;std::optional<U32> attachmentTimingAttempt;std::optional<float> attachmentGpuMilliseconds;};unsigned host=1;bool live=true;unsigned epoch=1;std::map<U32,Object> objects;};
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();std::map<unsigned,std::unique_ptr<VSWorldScene>> mNeighbours;bool budget=true;unsigned reservations=0;
 std::optional<Attachment> objectAttachment(const ObjectHandle& token)const {if(!mImpl->live||token.epoch!=mImpl->epoch||!mImpl->objects.count(token.local))return{};return Attachment{token};}
 auto guard(ObjectHandle token){std::weak_ptr<Impl> weak=mImpl;return [weak,token]{auto p=weak.lock();return p&&p->live&&token.epoch==p->epoch;};}
 std::shared_ptr<int> reserveSpatialBytes(size_t){++reservations;return budget?std::make_shared<int>(1):nullptr;}
 std::shared_ptr<const VSWorldRenderer::AvatarTiming> attachmentTiming(const ObjectHandle&);
};
METHOD
int main(){VSWorldScene world;world.mImpl->objects.emplace(1,VSWorldScene::Impl::Object{});VSWorldScene::ObjectHandle token;token.id.mData[0]=57;
 world.budget=false;assert(!world.attachmentTiming(token)&&world.reservations==1);world.budget=true;assert(!world.attachmentTiming(token)&&world.reservations==1);++LLDrawable::frame;
 auto timing=world.attachmentTiming(token);assert(timing&&timing->current()&&timing->identity[0]==57&&world.reservations==2);assert(world.attachmentTiming(token)==timing&&world.reservations==2);
 timing->publish(-1);timing->publish(NAN);assert(!world.mImpl->objects.at(1).attachmentGpuMilliseconds);timing->publish(0);assert(world.mImpl->objects.at(1).attachmentGpuMilliseconds==0);
 world.mImpl->objects.at(1).parent=10;assert(!timing->current());timing->publish(3);assert(world.mImpl->objects.at(1).attachmentGpuMilliseconds==0);++LLDrawable::frame;
 auto second=world.attachmentTiming(token);assert(second&&second->current());world.mImpl->objects.at(1).inventoryItem=2;assert(!second->current());++LLDrawable::frame;auto third=world.attachmentTiming(token);assert(third&&third->current());
 world.mImpl->objects.at(1).attachment=6;assert(!third->current());++LLDrawable::frame;auto fourth=world.attachmentTiming(token);assert(fourth&&fourth->current());world.mImpl->epoch=2;assert(!fourth->current()&&!world.attachmentTiming(token));world.mImpl.reset();assert(!fourth->current());fourth->publish(4);
}
