/**
 * @file llcpucheck_win.cpp
 * @brief Executable entry point that checks for an x86-64-v3 (AVX2) processor
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Vulkanstorm is built for x86-64-v3 (AVX2, FMA, BMI1/2, F16C, MOVBE, LZCNT).
 * On an older processor the first such instruction would crash with an
 * illegal-instruction fault, in some static initialiser, with no explanation.
 *
 * This file provides the executable's real entry point (/ENTRY:vkstormEntry).
 * It runs before the C runtime starts and before every static initialiser,
 * so nothing AVX2 can have executed yet. llwebrtc.dll, whose own initialisers
 * are built with /arch:AVX2, is delay-loaded for the same reason.
 *
 * Constraints that keep it safe on any x86-64 processor:
 *  - compiled with /arch:SSE2 /GS- /GL- (see CMakeLists.txt) and the function
 *    is marked safebuffers: no stack cookie, no AVX code, no cross-module
 *    inlining of AVX code;
 *  - no static data and no C runtime calls before the check.
 */

#include <windows.h>
#include <intrin.h>

extern "C" int wWinMainCRTStartup(void);

extern "C" __declspec(safebuffers) int vkstormEntry(void)
{
    bool ok = false;

    int regs[4] = { 0, 0, 0, 0 };
    __cpuid(regs, 0);
    const int max_leaf = regs[0];

    if (max_leaf >= 7)
    {
        __cpuid(regs, 1);
        const unsigned int ecx1 = static_cast<unsigned int>(regs[2]);
        const bool fma     = (ecx1 & (1u << 12)) != 0;
        const bool movbe   = (ecx1 & (1u << 22)) != 0;
        const bool osxsave = (ecx1 & (1u << 27)) != 0;
        const bool avx     = (ecx1 & (1u << 28)) != 0;
        const bool f16c    = (ecx1 & (1u << 29)) != 0;

        __cpuidex(regs, 7, 0);
        const unsigned int ebx7 = static_cast<unsigned int>(regs[1]);
        const bool bmi1 = (ebx7 & (1u << 3)) != 0;
        const bool avx2 = (ebx7 & (1u << 5)) != 0;
        const bool bmi2 = (ebx7 & (1u << 8)) != 0;

        __cpuid(regs, static_cast<int>(0x80000000u));
        bool lzcnt = false;
        if (static_cast<unsigned int>(regs[0]) >= 0x80000001u)
        {
            __cpuid(regs, static_cast<int>(0x80000001u));
            lzcnt = (static_cast<unsigned int>(regs[2]) & (1u << 5)) != 0;
        }

        if (fma && movbe && osxsave && avx && f16c && bmi1 && avx2 && bmi2 && lzcnt)
        {
            // XCR0 bits 1 and 2: the operating system saves XMM and YMM state.
            // Only valid to query once OSXSAVE is known to be set.
            ok = (_xgetbv(0) & 6) == 6;
        }
    }

    if (!ok)
    {
        MessageBoxW(NULL,
                    L"Vulkanstorm needs a processor with AVX2 (x86-64-v3): "
                    L"Intel Haswell (2013) or newer, AMD Zen or Excavator or newer. "
                    L"This computer's processor does not support it.",
                    L"Vulkanstorm",
                    MB_ICONERROR);
        ExitProcess(1);
    }

    return wWinMainCRTStartup();
}
