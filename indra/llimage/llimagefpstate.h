// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The floating-point state a C codec expects: round to nearest even, no flush-to-zero, no denormals-are-zero.
// The viewer's main thread runs with flush-to-zero on (LLVector4a::initClass), and a pool thread starts with
// whatever its platform gives it, so a codec whose output must not depend on the thread (neo-j2k's lossy
// decode and encode: float arithmetic and `lrintf`) runs inside this scope. The state is put back at the end of
// the scope; the exception flags are not touched.
#ifndef LL_LLIMAGEFPSTATE_H
#define LL_LLIMAGEFPSTATE_H

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__)
#include <xmmintrin.h>

class LLImageFPScope
{
public:
    LLImageFPScope() : mSaved(_mm_getcsr())
    {
        const unsigned int wanted = mSaved & ~kControl;
        if (wanted != mSaved)
        {
            _mm_setcsr(wanted);
        }
    }
    ~LLImageFPScope()
    {
        const unsigned int now = _mm_getcsr();
        if ((now & kControl) != (mSaved & kControl))
        {
            _mm_setcsr((now & ~kControl) | (mSaved & kControl));
        }
    }
    LLImageFPScope(const LLImageFPScope&) = delete;
    LLImageFPScope& operator=(const LLImageFPScope&) = delete;

private:
    // rounding control (bits 13-14), flush-to-zero (bit 15), denormals-are-zero (bit 6)
    static constexpr unsigned int kControl = 0x6000u | 0x8000u | 0x0040u;
    unsigned int mSaved;
};
#else
// Other architectures round to nearest even and keep denormals by default.
class LLImageFPScope
{
public:
    LLImageFPScope() = default;
    LLImageFPScope(const LLImageFPScope&) = delete;
    LLImageFPScope& operator=(const LLImageFPScope&) = delete;
};
#endif

#endif // LL_LLIMAGEFPSTATE_H
