// Genuine shared PartSim simulation and native graphics owner. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsparticleeffects.h"
#include "vsnativesession.h"
#include "vsobjectcontext.h"
#include "vsregioncontext.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llviewerpartsim.h"
#include "llviewerpartsource.h"
#include "llviewertexturelist.h"
#include "llviewercamera.h"
#include "llhudmanager.h"
#include "llhudeffecttrail.h"
#include "llhudobject.h"
#include "llagent.h"
#include "llprimitive.h"
#include "llagentcamera.h"
#include "llstrider.h"
#include "vsworldavatar.h"
#include "vsparticlegeometry.h"
#include "vsparticlebounds.h"
#include "lldrawable.h"
#include "vsparticlepick.h"
#include "llmutelist.h"
#include "pipeline.h"
#include <algorithm>
#include <map>

namespace
{
VSWorldScene* particleWorld()
{
    return gViewerWindow && gViewerWindow->isNativeVulkan() && gViewerWindow->nativeContext()
        ?&gViewerWindow->nativeContext()->world():nullptr;
}
VSParticleEffects* particleOwner(bool create)
{
    const auto owner=VSNativeSession::active();
    return owner?(create?owner->particles():owner->existingParticles()):nullptr;
}
}

struct VSParticleGroupState
{
    std::weak_ptr<VSNativeSession> owner;
    VSRegionContext region;
    std::function<bool()> account;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    std::vector<VSWorldRenderer::Packet> publication;
    std::shared_ptr<const VSResourceBudget::Reservation> publicationResidency;
    bool hud=false;
    U32 visibleFrame=0;
    F32 scale=1.f;
    LLVector3d publicationOrigin;
};

