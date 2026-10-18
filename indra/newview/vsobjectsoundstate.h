// Frame-owned sound metadata for the actual shared audio-source controller.
// LGPL-2.1-or-later.
#pragma once
#include "lluuid.h"
#include "v3math.h"
#include "v3dmath.h"
#include <functional>
#include <optional>

struct VSAudioObjectState
{
    LLUUID id, root, avatarRoot, agentRoot;
    LLVector3d position, avatarRootPosition;
    LLVector3 velocity;
    F32 cutoff = 0;
    bool dead = false, attachment = false, hud = false, agentRiding = false;
};
using VSAudioObjectQuery = std::function<std::optional<VSAudioObjectState>()>;
