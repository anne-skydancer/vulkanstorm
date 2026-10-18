// Lazy renderer-peer adapters for the shared LLAgent autopilot. LGPL-2.1.
#pragma once
#include "v3dmath.h"
#include "lluuid.h"
#include <functional>
#include <memory>
#include <optional>
class VSNativeSession;
class VSAgentNavigation
{
public:
    VSAgentNavigation();
    bool                      native() const;
    void                      face(const LLVector3&) const;
    bool                      valid() const;
    bool                      inAir() const;
    F32                       pelvisToFoot() const;
    LLVector3d                avatarPosition() const;
    std::optional<LLVector3d> objectPosition(const LLUUID&) const;
    F32                       stepHeight(const LLVector3d&, const LLVector3d&, LLVector3d&) const;
    std::function<bool()>     accountGuard() const;

private:
#if VS_NATIVE_VULKAN
    std::shared_ptr<VSNativeSession> mNative;
    LLUUID                           mAgent, mSession;
    U64                              mGeneration = 0;
    bool                             current() const;
#endif
};
