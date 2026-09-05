/**
 * @file llprogressview.h
 * @brief LLProgressView class definition
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLPROGRESSVIEW_H
#define LL_LLPROGRESSVIEW_H

#include "llpanel.h"
#include "llmediactrl.h"
#include "llframetimer.h"
#include "llevents.h"

class LLImageRaw;
class LLButton;
class LLProgressBar;
class LLViewerTexture;
class LLTextBox;

class LLProgressView :
    public LLPanel,
    public LLViewerMediaObserver

{
    LOG_CLASS(LLProgressView);

public:
    LLProgressView();
    virtual ~LLProgressView();

    bool postBuild();

    /*virtual*/ void draw();
    void drawStartTexture(F32 alpha);
    void drawLogos(F32 alpha);

    // <VulkanStorm> GL-free description of everything LLProgressView::draw()
    // paints itself (fade state, aspect-corrected start texture, 3p logos);
    // children are provided by the normal view walk. Pure state reads.
    struct VkDrawState
    {
        struct VkLogo
        {
            LLRect rect;            // screen space (draw rect + logos label offset)
            LLRectf clip_rect;
            LLRectf offset_rect;
            LLColor4 color;
            std::string image_name; // source file name; logos are local files
                                    // with no UI image registry name
            LLUUID texture_id;      // null when the GL-backed texture is unavailable
            const LLImageRaw* raw = nullptr;  // CPU pixels (null-safe, GL-free)
        };
        bool fading_from_login = false; // mFadeFromLoginTimer running
        bool fading_to_world = false;   // mFadeToWorldTimer running
        F32 fade_elapsed = 0.f;         // elapsed seconds on the active fade timer
        F32 alpha = 1.f;                // the alpha draw() pushes via LLViewDrawContext
        bool media_ctrl_visible = false;// fade-from-login skips the start texture when true
        bool draw_start_texture = false;// false => draw() paints a solid black rect
        LLUUID start_texture_id;        // null when gStartTexture is unavailable
        const LLImageRaw* start_raw = nullptr;  // CPU pixels of gStartTexture
        S32 start_image_width = 0;      // gStartImageWidth/Height (raw image aspect)
        S32 start_image_height = 0;
        LLRect start_texture_rect;      // screen space, aspect-corrected like drawStartTexture()
        std::vector<VkLogo> logos;
    };
    VkDrawState getVkDrawState() const;
    // </VulkanStorm>

    /*virtual*/ bool handleHover(S32 x, S32 y, MASK mask);
    /*virtual*/ bool handleKeyHere(KEY key, MASK mask);
    /*virtual*/ void setVisible(bool visible);

    // inherited from LLViewerMediaObserver
    /*virtual*/ void handleMediaEvent(LLPluginClassMedia* self, EMediaEvent event);

    void setText(const std::string& text);
    void setPercent(const F32 percent);

    // Set it to NULL when you want to eliminate the message.
    void setMessage(const std::string& msg);

    // turns on (under certain circumstances) the into video after login
    void revealIntroPanel();

    void fade(bool in);     // ## Zi: Fade teleport screens

    void setStartupComplete();

    // we have to preload local textures to make sure they won't be grey
    void initTextures(S32 location_id, bool is_in_production);
    void releaseTextures();

    void setCancelButtonVisible(bool b, const std::string& label);

    static void onCancelButtonClicked( void* );
    static void onClickMessage(void*);
    bool onAlertModal(const LLSD& sd);

protected:
    LLProgressBar* mProgressBar;
    LLMediaCtrl* mMediaCtrl;
    LLTextBox* mLogosLabel = nullptr;
    LLTextBox* mProgressText = nullptr;
    LLTextBox* mMessageText = nullptr;
    F32 mPercentDone;
    std::string mMessage;
    LLButton*   mCancelBtn;
    LLFrameTimer mFadeToWorldTimer;
    LLFrameTimer mFadeFromLoginTimer;
    LLRect mOutlineRect;
    LLView* mLayoutPanel4 = nullptr;
    LLView* mLayoutMOTD = nullptr;
    // Rects for resizing purposes
    LLRect mMessageTextRectInitial;
    LLRect mLayoutPanel4RectInitial;
    LLRect mLayoutMOTDRectInitial;

    bool mMouseDownInActiveArea;
    bool mStartupComplete;

    // The LLEventStream mUpdateEvents depends upon this class being a singleton
    // to avoid pump name conflicts.
    static LLProgressView* sInstance;
    LLEventStream mUpdateEvents;

    bool handleUpdate(const LLSD& event_data);
    static void onIdle(void* user_data);
    void loadLogo(const std::string &path, const U8 image_codec, const LLRect &pos_rect, const LLRectf &clip_rect, const LLRectf &offset_rect);
    // logos have unusual location and need to be preloaded to not appear grey, then deleted
    void initLogos();
    // Loads a bitmap to display during load
    void initStartTexture(S32 location_id, bool is_in_production);

private:
    // We need to draw textures on login, but only once.
    // So this vector gets filled up for textures to render and gets cleaned later
    // Some textures have unusual requirements, so we are rendering directly
    class TextureData
    {
    public:
        LLPointer<LLViewerTexture> mTexturep;
        LLRect mDrawRect;
        LLRectf mClipRect;
        LLRectf mOffsetRect;
        // <VulkanStorm> source file name, retained for the GL-free Vulkan path
        // (local textures have no UI image name to recover at draw time).
        std::string mVkImageName;
        // <VulkanStorm> the decoded pixels, retained for the Vulkan upload
        // bridge (local textures do not keep their raw image once uploaded).
        LLPointer<LLImageRaw> mRawImage;
        // </VulkanStorm>
    };
    std::vector<TextureData> mLogosList;
};

class LLProgressViewMini :
    public LLPanel
{
    public:
        LLProgressViewMini();

        bool postBuild();
        void setText(const std::string& text);
        void setPercent(const F32 percent);
        void setCancelButtonVisible(bool b, const std::string& label);

        static void onCancelButtonClicked(void* dummy);

    protected:
        static LLProgressViewMini* sInstance;

        LLProgressBar* mProgressBar;
        LLButton* mCancelBtn;
        LLTextBox* mProgressText;
};

#endif // LL_LLPROGRESSVIEW_H
