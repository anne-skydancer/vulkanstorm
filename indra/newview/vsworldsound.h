// Actual engine-owned attached sources for authenticated native scene objects.
// LGPL-2.1-or-later.
#pragma once
#include "vsworldscene.h"
#include <memory>
class LLMessageSystem;
class VSNativeSession;
class VSWorldSound
{
public:
    explicit VSWorldSound(std::weak_ptr<VSNativeSession>);
    ~VSWorldSound();
    void attached(const LLUUID& object, const LLUUID& asset, const LLUUID& owner, F32 gain, U8 flags, const LLHost& sender);
    void preload(const LLUUID& object, const LLUUID& asset, const LLUUID& owner, const LLHost& sender);
    void gain(const LLUUID& object, F32 gain, const LLHost& sender);
    void tick();
    void reset();
    bool isAttachment(const LLUUID&) const;
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
