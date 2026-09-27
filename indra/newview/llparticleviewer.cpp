// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llviewerprecompiledheaders.h"
#include "llparticleviewer.h"
#include "llparticlepipeline.h"
#include "llparticletexture.h"
#include "llviewerpartsim.h"
#include "llviewerpartsource.h"
#include "llviewertexture.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewercamera.h"
#include "llvoavatar.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llworld.h"
#include "pipeline.h"
#include "llglslshader.h"
#include "llrendertarget.h"
#include <glm/gtc/type_ptr.hpp>
#include <set>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <tuple>

namespace
{
using namespace LLParticlePipeline;
struct SourceOwner
{
    LLPointer<LLViewerPartSource> source;
    U32 generation = 0;
    double expires = 0;
    bool killed = false;
};
struct MaterialOwner
{
    LLPointer<LLViewerTexture> image;
    U32 blend = 0, flags = 0;
    double expires = 0;
    float textureArea = 4096.f;
};
std::vector<SourceOwner> owners;
std::vector<MaterialOwner> materials;
std::map<LLViewerPartSource*, U32> sourceSlots;
using MaterialKey = std::tuple<LLViewerTexture*, U32, U32>;
std::map<MaterialKey, U32> materialSlots;
std::map<LLImageGL*, std::unique_ptr<LLParticleTexture>> textures;
std::vector<Particle> births;
LLVector3 pendingShift;
double simulationTime = 0;
bool enabled = false, failed = false, materialsDirty = true;
GLuint materialBuffer = 0, demandReadback = 0;
GLsync demandFence = nullptr;
double lastDemandCopy = -1;
GLuint imageFBO = 0, copyFBO = 0, colorCopy = 0, depthCopy = 0, virtualDepth = 0, depthSampler = 0;
GLint targetWidth = 0, targetHeight = 0;
glm::mat4 viewMatrix(1.f);
void releaseTargets()
{
    GLuint framebuffers[] = {imageFBO,copyFBO}; glDeleteFramebuffers(2,framebuffers);
    GLuint images[] = {colorCopy,depthCopy,virtualDepth}; glDeleteTextures(3,images);
    if (depthSampler) glDeleteSamplers(1,&depthSampler);
    imageFBO=copyFBO=colorCopy=depthCopy=virtualDepth=depthSampler=0;
    targetWidth=targetHeight=0;
}
bool targets(GLint width, GLint height)
{
    if (width==targetWidth && height==targetHeight && imageFBO) return true;
    releaseTargets();
    glCreateTextures(GL_TEXTURE_2D,1,&virtualDepth);
    glTextureStorage2D(virtualDepth,1,GL_R32F,width,height);
    glCreateTextures(GL_TEXTURE_2D,1,&colorCopy);
    glTextureStorage2D(colorCopy,1,GL_RGBA16F,width,height);
    glCreateTextures(GL_TEXTURE_2D,1,&depthCopy);
    glTextureStorage2D(depthCopy,1,GL_DEPTH_COMPONENT24,width,height);
    glGenSamplers(1,&depthSampler);
    glSamplerParameteri(depthSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glSamplerParameteri(depthSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&imageFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,imageFBO);
    glFramebufferParameteri(GL_DRAW_FRAMEBUFFER,GL_FRAMEBUFFER_DEFAULT_WIDTH,width);
    glFramebufferParameteri(GL_DRAW_FRAMEBUFFER,GL_FRAMEBUFFER_DEFAULT_HEIGHT,height);
    glDrawBuffer(GL_NONE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,imageFBO);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return false;
    glGenFramebuffers(1,&copyFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,copyFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,colorCopy,0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depthCopy,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    const bool okay=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    targetWidth=width; targetHeight=height;
    return okay && glGetError()==GL_NO_ERROR;
}


void failure(const char* stage)
{
    LL_WARNS("ParticlePipeline") << "Resident particle " << stage
        << " failed; restarting emission on CPU without a parallel simulator." << LL_ENDL;
    LLParticleViewer::destroyGL();
    failed = true;
}
void vector3(float* dest, const LLVector3& value) { std::copy(value.mV, value.mV+3, dest); }
void color4(float* dest, const LLColor4& value) { std::copy(value.mV, value.mV+4, dest); }
U32 sourceSlot(LLViewerPartSource* source, double expires)
{
    const auto found = sourceSlots.find(source);
    if (found != sourceSlots.end() && !owners[found->second].killed)
    {
        auto& owner = owners[found->second];
        owner.expires = std::max(owner.expires, expires);
        return found->second;
    }
    U32 slot = 0;
    while (slot < owners.size() && owners[slot].source.notNull()) ++slot;
    if (slot == owners.size())
    {
        if (slot == MAX_CAPACITY) return ~0u;
        owners.emplace_back();
    }
    auto& owner = owners[slot];
    if (++owner.generation == 0) { failed = true; return ~0u; }
    owner.source = source; owner.expires = expires; owner.killed = false;
    sourceSlots[source] = slot;
    return slot;
}
U32 materialSlot(LLViewerPart* part, double expires)
{
    U32 blend = U32(part->mBlendFuncSource) | (U32(part->mBlendFuncDest)<<8);
    U32 flags = (part->mFlags & LLPartData::LL_PART_EMISSIVE_MASK) ? 1 : 0;
    LLViewerTexture* image = part->mImagep;
    if (!image) image = LLViewerFetchedTexture::sDefaultParticleImagep;
    const MaterialKey key{image, blend, flags};
    const auto found = materialSlots.find(key);
    if (found != materialSlots.end())
    {
        auto& material = materials[found->second];
        material.expires = std::max(material.expires, expires);
        return found->second;
    }
    U32 slot = 0;
    while (slot < materials.size() && materials[slot].image.notNull()) ++slot;
    if (slot == materials.size())
    {
        if (slot == MAX_CAPACITY) return ~0u;
        materials.emplace_back();
    }
    materials[slot] = {image, blend, flags, expires};
    materialSlots[key] = slot;
    return slot;
}
bool publish()
{
    std::vector<Source> snapshots(owners.size());
    std::vector<WindVelocity> winds;
    std::map<LLViewerRegion*, U32> windOffsets;
    std::vector<Region> regions;
    auto appendRegion = [&](LLViewerRegion* region)
    {
        if (!region || windOffsets.count(region)) return;
        Region r{};
        const LLVector3 origin=region->getOriginAgent();
        r.originSize[0]=origin[0]; r.originSize[1]=origin[1];
        r.originSize[2]=r.originSize[3]=region->getWidth();
        r.wind[0]=U32(winds.size()); r.wind[1]=1;
        float width=gAgent.getRegion()?gAgent.getRegion()->getWidth():region->getWidth();
        std::memcpy(&r.wind[2],&width,sizeof(width));
        windOffsets[region]=r.wind[0];
        const auto* x=region->mWind.getVelocityX(); const auto* y=region->mWind.getVelocityY();
        for(U32 j=0;j<256;++j) winds.push_back({x?x[j]:0.f,y?y[j]:0.f});
        regions.push_back(r);
    };
    appendRegion(gAgent.getRegion());
    for (auto* region : LLWorld::getInstance()->getRegionList()) appendRegion(region);
    for (U32 i = 0; i < owners.size(); ++i)
    {
        auto& owner = owners[i];
        Source& s = snapshots[i];
        s.control[0] = owner.generation;
        s.control[1] = owner.killed || owner.source.isNull();
        if (owner.source.isNull()) continue;
        auto* source = owner.source.get();
        vector3(s.position, source->mPosAgent);
        vector3(s.target, source->mTargetPosAgent);
        LLViewerObject* object = source->mSourceObjectp;
        LLVector3 callback = object && object->mDrawable ? object->getRenderPosition() : source->mPosAgent;
        vector3(s.callbackPosition, callback);
        vector3(s.callbackTarget, source->mTargetPosAgent);
        s.control[2] = source->getType() == LLViewerPartSource::LL_PART_SOURCE_BEAM ? 2 :
            (source->getType() == LLViewerPartSource::LL_PART_SOURCE_CHAT ||
             source->getType() == LLViewerPartSource::LL_PART_SOURCE_SPIRAL) ? 1 : 0;
        s.control[3] = object != nullptr;
        if (s.control[2] == 2)
        {
            auto* beam = static_cast<LLViewerPartSourceBeam*>(source);
            if (object && object->mDrawable && object->isAvatar())
            {
                auto* avatar = static_cast<LLVOAvatar*>(object);
                if (avatar->mWristLeftp) vector3(s.callbackPosition, avatar->mWristLeftp->getWorldPosition());
            }
            if (beam->mTargetObjectp && beam->mTargetObjectp->mDrawable)
                vector3(s.callbackTarget, beam->mTargetObjectp->getRenderPosition());
            else vector3(s.callbackTarget, gAgent.getPosAgentFromGlobal(beam->mLKGTargetPosGlobal));
        }
        if (object)
        {
            vector3(s.ribbonAxis, LLVector3::z_axis * object->getRenderRotation());
            s.ribbonAxis[3] = 1;
        }
        LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosAgent(source->mPosAgent);
        if (!region) region = gAgent.getRegion();
        if (region)
        {
            vector3(s.regionOriginWidth, region->getOriginAgent());
            s.regionOriginWidth[3] = gAgent.getRegion() ? gAgent.getRegion()->getWidth() : region->getWidth();
            auto [it, inserted] = windOffsets.emplace(region, U32(winds.size()));
            if (inserted)
            {
                const auto* x = region->mWind.getVelocityX();
                const auto* y = region->mWind.getVelocityY();
                for (U32 j = 0; j < 256; ++j) winds.push_back({x ? x[j] : 0.f, y ? y[j] : 0.f});
            }
            s.wind[0] = it->second; s.wind[1] = 1;
        }
    }
    return publishSources(snapshots, winds) && publishRegions(regions);
}
bool updateMaterials()
{
    if (!materialsDirty) return true;
    struct RestoreTexture
    {
        U32 active=gGL.getCurrentTexUnitIndex(), name=gGL.getTexUnit(0)->getCurrTexture();
        LLTexUnit::eTextureType type=gGL.getTexUnit(0)->getCurrType();
        ~RestoreTexture()
        {
            if (type==LLTexUnit::TT_NONE) gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            else gGL.getTexUnit(0)->bindManual(type,name);
            gGL.getTexUnit(active)->activate();
        }
    } restore;

    std::vector<Material> records(materials.size());
    std::set<LLImageGL*> used;
    for (U32 i = 0; i < materials.size(); ++i)
    {
        auto& m = materials[i];
        if (m.image.isNull()) continue;
        LLViewerTexture* image = m.image;
        if (image->hasParcelMedia() && image->getParcelMedia()->isPlaying()) image = image->getParcelMedia();
        // Source-level texture demand survives without CPU LLFace objects.
        image->addTextureStats(m.textureArea);
        LLImageGL* glimage = image->getGLTexture();
        if (!glimage || !glimage->getTexName()) glimage = LLViewerFetchedTexture::sDefaultParticleImagep->getGLTexture();
        if (!glimage) return false;
        // Apply pending sampler options through the viewer's binding cache before
        // copying them. This is resource work, not a particle traversal.
        auto& texture = textures[glimage];
        if (!texture) texture = std::make_unique<LLParticleTexture>();
        if (used.insert(glimage).second)
        {
            gGL.getTexUnit(0)->bind(glimage);
            if (!texture->update(glimage)) return false;
        }
        records[i] = {texture->handle(), m.blend, m.flags};
    }
    for (auto it = textures.begin(); it != textures.end();)
        if (!used.count(it->first)) it = textures.erase(it); else ++it;
    if (!materialBuffer) glGenBuffers(1, &materialBuffer);
    GLint previous = 0; glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previous);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(sizeof(Material), records.size()*sizeof(Material)),
        records.empty() ? nullptr : records.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, previous);
    const bool okay=glGetError() == GL_NO_ERROR;
    materialsDirty=!okay;
    return okay;
}
}

