// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lltexturefetchpolicy.h (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lltexturefetchpolicy_test.cpp
// A texture that has data and whose refinement fetch ends without data, replayed through the rules of
// LLViewerFetchedTexture: updateFetch makes no request while the texture is at or below mMinDiscardLevel or
// while a retry waits (FetchRetry::mayRequest); processFetchResults retries a failed refinement after 1, 2
// and 4 s and holds the texture at its level (mMinDiscardLevel) only when the third retry fails too, or at
// once for a failure a retry cannot fix (a 404, data that did not decode, data that cannot arrive); good
// data and Refresh Textures (clearFetchedResults) start over.
// A texture with no data yet whose fetch ends without data: a failure that may pass (an HTTP error other
// than 404, a time-out, an empty reply, an abort) gets the same retries and marks the asset missing when they
// are spent; a 404 or a failure that is not the network's marks it missing at once, as every failure did.
// A fetch worker whose request for more data fails decodes the data it holds only when that data reaches the
// requested level (decodeAfterFailedRequest); otherwise the fetch ends without data, so a refinement whose
// request keeps failing sends four requests in about 7 s and is then held, instead of sending its request
// again at once for as long as its texture stays on screen.
// The orderings of waiting requests (LLTextureFetch::releaseHttpWaiters, sendRequestListToSimulators) compare
// PrioritySnapshot copies, each priority read once: the order is the copies', whatever the priorities do after.
// That the fetcher takes each copy under the worker's lock, and outside the network queue lock, is guarded by
// scripts/tests/test_texture_streaming_fixes.py (PrioritySnapshots); nothing here runs lltexturefetch.cpp.
// A worn texture's request is queued by its avatar's area (wornFetchArea, fetchPriority): a face with no area, or with
// no avatar or one that is not drawn (the caller hands no area), counts nothing, a worn face the larger of its own area
// and its avatar's; the request's priority is raised to that worn size, at most to just under a boosted texture's,
// except under the abort threshold, so the same fetches abort, boosted textures stay first and a request on no worn
// face keeps its priority exactly. Replayed through the HTTP release: with a single worn request, a rigged eye in a busy
// place leaves first instead of last and the other requests leave in the same order as before; in a crowd, the eye
// leaves within the rounds of its avatar's textures at the top of the worn rank, after the boosted texture and the
// textures at the cap on their own, the textures of an avatar that is not drawn keep their own rank, and every other
// request leaves in the order of its own size, later than before by exactly the worn requests raised past it. That
// LLViewerTextureList ranks only drawn avatars, scales the worn size as the face's own and caps it as mMaxVirtualSize
// is capped, and that updateFetch decides everything else on mMaxVirtualSize, is guarded by
// test_texture_streaming_fixes.py (WornFetchRank).

