// Real media-face ownership for shared viewer controls. LGPL-2.1.
#pragma once
#include "lluuid.h"
#include "llbbox.h"
#include "vsworldscene.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
class VSNativeSession;
class LLMediaEntry;
class LLViewerMediaImpl;

class VSPrimMediaTarget
{
public:
    static std::optional<VSPrimMediaTarget> resolve(const LLUUID&,S32 face);
    bool current() const;
    std::function<bool()> guard() const;
    const LLUUID& id() const;
    S32 face() const;
    S32 faceCount() const;
    LLUUID mediaID() const;
    // Borrow only during the current synchronous controller call; never cache
    // the pointer across an event, coroutine or media metadata publication.
    const LLMediaEntry* metadata() const;
    LLViewerMediaImpl* media() const;
    bool hud() const;
    bool permission(bool interact) const;
    std::optional<std::pair<LLUUID,LLUUID>> hoverOwners() const;
    std::optional<LLVector3d> position() const;
    std::optional<LLBBox> bounds() const;
    std::optional<std::array<LLVector3,8>> faceBounds() const;
    bool selectFace() const;
    std::shared_ptr<const void> retainSelection() const;
    static void deselectAll();
private:
    VSPrimMediaTarget(std::weak_ptr<VSNativeSession>,VSWorldScene::ObjectHandle,S32,const LLUUID&);
    std::weak_ptr<VSNativeSession> mSession;
    VSWorldScene::ObjectHandle mObject;
    S32 mFace=0;
    LLUUID mMedia;
};
