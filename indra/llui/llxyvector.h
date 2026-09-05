/**
* @file llxyvector.h
* @author Andrey Lihatskiy
* @brief Header file for LLXYVector
*
* $LicenseInfo:firstyear=2001&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2018, Linden Research, Inc.
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

// A control that allows to set two related vector magnitudes by manipulating a single vector on a plane.

#ifndef LL_LLXYVECTOR_H
#define LL_LLXYVECTOR_H

#include "lluictrl.h"
#include "llpanel.h"
#include "lltextbox.h"
#include "lllineeditor.h"

class LLXYVector
    : public LLUICtrl
{
public:
    struct Params
        : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<LLLineEditor::Params>      x_entry;
        Optional<LLLineEditor::Params>      y_entry;
        Optional<LLPanel::Params>           touch_area;
        Optional<LLViewBorder::Params>      border;
        Optional<S32>                       edit_bar_height;
        Optional<S32>                       padding;
        Optional<S32>                       label_width;
        Optional<F32>                       min_val_x;
        Optional<F32>                       max_val_x;
        Optional<F32>                       increment_x;
        Optional<F32>                       min_val_y;
        Optional<F32>                       max_val_y;
        Optional<F32>                       increment_y;
        Optional<LLUIColor>                 arrow_color;
        Optional<LLUIColor>                 ghost_color;
        Optional<LLUIColor>                 area_color;
        Optional<LLUIColor>                 grid_color;
        Optional<bool>                      logarithmic;

        Params();
    };


    ~LLXYVector() override;
    bool postBuild() override;

    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;

    void draw() override;

    // <VulkanStorm> GL-free state for the Vulkan UI walker (mirrors draw()).
    // prepareVkDraw() performs draw()'s ghost-position sync; getVkDrawState()
    // returns the geometry in widget-LOCAL coordinates (bottom-left origin).
    struct VkDrawState
    {
        LLRect      touch_rect;         // local
        LLColor4    area_color;
        LLColor4    grid_color;
        S32         center_x = 0;       // local
        S32         center_y = 0;
        S32         point_x = 0;        // arrow tip (local)
        S32         point_y = 0;
        bool        draw_arrow = false;
        LLColor4    arrow_color;
        bool        draw_ghost = false;
        S32         ghost_x = 0;        // local
        S32         ghost_y = 0;
        LLColor4    ghost_color;
        F32         circle_radius = 2.f;
    };
    void prepareVkDraw();
    void getVkDrawState(VkDrawState& out) const;
    // </VulkanStorm>

    void setValue(const LLSD& value) override;
    void setValue(F32 x, F32 y);
    LLSD getValue() const override;

protected:
    friend class LLUICtrlFactory;
    LLXYVector(const Params&);
    void onEditChange();

protected:
    LLTextBox*          mXLabel;
    LLTextBox*          mYLabel;
    LLLineEditor*       mXEntry;
    LLLineEditor*       mYEntry;
    LLPanel*            mTouchArea;
    LLViewBorder*       mBorder;

private:
    void update();
    void setValueAndCommit(F32 x, F32 y);

    F32 mValueX;
    F32 mValueY;

    F32 mMinValueX;
    F32 mMaxValueX;
    F32 mIncrementX;
    F32 mMinValueY;
    F32 mMaxValueY;
    F32 mIncrementY;

    U32 mGhostX;
    U32 mGhostY;

    LLUIColor mArrowColor;
    LLUIColor mGhostColor;
    LLUIColor mAreaColor;
    LLUIColor mGridColor;

    bool mLogarithmic;
    F32 mLogScaleX;
    F32 mLogScaleY;
};

#endif

