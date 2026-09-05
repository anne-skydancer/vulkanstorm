/**
 * @file llvkuimaps.h
 * @brief Vulkan UI hooks for the map widgets (LLNetMap minimap and
 *        LLWorldMapView world map).
 *
 * @details
 * The greenfield Vulkan UI renderer (indra/llvulkan/llvkuirender.cpp) walks
 * the LLView tree without executing any GL draw() code. This file's
 * implementation (llvkuimaps.cpp) registers per-class prepare/render hooks
 * for the two map views; the hooks read GL-free Vk* state accessors on the
 * views and emit the equivalent primitives into the LLVKUI2D sink.
 *
 * GL-free: no llgl / gGL / LLRender usage.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#ifndef LL_LLVKUIMAPS_H
#define LL_LLVKUIMAPS_H

// <VulkanStorm> Register the LLNetMap / LLWorldMapView hooks with
// LLVKUIRender. Idempotent. Called from llviewerdisplay.cpp's
// vk_register_ui_hooks().
void vk_register_map_hooks();

#endif // LL_LLVKUIMAPS_H
