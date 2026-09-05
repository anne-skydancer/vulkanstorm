/**
 * @file llmultislider.h
 * @brief A simple multislider
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#ifndef LL_MULTI_SLIDER_H
#define LL_MULTI_SLIDER_H

#include "llf32uictrl.h"
#include "v4color.h"

class LLUICtrlFactory;

class LLMultiSlider : public LLF32UICtrl
{
public:
    struct SliderParams : public LLInitParam::Block<SliderParams>
    {
        Optional<std::string>   name;
        Mandatory<F32>          value;
        SliderParams();
    };

    struct Params : public LLInitParam::Block<Params, LLF32UICtrl::Params>
    {
        Optional<S32>   max_sliders;

        Optional<bool>  allow_overlap,
                        loop_overlap,
                        draw_track,
                        use_triangle;

        Optional<F32>   overlap_threshold;

        Optional<LLUIColor> track_color,
                            thumb_disabled_color,
                            thumb_highlight_color,
                            thumb_outline_color,
                            thumb_center_color,
                            thumb_center_selected_color,
                            triangle_color;

        Optional<std::string>   orientation,
                                thumb_image;

        Optional<CommitCallbackParam>   mouse_down_callback,
                                        mouse_up_callback;
        Optional<S32>       thumb_width;

        Multiple<SliderParams>  sliders;
        Params();
    };

protected:
    LLMultiSlider(const Params&);
    friend class LLUICtrlFactory;
public:
    virtual ~LLMultiSlider();

    // Multi-slider rounds values to nearest increments (bias towards rounding down)
    F32                 getNearestIncrement(F32 value) const;

    void                setSliderValue(const std::string& name, F32 value, bool from_event = false);
    F32                 getSliderValue(const std::string& name) const;
    F32                 getSliderValueFromPos(S32 xpos, S32 ypos) const;
    LLRect              getSliderThumbRect(const std::string& name) const;

    void                setSliderThumbImage(const std::string &name);
    void                clearSliderThumbImage();


    const std::string&  getCurSlider() const                    { return mCurSlider; }
    F32                 getCurSliderValue() const               { return getSliderValue(mCurSlider); }
    void                setCurSlider(const std::string& name);
    void                resetCurSlider();
    void                setCurSliderValue(F32 val, bool from_event = false) { setSliderValue(mCurSlider, val, from_event); }

    /*virtual*/ void    setValue(const LLSD& value) override;
    /*virtual*/ LLSD    getValue() const override { return mValue; }

    boost::signals2::connection setMouseDownCallback( const commit_signal_t::slot_type& cb );
    boost::signals2::connection setMouseUpCallback( const commit_signal_t::slot_type& cb );

    bool                findUnusedValue(F32& initVal);
    const std::string&  addSlider();
    const std::string&  addSlider(F32 val);
    bool                addSlider(F32 val, const std::string& name);
    void                deleteSlider(const std::string& name);
    void                deleteCurSlider()           { deleteSlider(mCurSlider); }
    /*virtual*/ void    clear() override;

    /*virtual*/ bool    handleHover(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleMouseUp(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleMouseDown(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleKeyHere(KEY key, MASK mask) override;
    /*virtual*/ void    onMouseLeave(S32 x, S32 y, MASK mask) override;
    /*virtual*/ void    draw() override;

    // <VulkanStorm> GL-free state for the Vulkan UI walker (mirrors draw()).
    // All rects are GL-space SCREEN rects; the walker reproduces draw()'s
    // branch structure (triangle/solid/image thumbs, ghost, focus, hover).
    struct VkThumbState
    {
        std::string name;
        LLRect      screen_rect;
    };
    struct VkDrawState
    {
        bool        horizontal = true;
        bool        enabled = true;
        bool        draw_track = false;
        bool        use_triangle = false;
        bool        has_focus = false;
        bool        mouse_capture = false;
        LLRect      track_rect;             // screen space (draw()'s rect)
        LLRect      drag_start_thumb_rect;  // screen space
        std::string cur_slider;
        std::string hover_slider;
        std::string thumb_image;            // empty when no thumb image
        std::string rounded_square_image;   // empty when unavailable
        LLColor4    track_color;            // unmodulated; pass applies opacity
        LLColor4    triangle_color;
        LLColor4    thumb_center_color;
        LLColor4    thumb_center_selected_color;
        LLColor4    thumb_highlight_color;
        std::vector<VkThumbState> thumbs;
    };
    VkDrawState getVkDrawState(F32 alpha) const;
    // </VulkanStorm>

    S32             getMaxNumSliders() const { return mMaxNumSliders; }
    S32             getCurNumSliders() const { return static_cast<S32>(mValue.size()); }
    F32             getOverlapThreshold() const { return mOverlapThreshold; }
    bool            canAddSliders() const { return mValue.size() < mMaxNumSliders; }


protected:
    LLSD            mValue;
    std::string     mCurSlider;
    std::string     mHoverSlider;
    static S32      mNameCounter;

    S32             mMaxNumSliders;
    bool            mAllowOverlap;
    bool            mLoopOverlap;
    F32             mOverlapThreshold;
    bool            mDrawTrack;
    bool            mUseTriangle;           /// hacked in toggle to use a triangle

    S32             mMouseOffset;
    LLRect          mDragStartThumbRect;
    S32             mThumbWidth;

    std::map<std::string, LLRect>
                    mThumbRects;
    LLUIColor       mTrackColor;
    LLUIColor       mThumbOutlineColor;
    LLUIColor       mThumbHighlightColor;
    LLUIColor       mThumbCenterColor;
    LLUIColor       mThumbCenterSelectedColor;
    LLUIColor       mDisabledThumbColor;
    LLUIColor       mTriangleColor;
    LLUIImagePtr    mThumbImagep; //blimps on the slider, for now no 'disabled' support
    LLUIImagePtr    mRoundedSquareImgp; //blimps on the slider, for now no 'disabled' support

    // <VulkanStorm> XUI name of the thumb image, retained for the GL-free
    // Vulkan path (mThumbImagep is null when no GL context exists).
    std::string     mVkThumbImage;
    // </VulkanStorm>

    const EOrientation  mOrientation;

    commit_signal_t*    mMouseDownSignal;
    commit_signal_t*    mMouseUpSignal;
};

#endif  // LL_MULTI_SLIDER_H
