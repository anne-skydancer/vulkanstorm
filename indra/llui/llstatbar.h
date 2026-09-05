/**
 * @file llstatbar.h
 * @brief A little map of the world with network information
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

#ifndef LL_LLSTATBAR_H
#define LL_LLSTATBAR_H

#include "llview.h"
#include "llframetimer.h"
#include "lltracerecording.h"

class LLStatBar : public LLView
{
public:

    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Optional<std::string>   label,
                                unit_label;

        Optional<F32>           bar_min,
                                bar_max,
                                tick_spacing;

        Optional<bool>          show_bar,
                                show_history,
                                scale_range,
                                show_median; // default is mean

        Optional<S32>           decimal_digits,
                                num_frames,
                                num_frames_short,
                                max_height;
        Optional<std::string>   stat;
        Optional<EOrientation>  orientation;

        // <FS:Ansariel> Save display state
        Optional<std::string>   setting;

        Params();
    };
    LLStatBar(const Params&);
    // <FS:Ansariel> Save display state
    ~LLStatBar();

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool handleHover(S32 x, S32 y, MASK mask);

    void setStat(const std::string& stat_name);

    void setRange(F32 bar_min, F32 bar_max);
    void getRange(F32& bar_min, F32& bar_max) const { bar_min = mTargetMinBar; bar_max = mTargetMaxBar; }

    /*virtual*/ LLRect getRequiredRect();   // Return the height of this object, given the set options.

    // <VulkanStorm> GL-free draw state for the Vulkan UI walker.
    // prepareVkDraw() performs draw()'s value computation and state mutation
    // (display-value rate limiting, range smoothing, auto-scaling) once per
    // frame; getVkDrawState() exposes the computed geometry in widget-LOCAL
    // coordinates (bottom-left origin, same frame draw() renders in).
    struct VkTick
    {
        LLRect      rect;           // tick mark quad (local)
        bool        labeled = false;
        LLWString   label;
        F32         label_x = 0.f;  // local coords
        F32         label_y = 0.f;
        LLFontGL::VAlign label_valign = LLFontGL::VCENTER;
    };
    struct VkDrawState
    {
        bool        valid = false;          // stat bound
        LLWString   label;
        std::string value_text;
        bool        horizontal = true;
        LLRect      bar_rect;               // local
        bool        bar_visible = false;
        bool        band_valid = false;
        LLRect      band_rect;              // min..max red band (local)
        bool        history_mode = false;
        std::vector<LLRect> hist_quads;     // per-period min..max quads (local)
        bool        cur_valid = false;
        LLRect      cur_rect;               // current-value marker (local)
        LLRect      mean_rect;              // mean marker (local)
        std::vector<VkTick> ticks;
    };
    void prepareVkDraw();
    const VkDrawState& getVkDrawState() const { return mVkState; }
    // </VulkanStorm>

private:
    void drawLabelAndValue( F32 mean, std::string &unit_label, LLRect &bar_rect, S32 decimal_digits );
    void drawTicks( F32 min, F32 max, F32 value_scale, LLRect &bar_rect );

    // <VulkanStorm>
    VkDrawState  mVkState;
    // </VulkanStorm>

    F32          mTargetMinBar,
                 mTargetMaxBar,
                 mFloatingTargetMinBar,
                 mFloatingTargetMaxBar,
                 mCurMaxBar,
                 mCurMinBar,
                 mTickSpacing;
    S32          mDecimalDigits,
                 mNumHistoryFrames,
                 mNumShortHistoryFrames;
    S32          mMaxHeight;
    EOrientation mOrientation;
    F32          mLastDisplayValue;
    LLFrameTimer mLastDisplayValueTimer;

    enum
    {
        STAT_NONE,
        STAT_COUNT,
        STAT_EVENT,
        STAT_SAMPLE
    } mStatType;

    union
    {
        void*                                                   valid;
        const LLTrace::StatType<LLTrace::CountAccumulator>*     countStatp;
        const LLTrace::StatType<LLTrace::EventAccumulator>*     eventStatp;
        const LLTrace::StatType<LLTrace::SampleAccumulator>*    sampleStatp;
    } mStat;

    LLUIString   mLabel;
    std::string  mUnitLabel;

    bool         mDisplayBar,           // Display the bar graph.
                 mDisplayHistory,
                 mShowMedian,
                 mAutoScaleMax,
                 mAutoScaleMin;

    // <FS:Ansariel> Save display state
    std::string mSetting;
};

#endif
