// Transpose LLTerrainMaterials ownership without creating GL texture owners. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsterrainassets.h"
#include "vsterrainreadiness.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "vsnativesession.h"
#include "llgltfmateriallist.h"
#include "llfetchedgltfmaterial.h"
#include "llviewercontrol.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llframetimer.h"
#include "vsnormalbasis.h"
#include "workqueue.h"
#include <algorithm>
#include <cmath>
#include <utility>

struct VSTerrainAssets::Impl
{
    LLHost host;
    std::array<LLUUID,4> ids{};
    std::array<LLPointer<LLFetchedGLTFMaterial>,4> materials;
    std::array<LLPointer<LLGLTFMaterial>,4> overrides;
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t request;
    boost::signals2::signal<void()> changed;
    bool queried=false,retired=false;
    double retryAt=0;
    std::shared_ptr<const VSWorldRenderer::Terrain> published;
    explicit Impl(const LLHost& value):host(value) {}
    void query(const std::shared_ptr<Impl>& self)
    {
        const auto session=VSNativeSession::active();
        if(!session || queried || request || LLFrameTimer::getTotalSeconds()<retryAt) return;
        const auto url=session->capability(host,"ModifyRegion");
        if(url.empty()) return;
        retryAt=LLFrameTimer::getTotalSeconds()+15;
        request=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeTerrainOverrides",LLCore::HttpRequest::DEFAULT_POLICY_ID);
        const auto pending=request;
        const auto guard=session->accountGuard();
        const auto peer=host;
        const auto circuit=session->circuitGeneration(peer);
        const std::weak_ptr<Impl> weak=self;
        const std::weak_ptr<VSNativeSession> owner=session;
        LLCoros::instance().launch("nativeTerrainOverrides",[weak,owner,guard,peer,circuit,url,pending]()
        {
            auto http=std::make_shared<LLCore::HttpRequest>();
            auto options=std::make_shared<LLCore::HttpOptions>();
            options->setTransferTimeout(15);options->setRetries(0);options->setMaximumResponseBodyBytes(64*1024);
            const auto result=pending->getAndSuspend(http,url,options);
            LL::WorkQueue::getInstance("mainloop")->post([weak,owner,guard,peer,circuit,pending,result]()
            {
                const auto state=weak.lock();const auto session=owner.lock();
                if(!state || state->retired || state->request!=pending) return;
                state->request.reset();
                if(!guard() || !session || session->circuitGeneration(peer)!=circuit) return;
                if(!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]) ||
                   !result["success"].asBoolean() || !result["overrides"].isArray() || result["overrides"].size()!=4) return;
                std::array<LLPointer<LLGLTFMaterial>,4> candidate;
                for(unsigned layer=0;layer<4;++layer)
                {
                    const auto& row=result["overrides"][layer];
                    if(!row.isMap() && !row.isUndefined()) return;
                    candidate[layer]=new LLGLTFMaterial;
                    candidate[layer]->applyOverrideLLSD(row);
                }
                state->overrides=std::move(candidate);state->queried=true;
                state->changed();
            });
        });
    }
};
VSTerrainAssets::VSTerrainAssets(const LLHost& host):mImpl(std::make_shared<Impl>(host)) {}
VSTerrainAssets::~VSTerrainAssets()
{
    mImpl->retired=true;
    auto pending=std::exchange(mImpl->request,{});
    if(pending) pending->cancelSuspendedOperation();
}
std::pair<bool,bool> VSTerrainAssets::settings(const std::array<LLUUID,4>& ids,
    std::array<LLPointer<LLGLTFMaterial>,4>& overrides)
{
    mImpl->query(mImpl);
    bool texturesReady=false,materialsReady=false,defined=true;
    for(unsigned layer=0;layer<4;++layer)
    {
        const auto image=ids[layer].notNull()?vs_request_world_image(ids[layer],128):nullptr;
        texturesReady|=image && vs_terrain_texture_state(vs_ui_image_discard(ids[layer]),
            image->getWidth(),image->getHeight(),vs_ui_image_components(ids[layer]))==VSTerrainTextureState::Ready;
        if(mImpl->ids[layer]!=ids[layer])
        {
            mImpl->ids[layer]=ids[layer];
            mImpl->materials[layer]=ids[layer].notNull()?gGLTFMaterialList.getMaterial(ids[layer]):nullptr;
        }
        const auto& material=mImpl->materials[layer];
        defined=defined && bool(material);materialsReady|=material && material->isLoaded();
        // The accepted query replaces pointers; this snapshot retains the exact
        // immutable generation instead of reading the next simulator's overrides.
        overrides[layer]=mImpl->overrides[layer];
    }
    static LLCachedControl<bool> pbr(gSavedSettings,"RenderTerrainPBREnabled",false);
    static LLCachedControl<bool> force(gSavedSettings,"RenderTerrainPBRForce",false);
    if(pbr && force && defined)materialsReady=true;
    return {texturesReady,materialsReady};
}
boost::signals2::connection VSTerrainAssets::observeSettings(const boost::signals2::signal<void()>::slot_type& slot)
{
    return mImpl->changed.connect(slot);
}
void VSTerrainAssets::releaseImages() {mImpl->published.reset();}
std::shared_ptr<const VSWorldRenderer::Terrain> VSTerrainAssets::snapshot(const std::array<LLUUID,4>& ids,
    unsigned demand,unsigned regionWidth,VSUIResources& resources,const VSUIRenderer::Image& ramp)
{
    mImpl->query(mImpl);
    bool textureReady=false,materialReady=false,localReady=false;
    std::array<LLPointer<LLFetchedGLTFMaterial>,4> local;
    for(unsigned layer=0;layer<4;++layer)
    {
        if(mImpl->ids[layer]!=ids[layer])
        {
            mImpl->ids[layer]=ids[layer];
            mImpl->materials[layer]=ids[layer].notNull()?gGLTFMaterialList.getMaterial(ids[layer]):nullptr;
        }
        const auto& material=mImpl->materials[layer];
        materialReady|=material && material->isLoaded();
        const LLUUID localID(gSavedSettings.getString("LocalTerrainAsset"+std::to_string(layer+1)));
        if(localID.notNull()) local[layer]=gGLTFMaterialList.getMaterial(localID);
        localReady|=local[layer] && local[layer]->isLoaded();
        textureReady|=ids[layer].notNull() && bool(resources.image("remote:"+vs_native_image_id(ids[layer]).asString()));
    }
    // Preserve the reference's preference for a decoded legacy swatch. A local
    // terrain material override takes precedence once at least one asset loads.
    if(!localReady && (textureReady || !materialReady)) {mImpl->published.reset();return {};}
    VSWorldRenderer::Terrain candidate;
    candidate.detail=F32(llclamp(gSavedSettings.getS32("RenderTerrainPBRDetail"),-3,0));
    candidate.planar=gSavedSettings.getS32("RenderTerrainPBRPlanarSampleCount")==3?3.f:1.f;
    candidate.sharpness=std::max(gSavedSettings.getF32("RenderTerrainPBRTriplanarBlendFactor"),.01f);
    candidate.regionWidth=F32(regionWidth);
    candidate.blend=ramp;
    if(localReady && gSavedSettings.getBOOL("LocalTerrainPaintEnabled"))
    {
        candidate.paint=true;
        candidate.blend=resources.image("terrain:local-paint");
        if(!candidate.blend)
        {
            candidate.blend=resources.image("terrain:empty-paint");
            if(!candidate.blend)
            {
                LLPointer<LLImageRaw> black=new LLImageRaw(1,1,4);black->clear(0,0,0,255);
                resources.publish("terrain:empty-paint",*black);
                candidate.blend=resources.image("terrain:empty-paint");
            }
        }
    }
    const F32 scale=1.f/std::max(gSavedSettings.getF32("RenderTerrainPBRScale"),.001f);
    for(unsigned layer=0;layer<4;++layer)
    {
        const auto& asset=localReady?local[layer]:mImpl->materials[layer];
        LLGLTFMaterial value(asset && asset->isLoaded()?*asset:LLGLTFMaterial::sDefault);
        if(localReady)
        {
            LLGLTFMaterial override;
            auto& t=override.mTextureTransform[0];
            const auto prefix="LocalTerrainTransform"+std::to_string(layer+1);
            t.mScale.set(gSavedSettings.getF32(prefix+"ScaleU"),gSavedSettings.getF32(prefix+"ScaleV"));
            t.mRotation=gSavedSettings.getF32(prefix+"Rotation")*DEG_TO_RAD;
            t.mOffset.set(gSavedSettings.getF32(prefix+"OffsetU"),gSavedSettings.getF32(prefix+"OffsetV"));
            for(unsigned info=1;info<LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT;++info) override.mTextureTransform[info]=t;
            value.applyOverride(override);
        }
        else if(mImpl->overrides[layer]) value.applyOverride(*mImpl->overrides[layer]);
        auto& state=candidate.layers[layer];state=VSMaterial::pbr(value);
        for(auto& sampler:state.samplers) sampler.anisotropy=gSavedSettings.getBOOL("RenderAnisotropic")?16u:1u;
        // terrain_texture_transform converts V with a sign change, without the
        // unit translation used by an object's standard glTF UV origin.
        const auto& t=value.mTextureTransform[0];
        const F32 c=std::cos(t.mRotation),s=std::sin(t.mRotation);
        state.uv[0].matrix={c*t.mScale.mV[0]*scale,s*t.mScale.mV[0]*scale,
                           -s*t.mScale.mV[1]*scale,c*t.mScale.mV[1]*scale};
        state.uv[0].offset={t.mOffset.mV[0],-t.mOffset.mV[1]};
        state.normalTangent=VSNormalBasis::primitive(t.mScale.mV[0],t.mScale.mV[1],t.mRotation);
        for(unsigned channel=0;channel<4;++channel)
        {
            if((channel==VSMaterial::Normal && candidate.detail<-2) ||
               (channel==VSMaterial::Emissive && candidate.detail<0)) continue;
            LLUUID id;std::copy_n(state.textures[channel].begin(),UUID_BYTES,id.mData);
            if(id.isNull()) continue;
            vs_request_world_image(id,demand);
            candidate.images[layer][channel]=resources.image("remote:"+vs_native_image_id(id).asString());
        }
    }
    const auto& old=mImpl->published;
    if(old && old->layers==candidate.layers && old->images==candidate.images && old->blend==candidate.blend &&
        old->detail==candidate.detail && old->planar==candidate.planar && old->sharpness==candidate.sharpness &&
        old->regionWidth==candidate.regionWidth && old->paint==candidate.paint) return old;
    mImpl->published=std::make_shared<const VSWorldRenderer::Terrain>(std::move(candidate));
    return mImpl->published;
}
