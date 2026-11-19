// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// A model of the decode-in-flight protocol of lltexturefetch.cpp (LLTextureFetchWorker::DecodeResponder and deleteOK),
// no viewer libraries:
//   g++ -std=c++20 -Wall -Wextra -Werror -pthread indra/newview/tests/lltexturefetch_decode_inflight_test.cpp
// The responder counts itself on the worker when it is made (fetch thread) and drops the count as its last touch of the
// worker, in completed() or, when the decode never ran, in its destructor. The main thread deletes a worker only when
// the count is zero. The model checks that (1) a decode thread that stalls for as long as it likes never touches a freed
// worker, (2) every worker is deleted once its decodes are done or dropped (no leak), (3) a dropped responder (a queue
// closed with the request still in it) releases its count, (4) two decodes of one re-initialised worker both hold it.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
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

    struct Worker
    {
        std::atomic<int> mDecodesInFlight{0};
        std::atomic<int> mCallbacks{0};
        std::atomic<bool> mFreed{false};
        bool deleteOK() const { return mDecodesInFlight.load(std::memory_order_acquire) <= 0; }
        void callbackDecoded()
        {
            if (mFreed.load()) { std::printf("FAIL callback on a freed worker\n"); ++failures; }
            ++mCallbacks;
        }
    };

    struct Responder
    {
        explicit Responder(Worker* w) : mWorker(w) { mWorker->mDecodesInFlight.fetch_add(1, std::memory_order_acq_rel); }
        ~Responder()
        {
            if (mWorker)
            {
                mWorker->mDecodesInFlight.fetch_sub(1, std::memory_order_acq_rel);
            }
        }
        void completed()
        {
            Worker* w = mWorker;
            mWorker = nullptr;
            if (w)
            {
                w->callbackDecoded();
                w->mDecodesInFlight.fetch_sub(1, std::memory_order_acq_rel); // last touch
            }
        }
        Worker* mWorker;
    };
}

int main()
{
    // (1) + (2): stalled decode threads against a main thread that deletes as soon as deleteOK allows
    for (int round = 0; round < 200; ++round)
    {
        std::vector<std::unique_ptr<Worker>> workers;
        std::vector<std::unique_ptr<Responder>> responders;
        for (int i = 0; i < 8; ++i)
        {
            workers.push_back(std::make_unique<Worker>());
            responders.push_back(std::make_unique<Responder>(workers.back().get()));
        }
        std::thread decode([&]() {
            for (auto& r : responders)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50)); // the stall between "lookup" and "lock"
                r->completed();
            }
        });
        size_t deleted = 0;
        std::vector<bool> gone(workers.size(), false);
        while (deleted < workers.size())
        {
            for (size_t i = 0; i < workers.size(); ++i)
            {
                if (!gone[i] && workers[i]->deleteOK())
                {
                    workers[i]->mFreed = true; // stands for delete: any later touch fails the check
                    gone[i] = true;
                    ++deleted;
                }
            }
        }
        decode.join();
        for (auto& w : workers)
        {
            check(w->mCallbacks == 1, "each decode called back exactly once");
        }
    }

    // (3) a responder destroyed without running releases its count
    {
        Worker w;
        {
            Responder r(&w);
            check(!w.deleteOK(), "worker held while a responder exists");
        }
        check(w.deleteOK(), "dropped responder releases the worker");
    }

    // (4) two decodes of one worker
    {
        Worker w;
        Responder a(&w), b(&w);
        a.completed();
        check(!w.deleteOK(), "second decode still holds the worker");
        b.completed();
        check(w.deleteOK(), "worker free after both decodes");
        check(w.mCallbacks == 2, "both callbacks ran");
    }

    if (failures == 0)
    {
        std::printf("lltexturefetch_decode_inflight_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
