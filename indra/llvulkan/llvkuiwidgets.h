/**
 * @file llvkuiwidgets.h
 * @brief Additional greenfield Vulkan passes for the remaining llui widgets.
 *
 * @details
 * The walker core (llvkuirender.cpp) covers panels/floaters, buttons, icons,
 * line editors, text, scrollbars, sliders, scroll lists, menus and borders.
 * This module carries the passes for the rest of llui's custom-draw widgets
 * (progress bars, tooltips, loading indicators, badges, multi-sliders,
 * accordion chrome, editor extras, stat widgets, ...), keeping llvkuirender.cpp
 * to the walker core.
 *
 * GL-free: passes read widget state via the widgets' public Vk* accessors and
 * emit into the LLVKUI2D sink. Never calls widget draw() and never reads gGL.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#ifndef LLVKUIWIDGETS_H
#define LLVKUIWIDGETS_H

#include "llrect.h"

class LLView;
class LLVKContext;

namespace LLVKUIRenderInternal
{
    struct RenderCtx;
}

namespace LLVKUIWidgets
{
    // Per-view GL-free preparation (layout/state reconciliation the GL path
    // performs inside draw()). Called by LLVKUIRender's prepareView walk.
    void prepareView(LLVKContext* context, const LLView* view);

    // Per-view chrome emission. Called by the walker after the built-in
    // passes, before recursing into children (painter's order preserved).
    void renderChrome(LLVKUIRenderInternal::RenderCtx& rc, const LLView* view);

    // Alpha the view pushes for its own subtree (e.g. LLToolTip fade).
    // Returns 1 for views that do not modulate alpha. The walker multiplies
    // this into the accumulated parent alpha before recursing.
    float subtreeAlpha(const LLView* view);

    // True + out rect when the view clips its whole subtree (GL-space screen
    // rect). Currently: LLAccordionCtrl (clip to its rect).
    bool subtreeClip(const LLView* view, LLRect& gl_screen_rect);

    // Child-selective clip: true + out rect when `child` of `view` must be
    // clipped (GL-space screen rect). Currently: LLAccordionCtrlTab's
    // container panel in non-fit mode.
    bool childClip(const LLView* view, const LLView* child, LLRect& gl_screen_rect);
}

#endif // LLVKUIWIDGETS_H
