// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llcomputedependencies.h"
#include <cassert>
#include <memory>
#include <vector>
#include <iostream>

int main()
{
    using namespace LLComputeMesh;
    unsigned remaining = 1000, elapsed = 0;
    auto next = [&]() { if (!remaining) return false; --remaining; ++elapsed; return true; };
    assert(wakeResourceBatch(next, [&]() { return elapsed>=200; })==200); // no fixed 64 ceiling
    assert(remaining==800);
    assert(wakeResourceBatch(next, []() { return true; })==0);
    assert(remaining==800);
    elapsed=0;
    assert(wakeResourceBatch(next, [&]() { return elapsed>=10; })==10);
    assert(remaining==790); // resume rather than rescan the earlier candidates
    assert(wakeResourceBatch(next, []() { return false; })==790);
    assert(wakeResourceBatch(next, []() { return false; })==0);
    elapsed=0;
    assert(wakeResourceBatch([&]() { elapsed+=10; return true; }, [&]() { return elapsed>=1; })==1);
    DependencyWaits<unsigned, std::shared_ptr<unsigned>> waits;
    auto job = std::make_shared<unsigned>(1);
    std::weak_ptr<unsigned> lifetime = job;
    waits.park(1, MESH, job);
    job.reset();
    assert(!lifetime.expired());
    for (unsigned event : {unsigned(SKIN), unsigned(MATERIAL), unsigned(DRAWABLE), unsigned(RESOURCES)})
        assert(!waits.wake(1, event));
    assert(!waits.wake(2, MESH)); // another object's asset arrival cannot wake this one
    auto ready = waits.wake(1, MESH);
    assert(ready && *ready==1 && waits.size()==0);
    assert(!waits.wake(1, MESH)); // duplicate callback cannot enqueue twice
    ready.reset();
    assert(lifetime.expired());

    // Re-evaluation can discover another missing dependency. Only its event
    // wakes the next wait, and cancellation immediately releases ownership.
    waits.park(1, MATERIAL, std::make_shared<unsigned>(2));
    assert(!waits.wake(1, MESH));
    ready = waits.wake(1, MATERIAL);
    waits.park(1, DRAWABLE | SKIN, ready);
    assert(!waits.wake(1, MATERIAL));
    assert(waits.wake(1, SKIN)==ready);
    assert(!waits.wake(1, DRAWABLE));
    lifetime = ready;
    waits.park(1, RESOURCES, ready);
    ready.reset();
    waits.erase(1);
    assert(lifetime.expired() && !waits.wake(1, RESOURCES));

    // Replacing an object generation releases the stale job. A late event
    // cannot recover that generation; the caller validates the new owner.
    job = std::make_shared<unsigned>(3);
    lifetime = job;
    waits.park(1, MESH, job); job.reset();
    waits.park(1, DRAWABLE, std::make_shared<unsigned>(4));
    assert(lifetime.expired() && !waits.wake(1, MESH));
    assert(*waits.wake(1, DRAWABLE)==4);

    // Crowded scenes: notifications remove only the addressed waits. Parked
    // jobs require no per-frame iteration and shutdown releases every job.
    std::vector<std::weak_ptr<unsigned>> live;
    for (unsigned i=0; i<8192; ++i)
    {
        auto value = std::make_shared<unsigned>(i);
        live.push_back(value);
        waits.park(i, i%2 ? MESH : RESOURCES, value);
    }
    for (unsigned i=1; i<8192; i+=2) assert(*waits.wake(i, MESH)==i);
    assert(waits.size()==4096);
    waits.clear();
    for (const auto& value : live) assert(value.expired());
    std::cout << "PASS: targeted wakeups, coalescing, dependency transitions, cancellation, generation replacement, 8192-job cleanup\n";
}
