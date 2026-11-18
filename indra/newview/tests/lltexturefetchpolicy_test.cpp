// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lltexturefetchpolicy.h (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -I indra/newview indra/newview/tests/lltexturefetchpolicy_test.cpp
// (add -fsanitize=thread where ThreadSanitizer is available: the snapshot test then also proves that the
// orderings read no priority without its lock)
// A texture that has data and whose refinement fetch ends without data, replayed through the rules of
// LLViewerFetchedTexture: updateFetch makes no request while the texture is at or below mMinDiscardLevel or
// while a retry waits (FetchRetry::mayRequest); processFetchResults retries a failed refinement after 1, 2
// and 4 s and holds the texture at its level (mMinDiscardLevel) only when the third retry fails too, or at
// once for a failure a retry cannot fix (a 404, data that did not decode, data that cannot arrive); good
// data and Refresh Textures (clearFetchedResults) start over.
// A texture with no data yet whose fetch ends without data: a failure that may pass (an HTTP error other
// than 404, a time-out, an empty reply, an abort) gets the same retries and marks the asset missing when they
// are spent; a 404 or a failure that is not the network's marks it missing at once, as every failure did.
// The orderings of waiting requests (LLTextureFetch::releaseHttpWaiters, sendRequestListToSimulators): each
// request's priority is read once under its lock and the order compares those snapshots, so a thread that
// changes the priorities meanwhile (createRequest, updateRequestPriority) cannot break it.

#include "lltexturefetchpolicy.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>
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

    // std::partial_sort's result: the first `needed` in order, highest first, none after them higher
    bool partiallySorted(const std::vector<Snapshot>& v, std::size_t needed)
    {
        for (std::size_t i = 1; i < needed; ++i)
        {
            if (v[i - 1].mPriority < v[i].mPriority)
            {
                return false;
            }
        }
        for (std::size_t j = needed; j < v.size(); ++j)
        {
            if (v[needed - 1].mPriority < v[j].mPriority)
            {
                return false;
            }
        }
        return true;
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
        // stays (as with the set ordered by the live value)
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
        check(first_kept, "of two requests with equal priority the set keeps the first inserted");
    }

    // Another thread changes the priorities under their locks (createRequest on the main thread,
    // updateRequestPriority's task on the fetch thread) while this one orders them, as the fetch thread
    // (releaseHttpWaiters) and the main thread (sendRequestListToSimulators) do: every order is valid for the
    // snapshots it compares.
    void test_snapshots_under_concurrent_writes()
    {
        constexpr std::size_t COUNT = 64;
        constexpr std::size_t NEEDED = 50;
        constexpr int ROUNDS = 2000;
        std::vector<Request> requests(COUNT);
        std::atomic<bool> stop{ false };

        std::thread writer([&]()
            {
                unsigned step = 0;
                while (!stop.load(std::memory_order_relaxed))
                {
                    for (std::size_t i = 0; i < COUNT; ++i)
                    {
                        std::lock_guard<std::mutex> lock(requests[i].mutex);
                        requests[i].priority = float(((i * 7919u) + step) % 13u) * 1000.f; // ties too
                    }
                    ++step;
                }
            });

        bool sorted = true;
        bool sets_ordered = true;
        for (int round = 0; round < ROUNDS; ++round)
        {
            std::vector<Snapshot> waiters = snapshot(requests);
            std::partial_sort(waiters.begin(), waiters.begin() + NEEDED, waiters.end());
            sorted = sorted && partiallySorted(waiters, NEEDED);

            std::set<Snapshot> list(waiters.begin(), waiters.end());
            for (auto it = list.begin(); it != list.end() && std::next(it) != list.end(); ++it)
            {
                sets_ordered = sets_ordered && it->mPriority > std::next(it)->mPriority;
            }
        }
        stop = true;
        writer.join();
        check(sorted, "every release order is valid for its snapshots while the priorities change");
        check(sets_ordered, "every UDP list is strictly ordered by its snapshots while the priorities change");
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
    test_snapshot_orderings();
    test_snapshots_under_concurrent_writes();
    if (failures == 0)
    {
        std::printf("lltexturefetchpolicy_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
