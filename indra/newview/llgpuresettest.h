// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The forced GPU reset test hook: development builds only (-DVKS_GPU_RESET_TEST=ON), never on release or in CI.
// The RenderGPUResetTest debug setting runs one test at the next frame. See doc/gl/gpu_reset.md, "Testing".
#pragma once

#if VKS_GPU_RESET_TEST

namespace LLGPUResetTest
{
    // Main thread, once a frame, just before the frame's GPU reset check: runs the test RenderGPUResetTest asks for,
    // once, and sets the setting back to 0.
    void update();
}

#endif
