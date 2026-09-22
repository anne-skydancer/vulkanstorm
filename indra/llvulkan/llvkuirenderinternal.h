/**
 * @file llvkuirenderinternal.h
 * @brief Internal shared context for the greenfield Vulkan UI walker passes.
 *
 * @details
 * The walker core lives in llvkuirender.cpp; additional per-widget chrome
 * passes live in sibling TUs (llvkuiwidgets*.cpp, llvkuifolder.cpp) so the
 * ports can grow without bloating the walker. This header exposes the
 * per-frame render context and the emission helpers those passes share.
 *
 * Everything here is GL-free: passes read widget state via public Vk*
 * accessors and emit into the LLVKUI2D sink.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#ifndef LLVKUIRENDERINTERNAL_H
#define LLVKUIRENDERINTERNAL_H

#include <algorithm>            // std::swap
#include <vector>

#include "llrect.h"
#include "v4color.h"
#include "llmath.h"             // ll_round
#include "llvkui2d.h"

class LLView;

namespace LLVKUIRenderInternal
{
    // Per-frame render context, threaded through the tree walk.
    struct RenderCtx
    {
        unsigned dev_w = 0;
        unsigned dev_h = 0;
        float    ui_scale_x = 1.f;
        float    ui_scale_y = 1.f;
        float    parent_alpha = 1.f;   // accumulated draw-context alpha
        // GL-space screen clip stack (scroll containers, accordion tabs, hook
        // clippers). The effective scissor is the intersection of all entries.
        std::vector<LLRect> clip_stack;
        // Diagnostics
        int visited = 0;      // views walked
        int visible = 0;      // views passing getVisible()
        int panels  = 0;      // views that are LLPanel
        int emitted = 0;      // rects actually emitted
        // One-shot widget-tree dump (VULKANSTORM_TREE_DUMP=1)
        bool dump   = false;
        int  depth  = 0;
        // True while rendering a registered popup via renderOverlaySubtree()
        // (LLPopupView). A combo's open dropdown list renders only in this
        // pass; the normal tree walk skips it so the two don't double-draw.
        bool in_overlay = false;
    };

    // Convert a GL bottom-left-origin screen rect (from calcScreenRect) into
    // the sink's top-left-origin coordinate space. Normalizes so top <= bottom
    // (the GL->top-left conversion can produce inverted or off-window rects for
    // some widgets; draw9Slice's band mapping assumes a sane top<bottom rect).
    inline void toSinkRect(const RenderCtx& rc, const LLRect& gl_rect,
                           float& left, float& top, float& right, float& bottom)
    {
        const F32 ui_h = (F32)rc.dev_h / rc.ui_scale_y;
        left   = (F32)gl_rect.mLeft;
        right  = (F32)gl_rect.mRight;
        top    = ui_h - (F32)gl_rect.mTop;
        bottom = ui_h - (F32)gl_rect.mBottom;
        if (left > right) std::swap(left, right);
        if (top > bottom) std::swap(top, bottom);
    }

    // Clip stack. The GL path clips scrolled/accordion content with
    // LLScreenClipRect/LLLocalClipRect scopes around child draws; the sink
    // only supports a single flat scissor, so the walker keeps the stack of
    // GL-space screen rects and applies their intersection.
    void applyClip(const RenderCtx& rc);
    void pushClip(RenderCtx& rc, const LLRect& gl_screen_rect);
    void popClip(RenderCtx& rc);

    // Emit one GL-space line segment into the sink (GL bottom-left -> top-left
    // conversion, same mapping as toSinkRect). Each gl_line_2d edge becomes
    // its own 2-vertex strip so independent segments never connect.
    void emitBorderLine(const RenderCtx& rc, S32 x1, S32 y1, S32 x2, S32 y2,
                        const LLColor4& c);

    // Mirror gl_drop_shadow (llrender2dutils.cpp): the same 30-vertex gradient
    // fan hugging the right/bottom edges, with the same 1px overlap hack and
    // per-vertex alpha fade, in sink space.
    void emitDropShadow(const RenderCtx& rc, const LLRect& gl_screen,
                        const LLColor4& start_color, S32 lines);

    // Emit a GL-space circle as a CPU-triangulated fan (gl_circle_2d filled)
    // or a line strip (outline). segments controls tessellation.
    void emitCircle(const RenderCtx& rc, F32 gl_center_x, F32 gl_center_y,
                    F32 radius, const LLColor4& color, bool filled,
                    S32 segments = 32);
}

#endif // LLVKUIRENDERINTERNAL_H
