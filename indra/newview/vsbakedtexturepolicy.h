// Original LLViewerObject baked texture source policy, shared by real scene owners.
// LGPL-2.1.
#pragma once
#include "llavatarappearancedefines.h"
#include "lluuid.h"
#include "llviewertexture.h"
template<class Object,class Service>
typename Service::Image vs_baked_texture_for_magic_id(Object& object,const LLUUID& id,const Service& service)
{
    using Dictionary=LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary;
    if(!Dictionary::isBakedImageId(id))return {};
    auto* root=object.getRootEdit();
    if(root && root->isAnimatedObject())return service.fetch(id);
    auto* avatar=object.getAvatar();
    if(avatar && !object.isHUDAttachment())
    {
        const auto index=Dictionary::assetIdToBakedTextureIndex(id);
        auto texture=service.bake(avatar,index);
        if(!texture || service.missing(texture))return service.fetch(IMG_DEFAULT);
        return texture;
    }
    return service.fetch(id);
}
