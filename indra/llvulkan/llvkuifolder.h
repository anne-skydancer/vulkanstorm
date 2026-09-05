/**
 * @file llvkuifolder.h
 * @brief Greenfield Vulkan passes for the folder-view (inventory tree) widgets.
 *
 * @details
 * LLFolderView/LLFolderViewItem/LLFolderViewFolder draw their chrome (disclosure
 * arrows, item icons, link overlays, selection/filter highlights, label text)
 * directly in draw() with GL calls. These passes reproduce that output by
 * reading the items' GL-free Vk* state and emitting into the LLVKUI2D sink.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#ifndef LLVKUIFOLDER_H
#define LLVKUIFOLDER_H

class LLView;
class LLVKContext;

namespace LLVKUIRenderInternal
{
    struct RenderCtx;
}

namespace LLVKUIFolder
{
    // Per-view GL-free preparation (layout/state reconciliation the GL path
    // performs inside draw()).
    void prepareView(LLVKContext* context, const LLView* view);

    // Per-view chrome emission for folder view items.
    void renderChrome(LLVKUIRenderInternal::RenderCtx& rc, const LLView* view);
}

#endif // LLVKUIFOLDER_H
