// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file lltexturefetchpolicy.h
 * @brief Texture fetch rules: the bounded retry of fetches that end without data, the end of a fetch
 *        whose request failed, and the priority snapshots that order waiting requests.
 *
 * Free of GL and viewer types: LLViewerFetchedTexture (llviewertexture.cpp) and LLTextureFetch
 * (lltexturefetch.cpp) apply the rules, and indra/newview/tests/lltexturefetchpolicy_test.cpp checks
 * them standalone.
 *
 * Why: a refinement fetch that ended without data (an HTTP error, an empty or broken reply, a failed
 * decode, a request aborted while its texture was off screen) set mMinDiscardLevel to the texture's
 * level at once, and no request was made again while the texture stayed at that level: it stayed
 * blurry for the life of the texture object, zooming in did not help, and Refresh Textures did not
 * clear it either. A first fetch that ended without data marked the asset missing for the session,
 * whatever the cause. A refinement whose request failed while its worker held data it had decoded
 * before did not end at all: the worker decoded that data again and sent the same request again at
 * once, for as long as the texture stayed on screen. And the waiting requests were sorted by their
 * live priorities, which other threads change during the sort.
 */

#ifndef LL_LLTEXTUREFETCHPOLICY_H
#define LL_LLTEXTUREFETCHPOLICY_H

namespace LLTextureFetchPolicy
{
    // Fetches in a row that end without data are retried after 1, 2 and 4 s. When the third retry
    // fails too, or at once for a failure a retry cannot fix (a 404, data that did not decode, data
    // that cannot arrive; the caller stops then without calling onFailure), a texture with data stops
    // where it is (the caller sets mMinDiscardLevel) and a texture with no data yet is marked missing
    // (setIsMissingAsset), as both used to at the first failure. Good data and Refresh Textures start
    // over (reset).
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

    // Whether a fetch worker whose request for more data failed decodes the data it already holds
    // (LLTextureFetchWorker::doWork): only when that data reaches the requested level. `available_discard`
    // is the level that data was last decoded at, -1 when it never was; `desired_discard` is the level
    // requested. Otherwise the fetch ends without data and FetchRetry takes over. The worker used to decode
    // it whatever its level: data decoded in an earlier round, coarser than requested, decoded at that
    // level again, so the worker went back to INIT and sent the failed request again at once, with no
    // bound while the texture stayed on screen; data never decoded already ended the fetch without data
    // (an invalid level).
    inline bool decodeAfterFailedRequest(int available_discard, int desired_discard)
    {
        return available_discard >= 0 && available_discard <= desired_discard;
    }

    // A request's priority, read once under the request's lock, for one ordering of the waiting requests
    // (LLTextureFetch::releaseHttpWaiters, and sendRequestListToSimulators on OpenSim). The orderings used
    // to compare the live priorities, which other threads change meanwhile (createRequest on the main
    // thread, updateRequestPriority's task on the fetch thread): a data race, and a comparison that can
    // contradict itself during the sort (std::partial_sort, std::set), which then breaks its ordering.
    // Greater priority first; equal priorities are equivalent, so a std::set keeps the first one inserted,
    // as the set of requests ordered by the live value did (upstream behaviour, kept: the UDP list sends one
    // request per priority and round; a tie-break would change which requests are sent).
    template <typename Request>
    struct PrioritySnapshot
    {
        float mPriority;
        Request* mRequest;

        bool operator<(const PrioritySnapshot& rhs) const
        {
            return mPriority > rhs.mPriority;
        }
    };
}

#endif // LL_LLTEXTUREFETCHPOLICY_H
