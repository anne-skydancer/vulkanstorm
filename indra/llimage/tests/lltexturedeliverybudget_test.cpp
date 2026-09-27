#include "lltexturedeliverybudget.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    using Budget = LLTextureDeliveryBudget;
    constexpr auto MiB = Budget::MiB;
    assert(!Budget::reserve(0, true));
    assert(!Budget::reserve(Budget::Ceiling + 1, true));
    auto upgrades = Budget::reserve(1536 * MiB, false);
    assert(upgrades && !Budget::reserve(1, false));
    auto blanks = Budget::reserve(512 * MiB, true);
    assert(blanks && Budget::used() == Budget::Ceiling);
    assert(!Budget::reserve(1, true));
    auto shared = blanks;
    blanks.reset();
    assert(Budget::used() == Budget::Ceiling); // handoff retains the reservation
    shared->release();
    shared->release(); // publication/cancellation may both release
    shared.reset();
    assert(Budget::used() == 1536 * MiB);
    Budget::updateAvailableMemory(1024 * MiB);
    assert(Budget::limit() == 256 * MiB);
    assert(Budget::used() == 1536 * MiB); // pressure stops admission, not ownership
    assert(!Budget::reserve(1, true));
    Budget::updateAvailableMemory(1024 * MiB);
    assert(Budget::limit() == 256 * MiB); // repeated stale samples cannot grow it
    upgrades.reset();
    assert(Budget::used() == 0);

    Budget::updateAvailableMemory(0);
    assert(Budget::limit() == 256 * MiB);
    assert(!Budget::reserve(257 * MiB, true));
    auto outstanding = Budget::reserve(256 * MiB, true);
    Budget::updateAvailableMemory(0);
    assert(outstanding->bytes() == 256 * MiB); // pressure does not revoke work
    outstanding.reset();
    Budget::updateAvailableMemory(16 * 1024 * MiB);
    assert(Budget::limit() == Budget::Ceiling);

    // Exercise admission/release races and independent cancellation owners.
    std::vector<std::thread> threads;
    for (int i = 0; i < 12; ++i)
        threads.emplace_back([]
        {
            for (int j = 0; j < 5000; ++j)
            {
                auto lease = Budget::reserve(128 * MiB, j % 3 == 0);
                if (lease)
                {
                    auto second_owner = lease;
                    if (j % 2) second_owner->release();
                }
                assert(Budget::used() <= Budget::Ceiling);
            }
        });
    for (auto& thread : threads) thread.join();
    assert(Budget::used() == 0);
    std::cout << "Texture delivery budget: admission, headroom, ownership and concurrency passed\n";
}
