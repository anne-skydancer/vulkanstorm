// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Fixture: a settings hook. The GLSL case is a run-time macro the loader defines; the SPIR-V case is a constant.
module hooks_mod;

public int hookSSR()
{
    __target_switch
    {
    case glsl: __intrinsic_asm "(SSR)";
    case spirv: return 0;
    }
}
