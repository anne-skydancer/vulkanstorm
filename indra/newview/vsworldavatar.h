// Native CPU avatar appearance, animation and Vulkan mesh publication. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "lluuid.h"
#include "llpointer.h"
#include "v3math.h"
#include "v3dmath.h"
#include "llquaternion.h"
#include <functional>
#include <memory>
#include <map>
#include <optional>
class LLMessageSystem;
class VSUIResources;
class LLSD;
class LLVolumeFace;
class LLCharacter;
class LLAvatarAppearance;
class LLWearable;
class LLImageRaw;
class VSWorldAvatar
{
public:
    using GroundQuery = std::function<bool(const LLVector3&, LLVector3&, LLVector3&)>;
    static void initializeClass();
    static void cleanupClass();
    VSWorldAvatar(const LLUUID&, bool self, U64 regionHandle, VSWorldRenderer&, VSUIResources&, GroundQuery,
                  std::shared_ptr<const void> ownerReservation = {}, bool controlAvatar = false);
    ~VSWorldAvatar();
    void rebase(U64 regionHandle, GroundQuery);
    bool appearance(LLMessageSystem&);
    bool animations(LLMessageSystem&);
    void     animations(const std::map<LLUUID, S32>&);
    void     transform(const LLVector3&, const LLQuaternion&, const LLVector3& velocity, const LLVector3& angularVelocity,
                       F32 simulatorHeight = 0);
    void     append(std::vector<VSWorldRenderer::Packet>&, const LLVector3& eye, float farClip, unsigned viewportHeight, float timeDilation,
                    unsigned viewportWidth = 0, unsigned visibleAvatars = 1);
    bool     visibleFrom(const LLVector3& eye, float farClip) const;
    bool attachmentTransform(unsigned point, LLVector3&, LLQuaternion&);
    bool skinRiggedFace(const LLVolumeFace&,const LLSD&,std::vector<VSWorldRenderer::Vertex>&);
    U64      poseGeneration() const;
    void     attachmentSkins(const std::map<LLUUID, LLSD>&);
    bool     hudAttachment(unsigned point) const;
    unsigned attachmentPoint(const std::string& name) const;
    struct AttachmentPoint
    {
        unsigned    index = 0;
        std::string name;
        bool        hud = false, firstPerson = false;
    };
    std::vector<AttachmentPoint>         attachmentPoints() const;
    bool                                 attachmentVisible(unsigned point, bool firstPerson) const;
    LLCharacter&                         character();
    bool                                 isControlAvatar() const;
    bool                                 resetSkeleton(bool resetAnimations);
    void                                 resyncAnimations();
    LLAvatarAppearance&                  appearanceOwner();
    void hoverOffset(const LLVector3&);
    LLVector3 hoverOffset() const;
    const std::map<LLUUID, S32>&         signaledAnimations() const;
    const std::multimap<LLUUID, LLUUID>& animationSources() const;
    bool                                 wearables(const std::vector<std::shared_ptr<LLWearable>>&, S32 cofVersion);
    U32                                  wearableCount(unsigned type) const;
    std::vector<LLUUID>                  wearableItems(unsigned type) const;
    void                                 invalidateLocalAppearance();
    U64                                  ownerGeneration() const;
    void                                 sitting(bool);
    bool                                 sitting() const;
    bool                                 inAir() const;
    void                                 environment(const LLVector3& wind, F32 waterHeight, const LLVector3& eye, const LLVector3& at);
    enum class Visibility
    {
        Normal,
        Jellydoll,
        Invisible
    };
    Visibility updateVisibility(U32 attachmentCost = 0, F32 attachmentArea = 0, bool animatedAttachments = false);
    Visibility visibility() const;
    using Attention = std::function<std::optional<LLVector3d>()>;
    void      attention(bool point, Attention target, F32 duration);
    LLVector3 headPosition() const;
    void      controlConstraints(const LLVector3& official, const LLVector3* bounds, bool attached, const LLSD& rootSkin);
    struct LocalBake
    {
        unsigned              index      = 0;
        U64                   generation = 0;
        LLPointer<LLImageRaw> color, mask;
        VSUIRenderer::Image   image; // Exact immutable composite generation.
    };
    std::vector<LocalBake> localBakes() const;
    LocalBake              localBake(unsigned index) const;
    bool                   setLocalBake(unsigned index, U64 generation, const LLUUID&);
    bool                   sendAppearance(LLMessageSystem&, const LLUUID& session, U32 serial) const;
    LLSD evidence() const;
#if VS_VULKAN_DIAGNOSTICS
    LLSD qualifyBodyLODs(const LLVector3& eye);
#endif
private:
    std::shared_ptr<const void> mOwnerReservation;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
