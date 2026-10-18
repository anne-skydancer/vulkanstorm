// CPU avatar owner and native geometry publication. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldavatar.h"
#include "vsattentionmotion.h"
#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "vswearable.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "llimage.h"
#include "llavatarappearance.h"
#include "llwearabledata.h"
#include "llwearable.h"
#include "lllocaltextureobject.h"
#include "lltexlayerparams.h"
#include "llavatarjointmesh.h"
#include "llpolymesh.h"
#include "llpolymorph.h"
#include "llprimitive.h"
#include "lltextureentry.h"
#include "lltexlayer.h"
#include "llmatrix4a.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llui.h"
#include "llquantize.h"
#include "llvolume.h"
#include "llmodel.h"
#include "llregionhandle.h"
#include "llframetimer.h"
#include "llanimationstates.h"
#include "llkeyframestandmotion.h"
#include "llkeyframewalkmotion.h"
#include "llkeyframefallmotion.h"
#include "llemote.h"
#include "vsavatarmotions.h"
#include "aoengine.h"
#include "llagent.h"
#include "vsnativesession.h"
#include "rlvhandler.h"
#include "vscontrolconstraints.h"
#include "vsresourcebudget.h"
#include "vsavatarcloth.h"
#include "vsavatarlod.h"
#include "llcriticaldamp.h"
#include "noise.h"
#include "llskinningutil.h"
#include "llmutelist.h"
#include "llcallingcard.h"
#include "fsavatarrenderpersistence.h"
#include "message.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
using namespace LLAvatarAppearanceDefines;
namespace
{
bool ownsAppearanceClass=false;
U64 nextAvatarGeneration=1; // Native avatar creation and publication run on mainloop.
class NativeCompositeImage final : public LLImageRaw
{
    std::shared_ptr<const VSResourceBudget::Reservation> mResidency;

public:
    NativeCompositeImage(S32 width, S32 height, S32 components, std::shared_ptr<const VSResourceBudget::Reservation> residency) :
        LLImageRaw(U16(width), U16(height), S8(components)),
        mResidency(std::move(residency))
    {
    }
    ~NativeCompositeImage() override
    {
        // Free CPU pixels while the residency member still owns their charge.
        // LLImageRaw's later base teardown sees an already empty buffer.
        deleteData();
    }
};
class NativeJoint final : public LLAvatarJoint
{
public:
    NativeJoint()=default;
    explicit NativeJoint(S32 number):LLAvatarJoint(number) {}
    U32 render(F32,bool,bool) override { return 0; } // Geometry is published separately.
};
class NativeJointMesh final : public LLAvatarJointMesh
{
public:
    U32 render(F32,bool,bool) override { return 0; }
};
class NativeLayers final : public LLTexLayerSet
{
public:
    bool pending=true;
    explicit NativeLayers(LLAvatarAppearance* a):LLTexLayerSet(a) {}
    void requestUpdate() override { pending=true; }
    void createComposite() override { pending=true; }
};
bool finite(const LLVector3& v)
{ return std::isfinite(v.mV[0]) && std::isfinite(v.mV[1]) && std::isfinite(v.mV[2]); }
bool validSkinMetadata(const LLSD& metadata, bool overrides)
{
    const auto& names = metadata["joint_names"];
    if (!metadata.isMap() || !names.isArray() || !names.size() || names.size() > 256)
        return false;
    auto matrixValid = [](const LLSD& matrix)
    {
        if (!matrix.isArray() || matrix.size() != 16)
            return false;
        for (unsigned element = 0; element < 16; ++element)
            if ((!matrix[element].isReal() && !matrix[element].isInteger()) || !std::isfinite(float(matrix[element].asReal())))
                return false;
        return true;
    };
    const auto& inverse = metadata["inverse_bind_matrix"];
    if (!inverse.isArray() || inverse.size() != names.size())
        return false;
    std::set<std::string> unique;
    for (unsigned joint = 0; joint < names.size(); ++joint)
        if (!names[joint].isString() || names[joint].asString().empty() || names[joint].asString().size() > 256 ||
            !unique.insert(names[joint].asString()).second || !matrixValid(inverse[joint]))
            return false;
    if (metadata.has("bind_shape_matrix") && !matrixValid(metadata["bind_shape_matrix"]))
        return false;
    if (metadata.has("pelvis_offset") && !std::isfinite(float(metadata["pelvis_offset"].asReal())))
        return false;
    const auto& alternate = metadata["alt_inverse_bind_matrix"];
    if (overrides || metadata.has("alt_inverse_bind_matrix"))
    {
        if (!alternate.isArray() || alternate.size() != names.size())
            return false;
        for (unsigned joint = 0; joint < names.size(); ++joint)
            if (!matrixValid(alternate[joint]))
                return false;
    }
    return true;
}
}
struct VSWorldAvatar::Impl : LLWearableData, LLAvatarAppearance
{
    friend class VSWorldAvatar;
    LLUUID id;
    bool self=false,positionKnown=false,appearanceKnown=false,serverBakes=false;
    bool                                                   control       = false;
    bool                                                   sitting       = false;
    bool                                                   objectSitting = false;
    U64 handle=0,generation=nextAvatarGeneration++;
    VSWorldRenderer& renderer;
    VSUIResources& resources;
    VSResourceBudget                                       compositeBudget{ 384 * 1024 * 1024 };
    VSAvatarCloth                                          cloth;
    F32                                                    clothFrequency = 0;
    double                                                 clothTime      = 0;
    bool                                                   clothEnabled   = false;
    GroundQuery ground;
    LLVector3 position,velocity,angularVelocity,hover;
    LLQuaternion rotation;
    LLVector3                                              constraintPosition;
    F32                                                    constraintScale = 1, appliedGlobalScale = 1;
    LLQuaternion                                           bindRotation;
    Visibility                                             visibility     = Visibility::Normal;
    U32                                                    complexity     = 0;
    F32                                                    attachmentArea = 0;
    Attention                                              lookTarget, pointTarget;
    LLVector3                                              lookPoint, pointPoint;
    double                                                 lookDeadline = 0, pointDeadline = 0;
    F32 pixelArea=0,timeDilation=1;
    std::map<EBakedTextureIndex,LLPointer<LLImageRaw>> appliedMasks;
    std::map<EBakedTextureIndex, LLPointer<LLImageRaw>>    appliedLocalMasks;
    S32 cofVersion=-1;
    LLPrimitive textures;
    std::map<LLUUID,S32> animations;
    std::multimap<LLUUID, LLUUID>                          animationSources;
    std::map<S32, F32>                                     normalWeights;
    LLUUID                                                 jellyMotion;
    F32                                                    simulatorHeight = 0;
    std::map<unsigned, std::pair<LLVector3, LLQuaternion>> attachmentRest;
    std::map<unsigned,std::unique_ptr<LLJoint>> attachments;
    std::unique_ptr<NativeJoint>                           screen;
    std::set<unsigned>                                     hudPoints;
    std::set<unsigned>                                     firstPersonPoints;
    std::map<LLUUID,unsigned> simAttachments;
    std::map<LLUUID, LLSD>                                 overrideSkins;
    std::map<S32,VSWorldRenderer::Mesh> meshes;
    std::vector<std::shared_ptr<LLWearable>>               ownedWearables;
    S32                                                    pendingCOFVersion = -1;
    U32                                                    skeletonSerial    = 0;
    bool                                                   localAppearance   = false;
    std::vector<U64>                                       compositeSignature;
    std::map<EBakedTextureIndex, std::vector<U64>>         bakeSignatures;
    std::string                                            compositeError;
    std::map<EBakedTextureIndex, std::string>              composites;
    U64                                                    compositeEpoch = 0;
    struct Publication
    {
        LLAvatarJointMesh*  part = nullptr;
        U64                 pose = 0, shape = 0;
        VSUIRenderer::Image image;
    };
    std::map<S32, Publication> publications;
    std::vector<S32>           publishedParts;
    std::vector<F32>           poseSignature;
    U64                        poseGeneration = 0, shapeGeneration = 0;
    U32                        motionFrame      = 0;
    bool                       motionFrameKnown = false;
    bool shapeDirty=true;
    Impl(const LLUUID& uuid, bool is_self, U64 region, VSWorldRenderer& r, VSUIResources& u, GroundQuery g, bool isControl) :
        LLWearableData(),
        LLAvatarAppearance(static_cast<LLWearableData*>(this)),
        id(uuid),
        self(is_self),
        control(isControl),
        handle(region),
        renderer(r),
        resources(u),
        ground(std::move(g))
    {
        setAvatarAppearance(this);textures.setNumTEs(TEX_NUM_INDICES);
        vs_register_avatar_motions(*this);
        initInstance();
        if (!isBuilt()) throw std::runtime_error("Native avatar character assets failed to load");
        if (self)
        {
            screen = std::make_unique<NativeJoint>();
            screen->setName("mScreen");
        }
        for (const auto* info:sAvatarXmlInfo->mAttachmentInfoList)
        {
            if (info->mAttachmentID <= 0 || (info->mIsHUDAttachment && !self))
                continue;
            auto* parent = info->mIsHUDAttachment ? screen.get() : mRoot->findJoint(info->mJointName);
            if (!parent) continue;
            auto joint=std::make_unique<NativeJoint>();joint->setName(info->mName);parent->addChild(joint.get());
            if (info->mHasPosition) joint->setPosition(info->mPosition);
            if (info->mHasRotation)
            {
                LLQuaternion q;q.setQuat(info->mRotationEuler.mV[0]*DEG_TO_RAD,info->mRotationEuler.mV[1]*DEG_TO_RAD,info->mRotationEuler.mV[2]*DEG_TO_RAD);
                joint->setRotation(q);
            }
            attachmentRest.emplace(unsigned(info->mAttachmentID), std::make_pair(joint->getPosition(), joint->getRotation()));
            attachments[unsigned(info->mAttachmentID)]=std::move(joint);
            if (info->mIsHUDAttachment)
                hudPoints.insert(unsigned(info->mAttachmentID));
            if (info->mVisibleFirstPerson)
                firstPersonPoints.insert(unsigned(info->mAttachmentID));
        }
        LLCharacter::sInstances.push_back(this);
        if (!control)
            vs_start_avatar_default_motions(*this);
    }
    ~Impl() override
    {
        mWearableDatas.clear();
        for (const auto& [baked, key] : composites)
            resources.erase(key);
        LLCharacter::sInstances.remove(this);
        flushAllMotions();
        attachments.clear();
    }
    bool usesGraphicsContext() const override { return false; }
    bool isSelf() const override { return self; }
    bool isUsingLocalAppearance() const override { return localAppearance; }
    bool isEditingAppearance() const override { return false; }
    bool isUsingServerBakes() const override { return serverBakes; }
    F32 getAvatarOffset() override
    {
#ifdef OPENSIM
        if (self && !LLGridManager::getInstance()->isInSecondLife() && !serverBakes)
            return gSavedPerAccountSettings.getF32("AvatarHoverOffsetZ");
#endif
        return LLAvatarAppearance::getAvatarOffset();
    }
    bool isWearingWearableType(LLWearableType::EType type) const override
    {
        if (self)
            return LLAvatarAppearance::isWearingWearableType(type);
        if (type == LLWearableType::WT_SHAPE || type == LLWearableType::WT_SKIN || type == LLWearableType::WT_HAIR ||
            type == LLWearableType::WT_EYES)
            return true;
        for (const auto& [index, entry] : LLAvatarAppearance::getDictionary()->getTextures())
            if (entry->mWearableType == type)
                return entry->mIsUsedByBakedTexture &&
                       isTextureDefined(LLAvatarAppearance::getDictionary()->getBakedTexture(entry->mBakedTextureIndex)->mTextureIndex, 0);
        return false;
    }
    bool startMotion(const LLUUID& id, F32 offset = 0) override
    {
        if (self && AOEngine::instanceExists())
        {
            const auto replacement = AOEngine::instance().override(id, true);
            if (replacement.notNull())
            {
                gAgent.sendAnimationRequest(replacement, ANIM_REQUEST_START);
                return true;
            }
        }
        return LLCharacter::startMotion(control ? id : vs_remap_avatar_motion(id, getSex() == SEX_FEMALE), offset);
    }
    bool stopMotion(const LLUUID& id, bool immediate = false) override
    {
        if (self && AOEngine::instanceExists())
        {
            const auto replacement = AOEngine::instance().override(id, false);
            if (replacement.notNull())
            {
                gAgent.sendAnimationRequest(replacement, ANIM_REQUEST_STOP);
                return true;
            }
        }
        return LLCharacter::stopMotion(control ? id : vs_remap_avatar_motion(id, getSex() == SEX_FEMALE), immediate);
    }
    void updateJellyMotion()
    {
        if (control || visibility != Visibility::Jellydoll)
            return;
        const LLUUID selected = objectSitting ? ANIM_AGENT_SIT_FEMALE : sitting ? ANIM_AGENT_SIT_GROUND_CONSTRAINED : ANIM_AGENT_STAND;
        if (selected == jellyMotion)
            return;
        if (jellyMotion.notNull())
            LLCharacter::stopMotion(jellyMotion, true);
        jellyMotion = selected;
        LLCharacter::startMotion(jellyMotion);
        motionFrameKnown = false;
    }
    void resetAppearanceSkeleton(bool jelly)
    {
        const LLVector3 pelvis = mPelvisp->getPosition();
        // Keep the existing joint objects: motions, mesh skin palettes and
        // attachment parents own references to these joints.
        if (!buildSkeleton(sAvatarSkeletonInfo))
            throw std::runtime_error("Native avatar skeleton reset failed");
        LLAvatarAppearance::resetVisualParams();
        if (!buildSkeleton(sAvatarSkeletonInfo))
            throw std::runtime_error("Native avatar skeleton reset failed");
        for (const auto& [point, rest] : attachmentRest)
        {
            auto& joint = attachments.at(point);
            joint->setPosition(rest.first);
            joint->setRotation(rest.second);
            joint->setScale(LLVector3(1, 1, 1));
        }
        for (auto* param = getFirstVisualParam(); param; param = getNextVisualParam())
            if (auto* morph = dynamic_cast<LLPolyMorphTarget*>(param))
                morph->applyVolumeChanges(morph->getLastWeight() - morph->getDefaultWeight());
        if (jelly)
            LLAvatarAppearance::applyDefaultParams();
        else
            for (const auto& [id, weight] : normalWeights)
                setVisualParamWeight(id, weight, false);
        updateVisualParams();
        computeBodySize();
        mPelvisp->setPosition(pelvis);
        shapeDirty       = true;
        motionFrameKnown = false;
    }
    void updateVisualParams() override
    {
        const ESex sex = getVisualParamWeight("male") > .5f ? SEX_MALE : SEX_FEMALE;
        if (getSex() != sex)
        {
            std::vector<LLUUID> remapped;
            if (!control)
                for (const auto& [id, sequence] : animations)
                {
                    const auto previous = vs_remap_avatar_motion(id, getSex() == SEX_FEMALE);
                    const auto next     = vs_remap_avatar_motion(id, sex == SEX_FEMALE);
                    if (previous != next && LLCharacter::isMotionActive(previous))
                    {
                        LLCharacter::stopMotion(previous, true);
                        remapped.push_back(next);
                    }
                }
            setSex(sex);
            for (const auto& motion : remapped)
                LLCharacter::startMotion(motion);
            shapeDirty = true;
            compositeSignature.clear();
        }
        LLCharacter::updateVisualParams();
        if (skeletonSerial != getSkeletonSerialNum())
        {
            skeletonSerial = getSkeletonSerialNum();
            computeBodySize();
            mRoot->updateWorldMatrixChildren();
            shapeDirty = true;
        }
    }
    LLAvatarJoint* createAvatarJoint() override { return new NativeJoint; }
    LLAvatarJoint* createAvatarJoint(S32 number) override { return new NativeJoint(number); }
    LLAvatarJointMesh* createAvatarJointMesh() override { return new NativeJointMesh; }
    LLTexLayerSet* createTexLayerSet() override { return new NativeLayers(this); }
    void bodySizeChanged() override { shapeDirty=true; }
    void dirtyMesh() override { shapeDirty=true; }
    void dirtyMesh(S32) override { shapeDirty=true; }
    void updateMeshTextures() override { shapeDirty=true; }
    void invalidateComposite(LLTexLayerSet* set,bool) override { if (set) set->requestUpdate();shapeDirty=true; }
    void onGlobalColorChanged(const LLTexGlobalColor*,bool) override { shapeDirty=true; }
    bool isTextureDefined(ETextureIndex te,U32) const override
    {
        if (unsigned(te)>=textures.getNumTEs()) return false;
        const LLUUID& uuid=textures.getTE(U8(te))->getID();return uuid.notNull() && uuid!=IMG_DEFAULT_AVATAR;
    }
    std::vector<U64> bakeSignature(const LLAvatarAppearanceDictionary::BakedEntry& entry, bool& ready)
    {
        std::vector<U64> result{ U64(getSex()) };
        const auto       relevant = [&](S32 type)
        {
            return type < 0 || std::find(entry.mWearables.begin(), entry.mWearables.end(), type) != entry.mWearables.end();
        };
        for (auto* param = getFirstVisualParam(); param; param = getNextVisualParam())
            if (auto* layer = dynamic_cast<LLTexLayerParam*>(param); layer && relevant(layer->getWearableType()))
            {
                result.push_back(U64(layer->getID()));
                result.push_back(std::bit_cast<U32>(layer->getWeight()));
            }
        for (const auto& wearable : ownedWearables)
        {
            if (!relevant(wearable->getType()))
                continue;
            result.push_back(0xffffffffffffffffULL);
            result.push_back(U64(wearable->getType()));
            for (const auto& [index, param] : wearable->mVisualParamIndexMap)
                if (auto* layer = dynamic_cast<LLTexLayerParam*>(param))
                {
                    result.push_back(U64(index));
                    result.push_back(std::bit_cast<U32>(layer->getWeight()));
                }
            result.push_back(0xfffffffffffffffeULL);
            for (const auto index : entry.mLocalTextures)
            {
                const auto* texture = wearable->getLocalTextureObject(index);
                if (!texture)
                    continue;
                const auto& uuid = texture->getID();
                U64         identity[2];
                std::memcpy(identity, uuid.mData, UUID_BYTES);
                result.push_back(U64(index));
                result.insert(result.end(), identity, identity + 2);
                const bool required = uuid.notNull() && uuid != IMG_DEFAULT_AVATAR && uuid != IMG_INVISIBLE;
                if (required)
                    LLUI::getUIImageByID(uuid);
                const auto key   = "remote:" + vs_native_image_id(uuid).asString();
                const auto image = resources.image(key);
                if (required && !image)
                    ready = false;
                result.push_back(image ? resources.generation(key) : 0);
            }
        }
        return result;
    }
    void compose()
    {
        if (!self || !localAppearance || ownedWearables.empty())
            return;
        bool                                           dependenciesReady = true;
        std::vector<U64>                               signature;
        std::map<EBakedTextureIndex, std::vector<U64>> wanted;
        for (const auto& [index, entry] : LLAvatarAppearance::getDictionary()->getBakedTextures())
        {
            if (index == BAKED_SKIRT && !isWearingWearableType(LLWearableType::WT_SKIRT))
                continue;
            auto value = bakeSignature(*entry, dependenciesReady);
            signature.push_back(U64(index));
            signature.push_back(U64(value.size()));
            signature.insert(signature.end(), value.begin(), value.end());
            wanted.emplace(index, std::move(value));
        }
        if (!dependenciesReady)
        {
            compositeError = "Local texture dependencies are still loading";
            return;
        }
        if (!compositeSignature.empty() && signature == compositeSignature)
            return;
        auto working = compositeBudget.reserve(64 * 1024 * 1024);
        if (!working)
        {
            compositeError = "Compositor working allocation refused";
            return;
        }
        compositeError.clear();
        LLTexLayerCPUAllocate allocate = [this](S32 width, S32 height, S32 components) -> LLPointer<LLImageRaw>
        {
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || components < 1 || components > 4)
                return nullptr;
            auto residency = compositeBudget.reserve(std::size_t(width) * height * components);
            if (!residency)
                return nullptr;
            return new NativeCompositeImage(width, height, components, std::move(residency));
        };
        struct RestoreWeights
        {
            Impl&                            avatar;
            std::vector<std::pair<S32, F32>> weights;
            explicit RestoreWeights(Impl& owner) : avatar(owner)
            {
                for (auto* param = avatar.getFirstVisualParam(); param; param = avatar.getNextVisualParam())
                    weights.emplace_back(param->getID(), param->getWeight());
            }
            ~RestoreWeights()
            {
                for (const auto& [id, weight] : weights)
                    avatar.setVisualParamWeight(id, weight, false);
            }
        } restore(*this);
        LLTexLayerCPUTexture texture{ [&](const LLUUID& uuid) -> LLPointer<LLImageRaw>
                                      {
                                          if (uuid == IMG_INVISIBLE)
                                          {
                                              auto transparent = allocate(1, 1, 4);
                                              if (transparent)
                                                  transparent->clear(0, 0, 0, 0);
                                              return transparent;
                                          }
                                          LLPointer<LLImageRaw> raw;
                                          {
                                              const auto image = resources.image("remote:" + vs_native_image_id(uuid).asString());
                                              if (!image)
                                                  return nullptr;
                                              const auto extent = VSUIRenderer::imageExtent(image);
                                              raw               = allocate(S32(extent[0]), S32(extent[1]), 4);
                                              if (!raw)
                                                  return nullptr;
                                              const auto&       pixels = VSUIRenderer::imagePixels(image);
                                              const std::size_t stride = extent[0] * 4;
                                              for (unsigned row = 0; row < extent[1]; ++row)
                                                  std::copy_n(pixels.data() + row * stride, stride,
                                                              raw->getData() + (extent[1] - 1 - row) * stride);
                                          }
                                          return raw;
                                      },
                                      allocate,
                                      [this](const std::string& reason)
                                      {
                                          if (compositeError.empty())
                                              compositeError = reason;
                                      } };
        bool                                      complete = true;
        const U64                                 epoch    = ++compositeEpoch;
        std::map<EBakedTextureIndex, std::string> candidates, newKeys;
        struct CandidateCleanup
        {
            VSUIResources&                             resources;
            std::map<EBakedTextureIndex, std::string>& keys;
            bool                                       committed = false;
            ~CandidateCleanup()
            {
                if (!committed)
                    for (const auto& [baked, key] : keys)
                        resources.erase(key);
            }
        } cleanup{ resources, newKeys };
        std::map<EBakedTextureIndex, LLPointer<LLImageRaw>> masks;
        for (unsigned baked = 0; baked < mBakedTextureDatas.size(); ++baked)
        {
            const auto index   = EBakedTextureIndex(baked);
            const auto desired = wanted.find(index);
            if (desired == wanted.end())
                continue;
            const auto previous = composites.find(index);
            if (previous != composites.end() && bakeSignatures[index] == desired->second && resources.image(previous->second))
            {
                candidates[index] = previous->second;
                continue;
            }
            auto* layers = dynamic_cast<NativeLayers*>(mBakedTextureDatas[baked].mTexLayerSet);
            if (!layers)
                continue;
            const auto* info = layers->getInfo();
            if (!info || info->getWidth() <= 0 || info->getHeight() <= 0 || info->getWidth() > 2048 || info->getHeight() > 2048)
            {
                if (compositeError.empty())
                    compositeError = "Invalid bake dimensions: " + std::to_string(baked);
                complete = false;
                continue;
            }
            LLPointer<LLImageRaw> composite = allocate(info->getWidth(), info->getHeight(), 4);
            if (!composite)
            {
                if (compositeError.empty())
                    compositeError = "Bake allocation refused: " + std::to_string(baked);
                complete = false;
                continue;
            }
            if (!layers->renderCPU(*composite, texture))
            {
                if (compositeError.empty())
                    compositeError = "Bake rendering refused: " + std::to_string(baked);
                complete = false;
                continue;
            }
            const auto key = "avatar-composite:" + id.asString() + ":" + std::to_string(generation) + ":" + std::to_string(epoch) + ":" +
                             std::to_string(baked);
            newKeys[EBakedTextureIndex(baked)]    = key;
            candidates[EBakedTextureIndex(baked)] = key;
            if (!resources.publish(key, *composite))
            {
                if (compositeError.empty())
                    compositeError = "Bake GPU publication refused: " + std::to_string(baked);
                complete = false;
                continue;
            }
            masks[EBakedTextureIndex(baked)] = layers->cpuMorphMask();
        }
        if (!complete)
            return;
        for (const auto& [baked, mask] : masks)
            if (mask)
            {
                auto& previous = appliedLocalMasks[baked];
                if (!previous || previous->getWidth() != mask->getWidth() || previous->getHeight() != mask->getHeight() ||
                    !std::equal(mask->getData(), mask->getData() + mask->getDataSize(), previous->getData()))
                {
                    applyMorphMask(mask->getData(), mask->getWidth(), mask->getHeight(), 1, baked);
                    previous = mask;
                }
            }
        for (const auto& [baked, key] : composites)
            if (!candidates.count(baked) || candidates.at(baked) != key)
            {
                resources.erase(key);
                if (!candidates.count(baked))
                {
                    appliedLocalMasks.erase(baked);
                    const auto* entry = LLAvatarAppearance::getDictionary()->getBakedTexture(baked);
                    if (entry)
                        textures.setTETexture(U8(entry->mTextureIndex), IMG_DEFAULT_AVATAR);
                }
            }
        bakeSignatures     = std::move(wanted);
        composites         = std::move(candidates);
        cleanup.committed  = true;
        compositeSignature = std::move(signature);
        for (auto& baked : mBakedTextureDatas)
            if (auto* layers = dynamic_cast<NativeLayers*>(baked.mTexLayerSet))
                layers->pending = false;
    }
    void applyMorphMask(const U8* data,S32 w,S32 h,S32 components,EBakedTextureIndex baked) override
    {
        if (unsigned(baked)>=mBakedTextureDatas.size() || !data || w<=0 || h<=0 || components<1 || components>4) return;
        for (const auto* masked:mBakedTextureDatas[baked].mMaskedMorphs)
            if (auto* morph=dynamic_cast<LLPolyMorphTarget*>(masked->mMorphTarget)) morph->applyMask(data,w,h,components,masked->mInvert);
        shapeDirty=true;
    }
    LLVector3 getCharacterPosition() override { return position; }
    LLQuaternion getCharacterRotation() override { return rotation; }
    LLVector3 getCharacterVelocity() override { return velocity; }
    LLVector3 getCharacterAngularVelocity() override { return angularVelocity; }
    void getGround(const LLVector3& input,LLVector3& output,LLVector3& normal) override
    { if (!ground(input,output,normal)) {output=input;normal=LLVector3::zero;} }
    F32 getTimeDilation() override { return timeDilation; }
    F32 getPixelArea() const override { return pixelArea; }
    LLVector3d getPosGlobalFromAgent(const LLVector3& p) override { return from_region_handle(handle)+LLVector3d(p); }
    LLVector3 getPosAgentFromGlobal(const LLVector3d& p) override { return LLVector3(p-from_region_handle(handle)); }
    void addDebugText(const std::string&) override {}
    const LLUUID& getID() const override { return id; }
    void pose()
    {
        const bool groundSitting = animations.count(ANIM_AGENT_SIT_GROUND_CONSTRAINED) != 0;
        LLVector3  root          = position + (objectSitting ? hover * rotation : groundSitting ? LLVector3::zero : hover);
        if (control)
            root += constraintPosition;
        if (!control && !objectSitting)
            root.mV[2] -= .5f * mBodySize.mV[2] - mPelvisToFoot;
        if (!control && !sitting && visibility == Visibility::Jellydoll && simulatorHeight > 0)
            root.mV[VZ] -= .5f * (simulatorHeight - mBodySize.mV[VZ]);
        F32 pelvisFixup = 0;
        if (!control && hasPelvisFixup(pelvisFixup))
            root.mV[2] += pelvisFixup;
        mRoot->setWorldPosition(root);
        mRoot->setWorldRotation(control ? bindRotation * rotation : rotation);
        auto attention = [&](Attention& target, double deadline, const char* name, LLVector3& value, bool point)
        {
            if (!target || (deadline && LLFrameTimer::getTotalSeconds() > deadline))
            {
                removeAnimationData(name);
                if(target && !point)vs_attention_motion(*this,false,false,false,ANIM_AGENT_HEAD_ROT,ANIM_AGENT_EDITING);
                target = {};
                return;
            }
            const auto position = target();
            if (!position || !position->isFinite())
            {
                removeAnimationData(name);
                if(target && !point)vs_attention_motion(*this,false,false,false,ANIM_AGENT_HEAD_ROT,ANIM_AGENT_EDITING);
                target = {};
                return;
            }
            if (!point && gSavedSettings.getBOOL("DisableLookAtAnimation"))
            {
                removeAnimationData(name);
                vs_attention_motion(*this,false,true,true,ANIM_AGENT_HEAD_ROT,ANIM_AGENT_EDITING);
                return;
            }
            value = getPosAgentFromGlobal(*position) - (point ? this->position : mHeadp->getWorldPosition());
            setAnimationData(name, &value);
            vs_attention_motion(*this,point,true,false,ANIM_AGENT_HEAD_ROT,ANIM_AGENT_EDITING);
        };
        attention(lookTarget, lookDeadline, "LookAtPoint", lookPoint, false);
        attention(pointTarget, pointDeadline, "PointAtPoint", pointPoint, true);
        // Transform messages and scene publication can both request a pose in
        // one frame. Advance the motion controller only once, but always
        // propagate an authoritative transform to the skeleton.
        const U32 frame = LLFrameTimer::getFrameCount();
        if (!motionFrameKnown || motionFrame != frame)
        {
            updateMotions(NORMAL_UPDATE);
            motionFrame      = frame;
            motionFrameKnown = true;
        }
        if (!objectSitting && groundSitting && hover.mV[VZ] != 0)
            mRoot->setWorldPosition(mRoot->getWorldPosition() + LLVector3(0, 0, hover.mV[VZ]));
        mRoot->updateWorldMatrixChildren();
        std::vector<F32>              signature;
        std::function<void(LLJoint*)> visit = [&](LLJoint* joint)
        {
            const auto& matrix = joint->getWorldMatrix();
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned column = 0; column < 4; ++column)
                    signature.push_back(matrix.mMatrix[row][column]);
            for (auto* child : joint->mChildren)
                visit(child);
        };
        visit(mRoot);
        // Emotes and other visual-param motions can change a morph without
        // changing any joint. They participate in the same publication key.
        for (auto* param = getFirstVisualParam(); param; param = getNextVisualParam())
            signature.push_back(param->getWeight());
        signature.push_back(clothEnabled ? 1.f : 0.f);
        if (clothEnabled)
        {
            for (const auto& vector : { cloth.wind, cloth.eye, cloth.at })
                signature.insert(signature.end(), vector.mV, vector.mV + 3);
            signature.insert(signature.end(), { cloth.strength, cloth.frequency, cloth.phase });
        }
        if (signature != poseSignature)
        {
            poseSignature = std::move(signature);
            ++poseGeneration;
        }
    }
    bool skin(LLAvatarJointMesh& part,std::vector<VSWorldRenderer::Vertex>& vertices,std::vector<unsigned>& indices)
    {
        auto* mesh=part.getMesh();if (!mesh || !mesh->getNumVertices() || mesh->getNumVertices()>65536 || mesh->getNumFaces()>131072) return false;
        auto* reference=mesh->getReferenceMesh();const auto& joints=reference->mJointRenderData;
        std::vector<LLMatrix4> palette;std::vector<LLVector3> pivots;
        if (mesh->hasWeights())
        {
            if (joints.empty() || joints.size()>256) return false;
            bool last=false;
            for (auto* data:joints)
            {
                palette.push_back(*data->mWorldMatrix);
                if (data->mSkinJoint)
                {
                    if (!last) pivots.push_back(data->mSkinJoint->mRootToParentJointSkinOffset);
                    pivots.push_back(data->mSkinJoint->mRootToJointSkinOffset);last=true;
                }
                else last=false;
            }
            if (pivots.size()!=palette.size()) return false;
            for (unsigned i=0;i<palette.size();++i) palette[i].translate(pivots[i]*palette[i].getMat3());
        }
        std::vector<LLMatrix4a> aligned(palette.size());
        for (unsigned i=0;i<palette.size();++i) aligned[i].loadu(palette[i]);
        LLMatrix4a rigid;rigid.loadu(part.getWorldMatrix());
        for (unsigned i=0;i<mesh->getNumVertices();++i)
        {
            LLMatrix4a blend=rigid;
            if (mesh->hasWeights())
            {
                const float weight=mesh->getWeights()[i];
                if (!std::isfinite(weight) || weight<0 || weight>=float(aligned.size())) return false;
                const unsigned joint=unsigned(weight);const float w=weight-joint;
                if (w && joint+1>=aligned.size()) return false;
                if (w) blend.setLerp(aligned[joint],aligned[joint+1],w);else blend=aligned[joint];
            }
            LLVector4a p,n;blend.affineTransform(mesh->getCoords()[i],p);blend.rotate(mesh->getNormals()[i],n);
            LLVector3  normal(n.getF32ptr());
            normal.normalize();
            if (clothEnabled && mesh->getClothingWeights())
            {
                const auto* clothing = mesh->getClothingWeights()[i].getF32ptr();
                const F32   amount   = vs_avatar_cloth(cloth, LLVector3(p.getF32ptr()), normal, clothing[3]);
                LLVector4a  offset;
                offset.set(clothing[0] * amount, clothing[1] * amount, clothing[2] * amount, 0);
                LLVector4a transformed;
                blend.rotate(offset, transformed);
                p.add(transformed);
            }
            const auto* c  = p.getF32ptr();
            const auto& uv = mesh->getTexCoords()[i];
            vertices.push_back({c[0],c[1],c[2],normal.mV[0],normal.mV[1],normal.mV[2],uv.mV[0],1-uv.mV[1]});
        }
        for (S32 f=0;f<mesh->getNumFaces();++f) for (unsigned c=0;c<3;++c)
        {
            const S32 index=mesh->getFaces()[f][c];if (index<0 || unsigned(index)>=vertices.size()) return false;indices.push_back(unsigned(index));
        }
        return !indices.empty();
    }
};
void VSWorldAvatar::initializeClass()
{
    if (!LLAvatarAppearance::getDictionary()) { LLAvatarAppearance::initClass("avatar_lad.xml","avatar_skeleton.xml");ownsAppearanceClass=true; }
}
void VSWorldAvatar::cleanupClass()
{
    if (ownsAppearanceClass) { LLAvatarAppearance::cleanupClass();LLPolyMesh::freeAllMeshes();ownsAppearanceClass=false; }
}
VSWorldAvatar::VSWorldAvatar(const LLUUID& id, bool self, U64 region, VSWorldRenderer& r, VSUIResources& u, GroundQuery g,
                             std::shared_ptr<const void> ownerReservation, bool controlAvatar) :
    mOwnerReservation(std::move(ownerReservation)),
    mImpl(std::make_unique<Impl>(id, self, region, r, u, std::move(g), controlAvatar))
{
}
VSWorldAvatar::~VSWorldAvatar()=default;
LLCharacter& VSWorldAvatar::character()
{ return *mImpl; }
bool VSWorldAvatar::resetSkeleton(bool resetAnimations)
{
    auto& avatar = *mImpl;
    if (!avatar.control && !avatar.appearanceKnown)
        return false;
    const bool jelly = avatar.visibility == Visibility::Jellydoll;
    if (!jelly)
    {
        avatar.normalWeights.clear();
        for (auto* param = avatar.getFirstVisualParam(); param; param = avatar.getNextVisualParam())
            avatar.normalWeights.emplace(param->getID(), param->getWeight());
    }
    const auto overrides = avatar.overrideSkins;
    attachmentSkins({});
    avatar.resetAppearanceSkeleton(jelly);
    avatar.appliedGlobalScale = 1;
    attachmentSkins(overrides);
    if (!jelly)
        avatar.normalWeights.clear();
    if (resetAnimations)
    {
        if (avatar.self)
            gAgent.stopCurrentAnimations();
        else
            resyncAnimations();
    }
    return true;
}
void VSWorldAvatar::resyncAnimations()
{
    auto& avatar = *mImpl;
    if (!avatar.control && avatar.visibility == Visibility::Jellydoll)
    {
        if (avatar.jellyMotion.notNull())
            avatar.LLCharacter::stopMotion(avatar.jellyMotion, true);
        avatar.jellyMotion.setNull();
        avatar.updateJellyMotion();
    }
    else
        for (const auto& [id, sequence] : avatar.animations)
        {
            const auto motion = avatar.control ? id : vs_remap_avatar_motion(id, avatar.getSex() == SEX_FEMALE);
            avatar.LLCharacter::stopMotion(motion, true);
            avatar.LLCharacter::startMotion(motion);
        }
    avatar.motionFrameKnown = false;
}
U64 VSWorldAvatar::ownerGeneration() const
{ return mImpl->generation; }
void VSWorldAvatar::environment(const LLVector3& wind, F32 waterHeight, const LLVector3& eye, const LLVector3& at)
{
    auto& avatar        = *mImpl;
    avatar.clothEnabled = !avatar.control && avatar.visibility == Visibility::Normal && gSavedSettings.getBOOL("RenderAvatarCloth");
    if (!avatar.clothEnabled || !finite(wind) || !finite(eye) || !finite(at))
        return;
    const double now = LLFrameTimer::getTotalSeconds();
    if (now != avatar.clothTime)
    {
        const F32 dt          = avatar.clothTime ? F32(std::clamp(now - avatar.clothTime, 0.0, .2)) : 0;
        avatar.clothTime      = now;
        const F32  speed      = avatar.velocity.length();
        const bool underwater = avatar.position.mV[2] < waterHeight;
        const F32  hover      = underwater ? .1f : 0.f;
        LLVector3  direction  = wind - avatar.velocity;
        direction.mV[2] += hover;
        direction.normalize();
        const F32 strength      = std::min(.025f + speed * .015f + hover, .5f);
        const F32 interpolation = LLSmoothInterpolation::getInterpolant(strength > avatar.cloth.strength ? .2f : .4f);
        avatar.cloth.wind       = lerp(avatar.cloth.wind, direction, interpolation);
        avatar.cloth.strength   = lerp(avatar.cloth.strength, strength, interpolation);
        const F32 frequency     = hover + llclamp(8.f + speed * .7f + noise1(avatar.cloth.phase) * 4, 8.f, 25.f);
        avatar.clothFrequency   = lerp(avatar.clothFrequency, frequency, interpolation);
        if (underwater)
            avatar.clothFrequency *= .33f;
        avatar.cloth.phase     = std::fmod(avatar.cloth.phase + dt * avatar.clothFrequency, F_TWO_PI);
        avatar.cloth.frequency = 7 + noise1(avatar.cloth.phase) * 2;
    }
    avatar.cloth.eye = eye;
    avatar.cloth.at  = at;
}
bool VSWorldAvatar::inAir() const
{
    const auto& avatar=*mImpl;
    LLVector3 floor,normal;
    return !avatar.ground(avatar.position,floor,normal) ||
        avatar.position.mV[VZ]+avatar.hover.mV[VZ]-avatar.mPelvisToFoot-floor.mV[VZ]>.25f;
}
bool VSWorldAvatar::sitting() const
{ return mImpl->sitting; }
void VSWorldAvatar::sitting(bool sitting)
{
    auto&      avatar    = *mImpl;
    const bool effective = sitting || avatar.animations.count(ANIM_AGENT_SIT_GROUND_CONSTRAINED) != 0;
    if (avatar.sitting == effective && avatar.objectSitting == sitting)
        return;
    avatar.objectSitting = sitting;
    avatar.sitting       = effective;
    avatar.shapeDirty    = true;
    if (effective)
        avatar.LLCharacter::stopMotion(ANIM_AGENT_BODY_NOISE);
    else if (!avatar.control && avatar.visibility != Visibility::Jellydoll)
        avatar.LLCharacter::startMotion(ANIM_AGENT_BODY_NOISE);
    avatar.updateJellyMotion();
}
VSWorldAvatar::Visibility VSWorldAvatar::updateVisibility(U32 attachmentCost, F32 attachmentArea, bool animatedAttachments)
{
    auto& avatar = *mImpl;
    U32   cost   = attachmentCost;
    if (!avatar.control)
        for (const auto& [index, baked] : LLAvatarAppearance::getDictionary()->getBakedTextures())
        {
            if (index == BAKED_SKIRT && !avatar.isWearingWearableType(LLWearableType::WT_SKIRT))
                continue;
            const auto id = avatar.textures.getTE(U8(baked->mTextureIndex))->getID();
            if (id.notNull() && id != IMG_DEFAULT_AVATAR && id != IMG_INVISIBLE)
                cost = U32(std::min<U64>(U64(cost) + 200, std::numeric_limits<U32>::max()));
        }
    avatar.complexity     = cost;
    avatar.attachmentArea = attachmentArea;
    Visibility state      = Visibility::Normal;
    if (!avatar.self)
    {
        const bool friendAvatar = LLAvatarTracker::instance().isBuddy(avatar.id);
        const auto setting      = FSAvatarRenderPersistence::instance().getAvatarRenderSettings(avatar.id);
        if ((!avatar.control && gSavedSettings.getBOOL("RenderAvatarFriendsOnly") && !friendAvatar) ||
            (LLMuteList::instanceExists() && LLMuteList::instance().isMuted(avatar.id)))
            state = Visibility::Invisible;
        else if (setting != LLVOAvatar::AV_ALWAYS_RENDER)
        {
            const S32  mode          = gSavedSettings.getS32("RenderAvatarComplexityMode");
            const U32  maximum       = gSavedSettings.getU32("RenderAvatarMaxComplexity");
            const F32  maxArea       = gSavedSettings.getF32("RenderAutoMuteSurfaceAreaLimit");
            const bool friendExempt  = friendAvatar && mode > LLVOAvatar::AV_RENDER_LIMIT_BY_COMPLEXITY;
            const U32  exclusions    = gSavedSettings.getU32("FSImpostorAvatarExclude");
            const bool animeshExempt = (avatar.control && exclusions >= 2) || (animatedAttachments && (exclusions & 1));
            if (setting == LLVOAvatar::AV_DO_NOT_RENDER ||
                (!friendExempt && ((mode == LLVOAvatar::AV_RENDER_ONLY_SHOW_FRIENDS && !avatar.control) ||
                                   (!animeshExempt && maximum && (cost > maximum || (maxArea > 0 && attachmentArea > maxArea))))))
                state = Visibility::Jellydoll;
        }
    }
    if (state != avatar.visibility && !avatar.control)
    {
        const bool wasJelly = avatar.visibility == Visibility::Jellydoll;
        const bool isJelly  = state == Visibility::Jellydoll;
        if (isJelly)
        {
            avatar.normalWeights.clear();
            for (auto* param = avatar.getFirstVisualParam(); param; param = avatar.getNextVisualParam())
                avatar.normalWeights.emplace(param->getID(), param->getWeight());
            for (const auto& [id, sequence] : avatar.animations)
                avatar.stopMotion(id, true);
            for (const auto& id : { ANIM_AGENT_WALK_ADJUST, ANIM_AGENT_FLY_ADJUST, ANIM_AGENT_TARGET, ANIM_AGENT_BODY_NOISE })
                avatar.LLCharacter::stopMotion(id, true);
        }
        if (wasJelly && avatar.jellyMotion.notNull())
        {
            avatar.LLCharacter::stopMotion(avatar.jellyMotion, true);
            avatar.jellyMotion.setNull();
        }
        avatar.visibility = state;
        if (isJelly || wasJelly)
        {
            const auto overrides = avatar.overrideSkins;
            attachmentSkins({});
            avatar.resetAppearanceSkeleton(isJelly);
            attachmentSkins(overrides);
        }
        if (wasJelly && !isJelly)
        {
            const auto animations = avatar.animations;
            avatar.animations.clear();
            this->animations(animations);
            avatar.normalWeights.clear();
        }
        avatar.updateJellyMotion();
        avatar.clothEnabled = false;
    }
    else
        avatar.visibility = state;
    return state;
}
VSWorldAvatar::Visibility VSWorldAvatar::visibility() const
{ return mImpl->visibility; }
void VSWorldAvatar::attention(bool point, Attention target, F32 duration)
{
    if (!std::isfinite(duration) || duration < 0)
        return;
    auto& avatar                                         = *mImpl;
    if(!point && !target)vs_attention_motion(avatar,false,false,false,ANIM_AGENT_HEAD_ROT,ANIM_AGENT_EDITING);
    (point ? avatar.pointTarget : avatar.lookTarget)     = std::move(target);
    (point ? avatar.pointDeadline : avatar.lookDeadline) = duration ? LLFrameTimer::getTotalSeconds() + duration : 0;
    avatar.motionFrameKnown                              = false;
}
LLVector3 VSWorldAvatar::headPosition() const
{ return mImpl->mHeadp->getWorldPosition(); }
void VSWorldAvatar::controlConstraints(const LLVector3& official, const LLVector3* bounds, bool attached, const LLSD& rootSkin)
{
    auto& avatar = *mImpl;
    if (!avatar.control)
        return;
    const auto constraints    = vs_control_constraints(official, bounds, avatar.constraintPosition, avatar.constraintScale, attached,
                                                       gSavedSettings.getF32("AnimatedObjectsMaxLegalOffset"),
                                                       gSavedSettings.getF32("AnimatedObjectsMaxLegalSize"));
    avatar.constraintPosition = constraints.position;
    avatar.constraintScale    = constraints.scale;
    if (avatar.appliedGlobalScale != constraints.scale)
    {
        vs_scale_avatar_joints(avatar.mPelvisp, constraints.scale / avatar.appliedGlobalScale);
        avatar.appliedGlobalScale = constraints.scale;
        avatar.shapeDirty         = true;
    }
    avatar.bindRotation = LLQuaternion::DEFAULT;
    if (!attached && validSkinMetadata(rootSkin, false))
    {
        LLSD           copy = rootSkin;
        LLMeshSkinInfo skin(copy);
        avatar.bindRotation = LLSkinningUtil::getUnscaledQuaternion(LLMatrix4(skin.mBindShapeMatrix));
    }
}
LLAvatarAppearance& VSWorldAvatar::appearanceOwner()
{ return *mImpl; }
const std::map<LLUUID, S32>& VSWorldAvatar::signaledAnimations() const
{ return mImpl->animations; }
const std::multimap<LLUUID, LLUUID>& VSWorldAvatar::animationSources() const
{ return mImpl->animationSources; }
void VSWorldAvatar::invalidateLocalAppearance()
{
    auto& avatar = *mImpl;
    avatar.compositeSignature.clear();
    avatar.localAppearance = avatar.self;
    for (auto& data : avatar.mBakedTextureDatas)
        if (auto* layers = dynamic_cast<NativeLayers*>(data.mTexLayerSet))
            layers->pending = true;
}
U32 VSWorldAvatar::wearableCount(unsigned type) const
{ return type < unsigned(LLWearableType::WT_COUNT) ? mImpl->getWearableCount(LLWearableType::EType(type)) : 0; }
std::vector<LLUUID> VSWorldAvatar::wearableItems(unsigned type) const
{
    std::vector<LLUUID> result;
    if (type >= unsigned(LLWearableType::WT_COUNT))
        return result;
    for (const auto& wearable : mImpl->ownedWearables)
        if (unsigned(wearable->getType()) == type)
            if (const auto* native = dynamic_cast<const VSWearable*>(wearable.get()))
                result.push_back(native->item());
    return result;
}
bool VSWorldAvatar::wearables(const std::vector<std::shared_ptr<LLWearable>>& wearables, S32 cofVersion)
{
    auto& avatar = *mImpl;
    if (!avatar.self || wearables.size() > LLWearableData::MAX_CLOTHING_LAYERS + LLWearableData::MAX_BODY_PARTS ||
        (cofVersion >= 0 && cofVersion < avatar.pendingCOFVersion))
        return false;
    std::map<LLWearableType::EType, unsigned> counts;
    unsigned                                  clothing = 0;
    for (const auto& wearable : wearables)
    {
        if (!wearable || wearable->getType() < 0 || wearable->getType() >= LLWearableType::WT_COUNT)
            return false;
        const auto type = wearable->getType();
        if (++counts[type] > 1 && !LLWearableType::getInstance()->getAllowMultiwear(type))
            return false;
        if (LLWearableType::getInstance()->getAssetType(type) == LLAssetType::AT_CLOTHING &&
            ++clothing > LLWearableData::MAX_CLOTHING_LAYERS)
            return false;
    }
    for (const auto type : { LLWearableType::WT_SHAPE, LLWearableType::WT_SKIN, LLWearableType::WT_HAIR, LLWearableType::WT_EYES })
        if (counts[type] != 1)
            return false;
    avatar.mWearableDatas.clear();
    avatar.ownedWearables = wearables;
    for (const auto& wearable : avatar.ownedWearables)
        avatar.pushWearable(wearable->getType(), wearable.get(), false);
    // Wearable-type ordering and the top wearable determine authoritative
    // geometry values; the compositor still consumes every clothing layer.
    for (const auto& [type, entries] : avatar.mWearableDatas)
        if (!entries.empty())
            entries.back()->writeToAvatar(&avatar);
    for (const auto& [index, entry] : LLAvatarAppearance::getDictionary()->getTextures())
        if (entry->mIsLocalTexture)
        {
            const auto* wearable = avatar.getTopWearable(entry->mWearableType);
            const auto* local    = wearable ? wearable->getLocalTextureObject(index) : nullptr;
            avatar.textures.setTETexture(U8(index), local ? local->getID() : IMG_DEFAULT_AVATAR);
        }
    avatar.updateVisualParams();
    avatar.computeBodySize();
    avatar.pendingCOFVersion = cofVersion;
    avatar.appearanceKnown   = true;
    avatar.shapeDirty        = true;
    invalidateLocalAppearance();
    return true;
}
void VSWorldAvatar::rebase(U64 handle,GroundQuery ground)
{
    auto& avatar=*mImpl;
    if(avatar.handle!=handle)
    {
        if(avatar.positionKnown)avatar.position=LLVector3(from_region_handle(avatar.handle)+LLVector3d(avatar.position)-from_region_handle(handle));
        avatar.handle=handle;avatar.shapeDirty=true;avatar.meshes.clear();
    }
    avatar.ground=std::move(ground);
}
void VSWorldAvatar::transform(const LLVector3& p, const LLQuaternion& r, const LLVector3& v, const LLVector3& a, F32 simulatorHeight)
{
    if (!finite(p) || !r.isFinite() || !finite(v) || !finite(a)) return;
    if (std::isfinite(simulatorHeight) && simulatorHeight > 0)
        mImpl->simulatorHeight = simulatorHeight;
    mImpl->position=p;mImpl->rotation=r;mImpl->velocity=v;mImpl->angularVelocity=a;mImpl->positionKnown=true;mImpl->pose();
}
bool VSWorldAvatar::appearance(LLMessageSystem& msg)
{
    auto& a=*mImpl;LLUUID id;msg.getUUID("Sender","ID",id);if (id!=a.id) return false;
    U8 version=0;S32 cof=-1;
    if (msg.getNumberOfBlocks("AppearanceData")) {msg.getU8("AppearanceData","AppearanceVersion",version);msg.getS32("AppearanceData","CofVersion",cof);}
    if (version>1 || (version && cof<a.cofVersion)) return false;
    if (version && a.localAppearance && a.pendingCOFVersion >= 0 && cof < a.pendingCOFVersion)
        return false;
    LLPrimitive textures;textures.setNumTEs(TEX_NUM_INDICES);
    if (textures.unpackTEMessage(&msg,LLMessageStringTable::getInstance()->getString("ObjectData"),0)==TEM_INVALID) return false;
    const S32 count=msg.getNumberOfBlocks("VisualParam");if (count<0 || count>1024) return false;
    std::vector<std::pair<LLVisualParam*,F32>> params;
    auto* param=a.getFirstVisualParam();
    for (S32 i=0;i<count;++i)
    {
        while (param && param->getGroup()!=VISUAL_PARAM_GROUP_TWEAKABLE && param->getGroup()!=VISUAL_PARAM_GROUP_TRANSMIT_NOT_TWEAKABLE) param=a.getNextVisualParam();
        if (!param) break;
        U8 value;msg.getU8("VisualParam","ParamValue",value,i);
        params.emplace_back(param,U8_to_F32(value,param->getMinWeight(),param->getMaxWeight()));param=a.getNextVisualParam();
    }
    LLVector3 hover;
    if (msg.getNumberOfBlocks("AppearanceHover")) msg.getVector3("AppearanceHover","HoverHeight",hover);
    if (!finite(hover) || hover.lengthSquared()>65536) return false;
    std::map<LLUUID,unsigned> attachments;
    const S32 attached=msg.getNumberOfBlocks("AttachmentBlock");if (attached>256) return false;
    for (S32 i=0;i<attached;++i) {LLUUID uuid;U8 point;msg.getUUID("AttachmentBlock","ID",uuid,i);msg.getU8("AttachmentBlock","AttachmentPoint",point,i);if (uuid.notNull()) attachments[uuid]=point;}
    for (unsigned i=0;i<TEX_NUM_INDICES;++i) a.textures.setTE(U8(i),*textures.getTE(U8(i)));
    for (auto& [p, w] : params)
        if (a.visibility == Visibility::Jellydoll)
            a.normalWeights[p->getID()] = w;
        else
            a.setVisualParamWeight(p, w);
    a.updateVisualParams();a.computeBodySize();a.hover=hover;a.cofVersion=cof;a.serverBakes=version!=0;a.simAttachments=std::move(attachments);
    a.appearanceKnown = true;
    a.shapeDirty      = true;
    if (version && cof >= a.pendingCOFVersion)
    {
        a.localAppearance = false;
        for (const auto& [baked, key] : a.composites)
            a.resources.erase(key);
        a.composites.clear();
        a.appliedLocalMasks.clear();
    }
    return true;
}
bool VSWorldAvatar::animations(LLMessageSystem& msg)
{
    auto& a=*mImpl;LLUUID id;msg.getUUID("Sender","ID",id);if (id!=a.id) return false;
    const S32 count=msg.getNumberOfBlocks("AnimationList");if (count<0 || count>256) return false;
    std::map<LLUUID,S32> next;
    std::multimap<LLUUID, LLUUID> sources;
    const S32                     sourceCount = msg.getNumberOfBlocks("AnimationSourceList");
    if (sourceCount < 0 || sourceCount > 256)
        return false;
    for (S32 i = 0; i < count; ++i)
    {
        LLUUID animation;
        S32    sequence;
        msg.getUUID("AnimationList", "AnimID", animation, i);
        msg.getS32("AnimationList", "AnimSequenceID", sequence, i);
        if (animation.notNull())
            next[animation] = sequence;
        if (i < sourceCount && animation.notNull())
        {
            LLUUID source;
            msg.getUUID("AnimationSourceList", "ObjectID", source, i);
            if (source.notNull())
                sources.emplace(source, animation);
        }
    }
    a.animationSources = std::move(sources);
    animations(next);
    return true;
}
void VSWorldAvatar::animations(const std::map<LLUUID, S32>& next)
{
    auto& a = *mImpl;
    if (next == a.animations)
        return;
    if (!a.control && a.visibility == Visibility::Jellydoll)
    {
        a.animations = next;
        sitting(a.objectSitting);
        a.updateJellyMotion();
        return;
    }
    for (const auto& [uuid,sequence]:a.animations) if (!next.count(uuid)) a.stopMotion(uuid);
    for (const auto& [uuid, sequence] : next)
        if (!a.animations.count(uuid) || a.animations[uuid] != sequence)
        {
            a.stopMotion(uuid, true);
            a.startMotion(uuid);
        }
    a.animations = next;
    sitting(a.objectSitting);
    if (!a.control)
    {
        bool walking = false, aiming = false;
        for (S32 index = 0; index < NUM_AGENT_WALK_ANIMS; ++index)
            walking |= next.count(AGENT_WALK_ANIMS[index]) != 0;
        for (S32 index = 0; index < NUM_AGENT_GUN_AIM_ANIMS; ++index)
            aiming |= next.count(AGENT_GUN_AIM_ANIMS[index]) != 0;
        if (walking)
            a.LLCharacter::startMotion(ANIM_AGENT_WALK_ADJUST);
        else
            a.LLCharacter::stopMotion(ANIM_AGENT_WALK_ADJUST);
        if (aiming)
        {
            a.LLCharacter::startMotion(ANIM_AGENT_TARGET);
            a.LLCharacter::stopMotion(ANIM_AGENT_BODY_NOISE);
        }
        else
        {
            a.LLCharacter::stopMotion(ANIM_AGENT_TARGET);
            if (!a.sitting)
                a.LLCharacter::startMotion(ANIM_AGENT_BODY_NOISE);
        }
    }
    a.motionFrameKnown = false;
}
bool VSWorldAvatar::visibleFrom(const LLVector3& eye, float farClip) const
{
    const auto& a = *mImpl;
    return a.positionKnown && a.appearanceKnown && a.visibility != Visibility::Invisible &&
           (a.position - eye).lengthSquared() <= farClip * farClip;
}
bool VSWorldAvatar::isControlAvatar() const
{ return mImpl->control; }
void VSWorldAvatar::append(std::vector<VSWorldRenderer::Packet>& packets, const LLVector3& eye, float farClip, unsigned viewportHeight,
                           float timeDilation, unsigned viewportWidth, unsigned visibleAvatars)
{
    auto& a = *mImpl;
    if (a.screen)
    {
        const F32 aspect = viewportHeight && viewportWidth ? F32(viewportWidth) / viewportHeight : 1;
        a.screen->setScale(LLVector3(1, aspect, 1));
        a.screen->updateWorldMatrixChildren();
    }
    if (!a.positionKnown || !a.appearanceKnown)
        return;
    if (a.visibility == Visibility::Invisible)
    {
        a.meshes.clear();
        a.publications.clear();
        return;
    }
    const float distance = (a.position - eye).lengthSquared();
    if (distance > farClip * farClip)
    {
        a.meshes.clear();
        a.publications.clear();
        return;
    }
    a.timeDilation=llclamp(timeDilation,0.f,1.f);
    const float radius=.5f*a.mBodySize.length(),projected=radius*float(viewportHeight)*.5f/std::tan(F_PI/6);
    a.pixelArea=F_PI*projected*projected/std::max(distance,.01f);
    const auto session    = VSNativeSession::active();
    const bool fullDetail = a.self && session && session->cameraState().firstPerson;
    const F32 lodArea = vs_avatar_lod_area(a.pixelArea, gSavedSettings.getF32("RenderAvatarLODFactor"), visibleAvatars, a.self, fullDetail);
    if (!a.control && a.visibility != Visibility::Jellydoll)
    {
        bool walking = false;
        for (S32 index = 0; index < NUM_AGENT_WALK_ANIMS; ++index)
            walking |= a.animations.count(AGENT_WALK_ANIMS[index]) != 0;
        const bool inAir = this->inAir();
        if (walking || !inAir || a.sitting)
            a.LLCharacter::stopMotion(ANIM_AGENT_FLY_ADJUST);
        else
            a.LLCharacter::startMotion(ANIM_AGENT_FLY_ADJUST);
    }
    a.pose();
    a.compose();
    // Apply every newly decoded mask before publishing any body part. A mask
    // can invalidate other meshes, not just the part that uses its texture.
    for (const auto& [bakedIndex, baked] : LLAvatarAppearance::getDictionary()->getBakedTextures())
    {
        if (a.localAppearance && a.composites.count(bakedIndex))
            continue;
        const LLUUID& texture = a.textures.getTE(U8(baked->mTextureIndex))->getID();
        if (auto mask = vs_ui_image_aux(texture); mask && a.appliedMasks[bakedIndex].get() != mask.get())
        {
            a.applyMorphMask(mask->getData(), mask->getWidth(), mask->getHeight(), 1, bakedIndex);
            a.appliedMasks[bakedIndex] = mask;
        }
    }
    if (a.shapeDirty)
    {
        ++a.shapeGeneration;
        a.shapeDirty = false;
    }
    struct Selection
    {
        S32                 index;
        LLAvatarJointMesh*  part;
        VSUIRenderer::Image image;
    };
    struct Replacement
    {
        VSWorldRenderer::Mesh mesh;
        Impl::Publication     publication;
    };
    std::vector<Selection>     selected;
    std::map<S32, Replacement> replacements;
    bool                       ready        = true;
    const auto                 hiddenByView = [&](S32 index)
    {
        if (!a.self)
            return false;
        const auto session     = VSNativeSession::active();
        const bool firstPerson = session && session->cameraState().firstPerson;
        if ((firstPerson && !gSavedSettings.getBOOL("FirstPersonAvatarVisible")) || gRlvHandler.hasBehaviour(RLV_BHVR_SHOWSELF))
            return true;
        const bool head = index == MESH_ID_HEAD || index == MESH_ID_HAIR || index == MESH_ID_EYEBALL_LEFT ||
                          index == MESH_ID_EYEBALL_RIGHT || index == MESH_ID_EYELASH;
        return head && (firstPerson || gRlvHandler.hasBehaviour(RLV_BHVR_SHOWSELFHEAD));
    };
    for (const auto& [index,entry]:LLAvatarAppearance::getDictionary()->getMeshEntries())
    {
        if (hiddenByView(index))
            continue;
        if (entry->mBakedID==BAKED_NUM_INDICES || unsigned(index)>=a.mMeshLOD.size()) continue;
        if (index == MESH_ID_SKIRT && !a.isWearingWearableType(LLWearableType::WT_SKIRT))
            continue;
        if (a.visibility == Visibility::Jellydoll && index == MESH_ID_HAIR)
            continue;
        const auto* baked=LLAvatarAppearance::getDictionary()->getBakedTexture(entry->mBakedID);
        const LLUUID& texture=a.textures.getTE(U8(baked->mTextureIndex))->getID();
        VSUIRenderer::Image image;
        if (a.localAppearance && a.composites.count(entry->mBakedID))
            image = a.resources.image(a.composites.at(entry->mBakedID));
        else if (a.visibility != Visibility::Jellydoll)
        {
            if (texture.isNull() || texture == IMG_DEFAULT_AVATAR || texture == IMG_INVISIBLE)
                continue;
            LLUI::getUIImageByID(texture);
            image = a.resources.image("remote:" + vs_native_image_id(texture).asString());
        }
        if (!image && a.visibility != Visibility::Jellydoll)
        {
            if (a.meshes.count(index) && a.publications[index].image)
            {
                selected.push_back({ index, a.publications[index].part, a.publications[index].image });
                ready = false;
            }
            continue;
        }
        auto& parts=a.mMeshLOD[index]->mMeshParts;LLAvatarJointMesh* part=nullptr;
        for (auto* candidate : parts)
        {
            part = candidate;
            if (lodArea >= candidate->getLOD())
                break;
        }
        if (!part) continue;
        selected.push_back({ index, part, image });
        const auto  found     = a.publications.find(index);
        const auto* published = found == a.publications.end() ? nullptr : &found->second;
        if (!a.meshes[index] || !published || published->part != part || published->pose != a.poseGeneration ||
            published->shape != a.shapeGeneration)
        {
            std::vector<VSWorldRenderer::Vertex> vertices;
            std::vector<unsigned>                indices;
            if (!a.skin(*part, vertices, indices))
            {
                ready = false;
                continue;
            }
            auto mesh = a.renderer.upload(vertices, indices);
            if (!mesh)
            {
                ready = false;
                continue;
            }
            replacements[index] = { std::move(mesh), { part, a.poseGeneration, a.shapeGeneration, image } };
        }
    }
    // A rejected body generation must not mix old and new poses across eyes,
    // head, torso, limbs, hair and skirt. Commit every candidate together.
    if (ready)
    {
        for (auto& [index, replacement] : replacements)
        {
            a.meshes[index]       = std::move(replacement.mesh);
            a.publications[index] = std::move(replacement.publication);
        }
        a.publishedParts.clear();
        for (const auto& item : selected)
        {
            a.publications[item.index].image = item.image;
            a.publishedParts.push_back(item.index);
        }
        // Topology changes (for example taking off a skirt) retire removed
        // body parts only when the entire replacement generation is admitted.
        for (auto mesh = a.meshes.begin(); mesh != a.meshes.end();)
            if (std::find(a.publishedParts.begin(), a.publishedParts.end(), mesh->first) == a.publishedParts.end())
            {
                a.publications.erase(mesh->first);
                mesh = a.meshes.erase(mesh);
            }
            else
                ++mesh;
    }
    for (const auto index : a.publishedParts)
    {
        if (hiddenByView(index))
            continue;
        const auto published = a.publications.find(index);
        if (!a.meshes[index] || published == a.publications.end() || !published->second.part)
            continue;
        VSWorldRenderer::Packet packet;
        packet.mesh      = a.meshes[index];
        packet.image     = published->second.image;
        packet.masked    = true;
        packet.faceIndex = unsigned(index);
        packet.alpha     = published->second.part->isTransparent();
        if (a.visibility == Visibility::Jellydoll)
        {
            packet.image.reset();
            const auto colour = LLColor4::grey4;
            packet.color      = { colour.mV[0], colour.mV[1], colour.mV[2], colour.mV[3] };
            packet.alpha      = false;
            packet.masked     = false;
            packet.fullbright = true;
        }
        packet.distanceSquared = distance;
        packets.push_back(std::move(packet));
    }
}
U64 VSWorldAvatar::poseGeneration() const
{ return mImpl->poseGeneration; }
bool VSWorldAvatar::hudAttachment(unsigned point) const
{ return mImpl->self && mImpl->hudPoints.count(point); }
unsigned VSWorldAvatar::attachmentPoint(const std::string& name) const
{
    for (const auto& [point, joint] : mImpl->attachments)
        if (joint->getName() == name)
            return point;
    return 0;
}
std::vector<VSWorldAvatar::AttachmentPoint> VSWorldAvatar::attachmentPoints() const
{
    std::vector<AttachmentPoint> result;
    for (const auto& [point, joint] : mImpl->attachments)
        result.push_back({ point, joint->getName(), bool(mImpl->hudPoints.count(point)), bool(mImpl->firstPersonPoints.count(point)) });
    return result;
}
bool VSWorldAvatar::attachmentVisible(unsigned point, bool firstPerson) const
{ return !mImpl->self || !firstPerson || mImpl->hudPoints.count(point) || mImpl->firstPersonPoints.count(point); }
void VSWorldAvatar::attachmentSkins(const std::map<LLUUID, LLSD>& skins)
{
    auto&                  a = *mImpl;
    std::map<LLUUID, LLSD> accepted;
    for (const auto& [asset, metadata] : skins)
        if (asset.notNull() && validSkinMetadata(metadata, true))
            accepted.emplace(asset, metadata);
    if (accepted == a.overrideSkins)
        return;
    if (a.control && a.appliedGlobalScale != 1)
    {
        vs_scale_avatar_joints(a.mPelvisp, 1 / a.appliedGlobalScale);
        a.appliedGlobalScale = 1;
    }
    const auto label = a.id.asString();
    // Shared LLJoint arbitration restores the winning remaining override, or
    // the original skeleton value when its final owner detaches.
    for (const auto& [asset, metadata] : a.overrideSkins)
    {
        std::function<void(LLJoint*)> restore = [&](LLJoint* joint)
        {
            bool changed = false;
            joint->removeAttachmentPosOverride(asset, label, changed);
            joint->removeAttachmentScaleOverride(asset, label);
            for (auto* child : joint->mChildren)
                restore(child);
        };
        restore(a.mRoot);
        a.removePelvisFixup(asset);
    }
    a.overrideSkins.clear();
    for (const auto& [asset, metadata] : accepted)
    {
        const auto& names     = metadata["joint_names"];
        const auto& alternate = metadata["alt_inverse_bind_matrix"];
        if (asset.isNull() || !names.isArray() || !names.size() || names.size() > 256 || !alternate.isArray() ||
            alternate.size() != names.size())
            continue;
        bool valid = true;
        for (unsigned i = 0; i < names.size(); ++i)
        {
            valid &= names[i].isString() && names[i].asString().size() <= 256 && alternate[i].isArray() && alternate[i].size() == 16;
            for (unsigned element = 0; element < 16 && valid; ++element)
                valid &= std::isfinite(float(alternate[i][element].asReal()));
        }
        if (!valid || !std::isfinite(float(metadata["pelvis_offset"].asReal())))
            continue;
        LLSD           copy = metadata;
        LLMeshSkinInfo skin(copy);
        for (unsigned i = 0; i < skin.mJointNames.size(); ++i)
        {
            auto* joint = a.getJoint(skin.mJointNames[i]);
            if (!joint)
                continue;
            const LLVector3 position(skin.mAlternateBindMatrix[i].getTranslation());
            if (!joint->aboveJointPosThreshold(position))
                continue;
            bool changed = false;
            joint->addAttachmentPosOverride(position, asset, label, changed);
            if (skin.mLockScaleIfJointPosition)
                joint->addAttachmentScaleOverride(joint->getDefaultScale(), asset, label);
        }
        if (skin.mPelvisOffset != 0)
            a.addPelvisFixup(skin.mPelvisOffset, asset);
        a.overrideSkins[asset] = metadata;
    }
    a.computeBodySize();
    a.shapeDirty = true;
    a.pose();
}
bool VSWorldAvatar::attachmentTransform(unsigned point,LLVector3& p,LLQuaternion& q)
{
    auto& a=*mImpl;const auto i=a.attachments.find(point);if (i==a.attachments.end() || !a.positionKnown) return false;
    p=i->second->getWorldPosition();q=i->second->getWorldRotation();return finite(p) && q.isFinite();
}
bool VSWorldAvatar::skinRiggedFace(const LLVolumeFace& face,const LLSD& metadata,std::vector<VSWorldRenderer::Vertex>& vertices)
{
    auto& a=*mImpl;
    if (!a.positionKnown || !face.mWeights || face.mNumVertices <= 0 || face.mNumVertices > 65536 || !face.mPositions || !face.mNormals ||
        !face.mTexCoords || !validSkinMetadata(metadata, false))
        return false;
    const auto& names=metadata["joint_names"];
    if (!names.isArray() || !names.size() || names.size()>256) return false;
    if (!metadata["inverse_bind_matrix"].isArray() || metadata["inverse_bind_matrix"].size()!=names.size()) return false;
    auto matrixValid=[](const LLSD& matrix) {
        if (!matrix.isArray() || matrix.size()!=16) return false;
        for (unsigned i=0;i<16;++i) if (!std::isfinite(float(matrix[i].asReal()))) return false;
        return true;
    };
    for (unsigned i=0;i<names.size();++i)
        if (!names[i].isString() || names[i].asString().size()>256 || !matrixValid(metadata["inverse_bind_matrix"][i])) return false;
    if (metadata.has("bind_shape_matrix") && !matrixValid(metadata["bind_shape_matrix"])) return false;
    LLSD source=metadata;LLMeshSkinInfo skin(source);
    if (skin.mJointNames.size()!=skin.mInvBindMatrix.size() || skin.mJointNames.empty()) return false;
    std::vector<LLMatrix4a> palette(skin.mJointNames.size());
    for (unsigned i=0;i<palette.size();++i)
    {
        auto* joint=a.mRoot->findJoint(skin.mJointNames[i]);
        if (!joint) joint=a.mRoot->findJoint("mPelvis");
        if (!joint) return false;
        matMulUnsafe(skin.mInvBindMatrix[i],joint->getWorldMatrix4a(),palette[i]);
    }
    vertices.reserve(face.mNumVertices);
    for (S32 v=0;v<face.mNumVertices;++v)
    {
        const auto* packed=face.mWeights[v].getF32ptr();unsigned indices[4]{};float weights[4]{},total=0;
        for (unsigned c=0;c<4;++c)
        {
            if (!std::isfinite(packed[c]) || packed[c]<0) return false;
            const float integer=std::floor(packed[c]);
            weights[c]=packed[c]-integer;
            if (weights[c] && integer>=float(palette.size())) return false;
            indices[c]=weights[c]?unsigned(integer):0;total+=weights[c];
        }
        if (total<1.e-6f) return false;
        LLMatrix4a blend;blend.setMul(palette[indices[0]],weights[0]/total);
        for (unsigned c=1;c<4;++c) {LLMatrix4a term;term.setMul(palette[indices[c]],weights[c]/total);blend.add(term);}
        LLVector4a bound,point,normalBound,normal;
        skin.mBindShapeMatrix.affineTransform(face.mPositions[v],bound);blend.affineTransform(bound,point);
        skin.mBindShapeMatrix.rotate(face.mNormals[v],normalBound);blend.rotate(normalBound,normal);
        const auto* p=point.getF32ptr();LLVector3 n(normal.getF32ptr());n.normalize();
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || !finite(n)) return false;
        const auto& uv=face.mTexCoords[v];
        VSWorldRenderer::Vertex vertex{p[0],p[1],p[2],n.mV[0],n.mV[1],n.mV[2],uv.mV[0],1-uv.mV[1]};
        if(face.mTangents)
        {
            LLVector4a tangentBound,tangent;
            skin.mBindShapeMatrix.rotate(face.mTangents[v],tangentBound);blend.rotate(tangentBound,tangent);
            LLVector3 t(tangent.getF32ptr());t.normalize();
            if(!finite(t)) return false;
            vertex.tx=t.mV[0];vertex.ty=t.mV[1];vertex.tz=t.mV[2];vertex.tw=face.mTangents[v].getF32ptr()[3];
        }
        vertices.push_back(vertex);
    }
    return true;
}
#if VS_VULKAN_DIAGNOSTICS
LLSD VSWorldAvatar::qualifyBodyLODs(const LLVector3& eye)
{
    auto& a       = *mImpl;
    auto  require = [](bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    require(a.self && a.localAppearance && a.positionKnown && a.appearanceKnown,
            "R2 body LOD check requires the actual connected local appearance");
    require(a.isWearingWearableType(LLWearableType::WT_SKIRT), "R2 body LOD check lacks the ordinary worn skirt branch");
    struct Restore
    {
        bool& state;
        bool  previous;
        ~Restore() { state = previous; }
    } restore{ a.clothEnabled, a.clothEnabled };
    a.clothEnabled = false; // Isolate LOD/publication from independently qualified cloth time.
    std::map<S32, std::set<LLAvatarJointMesh*>> observed;
    LLSD                                        report;
    for (unsigned height = 1; height <= 32768; height *= 2)
    {
        std::vector<VSWorldRenderer::Packet> packets;
        append(packets, eye, 128, height, 1, height);
        require(!packets.empty(), "R2 body LOD case published no visible geometry");
        std::map<unsigned, VSWorldRenderer::Mesh> first;
        for (const auto& packet : packets)
        {
            const auto& publication = a.publications.at(S32(packet.faceIndex));
            auto*       source      = publication.part->getMesh();
            const auto  description = VSWorldRenderer::geometryDescription(packet.mesh);
            const auto& indices     = VSWorldRenderer::geometryIndices(packet.mesh);
            const auto  buffers     = VSWorldRenderer::geometryBuffers(packet.mesh);
            require(source && buffers[0] && buffers[1] && description.vertices == unsigned(source->getNumVertices()) &&
                        description.indices == unsigned(source->getNumFaces()) * 3,
                    "R2 body LOD GPU publication differs from its shared reference topology");
            require(indices.size() == description.indices, "R2 body LOD lacks its admitted picking topology");
            for (S32 face = 0; face < source->getNumFaces(); ++face)
                for (unsigned corner = 0; corner < 3; ++corner)
                    require(indices[unsigned(face) * 3 + corner] == unsigned(source->getFaces()[face][corner]),
                            "R2 body LOD changed reference triangle order or winding");
            observed[S32(packet.faceIndex)].insert(publication.part);
            first.emplace(packet.faceIndex, packet.mesh);
            LLSD row;
            row["part"]           = S32(packet.faceIndex);
            row["part_name"]      = a.mMeshLOD[packet.faceIndex]->getName();
            const auto& parts     = a.mMeshLOD[packet.faceIndex]->mMeshParts;
            row["lod"]            = S32(std::distance(parts.begin(), std::find(parts.begin(), parts.end(), publication.part)));
            row["height"]         = S32(height);
            row["reference_area"] = publication.part->getLOD();
            row["vertices"]       = S32(description.vertices);
            row["indices"]        = S32(description.indices);
            report["cases"].append(row);
        }
        packets.clear();
        append(packets, eye, 128, height, 1, height);
        require(packets.size() == first.size(), "R2 unchanged pose changed body topology");
        for (const auto& packet : packets)
            require(first.at(packet.faceIndex) == packet.mesh, "R2 unchanged pose/LOD rebuilt admitted body geometry");
    }
    for (const auto& [index, entry] : LLAvatarAppearance::getDictionary()->getMeshEntries())
    {
        if (entry->mBakedID == BAKED_NUM_INDICES || unsigned(index) >= a.mMeshLOD.size())
            continue;
        const auto& expected = a.mMeshLOD[index]->mMeshParts;
        require(!expected.empty() && observed[index].size() == expected.size(), "R2 body LOD sweep omitted a reference body part/LOD");
        for (auto* part : expected)
            require(observed[index].count(part) != 0, "R2 body LOD sweep omitted a loaded reference mesh");
    }
    report["all_parts_and_lods"]             = true;
    report["unchanged_pose_reuses_geometry"] = true;
    return report;
}
#endif
LLSD VSWorldAvatar::evidence() const
{
    const auto& a=*mImpl;LLSD result;result["id"]=a.id;result["self"]=a.self;result["position_known"]=a.positionKnown;
    result["control_avatar"]                                                                         = a.control;
    result["sitting"]                                                                                = a.sitting;
    result["owner_generation"]=ll_sd_from_U64(a.generation);result["region_handle"]=ll_sd_from_U64(a.handle);
    result["appearance_known"]=a.appearanceKnown;result["server_bakes"]=a.serverBakes;result["cof_version"]=a.cofVersion;
    result["animations"]=S32(a.animations.size());result["attachments"]=S32(a.simAttachments.size());result["meshes"]=S32(a.meshes.size());
    result["pose_generation"]              = ll_sd_from_U64(a.poseGeneration);
    result["shape_generation"]             = ll_sd_from_U64(a.shapeGeneration);
    result["local_compositor_implemented"] = true;
    result["local_appearance"]             = a.localAppearance;
    result["compositor_cpu_bytes"]         = ll_sd_from_U64(a.compositeBudget.used());
    result["compositor_cpu_limit"]         = ll_sd_from_U64(a.compositeBudget.limit());
    result["composites"]                   = S32(a.composites.size());
    result["compositor_error"]             = a.compositeError;
    result["wearables"]                    = S32(a.ownedWearables.size());
    return result;
}
std::vector<VSWorldAvatar::LocalBake> VSWorldAvatar::localBakes() const
{
    const auto&            avatar = *mImpl;
    std::vector<LocalBake> result;
    if (!avatar.self || !avatar.localAppearance || avatar.compositeSignature.empty())
        return result;
    for (const auto& [baked, key] : avatar.composites)
    {
        result.push_back({ unsigned(baked), avatar.resources.generation(key), {}, {} });
    }
    return result;
}
VSWorldAvatar::LocalBake VSWorldAvatar::localBake(unsigned index) const
{
    const auto& avatar = *mImpl;
    const auto  found  = avatar.composites.find(EBakedTextureIndex(index));
    if (found == avatar.composites.end() || index >= avatar.mBakedTextureDatas.size())
        return {};
    const auto mask = avatar.appliedLocalMasks.find(EBakedTextureIndex(index));
    return { index, avatar.resources.generation(found->second), avatar.resources.rawImage(found->second),
             mask == avatar.appliedLocalMasks.end() ? LLPointer<LLImageRaw>{} : mask->second, avatar.resources.image(found->second) };
}
bool VSWorldAvatar::setLocalBake(unsigned index, U64 generation, const LLUUID& asset)
{
    auto& avatar = *mImpl;
    if (asset.isNull() || index >= BAKED_NUM_INDICES)
        return false;
    const auto found = avatar.composites.find(EBakedTextureIndex(index));
    if (found == avatar.composites.end() || avatar.resources.generation(found->second) != generation)
        return false;
    const auto texture = LLAvatarAppearance::getDictionary()->bakedToLocalTextureIndex(EBakedTextureIndex(index));
    avatar.textures.setTETexture(U8(texture), asset);
    return true;
}
bool VSWorldAvatar::sendAppearance(LLMessageSystem& message, const LLUUID& session, U32 serial) const
{
    auto& avatar = *mImpl;
    if (!avatar.self || !avatar.appearanceKnown || session.isNull() || !serial)
        return false;
    message.newMessage("AgentSetAppearance");
    message.nextBlock("AgentData");
    message.addUUID("AgentID", avatar.id);
    message.addUUID("SessionID", session);
    message.addVector3("Size", avatar.mBodySize);
    message.addU32("SerialNum", serial);
    LLPrimitive wireTextures;
    wireTextures.setNumTEs(TEX_NUM_INDICES);
    for (const auto& [index, entry] : LLAvatarAppearance::getDictionary()->getTextures())
    {
        wireTextures.setTE(U8(index), *avatar.textures.getTE(U8(index)));
        if (!entry->mIsBakedTexture)
            wireTextures.setTETexture(U8(index), IMG_DEFAULT_AVATAR);
    }
    message.nextBlock("ObjectData");
    wireTextures.packTEMessage(&message);
    for (auto* param = avatar.getFirstVisualParam(); param; param = avatar.getNextVisualParam())
        if (param->getGroup() == VISUAL_PARAM_GROUP_TWEAKABLE || param->getGroup() == VISUAL_PARAM_GROUP_TRANSMIT_NOT_TWEAKABLE)
        {
            message.nextBlock("VisualParam");
            message.addU8("ParamValue", F32_to_U8(param->getWeight(), param->getMinWeight(), param->getMaxWeight()));
        }
    return true;
}

void VSWorldAvatar::hoverOffset(const LLVector3& offset)
{
    if (!mImpl->self || !finite(offset)) return;
    mImpl->hover = offset;
    mImpl->setHoverOffset(offset, false);
    mImpl->computeBodySize();
}
LLVector3 VSWorldAvatar::hoverOffset() const { return mImpl->hover; }
