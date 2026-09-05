/**
 * @file llslider.cpp
 * @brief LLSlider base class
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

#include "linden_common.h"

#include "llslider.h"
#include "llui.h"

#include "llgl.h"
#include "llwindow.h"
#include "llfocusmgr.h"
#include "llkeyboard.h"         // for the MASK constants
#include "llcontrol.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<LLSlider> r1("slider_bar");
//FIXME: make this into an unregistered template so that code constructed sliders don't
// have ambigious template lookup problem

LLSlider::Params::Params()
:   orientation ("orientation", std::string ("horizontal")),
    thumb_outline_color("thumb_outline_color"),
    thumb_center_color("thumb_center_color"),
    thumb_image("thumb_image"),
    thumb_image_pressed("thumb_image_pressed"),
    thumb_image_disabled("thumb_image_disabled"),
    track_image_horizontal("track_image_horizontal"),
    track_image_vertical("track_image_vertical"),
    track_highlight_horizontal_image("track_highlight_horizontal_image"),
    track_highlight_vertical_image("track_highlight_vertical_image"),
    mouse_down_callback("mouse_down_callback"),
    mouse_up_callback("mouse_up_callback")
{}

LLSlider::LLSlider(const LLSlider::Params& p)
:   LLF32UICtrl(p),
    mMouseOffset( 0 ),
    mOrientation ((p.orientation() == "horizontal") ? HORIZONTAL : VERTICAL),
    mThumbOutlineColor(p.thumb_outline_color()),
    mThumbCenterColor(p.thumb_center_color()),
    mThumbImage(p.thumb_image),
    mThumbImagePressed(p.thumb_image_pressed),
    mThumbImageDisabled(p.thumb_image_disabled),
    mTrackImageHorizontal(p.track_image_horizontal),
    mTrackImageVertical(p.track_image_vertical),
    mTrackHighlightHorizontalImage(p.track_highlight_horizontal_image),
    mTrackHighlightVerticalImage(p.track_highlight_vertical_image),
    mVkThumbImage(p.thumb_image.vk_image_name.isProvided() ? p.thumb_image.vk_image_name() : ""),
    mVkThumbImagePressed(p.thumb_image_pressed.vk_image_name.isProvided() ? p.thumb_image_pressed.vk_image_name() : ""),
    mVkThumbImageDisabled(p.thumb_image_disabled.vk_image_name.isProvided() ? p.thumb_image_disabled.vk_image_name() : ""),
    mVkTrackImageHorizontal(p.track_image_horizontal.vk_image_name.isProvided() ? p.track_image_horizontal.vk_image_name() : ""),
    mVkTrackImageVertical(p.track_image_vertical.vk_image_name.isProvided() ? p.track_image_vertical.vk_image_name() : ""),
    mVkTrackHighlightHorizontalImage(p.track_highlight_horizontal_image.vk_image_name.isProvided() ? p.track_highlight_horizontal_image.vk_image_name() : ""),
    mVkTrackHighlightVerticalImage(p.track_highlight_vertical_image.vk_image_name.isProvided() ? p.track_highlight_vertical_image.vk_image_name() : ""),
    mVkThumbWidth(16),
    mVkThumbHeight(16),
    mMouseDownSignal(NULL),
    mMouseUpSignal(NULL)
{
    mViewModel->setValue(p.initial_value);
    updateThumbRect();
    mDragStartThumbRect = mThumbRect;
    setControlName(p.control_name, NULL);
    setValue(getValueF32());

    if (p.mouse_down_callback.isProvided())
    {
        setMouseDownCallback(initCommitCallback(p.mouse_down_callback));
    }
    if (p.mouse_up_callback.isProvided())
    {
        setMouseUpCallback(initCommitCallback(p.mouse_up_callback));
    }
}

LLSlider::~LLSlider()
{
    delete mMouseDownSignal;
    delete mMouseUpSignal;
}

void LLSlider::setValue(F32 value, bool from_event)
{
    value = llclamp( value, mMinValue, mMaxValue );

    // Round to nearest increment (bias towards rounding down)
    value -= mMinValue;
    value += mIncrement/2.0001f;
    value -= fmod(value, mIncrement);
    value += mMinValue;

    if (!from_event && getValueF32() != value)
    {
        setControlValue(value);
    }

    LLF32UICtrl::setValue(value);
    updateThumbRect();
}

void LLSlider::updateThumbRect()
{
    F32 range = mMaxValue - mMinValue;
    F32 t = range != 0.f ? (getValueF32() - mMinValue) / range : 0.f;

    S32 thumb_width = getThumbWidth();
    S32 thumb_height = getThumbHeight();

    if ( mOrientation == HORIZONTAL )
    {
        S32 left_edge = (thumb_width / 2);
        S32 right_edge = getRect().getWidth() - (thumb_width / 2);

        S32 x = left_edge + S32( t * (right_edge - left_edge) );
        mThumbRect.mLeft = x - (thumb_width / 2);
        mThumbRect.mRight = mThumbRect.mLeft + thumb_width;
        mThumbRect.mBottom = getLocalRect().getCenterY() - (thumb_height / 2);
        mThumbRect.mTop = mThumbRect.mBottom + thumb_height;
    }
    else
    {
        S32 top_edge = (thumb_height / 2);
        S32 bottom_edge = getRect().getHeight() - (thumb_height / 2);

        S32 y = top_edge + S32( t * (bottom_edge - top_edge) );
        mThumbRect.mLeft = getLocalRect().getCenterX() - (thumb_width / 2);
        mThumbRect.mRight = mThumbRect.mLeft + thumb_width;
        mThumbRect.mBottom = y  - (thumb_height / 2);
        mThumbRect.mTop = mThumbRect.mBottom + thumb_height;
    }
}

S32 LLSlider::getThumbWidth() const
{
    return mThumbImage ? mThumbImage->getWidth() : mVkThumbWidth;
}

S32 LLSlider::getThumbHeight() const
{
    return mThumbImage ? mThumbImage->getHeight() : mVkThumbHeight;
}


void LLSlider::setValueAndCommit(F32 value)
{
    F32 old_value = getValueF32();
    setValue(value);

    if (getValueF32() != old_value)
    {
        onCommit();
    }
}


bool LLSlider::handleHover(S32 x, S32 y, MASK mask)
{
    if( hasMouseCapture() )
    {
        if ( mOrientation == HORIZONTAL )
        {
            S32 thumb_half_width = getThumbWidth()/2;
            S32 left_edge = thumb_half_width;
            S32 right_edge = getRect().getWidth() - (thumb_half_width);

            x += mMouseOffset;
            x = llclamp( x, left_edge, right_edge );

            F32 t = F32(x - left_edge) / (right_edge - left_edge);
            setValueAndCommit(t * (mMaxValue - mMinValue) + mMinValue );
        }
        else // mOrientation == VERTICAL
        {
            S32 thumb_half_height = getThumbHeight()/2;
            S32 top_edge = thumb_half_height;
            S32 bottom_edge = getRect().getHeight() - (thumb_half_height);

            y += mMouseOffset;
            y = llclamp(y, top_edge, bottom_edge);

            F32 t = F32(y - top_edge) / (bottom_edge - top_edge);
            setValueAndCommit(t * (mMaxValue - mMinValue) + mMinValue );
        }
        getWindow()->setCursor(UI_CURSOR_ARROW);
        LL_DEBUGS("UserInput") << "hover handled by " << getName() << " (active)" << LL_ENDL;
    }
    else
    {
        getWindow()->setCursor(UI_CURSOR_ARROW);
        LL_DEBUGS("UserInput") << "hover handled by " << getName() << " (inactive)" << LL_ENDL;
    }
    return true;
}

bool LLSlider::handleMouseUp(S32 x, S32 y, MASK mask)
{
    bool handled = false;

    if( hasMouseCapture() )
    {
        gFocusMgr.setMouseCapture( NULL );

        if (mMouseUpSignal)
            (*mMouseUpSignal)( this, getValueF32() );

        handled = true;
        make_ui_sound("UISndClickRelease");
    }
    else
    {
        handled = true;
    }

    return handled;
}

bool LLSlider::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // only do sticky-focus on non-chrome widgets
    if (!getIsChrome())
    {
        setFocus(true);
    }
    if (mMouseDownSignal)
        (*mMouseDownSignal)( this, getValueF32() );

    if (MASK_CONTROL & mask) // if CTRL is modifying
    {
        setValueAndCommit(mInitialValue);
    }
    else
    {
        // Find the offset of the actual mouse location from the center of the thumb.
        if (mThumbRect.pointInRect(x,y))
        {
            mMouseOffset = (mOrientation == HORIZONTAL)
                ? (mThumbRect.mLeft + getThumbWidth()/2) - x
                : (mThumbRect.mBottom + getThumbHeight()/2) - y;
        }
        else
        {
            mMouseOffset = 0;
        }

        // Start dragging the thumb
        // No handler needed for focus lost since this class has no state that depends on it.
        gFocusMgr.setMouseCapture( this );
        mDragStartThumbRect = mThumbRect;
    }
    make_ui_sound("UISndClick");

    return true;
}

bool LLSlider::handleKeyHere(KEY key, MASK mask)
{
    bool handled = false;
    switch(key)
    {
    case KEY_DOWN:
    case KEY_LEFT:
        setValueAndCommit(getValueF32() - getIncrement());
        handled = true;
        break;
    case KEY_UP:
    case KEY_RIGHT:
        setValueAndCommit(getValueF32() + getIncrement());
        handled = true;
        break;
    default:
        break;
    }
    return handled;
}

bool LLSlider::handleScrollWheel(S32 x, S32 y, S32 clicks)
{
    if ( mOrientation == VERTICAL )
    {
        F32 new_val = getValueF32() - clicks * getIncrement();
        setValueAndCommit(new_val);
        return true;
    }
    return LLF32UICtrl::handleScrollWheel(x,y,clicks);
}

void LLSlider::draw()
{
    F32 alpha = getDrawContext().mAlpha;

    // since thumb image might still be decoding, need thumb to accomodate image size
    updateThumbRect();

    // Draw background and thumb.

    // drawing solids requires texturing be disabled
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // Track
    LLPointer<LLUIImage>& trackImage = ( mOrientation == HORIZONTAL )
        ? mTrackImageHorizontal
        : mTrackImageVertical;

    LLPointer<LLUIImage>& trackHighlightImage = ( mOrientation == HORIZONTAL )
        ? mTrackHighlightHorizontalImage
        : mTrackHighlightVerticalImage;

    LLRect track_rect;
    LLRect highlight_rect;

    if (!mThumbImage || !trackImage || !trackHighlightImage)
    {
        // Native Vulkan paints from getVkDrawState().  More importantly,
        // never dereference the deliberately-null GL image provider.
        LLUICtrl::draw();
        return;
    }

    if ( mOrientation == HORIZONTAL )
    {
        track_rect.set(mThumbImage->getWidth() / 2,
                       getLocalRect().getCenterY() + (trackImage->getHeight() / 2),
                       getRect().getWidth() - mThumbImage->getWidth() / 2,
                       getLocalRect().getCenterY() - (trackImage->getHeight() / 2) );
        highlight_rect.set(track_rect.mLeft, track_rect.mTop, mThumbRect.getCenterX(), track_rect.mBottom);
    }
    else
    {
        track_rect.set(getLocalRect().getCenterX() - (trackImage->getWidth() / 2),
                       getRect().getHeight(),
                       getLocalRect().getCenterX() + (trackImage->getWidth() / 2),
                       0);
        highlight_rect.set(track_rect.mLeft, track_rect.mTop, track_rect.mRight, track_rect.mBottom);
    }

    LLColor4 color = isInEnabledChain() ? LLColor4::white % alpha : LLColor4::white % (0.6f * alpha);
    trackImage->draw(track_rect, color);
    trackHighlightImage->draw(highlight_rect, color);

    // Thumb
    if (hasFocus())
    {
        // Draw focus highlighting.
        mThumbImage->drawBorder(mThumbRect, gFocusMgr.getFocusColor() % alpha, gFocusMgr.getFocusFlashWidth());
    }

    if( hasMouseCapture() ) // currently clicking on slider
    {
        // Show ghost where thumb was before dragging began.
        if (mThumbImage.notNull())
        {
            mThumbImage->draw(mDragStartThumbRect, mThumbCenterColor.get() % (0.3f * alpha));
        }
        if (mThumbImagePressed.notNull())
        {
            mThumbImagePressed->draw(mThumbRect, mThumbOutlineColor % alpha);
        }
    }
    else if (!isInEnabledChain())
    {
        if (mThumbImageDisabled.notNull())
        {
            mThumbImageDisabled->draw(mThumbRect, mThumbCenterColor % alpha);
        }
    }
    else
    {
        if (mThumbImage.notNull())
        {
            mThumbImage->draw(mThumbRect, mThumbCenterColor % alpha);
        }
    }

    LLUICtrl::draw();
}

std::string LLSlider::getVkThumbImageName() const
{
    return mThumbImage.notNull() ? mThumbImage->getName() : mVkThumbImage;
}

void LLSlider::prepareVkDraw(S32 thumb_width, S32 thumb_height)
{
    thumb_width = llmax(1, thumb_width);
    thumb_height = llmax(1, thumb_height);
    if (mVkThumbWidth == thumb_width && mVkThumbHeight == thumb_height)
    {
        return;
    }
    mVkThumbWidth = thumb_width;
    mVkThumbHeight = thumb_height;
    updateThumbRect();
}

LLSlider::VkDrawState LLSlider::getVkDrawState(F32 alpha) const
{
    VkDrawState state;
    state.horizontal = mOrientation == HORIZONTAL;
    state.control_rect = calcScreenRect();
    localRectToScreen(mThumbRect, &state.thumb_rect);
    localRectToScreen(mDragStartThumbRect, &state.drag_start_thumb_rect);

    const auto image_name = [](const LLPointer<LLUIImage>& image,
                               const std::string& retained)
    {
        return image.notNull() ? image->getName() : retained;
    };
    state.track_image = state.horizontal
        ? image_name(mTrackImageHorizontal, mVkTrackImageHorizontal)
        : image_name(mTrackImageVertical, mVkTrackImageVertical);
    state.track_highlight_image = state.horizontal
        ? image_name(mTrackHighlightHorizontalImage, mVkTrackHighlightHorizontalImage)
        : image_name(mTrackHighlightVerticalImage, mVkTrackHighlightVerticalImage);

    const bool mouse_capture = gFocusMgr.getMouseCapture() == this;
    if (mouse_capture)
    {
        state.thumb_image = image_name(mThumbImagePressed, mVkThumbImagePressed);
        if (state.thumb_image.empty()) state.thumb_image = getVkThumbImageName();
        state.draw_ghost = true;
    }
    else if (!isInEnabledChain())
    {
        state.thumb_image = image_name(mThumbImageDisabled, mVkThumbImageDisabled);
        if (state.thumb_image.empty()) state.thumb_image = getVkThumbImageName();
    }
    else
    {
        state.thumb_image = getVkThumbImageName();
    }

    const F32 enabled_alpha = isInEnabledChain() ? alpha : 0.6f * alpha;
    state.track_color = LLColor4::white % enabled_alpha;
    state.thumb_color = (mouse_capture ? mThumbOutlineColor.get()
                                       : mThumbCenterColor.get()) % alpha;
    state.ghost_color = mThumbCenterColor.get() % (0.3f * alpha);
    state.draw_focus = hasFocus();
    return state;
}

boost::signals2::connection LLSlider::setMouseDownCallback( const commit_signal_t::slot_type& cb )
{
    if (!mMouseDownSignal) mMouseDownSignal = new commit_signal_t();
    return mMouseDownSignal->connect(cb);
}

boost::signals2::connection LLSlider::setMouseUpCallback(   const commit_signal_t::slot_type& cb )
{
    if (!mMouseUpSignal) mMouseUpSignal = new commit_signal_t();
    return mMouseUpSignal->connect(cb);
}