struct VSParticleEffects::Impl
{
    struct Source
    {
        LLPointer<LLViewerPartSource> source;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::function<void()> refresh;
    };
    struct Script
    {
        VSWorldScene::ObjectHandle object;
        LLPointer<LLViewerPartSourceScript> source;
        U64 revision=0;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::weak_ptr<VSNativeSession> owner;
    std::function<bool()> account;
    U64 revision=0;
    std::map<LLUUID,Script> scripts;
    std::map<const LLViewerPart*,std::shared_ptr<const VSResourceBudget::Reservation>> particles;
    std::map<const LLViewerPartSource*,Source> sources;
    LLVector3d origin;
    bool retired=false;
    boost::signals2::scoped_connection attachedParticlesSetting;
    explicit Impl(std::weak_ptr<VSNativeSession> value):owner(std::move(value)),origin(gAgent.getPosGlobalFromAgent(LLVector3::zero))
    {
        if(const auto session=owner.lock())account=session->accountGuard();
        LLViewerPartSim::instance();
        LLPipeline::sRenderAttachedParticles=gSavedSettings.getBOOL("RenderAttachedParticles");
        attachedParticlesSetting=gSavedSettings.getControl("RenderAttachedParticles")->getSignal()->connect(
            [](LLControlVariable*,const LLSD& value,const LLSD&){LLPipeline::sRenderAttachedParticles=value.asBoolean();});
        if(!LLViewerFetchedTexture::sDefaultParticleImagep)
            LLViewerFetchedTexture::sDefaultParticleImagep=LLViewerTextureManager::getFetchedTextureFromFile("pixiesmall.j2c");
    }
    bool current()const
    {return !retired && account && account() && owner.lock()==VSNativeSession::active();}
};

VSParticleEffects::VSParticleEffects(std::weak_ptr<VSNativeSession> owner):mImpl(std::make_shared<Impl>(std::move(owner))){}
VSParticleEffects::~VSParticleEffects(){reset();}

bool VSParticleEffects::admit(LLViewerPart* part)
{
    const auto state=mImpl;auto* world=particleWorld();
    if(!part || !state->current() || !world)return false;
    if(state->particles.count(part))return true;
    auto residency=world->reserveSpatialBytes(sizeof(LLViewerPart)+sizeof(decltype(state->particles)::value_type));
    if(!residency || !state->current())return false;
    state->particles.emplace(part,std::move(residency));return true;
}
void VSParticleEffects::release(const LLViewerPart* part){mImpl->particles.erase(part);}

bool VSParticleEffects::source(LLPointer<LLViewerPartSource> source)
{
    const auto state=mImpl;auto* world=particleWorld();
    if(!source || !state->current() || !world)return false;
    if(state->sources.count(source.get()))return true;
    const std::size_t bytes=dynamic_cast<LLViewerPartSourceScript*>(source.get())?sizeof(LLViewerPartSourceScript):
        dynamic_cast<LLViewerPartSourceBeam*>(source.get())?sizeof(LLViewerPartSourceBeam):
        dynamic_cast<LLViewerPartSourceSpiral*>(source.get())?sizeof(LLViewerPartSourceSpiral):sizeof(LLViewerPartSourceChat);
    auto residency=world->reserveSpatialBytes(bytes+sizeof(decltype(state->sources)::value_type));
    if(!residency || !state->current())return false;
    state->sources.emplace(source.get(),Impl::Source{source,std::move(residency),{}});return true;
}
std::function<bool()> VSParticleEffects::sourcePolicy(const LLUUID& source,const LLUUID& owner)
{
    const auto state=mImpl;auto* world=particleWorld();
    const auto token=world?world->object(source):std::nullopt;
    if(!token)return []{return false;};
    struct Policy {F64 checked=-1;bool muted=false;};
    auto policy=std::make_shared<Policy>();const auto guard=world->guard(*token);
    const std::weak_ptr<Impl> weak=state;
    return [weak,policy,guard,token=*token,owner]
    {
        const auto state=weak.lock();auto* world=particleWorld();
        if(!state || !state->current() || !guard() || !world)return false;
        const auto object=world->objectAdmission(token);if(!object)return false;
        const F64 now=LLFrameTimer::getTotalSeconds();
        if(policy->checked<0 || now-policy->checked>1.)
        {
            policy->checked=now;
            policy->muted=(object->pcode==LL_PCODE_LEGACY_AVATAR && LLMuteList::instance().isMuted(token.id)) ||
                (owner.notNull() && LLMuteList::instance().isMuted(owner));
        }
        if(policy->muted)return false;
        if(object->pcode==LL_PCODE_VOLUME)
        {
            if(object->attachment && !LLPipeline::sRenderAttachedParticles)return false;
            if(auto* avatar=world->avatarForObject(token.id))
                if(avatar->isTooComplex() && avatar->isTooSlow())return false;
        }
        return true;
    };
}
bool VSParticleEffects::positionSource(LLPointer<LLViewerPartSource> value,std::function<std::optional<LLVector3>()> query,const LLVector3& initial,std::function<bool()> allowed)
{
    if(!value)return false;
    const auto state=mImpl;auto* world=particleWorld();if(!state->current() || !world)return false;
    struct Pose
    {
        std::optional<LLVector3> position;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    auto reservation=world->reserveSpatialBytes(sizeof(Pose)+sizeof(query)+sizeof(VSWorldScene::ObjectHandle)+sizeof(VSObjectContext));
    if(!reservation || !state->current())return false;
    auto pose=std::make_shared<Pose>();pose->position=initial;pose->residency=std::move(reservation);
    const std::weak_ptr<Impl> weak=state;
    LLViewerPartSource::NativePosition read=[weak,pose]()->std::optional<LLVector3>
    {const auto state=weak.lock();return state && state->current()?pose->position:std::nullopt;};
    std::function<void()> refresh=[weak,pose,query=std::move(query)]
    {
        const auto state=weak.lock();if(!state || !state->current()){pose->position.reset();return;}
        auto position=query();
        if(state->current())pose->position=std::move(position);else pose->position.reset();
    };
    // Allocate/validate the complete publication before registering its actual
    // source. Refusal cannot strand an unqueued live source in the registry.
    if(!source(value))return false;
    value->setNativePosition(std::move(read));
    value->mNativeAllowed=std::move(allowed);
    state->sources.at(value.get()).refresh=std::move(refresh);
    return true;
}

std::shared_ptr<VSParticleGroupState> VSParticleEffects::group(const LLVector3& center,F32 side,bool hud)
{
    const auto state=mImpl;auto* world=particleWorld();
    if(!state->current() || !world || !center.isFinite() || !std::isfinite(side))return {};
    auto region=vs_region_context(gAgent.getPosGlobalFromAgent(center));
    if(!region)region=vs_current_region_context(); // Exact reference agent-region fallback.
    if(!region)return {};
    auto residency=world->reserveSpatialBytes(sizeof(LLViewerPartGroup)+sizeof(VSParticleGroupState));
    if(!residency || !state->current() || !region->current())return {};
    auto value=std::make_shared<VSParticleGroupState>();
    value->owner=state->owner;value->region=*region;value->account=state->account;value->residency=std::move(residency);value->hud=hud;
    return value;
}

void VSParticleEffects::tick()
{
    const auto state=mImpl;if(!state->current())return;
    reconcile();if(!state->current())return;
    const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    if(origin!=state->origin)
    {
        // Same reference origin rebasing, including all live particles, source
        // targets, skipped-time coordinates and group bounds.
        LLViewerPartSim::instance().shift(LLVector3(state->origin-origin));state->origin=origin;
    }
    if(LLHUDManager::instanceExists())LLHUDManager::instance().updateNativeParticles();
    for(auto& row:state->sources)
    {
        if(row.second.refresh)row.second.refresh();
        if(!state->current())return;
    }
    // Keep the actual reference source traversal/random admission and group
    // state transition; hidden drawing and FreezeTime do not stop simulation.
    LLViewerPartSim::instance().updateSimulation();
    for(auto it=state->sources.begin();it!=state->sources.end();)
    {
        // Retain its residency through actual particle callbacks retaining the
        // real source. The final registry reference is not a second simulator.
        if(it->second.source->isDead() && it->second.source->getNumRefs()==1)it=state->sources.erase(it);
        else ++it;
    }
}

void VSParticleEffects::reset()
{
    const auto state=mImpl;if(state->retired)return;
    state->retired=true;state->attachedParticlesSetting.disconnect();
    if(LLHUDManager::instanceExists())LLHUDManager::instance().retireNativeEffects();
    if(LLViewerPartSim::instanceExists())LLViewerPartSim::instance().destroyClass();
    state->scripts.clear();state->sources.clear();state->particles.clear();
}

bool vs_native_particle_peer(){return gViewerWindow && gViewerWindow->isNativeVulkan();}
bool vs_native_particle_admit(LLViewerPart* part){auto* owner=particleOwner(true);return owner && owner->admit(part);}
void vs_native_particle_release(const LLViewerPart* part){if(auto* owner=particleOwner(false))owner->release(part);}
bool vs_native_particle_source(LLPointer<LLViewerPartSource> source){auto* owner=particleOwner(true);return owner && owner->source(source);}
std::shared_ptr<VSParticleGroupState> vs_native_particle_group(const LLVector3& center,F32 side,bool hud)
{auto* owner=particleOwner(true);return owner?owner->group(center,side,hud):nullptr;}
bool vs_native_particle_group_current(const std::shared_ptr<VSParticleGroupState>& value)
{return value && value->owner.lock()==VSNativeSession::active() && value->account && value->account() && value->region.current();}
LLVector3 vs_native_particle_wind(const std::shared_ptr<VSParticleGroupState>& value,const LLVector3& position)
{
    if(!vs_native_particle_group_current(value))return LLVector3::zero;
    auto* world=particleWorld();if(!world)return LLVector3::zero;
    return world->windVelocity(value->region.getHost(),value->region.getPosRegionFromGlobal(gAgent.getPosGlobalFromAgent(position))).value_or(LLVector3::zero);
}

void vs_native_chat_particles(const VSObjectContext& context,const LLColor4& color,const LLUUID& owner)
{
    const auto session=VSNativeSession::active();auto* world=particleWorld();
    if(!session || !world || !context.current || !context.current())return;
    auto* particles=session->particles();if(!particles)return;
    const auto token=world->object(context.id);if(!token)return;
    const auto guard=world->guard(*token),account=session->accountGuard();
    const auto query=[token=*token,guard,account]()->std::optional<LLVector3>
    {
        if(!account() || !guard())return {};
        auto* world=particleWorld();if(!world)return {};
        const auto object=world->focusObject(token,gAgent.getPosGlobalFromAgent(LLVector3::zero));
        return object?std::optional<LLVector3>(object->renderPosition):std::nullopt;
    };
    const auto position=query();if(!position)return;
    LLPointer<LLViewerPartSourceChat> source=new LLViewerPartSourceChat(*position);
    source->setColor(color);source->setOwnerUUID(owner);source->mNativeSourceID=context.id;
    // The source represents this exact real avatar/object, not a GL stand-in.
    // Preserve the reference one-second mute cache; ordinary avatar sources
    // test their avatar UUID, non-avatar sources test the actual source owner.
    struct MuteState { F64 checked=-1;bool muted=false; };
    auto mute=std::make_shared<MuteState>();
    const bool avatar=context.pcode==LL_PCODE_LEGACY_AVATAR;
    const LLUUID identity=avatar?context.id:owner;
    auto allowed=[guard,account,mute,identity,context]()->bool
    {
        if(!account() || !guard())return false;
        const F64 now=LLFrameTimer::getTotalSeconds();
        if(mute->checked<0 || now-mute->checked>1.)
        {
            mute->checked=now;mute->muted=identity.notNull() && LLMuteList::getInstance()->isMuted(identity);
        }
        if(mute->muted)return false;
        if(context.pcode==LL_PCODE_VOLUME)
        {
            if(context.attachment && !LLPipeline::sRenderAttachedParticles)return false;
            if(auto* world=particleWorld())
                if(auto* avatar=world->avatarForObject(context.id))
                    if(avatar->isTooComplex() && avatar->isTooSlow())return false;
        }
        return true;
    };
    if(particles->positionSource(source,query,*position,std::move(allowed)))LLViewerPartSim::instance().addPartSource(source);
}


void VSParticleEffects::append(VSWorldRenderer& renderer,VSUIResources& resources,VSWorldScene& world,std::vector<VSWorldRenderer::Packet>& output,bool normalView)
{
    const auto state=mImpl;if(!state->current())return;
    // Reference visibility affects drawing alone; tick() always simulates.
    if(!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_PARTICLES))return;
    const LLVector3 worldEye=gAgentCamera.getCameraPositionAgent();
    const LLVector3 normal=-LLViewerCamera::getInstance()->getXAxis();
    const LLVector3d origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    const F32 maxArea=LLViewerPartSim::getMaxPartCount()*.02f;
    for(auto* group:LLViewerPartSim::instance().getParticleGroups())
    {
        const auto publication=group->mNativeGroup;
        if(!group->mNative || !vs_native_particle_group_current(publication))continue;
        const LLVector3 eye=group->mHud?LLVector3(-1,0,0):worldEye;
        if(!group->mHud)
        {
            LLVector4a center,radius;center.load3(group->getCenterAgent().mV);
            radius.splat(publication->scale+group->getBoxSide()*.5f);
            if(!LLViewerCamera::getInstance()->AABBInFrustum(center,radius))continue;
        }
        if(normalView)publication->visibleFrame=LLDrawable::getCurrentFrame();
        const auto appendPublication=[&]
        {
            if(!state->current() || !vs_native_particle_group_current(publication))return;
            const LLVector3 offset(publication->publicationOrigin-origin);
            for(auto packet:publication->publication)
            {
                if(!packet.hud)packet.regionOffset={offset.mV[0],offset.mV[1],offset.mV[2],0};
                output.push_back(std::move(packet));
            }
        };
        const std::size_t count=group->mParticles.size();
        // Charge staging arrays before allocating geometry/sort publications.
        auto temporary=world.reserveSpatialBytes(std::max<std::size_t>(1,count)*(8*sizeof(VSWorldRenderer::Vertex)+12*sizeof(unsigned)+sizeof(LLViewerPart*)+2*sizeof(VSWorldRenderer::Packet)));
        if(!temporary){appendPublication();continue;}
        struct Batch
        {
            VSWorldRenderer::Packet packet;
            std::vector<VSWorldRenderer::Vertex> vertices;
            std::vector<unsigned> indices;
        };
        std::vector<const LLViewerPart*> visible;visible.reserve(count);
        F32 totalArea=0,maxScale=0;
        bool complete=true;
        for(const auto* part:group->mParticles)
        {
            maxScale=llmax(maxScale,vs_particle_scale(*part));
            const F32 distance=(part->mPosAgent-eye).lengthSquared();
            const F32 area=part->mScale.mV[0]*part->mScale.mV[1]*(distance>1?1/distance:1);
            totalArea=llmax(totalArea,area); // Reference intentionally keeps the maximum.
            if(totalArea>maxArea)break;
            if(distance>25.f && area<.005f*.005f)continue;
            visible.push_back(part);
        }
        std::stable_sort(visible.begin(),visible.end(),[&](auto* a,auto* b)
        {return (a->mPosAgent-eye)*LLViewerCamera::getInstance()->getAtAxis()>(b->mPosAgent-eye)*LLViewerCamera::getInstance()->getAtAxis();});
        std::vector<Batch> batches,glowBatches;Batch glow;glow.packet.alpha=true;glow.packet.fullbright=true;glow.packet.hud=group->mHud;
        glow.packet.glowOnly=true;glow.packet.overlay=true;glow.packet.blend=VSWorldRenderer::Blend{LLRender::BF_ZERO,LLRender::BF_ONE,LLRender::BF_ONE,LLRender::BF_ONE,8};
        glow.packet.color={0,0,0,1};
        for(const auto* part:visible)
        {
            if(!complete)break;
            VSUIRenderer::Image image;
            if(part->mImagep==LLViewerFetchedTexture::sDefaultParticleImagep)
                image=vs_ui_named_image_snapshot("pixiesmall.j2c");
            else if(part->mImagep)
            {
                const F32 distance=(part->mPosAgent-eye).lengthSquared();
                const F32 area=part->mScale.mV[0]*part->mScale.mV[1]/llmax(distance,1.f);
                const unsigned demand=unsigned(llclamp(std::ceil(std::sqrt(llmax(area,0.f)*10.f)*LLViewerCamera::getInstance()->getPixelMeterRatio()),1.f,4096.f));
                vs_request_world_image(part->mImagep->getID(),demand);
                image=vs_ui_image_snapshot(part->mImagep->getID());
            }
            if(!image)image=resources.defaultWorldImage(); // Same shared pending/missing fetched-texture fallback as the world.
            if(!image){complete=false;break;}
            const bool fullbright=part->mFlags&LLPartData::LL_PART_EMISSIVE_MASK;
            if(batches.empty() || batches.back().packet.image!=image || batches.back().packet.fullbright!=fullbright || batches.back().packet.blend->source!=part->mBlendFuncSource || batches.back().packet.blend->destination!=part->mBlendFuncDest)
            {
                batches.emplace_back();auto& packet=batches.back().packet;
                packet.image=image;packet.imageSampler.u=packet.imageSampler.v=VSWorldContracts::AddressMode::Clamp;packet.imageSampler.mipmaps=true;packet.alpha=true;packet.fullbright=fullbright;packet.hud=group->mHud;
                packet.distanceSquared=(part->mPosAgent-eye).lengthSquared();
                packet.blend=VSWorldRenderer::Blend{part->mBlendFuncSource,part->mBlendFuncDest,LLRender::BF_ZERO,LLRender::BF_ONE_MINUS_SOURCE_ALPHA,15};
            }
            auto& batch=batches.back();
            LLVector4a positions[4];auto* pointer=positions;
            vs_particle_geometry(*part,eye,pointer,[&](){return part->mPartSourcep->sourceRotation();});
            const auto base=unsigned(batch.vertices.size());
            // Original GL UV is (0,1),(0,0),(1,1),(1,0); canonical native images reverse rows once.
            const float u[]{0,0,1,1},v[]{0,1,0,1};
            for(unsigned i=0;i<4;++i)
            {
                const auto* position=positions[i].getF32ptr();
                const LLColor4U color=(part->mFlags&LLPartData::LL_PART_RIBBON_MASK) && i<2?
                    LLColor4U(part->mParent?part->mParent->mColor:part->mStartColor):LLColor4U(part->mColor);
                VSWorldRenderer::Vertex vertex{position[0],position[1],position[2],normal.mV[0],normal.mV[1],normal.mV[2],u[i],v[i]};
                vertex.r=color.mV[0]/255.f;vertex.g=color.mV[1]/255.f;vertex.b=color.mV[2]/255.f;vertex.a=color.mV[3]/255.f;
                batch.vertices.push_back(vertex);
            }
            for(unsigned index:{0u,1u,2u,1u,3u,2u})batch.indices.push_back(base+index);
            const U8 parentGlow=(part->mFlags&LLPartData::LL_PART_RIBBON_MASK)?
                (part->mParent?part->mParent->mGlow.mV[3]:U8(ll_round(part->mStartGlow*255.f))):part->mGlow.mV[3];
            if(parentGlow || part->mGlow.mV[3])
            {
                // Match deferred/emissiveF.glsl: RGB untouched; alpha +=
                // diffuse alpha * quantized per-end glow. Post-process bloom
                // consumes this genuine emissive channel at its normal stage.
                if(glow.packet.image && glow.packet.image!=image)
                {glowBatches.push_back(std::move(glow));glow=Batch{};glow.packet=glowBatches.back().packet;}
                glow.packet.image=image;
                const auto glowBase=unsigned(glow.vertices.size());
                for(unsigned i=0;i<4;++i)
                {
                    auto vertex=batch.vertices[base+i];vertex.r=vertex.g=vertex.b=0;
                    vertex.a=(i<2?parentGlow:part->mGlow.mV[3])/255.f;glow.vertices.push_back(vertex);
                }
                for(unsigned index:{0u,1u,2u,1u,3u,2u})glow.indices.push_back(glowBase+index);
            }
        }
        if(!glow.vertices.empty())glowBatches.push_back(std::move(glow));
        for(auto& emission:glowBatches)batches.push_back(std::move(emission));
        std::vector<VSWorldRenderer::Packet> accepted;
        auto publicationResidency=world.reserveSpatialBytes(std::max<std::size_t>(1,batches.size())*sizeof(VSWorldRenderer::Packet));
        if(!publicationResidency)complete=false;
        if(complete)
        {
            accepted.reserve(batches.size());
            for(auto& batch:batches)
            {
                batch.packet.mesh=renderer.upload(batch.vertices,batch.indices,VSWorldRenderer::Topology::Triangles,
                    VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0|VSWorldContracts::Color);
                if(!batch.packet.mesh){complete=false;break;}
                accepted.push_back(std::move(batch.packet));
            }
        }
        if(complete && state->current() && vs_native_particle_group_current(publication))
        {publication->publication=std::move(accepted);publication->publicationResidency=std::move(publicationResidency);publication->publicationOrigin=origin;publication->scale=maxScale;}
        if(!state->current() || !vs_native_particle_group_current(publication))return;
        appendPublication();
    }
}


void VSParticleEffects::reconcile()
{
    const auto state=mImpl;auto* world=particleWorld();
    if(!state->current() || !world || state->revision==world->particleRevision())return;
    const auto revision=world->particleRevision();
    const auto temporary=world->reserveSpatialBytes(std::max<std::size_t>(1,world->particleSystemCount())*sizeof(VSWorldScene::ParticleSystem));
    if(!temporary)return; // The real authoritative index retries next tick.
    const auto received=world->particleSystems();
    bool complete=true;
    for(auto it=state->scripts.begin();it!=state->scripts.end();)
    {
        const auto found=std::find_if(received.begin(),received.end(),[&](const auto& entry)
        {return entry.object.id==it->first && entry.object.generation==it->second.object.generation && entry.object.host==it->second.object.host && entry.object.local==it->second.object.local && world->current(entry.object);});
        if(found==received.end()){it->second.source->setDead();it=state->scripts.erase(it);}
        else ++it;
    }
    for(const auto& entry:received)
    {
        if(!state->current() || !world->current(entry.object))return;
        const auto found=state->scripts.find(entry.object.id);
        if(found!=state->scripts.end() && found->second.revision==entry.revision)continue;
        LLPointer<LLViewerPartSourceScript> source;
        if(found!=state->scripts.end() && !found->second.source->isDead())source=found->second.source;
        else
        {
            if(LLMuteList::instance().isMuted(entry.owner,LLMute::flagParticles))continue;
            const auto position=world->particlePosition(entry.object);if(!position){complete=false;continue;}
            const auto residency=world->reserveSpatialBytes(sizeof(Impl::Script)+sizeof(decltype(state->scripts)::value_type));
            if(!residency){complete=false;continue;}
            source=new LLViewerPartSourceScript(*position);source->setOwnerUUID(entry.owner);source->mNativeSourceID=entry.object.id;
            struct Frame
            {
                std::optional<LLVector3> position,target;
                std::optional<LLQuaternion> rotation;
                bool hud=false,allowed=true,appearance=true;
                F64 muteTime=-1;bool muted=false;
                std::shared_ptr<const VSResourceBudget::Reservation> residency;
            };
            auto pose=std::make_shared<Frame>();pose->residency=world->reserveSpatialBytes(sizeof(Frame));
            if(!pose->residency){complete=false;continue;}
            const auto token=entry.object;const auto account=state->account;
            const std::weak_ptr<Impl> weak=state;
            const auto current=[weak,token,account]
            {const auto state=weak.lock();auto* world=particleWorld();return state && state->current() && account() && world && world->current(token);};
            source->mNativePosition=[pose,current]()->std::optional<LLVector3>{return current()?pose->position:std::nullopt;};
            source->mNativeRotation=[pose,current]()->std::optional<LLQuaternion>{return current()?pose->rotation:std::nullopt;};
            source->mNativeTarget=[pose,current]()->std::optional<LLVector3>{return current()?pose->target:std::nullopt;};
            source->mNativeAllowed=[pose,current]{return current() && pose->allowed;};
            source->mNativeAppearanceAllowed=[pose,current]{return current() && pose->appearance;};
            if(!this->source(source)){source->setDead();complete=false;continue;}
            // Non-owning source pointer: the enclosing registry's LLPointer is
            // the lifetime owner; refresh cannot outlive that real source.
            auto* raw=source.get();
            std::function<void()> refresh=[weak,pose,current,token,raw]
            {
                const auto state=weak.lock();auto* world=particleWorld();
                if(!state || !current() || !world){pose->position.reset();pose->rotation.reset();pose->target.reset();raw->setDead();return;}
                const auto object=world->focusObject(token,gAgent.getPosGlobalFromAgent(LLVector3::zero));
                const auto admission=world->objectAdmission(token);
                if(!object || !admission){pose->position.reset();return;}
                pose->position=object->renderPosition;pose->rotation=object->renderRotation;pose->hud=admission->hud;raw->mNativeHUD=pose->hud;
                const auto now=LLFrameTimer::getTotalSeconds();
                if(pose->muteTime<0 || now-pose->muteTime>1.)
                {pose->muteTime=now;pose->muted=raw->getOwnerUUID().notNull() && LLMuteList::instance().isMuted(raw->getOwnerUUID());}
                pose->allowed=!pose->muted && (LLPipeline::sRenderAttachedParticles || !admission->attachment);
                if(admission->pcode==LL_PCODE_VOLUME)
                    if(auto* avatar=world->avatarForObject(token.id))
                        if(avatar->isTooComplex() && avatar->isTooSlow())pose->allowed=false;
                pose->target.reset();
                if(raw->mPartSysData.mTargetUUID.notNull())
                    if(const auto target=world->object(raw->mPartSysData.mTargetUUID))pose->target=world->particlePosition(*target);
                pose->appearance=true;
                if(auto* avatar=world->avatarForObject(raw->getOwnerUUID()))pose->appearance=avatar->particleAppearanceNormal();
            };
            state->sources.at(source.get()).refresh=std::move(refresh);
            state->scripts[entry.object.id]=Impl::Script{entry.object,source,0,residency};
            LLViewerPartSim::instance().addPartSource(source);
        }
        source->setNativeParameters(entry.data,entry.fullMessage);
        LLViewerFetchedTexture* const image=entry.data.mPartImageID.isNull()?LLViewerFetchedTexture::sDefaultParticleImagep.get():
            LLViewerTextureManager::getFetchedTexture(entry.data.mPartImageID);
        source->setImage(image);
        state->scripts.at(entry.object.id).revision=entry.revision;
    }
    if(complete && state->current() && world->particleRevision()==revision)state->revision=revision;
}


LLPointer<LLViewerPartSourceBeam> VSParticleEffects::beam(const LLUUID& sourceID,const LLUUID& targetID,const LLVector3d& targetGlobal,const LLColor4& color)
{
    const auto state=mImpl;auto* world=particleWorld();if(!world || !state->current())return {};
    const auto sourceObject=world->object(sourceID);
    const auto targetObject=targetID.notNull()?world->object(targetID):std::nullopt;
    if(!sourceObject || (targetID.notNull() && !targetObject))return {};
    const auto sourceGuard=world->guard(*sourceObject);
    const auto targetGuard=targetObject?world->guard(*targetObject):std::function<bool()>([]{return true;});
    const auto account=state->account;
    const auto sourceQuery=[sourceObject=*sourceObject,sourceGuard,account]()->std::optional<LLVector3>
    {
        auto* world=particleWorld();return world && account() && sourceGuard()?world->particlePosition(sourceObject,true):std::nullopt;
    };
    const auto initial=sourceQuery();if(!initial)return {};
    LLPointer<LLViewerPartSourceBeam> value=new LLViewerPartSourceBeam;
    value->setColor(color);value->setOwnerUUID(gAgent.getID());value->mLKGTargetPosGlobal=targetGlobal;value->mNativeSourceID=sourceID;
    const auto policy=sourcePolicy(sourceID,gAgent.getID());
    if(!positionSource(value,sourceQuery,*initial,[sourceGuard,targetGuard,account,policy]{return account() && sourceGuard() && targetGuard() && policy();}))return {};
    struct Target
    {
        std::optional<LLVector3> position;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    auto target=std::make_shared<Target>();target->residency=world->reserveSpatialBytes(sizeof(Target)+sizeof(VSWorldScene::ObjectHandle));
    if(!target->residency){value->setDead();return {};}
    value->mNativeTarget=[target,account,targetGuard]()->std::optional<LLVector3>
    {return account() && targetGuard()?target->position:std::nullopt;};
    const auto old=state->sources.at(value.get()).refresh;
    state->sources.at(value.get()).refresh=[old,target,targetObject,targetGuard,account]
    {
        old();auto* world=particleWorld();
        target->position=world && account() && targetGuard() && targetObject?world->particlePosition(*targetObject):std::nullopt;
    };
    state->sources.at(value.get()).refresh();
    LLViewerPartSim::instance().addPartSource(value);return value;
}

void vs_native_beam_effect(const LLUUID& source,const LLUUID& target,const LLColor4U& color,F32 duration)
{
    const auto owner=VSNativeSession::active();if(!owner || owner->phase()!=VSNativeSession::Phase::Connected)return;
    auto* effect=static_cast<LLHUDEffectSpiral*>(LLHUDManager::instance().createViewerEffect(LLHUDObject::LL_HUD_EFFECT_BEAM,true));
    if(!effect)return;
    if(!effect->setNativeEndpoints(source,target)){effect->markDead();return;}
    effect->setDuration(duration);effect->setColor(color);
    // Same controller as DND: send the ordinary ViewerEffect; the authenticated
    // simulator echo triggers the actual shared local source, never a substitute.
}

bool VSParticleEffects::pick(const LLVector3& begin,const LLVector3& end,LLUUID& ownerID,LLUUID& sourceID) const
{
    const auto state=mImpl;if(!state->current() || !gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_PARTICLES))return false;
    LLVector4a start,nearest;start.load3(begin.mV);nearest.load3(end.mV);
    const LLVector3 eye=gAgentCamera.getCameraPositionAgent();bool hit=false;
    for(auto* group:LLViewerPartSim::instance().getParticleGroups())
    {
        // Original lineSegmentIntersectParticle visits PARTITION_PARTICLE,
        // never PARTITION_HUD_PARTICLE. No alpha/material hit-test is added.
        if(!group->mNative || group->mHud || !vs_native_particle_group_current(group->mNativeGroup))continue;
        S32 face=-1;LLVector4a intersection;
        if(!vs_particle_intersect(group->mParticles,start,nearest,&face,&intersection,
            [&](const LLViewerPart& part,LLVector4a*& vertices)
            {vs_particle_geometry(part,eye,vertices,[&]{return part.mPartSourcep->sourceRotation();});}))continue;
        if(face<0 || std::size_t(face)>=group->mParticles.size())continue;
        const auto* part=group->mParticles[face];
        ownerID=part->mPartSourcep->getOwnerUUID();sourceID=part->mPartSourcep->mNativeSourceID;
        nearest=intersection;hit=true;
    }
    return hit;
}

bool vs_native_particle_group_visible(const std::shared_ptr<VSParticleGroupState>& value)
{return vs_native_particle_group_current(value) && value->visibleFrame>=U32(LLDrawable::getCurrentFrame());}
