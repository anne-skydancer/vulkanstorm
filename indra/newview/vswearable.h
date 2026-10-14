// CPU wearable ownership for the native connected appearance service. LGPL-2.1.
#pragma once
#include "llwearable.h"
#include "lluuid.h"
#include <functional>
#include <memory>
class VSWearable final : public LLWearable
{
public:
    static std::shared_ptr<VSWearable> create(const LLUUID& item, const LLUUID& asset, std::function<void()> changed,
                                              std::shared_ptr<const void> residency = {});
    const LLUUID&                      item() const { return mItem; }
    const LLUUID&                      asset() const { return mAsset; }
    LLUUID                             getDefaultTextureImageID(LLAvatarAppearanceDefines::ETextureIndex) const override;
    void                               setUpdated() const override;
    void                               addToBakedTextureHash(LLMD5&) const override;
    bool                               usesGraphicsContext() const override { return false; }

private:
    VSWearable(const LLUUID& item, const LLUUID& asset, std::function<void()> changed);
    LLUUID                mItem, mAsset;
    std::function<void()> mChanged;
};
