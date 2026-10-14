// Shared appearance layer metadata for peer compositors. LGPL-2.1.
#pragma once
#include "lltexlayer.h"
#include "lltexlayerparams.h"
class LLTexLayerInfo
{
    friend class LLTexLayer;
    friend class LLTexLayerTemplate;
    friend class LLTexLayerInterface;

public:
    LLTexLayerInfo();
    ~LLTexLayerInfo();

    bool        parseXml(LLXmlTreeNode* node);
    bool        createVisualParams(LLAvatarAppearance* appearance);
    bool        isUserSettable() { return mLocalTexture != -1; }
    S32         getLocalTexture() const { return mLocalTexture; }
    bool        getOnlyAlpha() const { return mUseLocalTextureAlphaOnly; }
    std::string getName() const { return mName; }

private:
    std::string mName;

    bool                             mWriteAllChannels; // Don't use masking.  Just write RGBA into buffer,
    LLTexLayerInterface::ERenderPass mRenderPass;

    std::string mGlobalColor;
    LLColor4    mFixedColor;

    S32         mLocalTexture;
    std::string mStaticImageFileName;
    bool        mStaticImageIsMask;
    bool        mUseLocalTextureAlphaOnly; // Ignore RGB channels from the input texture.  Use alpha as a mask
    bool        mIsVisibilityMask;

    typedef std::vector<std::pair<std::string, bool>> morph_name_list_t;
    morph_name_list_t                                 mMorphNameList;
    param_color_info_list_t                           mParamColorInfoList;
    param_alpha_info_list_t                           mParamAlphaInfoList;
};