bool LLParticleViewer::active() { return enabled; }
bool LLParticleViewer::acceptsBirths() { return births.size() < MAX_CAPACITY; }
bool LLParticleViewer::beginFrame()
{
    if (enabled)
    {
        if (gParticleAlphaProgram.isComplete() && gParticleHUDAlphaProgram.isComplete() && gParticleDepthProgram.isComplete()) return true;
        failure("shader reload");
    }
    if (failed || !LLParticlePipeline::isSupported() || !gParticleAlphaProgram.isComplete() ||
        !gParticleHUDAlphaProgram.isComplete() || !gParticleDepthProgram.isComplete()) return false;
    if (!LLParticlePipeline::initializePool(std::vector<Particle>(MAX_CAPACITY)))
    { failure("initialization"); return false; }
    enabled = true;
    LL_INFOS("ParticlePipeline") << "Resident GPU particle simulation, ordering and alpha submission active." << LL_ENDL;
    return true;
}
void LLParticleViewer::add(LLViewerPart* part)
{
    std::unique_ptr<LLViewerPart> birth(part);
    if (!enabled || !part || !part->mPartSourcep || births.size() >= MAX_CAPACITY ||
        !std::isfinite(part->mMaxAge) || part->mMaxAge <= 0 || !part->mPosAgent.isFinite()) return;
    const double expires = simulationTime + part->mMaxAge + .2;
    U32 source = sourceSlot(part->mPartSourcep, expires), material = materialSlot(part, expires);
    if (source == ~0u || material == ~0u) return;
    Particle p{};
    vector3(p.positionAge, part->mPosAgent); p.positionAge[3] = part->mLastUpdateTime;
    vector3(p.velocityLife, part->mVelocity); p.velocityLife[3] = part->mMaxAge;
    vector3(p.accelerationParameter, part->mAccel); p.accelerationParameter[3] = part->mParameter;
    vector3(p.offsetStartGlow, part->mPosOffset); p.offsetStartGlow[3] = part->mStartGlow;
    color4(p.startColor, part->mStartColor); color4(p.endColor, part->mEndColor); color4(p.color, part->mColor);
    p.scales[0] = part->mStartScale[0]; p.scales[1] = part->mStartScale[1];
    p.scales[2] = part->mEndScale[0]; p.scales[3] = part->mEndScale[1];
    p.scaleGlow[0] = part->mScale[0]; p.scaleGlow[1] = part->mScale[1];
    p.scaleGlow[2] = part->mGlow.mV[3]; p.scaleGlow[3] = part->mEndGlow;
    p.identity[1] = 1; p.identity[2] = part->mFlags; p.identity[3] = material;
    p.sourceParent[0] = source; p.sourceParent[1] = owners[source].generation;
    p.sourceParent[2] = ~0u;
    vector3(p.axis, part->mAxis);
    if ((part->mFlags & LLPartData::LL_PART_RIBBON_MASK) && part->mPartSourcep->mSourceObjectp)
        vector3(p.axis, LLVector3::z_axis * part->mPartSourcep->mSourceObjectp->getRenderRotation());
    births.push_back(p);
}
void LLParticleViewer::finishFrame(float dt)
{
    if (!enabled) return;
    materialsDirty=true;
    if (!publish()) { failure("source upload"); return; }
    // Rebase old slots before births already expressed in the new coordinate
    // system arrive. Repair retired ribbons before recycling their slots.
    if (!advance(0.f, {pendingShift[0], pendingShift[1], pendingShift[2]}) ||
        !emit(births, std::clamp(LLViewerPartSim::getMaxPartCount(), 0, int(MAX_CAPACITY))) ||
        !advance(dt, {0,0,0})) { failure("simulation"); return; }
    pendingShift.clear(); births.clear(); simulationTime += dt;
    for (U32 i = 0; i < owners.size(); ++i)
    {
        auto& owner = owners[i];
        if (owner.source && (owner.killed || owner.expires < simulationTime))
        {
            auto found = sourceSlots.find(owner.source.get());
            if (found != sourceSlots.end() && found->second == i) sourceSlots.erase(found);
            owner.source = nullptr;
        }
    }
    for (auto& material : materials)
        if (material.image && material.expires < simulationTime)
        {
            materialSlots.erase(MaterialKey{material.image.get(), material.blend, material.flags});
            material.image = nullptr;
        }
}
void LLParticleViewer::shift(const LLVector3& offset)
{
    if (!enabled) return;
    pendingShift += offset;
    // Active emitters are shifted by LLViewerPartSim; retained, ended sources
    // need the same rebase while their particles finish their lifetime.
    const auto* activeSources = LLViewerPartSim::getInstance()->getParticleSystemList();
    for (auto& owner : owners)
        if (owner.source && std::find(activeSources->begin(), activeSources->end(), owner.source) == activeSources->end())
        {
            owner.source->mPosAgent += offset;
            owner.source->mTargetPosAgent += offset;
            owner.source->mLastUpdatePosAgent += offset;
        }
}
void LLParticleViewer::kill(U32 source)
{
    for (auto& owner : owners) if (owner.source && owner.source->getID() == source) owner.killed = true;
}
void LLParticleViewer::killOwner(const LLUUID& ownerID)
{
    for (auto& owner : owners) if (owner.source && owner.source->getOwnerUUID() == ownerID) owner.killed = true;
}
void LLParticleViewer::cleanupRegion(LLViewerRegion* region)
{
    if (!enabled || !region) return;
    const auto origin=region->getOriginAgent()-pendingShift;
    const float width=region->getWidth();
    if (!retireRegion({origin[0],origin[1],0},{origin[0]+width,origin[1]+width,0}))
        failure("region cleanup");
}
void LLParticleViewer::destroyGL()
{
    enabled = false;
    releaseTargets();
    LLParticlePipeline::destroyGL();
    owners.clear(); materials.clear(); births.clear(); textures.clear();
    sourceSlots.clear(); materialSlots.clear();
    LLParticleTexture::collect(true);
    if (demandFence) glDeleteSync(demandFence);
    demandFence=nullptr;
    if (demandReadback) glDeleteBuffers(1,&demandReadback);
    demandReadback=0; lastDemandCopy=-1;
    if (materialBuffer) glDeleteBuffers(1, &materialBuffer);
    materialBuffer = 0; pendingShift.clear(); simulationTime = 0; materialsDirty=true;
}
bool LLParticleViewer::beginView(const std::vector<float>& boundaries, bool hud)
{
    if (!enabled || owners.empty()) return false;
    viewMatrix = gGL.getModelviewMatrix();
    const glm::mat4 inverseView=glm::inverse(viewMatrix);
    const LLVector3 camera = hud ? LLVector3(-1,0,0) : LLVector3(inverseView[3][0],inverseView[3][1],inverseView[3][2]);
    LLVector3 forward = hud ? LLVector3(1,0,0) : LLVector3(-inverseView[2][0],-inverseView[2][1],-inverseView[2][2]);
    forward.normalize();
    const glm::mat4 projection=gGL.getProjectionMatrix()*viewMatrix;
    if (!updateMaterials() || !prepareView({camera[0],camera[1],camera[2]},
        {forward[0],forward[1],forward[2]}, 16.f,glm::value_ptr(projection),hud?1:0) || !prepareAlphaIntervals(boundaries, hud ? 1 : 0))
    { failure("view preparation"); return false; }
    // Resource-streaming telemetry only: one aggregate per material, at most
    // once per second. Draw offsets/counts and particle state never return here.
    const U32 demand=textureDemand(LLViewerCamera::getInstance()->getPixelMeterRatio(),{camera[0],camera[1],camera[2]});
    GLint oldRead=0,oldWrite=0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING,&oldRead);
    glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING,&oldWrite);
    if (demandFence)
    {
        GLenum status=glClientWaitSync(demandFence,0,0);
        if (status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED)
        {
            std::vector<float> areas(MAX_CAPACITY);
            glBindBuffer(GL_COPY_READ_BUFFER,demandReadback);
            glGetBufferSubData(GL_COPY_READ_BUFFER,0,areas.size()*sizeof(float),areas.data());
            for (U32 i=0;i<materials.size();++i) materials[i].textureArea=std::max(areas[i],64.f);
            glDeleteSync(demandFence); demandFence=nullptr;
        }
    }
    if (demand && !demandFence && simulationTime-lastDemandCopy>=1.)
    {
        if (!demandReadback) glGenBuffers(1,&demandReadback);
        glBindBuffer(GL_COPY_WRITE_BUFFER,demandReadback);
        glBufferData(GL_COPY_WRITE_BUFFER,MAX_CAPACITY*sizeof(U32),nullptr,GL_STREAM_READ);
        glBindBuffer(GL_COPY_READ_BUFFER,demand);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,MAX_CAPACITY*sizeof(U32));
        demandFence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        const U32 zero=0;
        glClearBufferData(GL_COPY_READ_BUFFER,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
        lastDemandCopy=simulationTime;
    }
    glBindBuffer(GL_COPY_READ_BUFFER,oldRead); glBindBuffer(GL_COPY_WRITE_BUFFER,oldWrite);
    return true;
}