#include "lltexturefetchpolicy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <set>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    using LLTextureFetchPolicy::FetchRetry;

    // The fields of LLViewerFetchedTexture the rules read and write, for a texture at discard level `level`
    struct Texture
    {
        int level = 4;          // the level it has (current_discard)
        int min_discard = 0;    // mMinDiscardLevel
        FetchRetry retry;       // mFetchRetry

        // updateFetch's rules for a texture that wants a finer level than it has
        bool requests(float now) const
        {
            if (level >= 0 && level <= min_discard)
            {
                return false;   // held at its level
            }
            return retry.mayRequest(now);
        }

        // processFetchResults: the refinement ended without data; a final failure (a 404, data that did not
        // decode, data that cannot arrive) holds the texture at once
        void failed(float now, bool final_failure = false)
        {
            if (final_failure || !retry.onFailure(now))
            {
                min_discard = level;
            }
        }

        // processFetchResults: good data at a finer level
        void succeeded(int new_level)
        {
            retry.reset();
            min_discard = 0;
            level = new_level;
        }

        // clearFetchedResults (Refresh Textures)
        void refreshed()
        {
            min_discard = 0;
            retry.reset();
        }
    };

    void test_retries_then_holds()
    {
        Texture t;
        check(t.requests(100.f), "a texture that wants a finer level requests it");

        // the trace's sequence: fail, then no request before 1 s, then a request after 1 s
        t.failed(100.f);
        check(t.min_discard == 0, "the first failure does not hold the texture");
        check(!t.requests(100.f) && !t.requests(100.99f), "no request within 1 s of the first failure");
        check(t.requests(101.f) && t.requests(150.f), "a request 1 s after the first failure");

        t.failed(101.f);
        check(!t.requests(102.99f), "no request within 2 s of the second failure");
        check(t.requests(103.f), "a request 2 s after the second failure");

        t.failed(103.f);
        check(!t.requests(106.99f), "no request within 4 s of the third failure");
        check(t.requests(107.f), "a request 4 s after the third failure");

        // the third retry fails too: held at its level, as the first failure used to do
        t.failed(107.f);
        check(t.min_discard == 4, "the fourth failure in a row holds the texture at its level");
        check(!t.requests(107.f) && !t.requests(10000.f), "a held texture makes no request, however long it waits");

        // a held texture that was scaled down (a coarser level) asks again; a failure there holds it again at once
        t.level = 5;
        check(t.requests(10000.f), "a held texture scaled down to a coarser level requests again");
        t.failed(10000.f);
        check(t.min_discard == 5 && !t.requests(20000.f), "with the retries spent, a new failure holds it again at once");
    }

    void test_final_refinement_failure_holds_at_once()
    {
        // a 404 or data that did not decode: retrying would download the same range again for nothing
        Texture t;
        t.failed(100.f, true);
        check(t.min_discard == 4 && t.retry.mFailures == 0, "a final failure holds the texture at once, without a retry");
        check(!t.requests(100.f) && !t.requests(10000.f), "a texture held by a final failure makes no request");
        t.refreshed();
        check(t.requests(100.f), "Refresh Textures lifts a hold from a final failure too");

        // a failure that may pass first, then a final one: held at once, the wait no longer matters
        Texture mixed;
        mixed.failed(100.f);
        check(mixed.min_discard == 0, "a failure that may pass is retried");
        mixed.failed(101.f, true);
        check(mixed.min_discard == 4 && !mixed.requests(10000.f), "a final failure on the retry holds it at once");
    }

    void test_good_data_and_refresh_start_over()
    {
        Texture t;
        t.failed(10.f);
        t.failed(11.f);
        check(t.retry.mFailures == 2, "two failures counted");
        t.succeeded(3);
        check(t.retry.mFailures == 0 && t.requests(12.f), "good data resets the count and the wait");
        t.failed(20.f);
        check(!t.requests(20.5f) && t.requests(21.f), "after good data, the next failure waits 1 s again");

        // held, then good data (the texture had been scaled down and fetched again) lifts the hold
        Texture held;
        for (float now : { 1.f, 2.f, 4.f, 8.f })
        {
            held.failed(now);
        }
        check(held.min_discard == 4, "four failures in a row hold the texture");
        held.level = 5;
        held.succeeded(2);
        check(held.min_discard == 0 && held.requests(9.f), "good data lifts the hold");

        // Refresh Textures lifts the hold and the wait
        Texture refreshed;
        for (float now : { 1.f, 2.f, 4.f, 8.f })
        {
            refreshed.failed(now);
        }
        refreshed.refreshed();
        check(refreshed.min_discard == 0 && refreshed.retry.mFailures == 0 && refreshed.requests(8.f),
              "Refresh Textures lifts the hold");
        Texture waiting;
        waiting.failed(50.f);
        waiting.refreshed();
        check(waiting.requests(50.f), "Refresh Textures ends a retry wait");
    }

    // A texture with no data yet: processFetchResults marks it missing (setIsMissingAsset) at once for a 404
    // or a failure that is not the network's, and otherwise when the retries are spent
    struct FirstFetch
    {
        bool missing = false;   // mIsMissingAsset: updateFetch makes no request
        FetchRetry retry;

        bool requests(float now) const
        {
            return !missing && retry.mayRequest(now);
        }

        void failed(float now, bool missing_at_once)
        {
            if (missing_at_once || !retry.onFailure(now))
            {
                missing = true;
            }
        }
    };

    // Visits the texture every `step` seconds from `start` on (the round robin of updateImagesFetchTextures),
    // fetching whenever a request is allowed, until it is missing or `end`. Each fetch fails. Returns the
    // number of fetches made; `when` gets the time the asset was marked missing.
    int failEveryFetch(FirstFetch& f, bool missing_at_once, float start, float step, float end, float& when)
    {
        int fetches = 0;
        when = -1.f;
        for (float now = start; now <= end && !f.missing; now += step)
        {
            if (f.requests(now))
            {
                ++fetches;
                f.failed(now, missing_at_once);
                if (f.missing)
                {
                    when = now;
                }
            }
        }
        return fetches;
    }

    void test_first_fetch()
    {
        // the owner's persistent 403s: the first fetch and three retries over about 7 s, then missing, never more
        FirstFetch persistent;
        float when = 0.f;
        const int fetches = failEveryFetch(persistent, false, 0.f, 0.25f, 3600.f, when);
        check(fetches == 4, "a failure that may pass gets the first fetch and three retries");
        check(persistent.missing && when >= 7.f && when < 7.5f, "then the asset is missing, about 7 s after the first fetch");

        // a 404, or the data did not decode (or cannot arrive): missing at the first failure, as before
        FirstFetch not_found;
        check(failEveryFetch(not_found, true, 0.f, 0.25f, 3600.f, when) == 1 && not_found.missing && when == 0.f,
              "a 404 or a failure that is not the network's marks the asset missing at once");

        // a transient failure (a 503, a time-out): the retry brings the data
        FirstFetch transient;
        transient.failed(0.f, false);
        check(!transient.missing && !transient.requests(0.9f) && transient.requests(1.f),
              "a transient failure is retried 1 s later instead of marking the asset missing");
        transient.retry.reset(); // good data (processFetchResults)
        check(transient.retry.mFailures == 0 && transient.requests(1.f), "good data after a retry starts over");
    }

    // A fetch worker whose request for more data fails (LLTextureFetchWorker::doWork, the HTTP error path) decodes
    // the data it holds only when that data reaches the requested level; otherwise the fetch ends without data
    void test_failed_request()
    {
        using LLTextureFetchPolicy::decodeAfterFailedRequest;
        // data the worker decoded in an earlier round, coarser than requested: decoding it again ended at that level,
        // which sent the worker back to INIT and the failed request out again at once
        check(!decodeAfterFailedRequest(5, 2) && !decodeAfterFailedRequest(3, 2), "data coarser than requested is not decoded again");
        // data never decoded (a new worker that read it from the cache): the fetch ended without data before too
        check(!decodeAfterFailedRequest(-1, 2) && !decodeAfterFailedRequest(-1, 5), "data never decoded ends the fetch without data");
        // data that reaches the requested level (the request was made coarser meanwhile) is decoded, as before
        check(decodeAfterFailedRequest(2, 2) && decodeAfterFailedRequest(3, 4) && decodeAfterFailedRequest(0, 0),
              "data that reaches the requested level is decoded, as before");

        // A refinement whose request keeps failing: the texture is at level 4 and wants 2. The first request is sent by
        // the worker that decoded level 4 (the data it holds), each retry by a new worker (processFetchResults deletes
        // a worker that ended without data), which holds data never decoded. Each fetch sends its request once and ends
        // without data; the texture retries after 1, 2 and 4 s, then holds its level.
        Texture t;
        const int desired = 2;
        int requests = 0;
        float held_at = -1.f;
        for (float now = 0.f; now <= 3600.f; now += 0.25f)
        {
            if (t.requests(now))
            {
                ++requests; // the request for more data, which fails
                const int at_hand = (requests == 1) ? t.level : -1;
                check(!decodeAfterFailedRequest(at_hand, desired), "the data at hand does not serve the request");
                t.failed(now); // the fetch ended without data
                if (held_at < 0.f && t.min_discard == t.level)
                {
                    held_at = now;
                }
            }
        }
        check(requests == 4, "a refinement whose request keeps failing sends four requests in all");
        check(held_at >= 7.f && held_at < 7.5f, "and is held at its level about 7 s after the first");
    }

    // A fetch worker as the orderings see it: its priority (mImagePriority) and its lock (mWorkMutex)
    struct Request
    {
        std::mutex mutex;
        float priority = 0.f;
    };
    using Snapshot = LLTextureFetchPolicy::PrioritySnapshot<Request>;

    // As releaseHttpWaiters and sendRequestListToSimulators take them: each priority read under its lock
    std::vector<Snapshot> snapshot(std::vector<Request>& requests)
    {
        std::vector<Snapshot> out;
        out.reserve(requests.size());
        for (Request& r : requests)
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            out.push_back({ r.priority, &r });
        }
        return out;
    }

    void test_snapshot_orderings()
    {
        std::vector<Request> requests(6);
        const float priorities[] = { 5.f, 900.f, 21404.f, 900.f, 2.9e6f, 0.f };
        for (std::size_t i = 0; i < requests.size(); ++i)
        {
            requests[i].priority = priorities[i];
        }

        // the HTTP release: the `needed` highest first
        std::vector<Snapshot> waiters = snapshot(requests);
        std::partial_sort(waiters.begin(), waiters.begin() + 3, waiters.end());
        check(waiters[0].mRequest == &requests[4] && waiters[1].mRequest == &requests[2] && waiters[2].mPriority == 900.f,
              "the HTTP release takes the highest priorities first");

        // a priority changed after the snapshot does not reorder it
        std::vector<Snapshot> taken = snapshot(requests);
        requests[0].priority = 1e9f;
        std::sort(taken.begin(), taken.end());
        check(taken.front().mRequest == &requests[4] && taken.back().mRequest == &requests[5],
              "the order is the snapshots', whatever the priorities do afterwards");
        requests[0].priority = priorities[0];

        // the UDP list: a set of snapshots, highest first; equal priorities are one key, the first one inserted
        // stays. Preserved upstream behaviour (the set ordered by the live value did the same): a request whose
        // priority equals another's waits for a later round. A tie-break would change which requests are sent,
        // so it is left to a feature change, not this fix.
        std::set<Snapshot> list;
        for (const Snapshot& s : snapshot(requests))
        {
            list.insert(s);
        }
        check(list.size() == 5 && list.begin()->mRequest == &requests[4], "the UDP list holds one request per priority, highest first");
        bool first_kept = false;
        for (const Snapshot& s : list)
        {
            first_kept = first_kept || (s.mPriority == 900.f && s.mRequest == &requests[1]);
        }
        check(first_kept, "of two requests with equal priority the set keeps the first inserted (upstream behaviour)");
    }

    // The rank of worn textures. The eye follows the trace's harness A: a 24 mm eyeball 1 m from the camera, a 1440-px
    // window and a 60 degree field of view give its face about 2,675 px², 21,404 with the camera boost of 8.
    constexpr float ABORT_BELOW = 0.0001f;     // F_ALMOST_ZERO (llmath.h): a fetch worker whose priority is under it aborts
    constexpr float CAP = 2048.f * 2048.f;     // mMaxVirtualSize's cap at the default RenderMaxTextureResolution
    using LLTextureFetchPolicy::BOOSTED_PRIORITY;
    using LLTextureFetchPolicy::WORN_PRIORITY_MAX;

    void test_worn_fetch_area()
    {
        using LLTextureFetchPolicy::wornFetchArea;
        const float eye = 2675.5f;
        const float avatar = 524288.f;
        check(wornFetchArea(eye, avatar) == avatar, "a worn face counts its avatar's area when that is larger");
        check(wornFetchArea(600000.f, avatar) == 600000.f, "a worn face larger than its avatar counts its own area");
        check(wornFetchArea(eye, 0.f) == 0.f,
              "a face that is not worn, or whose avatar is not drawn (the caller hands no area), counts nothing");
        check(wornFetchArea(0.f, avatar) == 0.f && wornFetchArea(-1.f, avatar) == 0.f,
              "a worn face with no area on screen counts nothing");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        check(wornFetchArea(nan, avatar) == 0.f && wornFetchArea(eye, nan) == 0.f, "an undefined area counts nothing");
    }

    void test_fetch_priority()
    {
        using LLTextureFetchPolicy::fetchPriority;
        check(WORN_PRIORITY_MAX < BOOSTED_PRIORITY && WORN_PRIORITY_MAX == std::nextafter(BOOSTED_PRIORITY, 0.f),
              "the worn rank tops out at the largest priority under a boosted texture's");
        check(BOOSTED_PRIORITY == CAP, "a boosted texture's priority is the cap of the default RenderMaxTextureResolution");
        check(fetchPriority(21404.f, 1.0e6f, ABORT_BELOW) == 1.0e6f, "a worn texture's request is raised to its worn size");
        check(fetchPriority(21404.f, CAP, ABORT_BELOW) == WORN_PRIORITY_MAX,
              "a worn size at the cap raises a request to just under a boosted texture's priority");
        check(fetchPriority(21404.f, 6.7e7f, ABORT_BELOW) == WORN_PRIORITY_MAX, "and no higher, whatever the worn size");
        check(fetchPriority(CAP, CAP, ABORT_BELOW) == CAP, "a texture whose own size is at the cap keeps its priority");
        check(fetchPriority(BOOSTED_PRIORITY, 0.f, ABORT_BELOW) == BOOSTED_PRIORITY, "a boosted texture keeps its priority");
        check(fetchPriority(2.9e6f, 0.f, ABORT_BELOW) == 2.9e6f, "a texture on no worn face keeps its priority");
        check(fetchPriority(2.9e6f, 1.0e6f, ABORT_BELOW) == 2.9e6f, "a worn size under the texture's own size changes nothing");
        check(fetchPriority(0.f, CAP, ABORT_BELOW) == 0.f, "a request at priority 0 stays at 0, so it still aborts");
        check(fetchPriority(-1.f, CAP, ABORT_BELOW) == -1.f, "a negative priority is left as it is");
        check(fetchPriority(0.00005f, CAP, ABORT_BELOW) == 0.00005f, "a priority under the abort threshold is left as it is");
        check(fetchPriority(ABORT_BELOW, CAP, ABORT_BELOW) == WORN_PRIORITY_MAX, "from the abort threshold on, the worn size counts");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        check(fetchPriority(21404.f, nan, ABORT_BELOW) == 21404.f, "an undefined worn size changes nothing");

        // Over a grid: a request is never ranked under its texture's own size, aborts exactly when that size would abort
        // it, is that size exactly when no worn face counts, and reaches a boosted texture's priority only by its own size
        const float sizes[] = { -1.f, 0.f, 1e-7f, 0.00005f, 0.0001f, 0.0002f, 1.f, 10.f, 574.f, 21404.f, 2.9e5f, 2.9e6f,
                                WORN_PRIORITY_MAX, CAP, 6.7e7f };
        bool never_lower = true;
        bool same_aborts = true;
        bool unworn_exact = true;
        bool under_boosted = true;
        for (float decode : sizes)
        {
            unworn_exact = unworn_exact && fetchPriority(decode, 0.f, ABORT_BELOW) == decode;
            for (float worn : sizes)
            {
                if (worn < 0.f)
                {
                    continue; // a worn size is never negative
                }
                const float priority = fetchPriority(decode, worn, ABORT_BELOW);
                never_lower = never_lower && priority >= decode;
                same_aborts = same_aborts && ((priority < ABORT_BELOW) == (decode < ABORT_BELOW));
                under_boosted = under_boosted && ((priority >= BOOSTED_PRIORITY) == (decode >= BOOSTED_PRIORITY));
            }
        }
        check(never_lower, "a request is never ranked under its texture's own size");
        check(same_aborts, "a request aborts exactly when its texture's own size would abort it");
        check(unworn_exact, "a texture on no worn face keeps its priority exactly");
        check(under_boosted, "a worn size never lifts a request to a boosted texture's priority: only its own size does");
    }

    // Replays LLTextureFetch::releaseHttpWaiters on requests that all wait: `per_round` leave in each round, highest
    // priority first. Returns the requests' indices in the order they leave.
    std::vector<std::size_t> releaseOrder(const std::vector<float>& priorities, std::size_t per_round)
    {
        std::vector<Request> requests(priorities.size());
        for (std::size_t i = 0; i < priorities.size(); ++i)
        {
            requests[i].priority = priorities[i];
        }
        std::vector<Snapshot> waiting = snapshot(requests);
        std::vector<std::size_t> order;
        while (!waiting.empty())
        {
            const std::size_t needed = std::min(per_round, waiting.size());
            std::partial_sort(waiting.begin(), waiting.begin() + needed, waiting.end());
            for (std::size_t i = 0; i < needed; ++i)
            {
                order.push_back(std::size_t(waiting[i].mRequest - requests.data()));
            }
            waiting.erase(waiting.begin(), waiting.begin() + needed);
        }
        return order;
    }

    void test_worn_release_order()
    {
        using LLTextureFetchPolicy::fetchPriority;
        using LLTextureFetchPolicy::wornFetchArea;
        // A busy place with a single worn request: 600 waiting requests on no worn face, all larger on screen than the eye
        // (10^5 to 4 x 10^6, distinct), and the rigged eye of an avatar 1 m away: 21,404 on its own, and its avatar's area
        // with the face's camera boost, at the cap. 50 leave per round, as releaseHttpWaiters releases them.
        std::vector<float> own;  // each texture's own size (mMaxVirtualSize, the decode priority)
        std::vector<float> worn; // its worn size (mWornVirtualSize)
        for (int i = 0; i < 600; ++i)
        {
            own.push_back(1.0e5f + 6500.f * float(i));
            worn.push_back(0.f);
        }
        const std::size_t eye = own.size();
        own.push_back(21404.f);
        worn.push_back(std::min(wornFetchArea(2675.5f, 524288.f) * 8.f, CAP));

        std::vector<float> rank(own.size());
        for (std::size_t i = 0; i < own.size(); ++i)
        {
            rank[i] = fetchPriority(own[i], worn[i], ABORT_BELOW);
        }
        std::vector<std::size_t> before = releaseOrder(own, 50); // ranked by their own sizes, as before
        std::vector<std::size_t> after = releaseOrder(rank, 50);
        check(before.back() == eye, "ranked by its own size, the eye leaves last, in the thirteenth round");
        check(after.front() == eye, "ranked by its avatar's area, a single worn request leaves first, in the first round");

        // with a single worn request, the other requests leave in the same order as before
        before.erase(std::find(before.begin(), before.end(), eye));
        after.erase(std::find(after.begin(), after.end(), eye));
        check(before == after, "with a single worn request, every other request leaves in the same order as before");
    }

    // A crowd, 50 requests leaving per round. The eye's avatar is drawn 1 m away: its 60 worn textures, the eye among them
    // (21,404 on its own), reach the cap of the worn rank. A second drawn avatar further away ranks its 60 at 1.5 x 10^6,
    // its eye (5,000 on its own) among them; those of its textures larger on their own keep their own size. A third avatar
    // stands behind the camera, so it is not drawn: the caller hands no area, and its 60 keep their own sizes. Then 598
    // textures of scenery, two more at the cap on their own, and a boosted texture (the map, a thumbnail). No two own sizes
    // tie except the three at the top; the rank makes ties, which std::partial_sort leaves in any order, so every check
    // holds for every order of the ties.
    void test_crowd_release_order()
    {
        using LLTextureFetchPolicy::fetchPriority;
        enum Kind { SCENERY, BOOSTED, NEAR, FAR, HIDDEN };
        struct Waiter
        {
            Kind kind;
            float own;  // mMaxVirtualSize, the decode priority
            float worn; // mWornVirtualSize, as updateImageDecodePriority stores it and processTextureStats caps it
        };
        // own sizes in steps of 6,500: the scenery's on the steps, each avatar's between them, so no two own sizes tie
        const auto step = [](int i, float offset) { return 1.0e5f + 6500.f * float(i) + offset; };
        std::vector<Waiter> textures;
        for (int i = 0; i < 598; ++i)
        {
            textures.push_back({ SCENERY, step(i, 0.f), 0.f });
        }
        textures.push_back({ SCENERY, CAP, 0.f });
        textures.push_back({ SCENERY, CAP, 0.f });
        textures.push_back({ BOOSTED, BOOSTED_PRIORITY, 0.f }); // its faces are skipped: no worn size
        const std::size_t near_eye = textures.size();
        textures.push_back({ NEAR, 21404.f, CAP });
        for (int k = 1; k < 60; ++k)
        {
            textures.push_back({ NEAR, step(10 * k, 3250.f), CAP });
        }
        const float far_rank = 1.5e6f + 1234.f;
        const std::size_t far_eye = textures.size();
        textures.push_back({ FAR, 5000.f, far_rank });
        for (int k = 1; k < 60; ++k)
        {
            textures.push_back({ FAR, step(10 * k - 5, 1625.f), far_rank });
        }
        for (int k = 0; k < 60; ++k)
        {
            textures.push_back({ HIDDEN, step(10 * k + 7, 4875.f), 0.f }); // not drawn: the caller hands no area
        }

        const std::size_t count = textures.size();
        std::vector<float> own(count);
        std::vector<float> rank(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            own[i] = textures[i].own;
            rank[i] = fetchPriority(textures[i].own, textures[i].worn, ABORT_BELOW);
        }
        const std::vector<std::size_t> before_order = releaseOrder(own, 50); // ranked by their own sizes, as before
        const std::vector<std::size_t> after_order = releaseOrder(rank, 50);
        std::vector<std::size_t> before(count); // the place each request leaves at
        std::vector<std::size_t> after(count);
        for (std::size_t place = 0; place < count; ++place)
        {
            before[before_order[place]] = place;
            after[after_order[place]] = place;
        }
        const auto round = [](std::size_t place) { return place / 50 + 1; };
        const std::size_t last_round = round(count - 1);

        bool top_first = true;      // the boosted texture and the two at the cap on their own
        bool near_early = true;     // every texture of the near avatar
        bool far_eye_ahead = true;  // the far avatar's eye, ahead of every request ranked under its avatar
        bool hidden_kept = true;    // the avatar that is not drawn: own sizes, behind the near avatar's eye
        bool exact_delay = true;    // a request that is not raised: later than before by exactly the raised ones passing it
        for (std::size_t i = 0; i < count; ++i)
        {
            const Waiter& t = textures[i];
            if (t.kind == BOOSTED || (t.kind == SCENERY && t.own == CAP))
            {
                top_first = top_first && after[i] < 3 && before[i] < 3;
            }
            if (t.kind == NEAR)
            {
                near_early = near_early && rank[i] == WORN_PRIORITY_MAX && after[i] < 3 + 60;
            }
            if (rank[i] < far_rank)
            {
                far_eye_ahead = far_eye_ahead && after[far_eye] < after[i];
            }
            if (t.kind == HIDDEN)
            {
                hidden_kept = hidden_kept && rank[i] == t.own && after[near_eye] < after[i];
            }
            if (rank[i] == t.own && t.own < CAP)
            {
                std::size_t passing = 0; // raised from under this request's size to over it
                for (std::size_t j = 0; j < count; ++j)
                {
                    passing += (own[j] < t.own && rank[j] > t.own) ? 1 : 0;
                }
                exact_delay = exact_delay && after[i] == before[i] + passing;
            }
        }
        check(round(before[near_eye]) == last_round, "ranked by its own size, the near avatar's eye leaves in the last round");
        check(top_first, "the boosted texture and the textures at the cap on their own leave first, ahead of every worn one");
        check(near_early && round(after[near_eye]) <= 2,
              "the near avatar's textures, its eye among them, leave right after those, in the first two rounds");
        check(round(before[far_eye]) == last_round && far_eye_ahead,
              "the far avatar's eye, last before, leaves ahead of every request ranked under its avatar");
        check(hidden_kept, "the textures of an avatar that is not drawn keep their own sizes, behind the drawn avatar's eye");
        check(exact_delay, "a request that is not raised leaves later than before by exactly the worn requests raised past it");

        // the requests that are not raised leave in the order of their own sizes, as before
        float previous = BOOSTED_PRIORITY;
        bool own_order = true;
        for (std::size_t i : after_order)
        {
            if (rank[i] == own[i])
            {
                own_order = own_order && own[i] <= previous;
                previous = own[i];
            }
        }
        check(own_order, "the requests that are not raised leave in the order of their own sizes, as before");
    }

    void test_retry_delays()
    {
        FetchRetry retry;
        check(retry.mayRequest(0.f), "a new texture may request at once");
        const float expected[] = { 1.f, 2.f, 4.f };
        for (float delay : expected)
        {
            check(retry.onFailure(1000.f), "a retry remains");
            check(retry.mRetryTime == 1000.f + delay, "the delays are 1, 2 and 4 s");
        }
        check(!retry.onFailure(1000.f), "three retries, then none");
        check(retry.mFailures == FetchRetry::MAX_RETRIES && retry.mRetryTime == 1004.f,
              "a failure with the retries spent changes nothing");
    }
}

int main()
{
    test_retry_delays();
    test_retries_then_holds();
    test_final_refinement_failure_holds_at_once();
    test_good_data_and_refresh_start_over();
    test_first_fetch();
    test_failed_request();
    test_snapshot_orderings();
    test_worn_fetch_area();
    test_fetch_priority();
    test_worn_release_order();
    test_crowd_release_order();
    if (failures == 0)
    {
        std::printf("lltexturefetchpolicy_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
