#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <set>
using F64=double;constexpr int LL_PCODE_VOLUME=9,LL_PCODE_LEGACY_AVATAR=47;
struct LLUUID{int value=0;bool notNull()const{return value!=0;}friend bool operator<(LLUUID a,LLUUID b){return a.value<b.value;}};
struct LLFrameTimer{static double time;static double getTotalSeconds(){return time;}};double LLFrameTimer::time=0;
struct LLMuteList{std::set<LLUUID>muted;int reads=0;static LLMuteList&instance(){static LLMuteList value;return value;}bool isMuted(LLUUID id){++reads;return muted.count(id);}};
struct LLPipeline{static bool sRenderAttachedParticles;};bool LLPipeline::sRenderAttachedParticles=true;
struct Avatar{bool complex=false,slow=false;bool isTooComplex(){return complex;}bool isTooSlow(){return slow;}};
struct World{struct Token{LLUUID id;int epoch;};struct Metadata{int pcode=9;bool attachment=false;};int epoch=1;bool present=true;Metadata metadata;Avatar avatar;std::optional<Token>object(LLUUID id){if(!present)return{};return Token{id,epoch};}std::function<bool()>guard(Token t){return[this,t]{return present&&epoch==t.epoch;};}std::optional<Metadata>objectAdmission(Token t){if(!present||epoch!=t.epoch)return{};return metadata;}Avatar*avatarForObject(LLUUID){return&avatar;}}world;
World*particleWorld(){return&world;}
struct VSParticleEffects{struct Impl{bool live=true;bool current(){return live;}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::function<bool()>sourcePolicy(const LLUUID&,const LLUUID&);};
POLICY
int main(){for(unsigned bits=0;bits<64;++bits){VSParticleEffects service;LLFrameTimer::time=0;world.present=true;++world.epoch;world.metadata.pcode=bits&1?47:9;world.metadata.attachment=bits&2;world.avatar.complex=bits&4;world.avatar.slow=bits&8;LLPipeline::sRenderAttachedParticles=!(bits&16);auto&muted=LLMuteList::instance().muted;muted.clear();if(bits&32)muted.insert({1});auto policy=service.sourcePolicy({1},{2});bool expected=!(world.metadata.pcode==47&&(bits&32))&&!(world.metadata.pcode==9&&((world.metadata.attachment&&!LLPipeline::sRenderAttachedParticles)||(world.avatar.complex&&world.avatar.slow)));assert(policy()==expected);service.mImpl->live=false;assert(!policy());service.mImpl->live=true;++world.epoch;assert(!policy());}
 VSParticleEffects service;world.present=true;world.metadata={47,false};LLPipeline::sRenderAttachedParticles=true;world.avatar={false,false};LLFrameTimer::time=0;auto&mute=LLMuteList::instance();mute.muted.clear();auto policy=service.sourcePolicy({1},{2});assert(policy());auto reads=mute.reads;mute.muted.insert({2});LLFrameTimer::time=.5;assert(policy()&&mute.reads==reads);LLFrameTimer::time=1.1;assert(!policy());mute.muted.clear();LLFrameTimer::time=2.2;assert(policy());world.present=false;assert(!policy());
 auto absent=service.sourcePolicy({1},{2});assert(!absent());
}
