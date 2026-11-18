// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file lltexturefetchpolicy.h
 * @brief Texture fetch rules: the bounded retry of fetches that end without data.
 *
 * Free of GL and viewer types: LLViewerFetchedTexture (llviewertexture.cpp) applies the rules, and
 * indra/newview/tests/lltexturefetchpolicy_test.cpp checks them standalone.
 *
 * Why: a refinement fetch that ended without data (an HTTP error, an empty or broken reply, a failed
 * decode, a request aborted while its texture was off screen) set mMinDiscardLevel to the texture's
 * level at once, and no request was made again while the texture stayed at that level: it stayed
 * blurry for the life of the texture object, zooming in did not help, and Refresh Textures did not
 * clear it either.
 */

#ifndef LL_LLTEXTUREFETCHPOLICY_H
#define LL_LLTEXTUREFETCHPOLICY_H

namespace LLTextureFetchPolicy
{
    // Fetches in a row that end without data are retried after 1, 2 and 4 s. When the third retry
    // fails too, the texture stops where it is, as it used to at the first failure (the caller sets
    // mMinDiscardLevel). Good data and Refresh Textures start over (reset).
    struct FetchRetry
    {
        static constexpr unsigned MAX_RETRIES = 3;

        unsigned mFailures = 0;     // fetches in a row that ended without data
        float mRetryTime = 0.f;     // no new request before this time (the texture clock, seconds)

        // Records a fetch that ended without data at time `now`. True while a retry remains: the
        // next request waits until mRetryTime. False once the retries are spent.
        bool onFailure(float now)
        {
            if (mFailures >= MAX_RETRIES)
            {
                return false;
            }
            mRetryTime = now + float(1u << mFailures); // 1, 2, 4 s
            ++mFailures;
            return true;
        }

        bool mayRequest(float now) const
        {
            return now >= mRetryTime;
        }

        void reset()
        {
            mFailures = 0;
            mRetryTime = 0.f;
        }
    };
}

#endif // LL_LLTEXTUREFETCHPOLICY_H
