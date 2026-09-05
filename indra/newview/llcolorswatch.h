/**
 * @file llcolorswatch.h
 * @brief LLColorSwatch class definition
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLCOLORSWATCH_H
#define LL_LLCOLORSWATCH_H

#include "lluictrl.h"
#include "v4color.h"
#include "llfloater.h"
#include "lltextbox.h"

//
// Classes
//
class LLColor4;

class LLColorSwatchCtrl
: public LLUICtrl
{
public:
    typedef enum e_color_pick_op
    {
        COLOR_CHANGE,
        COLOR_SELECT,
        COLOR_CANCEL
    } EColorPickOp;

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<LLUIColor>             color;
        Optional<bool>                  can_apply_immediately;
        Optional<LLUIImage*>            alpha_background_image;
        Optional<commit_callback_t>     cancel_callback;
        Optional<commit_callback_t>     select_callback;
        Optional<commit_callback_t>     preview_callback;
        Optional<LLUIColor>             border_color;
        Optional<S32>                   label_width;
        Optional<S32>                   label_height;

        Optional<LLTextBox::Params>     caption_text;
        Optional<LLViewBorder::Params>  border;

        Optional<LLUIColor>     text_enabled_color;     // <FS:Zi> Add label/caption colors
        Optional<LLUIColor>     text_disabled_color;    // <FS:Zi> Add label/caption colors

        Params();
    };

protected:
    LLColorSwatchCtrl(const Params& p);
    friend class LLUICtrlFactory;
public:
    ~LLColorSwatchCtrl ();

    /*virtual*/ void setValue(const LLSD& value);

    /*virtual*/ LLSD getValue() const { return mColor.getValue(); }
    const LLColor4& get()                           { return mColor; }

    void            set(const LLColor4& color, bool update_picker = false, bool from_event = false);
    void            setOriginal(const LLColor4& color);
    void            setValid(bool valid);
    void            setLabel(const std::string& label);
    void            setLabelWidth(S32 label_width) {mLabelWidth =label_width;}
    void            setCanApplyImmediately(bool apply) { mCanApplyImmediately = apply; }
    void            setOnCancelCallback(commit_callback_t cb) { mOnCancelCallback = cb; }
    void            setOnSelectCallback(commit_callback_t cb) { mOnSelectCallback = cb; }
    void            setPreviewCallback(commit_callback_t cb) { mPreviewCallback = cb; }
    void            setFallbackImage(LLPointer<LLUIImage> image) { mFallbackImage = image; mVkFallbackImageName = image.notNull() ? image->getName() : ""; } // <VulkanStorm> retain name for GL-free path

    // <VulkanStorm> GL-free description of what draw() paints: border rect,
    // interior, checkerboard/color/alpha-gradient or fallback/grey-X content.
    struct VkDrawState
    {
        bool valid = true;              // mValid
        LLColor4 color;                 // mColor % alpha
        LLColor4 border_color;          // mBorderColor, or gray when fallback shows
        LLRect border_rect;             // screen space (full width, above label)
        LLRect interior;                // screen space, border_rect stretched -1
        bool border_ctrl_visible = true;// draw() sets mBorder visible to this
        bool draw_checkerboard = false; // valid && color not opaque
        bool draw_alpha_gradient = false; // valid && not opaque && image known
        std::string alpha_gradient_image;
        bool use_fallback_image = false;// !valid && fallback known
        std::string fallback_image;
        bool draw_grey_x = false;       // !valid && no fallback image
        LLColor4 fallback_tint;         // LLColor4::white % alpha
    };
    VkDrawState getVkDrawState(F32 alpha) const;
    // </VulkanStorm>

    void            showPicker(bool take_focus);

    /*virtual*/ bool    handleMouseDown(S32 x, S32 y, MASK mask);
    /*virtual*/ bool    handleMouseUp(S32 x, S32 y, MASK mask);
    /*virtual*/ bool    handleDoubleClick(S32 x,S32 y,MASK mask);
    /*virtual*/ bool    handleHover(S32 x, S32 y, MASK mask);
    /*virtual*/ bool    handleUnicodeCharHere(llwchar uni_char);
    /*virtual*/ void    draw();
    /*virtual*/ void    setEnabled( bool enabled );

    static void     onColorChanged ( void* data, EColorPickOp pick_op = COLOR_CHANGE );
    void            closeFloaterColorPicker();

    void setLabelColor(const LLColor4& c)           { mTextEnabledColor = c; updateLabelColor();  } // <FS:Zi> Add label/caption colors
    void setDisabledLabelColor(const LLColor4& c)   { mTextDisabledColor = c; updateLabelColor(); } // <FS:Zi> Add label/caption colors

protected:
    void updateLabelColor();    // <FS:Zi> Add label/caption colors

    bool                    mValid;
    LLColor4                mColor;
    LLUIColor               mBorderColor;
    LLTextBox*              mCaption;
    LLHandle<LLFloater>     mPickerHandle;
    class LLViewBorder*     mBorder;
    bool                    mCanApplyImmediately;
    commit_callback_t       mOnCancelCallback,
                            mOnSelectCallback;
    commit_callback_t mPreviewCallback;
    S32                     mLabelWidth,
                            mLabelHeight;

    LLPointer<LLUIImage> mAlphaGradientImage;
    LLPointer<LLUIImage> mFallbackImage;

    // <VulkanStorm> raw image names for the GL-free Vulkan path; the
    // GL-backed LLUIImage pointers above may be null there. The fallback
    // image is set programmatically, so its name is captured at set time.
    std::string             mVkAlphaGradientImageName;
    std::string             mVkFallbackImageName;
    // </VulkanStorm>

    LLUIColor               mTextEnabledColor;      // <FS:Zi> Add label/caption colors
    LLUIColor               mTextDisabledColor;     // <FS:Zi> Add label/caption colors
};

#endif  // LL_LLBUTTON_H
