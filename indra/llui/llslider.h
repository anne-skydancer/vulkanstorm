/**
 * @file llslider.h
 * @brief A simple slider with no label.
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

#ifndef LL_LLSLIDER_H
#define LL_LLSLIDER_H

#include "llf32uictrl.h"
#include "v4color.h"
#include "lluiimage.h"

class LLSlider : public LLF32UICtrl
{
public:
    struct Params : public LLInitParam::Block<Params, LLF32UICtrl::Params>
    {
        Optional<std::string> orientation;

        Optional<LLUIColor> thumb_outline_color,
                            thumb_center_color;

        Optional<LLUIImage*>    thumb_image,
                                thumb_image_pressed,
                                thumb_image_disabled,
                                track_image_horizontal,
                                track_image_vertical,
                                track_highlight_horizontal_image,
                                track_highlight_vertical_image;

        Optional<CommitCallbackParam>   mouse_down_callback,
                                        mouse_up_callback;


        Params();
    };
protected:
    LLSlider(const Params&);
    friend class LLUICtrlFactory;
public:
    virtual ~LLSlider();
    void            setValue( F32 value, bool from_event = false );
    // overrides for LLF32UICtrl methods
    virtual void    setValue(const LLSD& value )    { setValue((F32)value.asReal(), true); }

    virtual void    setMinValue(const LLSD& min_value) { setMinValue((F32)min_value.asReal()); }
    virtual void    setMaxValue(const LLSD& max_value) { setMaxValue((F32)max_value.asReal()); }
    virtual void    setMinValue(F32 min_value) { LLF32UICtrl::setMinValue(min_value); updateThumbRect(); }
    virtual void    setMaxValue(F32 max_value) { LLF32UICtrl::setMaxValue(max_value); updateThumbRect(); }

    boost::signals2::connection setMouseDownCallback( const commit_signal_t::slot_type& cb );
    boost::signals2::connection setMouseUpCallback( const commit_signal_t::slot_type& cb );

    virtual bool    handleHover(S32 x, S32 y, MASK mask);
    virtual bool    handleMouseUp(S32 x, S32 y, MASK mask);
    virtual bool    handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool    handleKeyHere(KEY key, MASK mask);
    virtual bool    handleScrollWheel(S32 x, S32 y, S32 clicks);
    virtual void    draw();

    // <VulkanStorm> GL-free slider state.  Native Vulkan intentionally has no
    // LLUIImage objects, so preserve the XUI aliases and expose the same
    // geometry/color decisions that draw() uses without entering GL.
    struct VkDrawState
    {
        LLRect control_rect;
        LLRect thumb_rect;
        LLRect drag_start_thumb_rect;
        LLColor4 track_color;
        LLColor4 thumb_color;
        LLColor4 ghost_color;
        std::string track_image;
        std::string track_highlight_image;
        std::string thumb_image;
        bool horizontal = true;
        bool draw_ghost = false;
        bool draw_focus = false;
    };
    std::string getVkThumbImageName() const;
    void prepareVkDraw(S32 thumb_width, S32 thumb_height);
    VkDrawState getVkDrawState(F32 alpha) const;
    // </VulkanStorm>

private:
    void            setValueAndCommit(F32 value);
    void            updateThumbRect();
    S32             getThumbWidth() const;
    S32             getThumbHeight() const;

    bool            mVolumeSlider;
    S32             mMouseOffset;
    LLRect          mDragStartThumbRect;

    LLPointer<LLUIImage>    mThumbImage;
    LLPointer<LLUIImage>    mThumbImagePressed;
    LLPointer<LLUIImage>    mThumbImageDisabled;
    LLPointer<LLUIImage>    mTrackImageHorizontal;
    LLPointer<LLUIImage>    mTrackImageVertical;
    LLPointer<LLUIImage>    mTrackHighlightHorizontalImage;
    LLPointer<LLUIImage>    mTrackHighlightVerticalImage;

    // <VulkanStorm> XUI aliases survive the GL-free image provider used by
    // native Vulkan. Cached thumb dimensions also keep hit testing identical
    // to the independently decoded Vulkan asset.
    std::string     mVkThumbImage;
    std::string     mVkThumbImagePressed;
    std::string     mVkThumbImageDisabled;
    std::string     mVkTrackImageHorizontal;
    std::string     mVkTrackImageVertical;
    std::string     mVkTrackHighlightHorizontalImage;
    std::string     mVkTrackHighlightVerticalImage;
    S32             mVkThumbWidth;
    S32             mVkThumbHeight;
    // </VulkanStorm>

    const EOrientation  mOrientation;

    LLRect      mThumbRect;
    LLUIColor   mThumbOutlineColor;
    LLUIColor   mThumbCenterColor;

    commit_signal_t*    mMouseDownSignal;
    commit_signal_t*    mMouseUpSignal;
};

#endif  // LL_LLSLIDER_H
