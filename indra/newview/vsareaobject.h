// Real object/circuit peers for the original Area Search controller. LGPL-2.1.
#pragma once
#include "llviewerobject.h"
#include "vsregioncontext.h"
#include "vsobjectcontext.h"
#include <memory>
class VSAreaRegion
{
public:
    VSAreaRegion() = default;
    VSAreaRegion(std::nullptr_t) {}
    explicit VSAreaRegion(const VSRegionContext& value) : mRegion(value) {}
    explicit operator bool() const { return mRegion && mRegion->current(); }
    const VSRegionContext* operator->() const { return mRegion ? &*mRegion : nullptr; }
    bool operator==(const VSAreaRegion& other) const
    { return mRegion && other.mRegion && mRegion->current() && other.mRegion->current() && *mRegion == *other.mRegion; }
    bool operator!=(const VSAreaRegion& other) const { return !(*this == other); }
    void getNeighboringRegions(std::vector<VSAreaRegion>&) const;
    void sendReliableMessage() const;
    static VSAreaRegion current();
    static std::vector<VSAreaRegion> instances();
private:
    std::optional<VSRegionContext> mRegion;
};

class VSAreaObject
{
public:
    VSAreaObject() = default;
    VSAreaObject(std::nullptr_t) {}
    VSAreaObject(LLViewerObject*);
    explicit operator bool() const;
    const VSAreaObject* operator->() const { return this; }
    static VSAreaObject find(const LLUUID&);
    static VSAreaObject find(const LLHost&,const LLUUID&);
#if VS_NATIVE_VULKAN
    static VSAreaObject find(const VSWorldScene::ObjectHandle&);
    const VSWorldScene::ObjectHandle& token() const;
#endif
    static std::vector<VSAreaObject> instances();
    bool sameSource(const VSAreaObject&) const;
    bool native() const;
    LLViewerObject* legacy() const;
    std::optional<VSObjectContext> context() const;
    std::function<bool()> guard() const;
    const LLUUID& getID() const;
    U32 getLocalID() const;
    VSAreaRegion getRegion() const;
    U8 getPCode() const;
    bool canSelect() const;
    bool isAvatar() const;
    bool isRoot() const;
    bool isRootEdit() const;
    bool isAttachment() const;
    bool isHUDAttachment() const;
    bool isReflectionProbe() const;
    bool flagUsePhysics() const;
    bool flagTemporaryOnRez() const;
    bool flagPhantom() const;
    bool flagHandleTouch() const;
    U8 getClickAction() const;
    U8 getNumTEs() const;
    LLTextureEntry* getTE(U8) const;
    LLVector3d getPositionGlobal() const;
    LLVector3 getPositionAgent() const;
    LLBBox getBoundingBoxAgent() const;
    F32 getLinksetCost() const;
    S32 numChildren() const;
    bool permYouOwner() const;
    bool canTouch() const;
    bool canSit() const;
    bool canEdit() const;
    bool canDeleteOrReturn() const;
    bool selectFamily(bool add = false) const;
    void lookAt() const;
    void derender(const std::string&) const;
private:
    struct State;
    std::shared_ptr<State> mState;
};
