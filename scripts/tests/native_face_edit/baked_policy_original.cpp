// Frozen original LLViewerObject policy, LGPL-2.1.
LLViewerTexture* LLViewerObject::getBakedTextureForMagicId(const LLUUID& id)
{
    if (!LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(id))
    {
        return NULL;
    }

    LLViewerObject *root = getRootEdit();
    if (root && root->isAnimatedObject())
    {
        return LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    }

    LLVOAvatar* avatar = getAvatar();
    if (avatar && !isHUDAttachment())
    {
        LLAvatarAppearanceDefines::EBakedTextureIndex texIndex = LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::assetIdToBakedTextureIndex(id);
        LLViewerTexture* bakedTexture = avatar->getBakedTexture(texIndex);
        if (bakedTexture == NULL || bakedTexture->isMissingAsset())
        {
            return LLViewerTextureManager::getFetchedTexture(IMG_DEFAULT, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
        }
        else
        {
            return bakedTexture;
        }
    }
    else
    {
        return LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    }

}
