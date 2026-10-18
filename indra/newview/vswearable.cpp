// CPU wearable ownership for the native connected appearance service. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vswearable.h"
#include "vsavatarservices.h"
#include "llavatarappearance.h"
#include "llviewercontrol.h"
#include "llmd5.h"
std::shared_ptr<VSWearable> VSWearable::create(const LLUUID& item, const LLUUID& asset, std::function<void()> changed,
                                               std::shared_ptr<const void> residency)
{
    // LLWearable's base destructor releases its visual parameters and layers.
    // Keep the reservation through the entire delete, not just member teardown.
    return std::shared_ptr<VSWearable>(new VSWearable(item, asset, std::move(changed)),
                                       [residency = std::move(residency)](VSWearable* value) mutable
                                       {
                                           delete value;
                                           residency.reset();
                                       });
}
VSWearable::VSWearable(const LLUUID& item, const LLUUID& asset, std::function<void()> changed) :
    LLViewerWearable(asset),
    mChanged(std::move(changed))
{
    setItemID(item);
    mNativeOwned = true;
    mNativeAvatarGeneration = vs_avatar_generation();
}
LLUUID VSWearable::getDefaultTextureImageID(LLAvatarAppearanceDefines::ETextureIndex index) const
{
    const auto* texture = LLAvatarAppearance::getDictionary()->getTexture(index);
    return texture && !texture->mDefaultImageName.empty() ? LLUUID(gSavedSettings.getString(texture->mDefaultImageName))
                                                          : IMG_DEFAULT_AVATAR;
}
void VSWearable::setUpdated() const
{
    if (!currentNativeOwner()) return;
    if (mChanged)
        mChanged();
}
void VSWearable::addToBakedTextureHash(LLMD5& hash) const
{ hash.update(getAssetID().mData, UUID_BYTES); }
