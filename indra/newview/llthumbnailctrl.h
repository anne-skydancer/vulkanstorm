/**
 * @file llthumbnailctrl.h
 * @brief LLThumbnailCtrl base class
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023 Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLTHUMBNAILCTRL_H
#define LL_LLTHUMBNAILCTRL_H

#include "llui.h"
#include "lluictrl.h"
#include "llviewborder.h" // for params

class LLUICtrlFactory;
class LLUUID;
class LLViewerFetchedTexture;

//
// Classes
//

//
class LLThumbnailCtrl
: public LLUICtrl
{
public:
    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<LLViewBorder::Params> border;
        Optional<LLUIColor>        border_color;
        Optional<std::string>      image_name;
        Optional<LLUIImage*>       fallback_image;
        Optional<bool>             border_visible;
        Optional<bool>             interactable;
        Optional<bool>             show_loading;

        Params();
    };
protected:
    LLThumbnailCtrl(const Params&);
    friend class LLUICtrlFactory;

public:
    virtual ~LLThumbnailCtrl();

    virtual void draw() override;
    void setVisible(bool visible) override;

    virtual void setValue(const LLSD& value ) override;
    void setInitImmediately(bool val) { mInitImmediately = val; }
    void clearTexture();

    // <VulkanStorm> GL-free description of what draw() paints: border, then
    // texture / named image / fallback image / grey-X content per branch.
    // Read-only; does not lazily init the image like draw() does - a Vulkan
    // hook should treat image_asset_id/image_name as authoritative and fetch
    // or upload them itself.
    struct VkDrawState
    {
        LLUUID image_asset_id;          // mImageAssetID (may be null)
        bool has_texture = false;       // mTexturep resolved
        S32 texture_components = 0;     // 4 => dark grey backing rect drawn
        bool texture_fully_loaded = true;
        std::string image_name;         // named UI image (mImagep / raw value)
        std::string fallback_image;     // fallback image name
        bool fallback_centered = false; // draw() centers a native-size fallback
        S32 fallback_width = 0;         // native fallback size; 0 if unknown
        S32 fallback_height = 0;
        bool draw_grey_x = false;       // nothing available
        bool border_visible = false;
        LLColor4 border_color;
        LLRect border_rect;             // screen space, full local rect
        LLRect draw_rect;               // screen space, after border inset
        bool show_loading_placeholder = false;
        bool interactable = false;      // mInteractable (hand cursor on hover)
        bool enabled = true;
    };
    VkDrawState getVkDrawState(F32 alpha) const;
    // </VulkanStorm>

    virtual bool handleHover(S32 x, S32 y, MASK mask) override;

protected:
    void initImage();
    void unloadImage();

private:
    bool mBorderVisible;
    bool mInteractable;
    bool mShowLoadingPlaceholder;
    bool mInited;
    bool mInitImmediately;
    std::string mLoadingPlaceholderString;
    LLUUID mImageAssetID;
    LLViewBorder* mBorder;
    LLUIColor mBorderColor;

    LLPointer<LLViewerFetchedTexture> mTexturep;
    LLPointer<LLUIImage> mImagep;
    LLPointer<LLUIImage> mFallbackImagep;

    // <VulkanStorm> raw names for the GL-free Vulkan path: the image_name
    // value string (mImagep may be null when GL image loading is off) and the
    // raw XUI fallback_image name.
    std::string mVkImageName;
    std::string mVkFallbackImageName;
    // </VulkanStorm>
};

#endif
