// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file lltexturefetchpolicy.h
 * @brief Texture fetch rules: the bounded retry of fetches that end without data, the end of a fetch
 *        whose request failed, the priority snapshots that order waiting requests, and the place of a
 *        worn texture's request among them.
 *
 * Free of GL and viewer types: LLViewerFetchedTexture (llviewertexture.cpp), LLViewerTextureList
 * (llviewertexturelist.cpp) and LLTextureFetch (lltexturefetch.cpp) apply the rules, and
 * indra/newview/tests/lltexturefetchpolicy_test.cpp checks them standalone.
 *
 * Why: a refinement fetch that ended without data (an HTTP error, an empty or broken reply, a failed
 * decode, a request aborted while its texture was off screen) set mMinDiscardLevel to the texture's
 * level at once, and no request was made again while the texture stayed at that level: it stayed
 * blurry for the life of the texture object, zooming in did not help, and Refresh Textures did not
 * clear it either. A first fetch that ended without data marked the asset missing for the session,
 * whatever the cause. A refinement whose request failed while its worker held data it had decoded
 * before did not end at all: the worker decoded that data again and sent the same request again at
 * once, for as long as the texture stayed on screen. And the waiting requests were sorted by their
 * live priorities, which other threads change during the sort. The textures of worn objects, a rigged
 * eye above all, waited behind nearly every other texture of a busy place (wornFetchArea).
 */

#ifndef LL_LLTEXTUREFETCHPOLICY_H
#define LL_LLTEXTUREFETCHPOLICY_H

#include <algorithm>

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

    // The place of a worn texture's request among the waiting ones. They are released largest priority first, and
    // a request's priority is its texture's size on screen (mMaxVirtualSize). Since LL #2582 (Sep 2024) a rigged face
    // is sized by its own joint boxes instead of its avatar's box: right for the resolution it asks for, but a rigged
    // eye then queued 50 to 3,000 times lower than before, behind nearly every other texture of a busy place, and
    // loaded last unless the camera zoomed in on it.
    // A face of a worn object (an attachment, or animesh) that has an area on screen counts the larger of its own
    // area and its avatar's, as the avatar's baked textures are queued by the avatar's area
    // (LLVOAvatar::addBakedTextureStats) and as rigged faces were before #2582. `avatar_area` is 0 for a face that is
    // not worn, which counts nothing. LLViewerTextureList::updateImageDecodePriority scales the result by the face's
    // factors (texture scale, bias, camera boost) as it scales the face's own size, keeps the largest over the
    // texture's faces, and clamps it as mMaxVirtualSize is clamped: that is mFetchVirtualSize, the worn size.
    inline float wornFetchArea(float face_area, float avatar_area)
    {
        return (face_area > 0.f && avatar_area > 0.f) ? std::max(face_area, avatar_area) : 0.f;
    }

    // The priority of a texture's request (LLTextureFetch::createRequest, updateRequestPriority): `decode_priority`,
    // the size the texture is fetched for (mMaxVirtualSize), raised to its worn size `worn_size` (mFetchVirtualSize).
    // Under `abort_below` (F_ALMOST_ZERO: a fetch worker whose priority is under it aborts) it is `decode_priority`
    // as it is, so the same fetches abort; the level asked for and every other decision keep `decode_priority`. Only
    // the order of the waiting requests changes (and, on OpenSim, the priority a UDP request sends). A texture on no
    // worn face (a worn size of 0) keeps `decode_priority` exactly.
    inline float fetchPriority(float decode_priority, float worn_size, float abort_below)
    {
        return decode_priority < abort_below ? decode_priority : std::max(decode_priority, worn_size);
    }
}

#endif // LL_LLTEXTUREFETCHPOLICY_H