bool LLParticleViewer::draw(U32 interval, LLGLSLShader& shader, bool depthWrite, bool depthOnly, bool glow)
{
    if (!enabled) return false;
    gGL.flush();
    GLint drawFBO=0,readFBO=0,viewport[4],oldActive=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFBO);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFBO);
    glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    LLGLSLShader* previous=LLGLSLShader::sCurBoundShaderPtr;
    GLuint color=0,depth=0;
    if (drawFBO)
    {
        GLint kind=0,name=0,format=0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&kind);
        if (kind==GL_TEXTURE)
        {
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);
            glGetTextureLevelParameteriv(name,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
            if (format==GL_RGBA16F) color=name;
        }
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&kind);
        if (kind==GL_TEXTURE)
        {
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);
            depth=name;
        }
    }
    bool okay=targets(viewport[0]+viewport[2],viewport[1]+viewport[3]);
    if (!okay)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFBO);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,readFBO);
        failure("framebuffer allocation"); return false;
    }
    // The main HDR color and texture depth are consumed directly. Copies are
    // needed only for a default/non-HDR framebuffer (not the main world path).
    if ((!color && !depthOnly) || !depth)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,drawFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,copyFBO);
        glBlitFramebuffer(0,0,targetWidth,targetHeight,0,0,targetWidth,targetHeight,
            ((!color && !depthOnly)?GL_COLOR_BUFFER_BIT:0) | (!depth?GL_DEPTH_BUFFER_BIT:0),GL_NEAREST);
    }
    if (!color) color=colorCopy;
    if (!depth) depth=depthCopy;
    // Virtual depth belongs to one interval; non-particle depth changes are
    // visible through the live scene-depth sampler before the next interval.
    const float farDepth=1.f;
    glClearTexImage(virtualDepth,0,GL_RED,GL_FLOAT,&farDepth);
    GLint imageNames[2],imageLevels[2],imageLayers[2],imageLayered[2],imageAccess[2],imageFormats[2];
    for (U32 i=0;i<2;++i)
    {
        glGetIntegeri_v(GL_IMAGE_BINDING_NAME,i,&imageNames[i]);
        glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL,i,&imageLevels[i]);
        glGetIntegeri_v(GL_IMAGE_BINDING_LAYER,i,&imageLayers[i]);
        glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED,i,&imageLayered[i]);
        glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS,i,&imageAccess[i]);
        glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT,i,&imageFormats[i]);
    }
    glBindImageTexture(0,color,0,GL_FALSE,0,GL_READ_WRITE,GL_RGBA16F);
    glBindImageTexture(1,virtualDepth,0,GL_FALSE,0,GL_READ_WRITE,GL_R32F);
    gGL.matrixMode(LLRender::MM_MODELVIEW); gGL.pushMatrix(); gGL.loadMatrix(glm::value_ptr(viewMatrix));
    gPipeline.bindDeferredShaderFast(shader);
    GLint channel=shader.getTextureChannel(LLShaderMgr::ALPHA_PEEL_DEPTH);
    GLint oldTexture=0,oldSampler=0;
    if (channel>=0)
    {
        glActiveTexture(GL_TEXTURE0+channel);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
        glGetIntegeri_v(GL_SAMPLER_BINDING,channel,&oldSampler);
        glBindTexture(GL_TEXTURE_2D,depth); glBindSampler(channel,depthSampler);
    }
    shader.uniform1i(LLStaticHashedString("particle_depth_mode"),(depthWrite||depthOnly)?2:1);
    shader.uniform1i(LLStaticHashedString("particle_depth_only"),depthOnly);
    const bool interleaveGlow=glow && !depthOnly;
    shader.uniform1i(LLStaticHashedString("particle_glow_interleaved"),interleaveGlow);
    shader.uniform1i(LLStaticHashedString("particle_impostor"),LLPipeline::sImpostorRender);
    gGL.syncMatrices();
    {
        LLGLDisable blend(GL_BLEND),cull(GL_CULL_FACE);
        LLGLDepthTest noDepth(GL_FALSE,GL_FALSE);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,imageFBO);
        // Color then glow for each depth-sorted particle, in one indirect-count
        // call. Near alpha attenuates far glow before adding its own emission.
        okay=drawAlphaInterval(interval,materialBuffer,shader.getUniformLocation(LLStaticHashedString("particle_interval")),interleaveGlow);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFBO);
    if (okay && (depthWrite||depthOnly))
    {
        // Replay only covered particle quads, not a full-screen depth resolve
        // per world group. Fixed-function LEQUAL preserves closer scene depth.
        LLGLDepthTest writeDepth(GL_TRUE,GL_TRUE,GL_LEQUAL);
        LLGLDisable cull(GL_CULL_FACE);
        GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        gParticleDepthProgram.bind();
        gParticleDepthProgram.uniform1i(LLStaticHashedString("particle_glow_interleaved"),0);
        gGL.syncMatrices();
        okay=drawAlphaInterval(interval,materialBuffer,gParticleDepthProgram.getUniformLocation(LLStaticHashedString("particle_interval")));
        glColorMask(mask[0],mask[1],mask[2],mask[3]);
    }
    if (color==colorCopy && !depthOnly)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,copyFBO);
        glBlitFramebuffer(0,0,targetWidth,targetHeight,0,0,targetWidth,targetHeight,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    if (channel>=0)
    {
        glActiveTexture(GL_TEXTURE0+channel); glBindTexture(GL_TEXTURE_2D,oldTexture); glBindSampler(channel,oldSampler);
    }
    glActiveTexture(oldActive);
    for (U32 i=0;i<2;++i)
        glBindImageTexture(i,imageNames[i],imageLevels[i],imageLayered[i],imageLayers[i],imageAccess[i],imageFormats[i]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,readFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFBO);
    gGL.popMatrix();
    if (previous) gPipeline.bindDeferredShaderFast(*previous); else LLGLSLShader::unbind();
    if (!okay || glGetError()!=GL_NO_ERROR) { failure("alpha submission"); return false; }
    return true;
}
bool LLParticleViewer::pick(const LLVector3& start,const LLVector3& end,LLUUID& owner,LLUUID& object)
{
    if (!enabled) return false;
    const auto camera=gAgentCamera.getCameraPositionAgent();
    const auto forward=LLViewerCamera::getInstance()->getAtAxis();
    if (!prepareView({camera[0],camera[1],camera[2]},{forward[0],forward[1],forward[2]},16.f)) return false;
    std::array<U32,2> source; float fraction=0;
    if (!LLParticlePipeline::pick({start[0],start[1],start[2]},{end[0],end[1],end[2]},source,fraction) ||
        source[0]>=owners.size()) return false;
    const auto& entry=owners[source[0]];
    if (!entry.source || entry.generation!=source[1]) return false;
    owner=entry.source->getOwnerUUID();
    object=entry.source->mSourceObjectp ? entry.source->mSourceObjectp->getID() : LLUUID::null;
    return true;
}
