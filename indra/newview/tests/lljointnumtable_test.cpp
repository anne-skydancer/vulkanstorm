// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lljointnumtable.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -pthread -I indra/newview indra/newview/tests/lljointnumtable_test.cpp
//   cl /std:c++20 /EHsc /W4 /WX /I indra\newview indra\newview\tests\lljointnumtable_test.cpp

#include "lljointnumtable.h"

#include <cstdio>
#include <thread>

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

    std::unique_ptr<const LLJointNumTable> makeTable(int32_t base, int count)
    {
        auto table = std::make_unique<LLJointNumTable>();
        for (int i = 0; i < count; ++i)
        {
            table->add("joint" + std::to_string(i), base + i);
        }
        return table;
    }
}

int main()
{
    LLJointNumTablePublisher pub;
    check(pub.get() == nullptr, "nothing published yet");

    pub.publish(makeTable(10, 5));
    const LLJointNumTable* first = pub.get();
    check(first != nullptr && first->size() == 5, "first table published");

    int32_t num = -1;
    check(first->find("joint3", num) && num == 13, "find a known name");
    check(!first->find("mNoSuch", num) && num == 13, "unknown name: false, output untouched");

    std::vector<int32_t> nums{7, 7};
    check(first->lookupAll({"joint0", "joint4", "joint0"}, nums) && nums.size() == 3 && nums[0] == 10 &&
              nums[1] == 14 && nums[2] == 10,
          "lookupAll maps every name in order");
    nums = {7, 7};
    check(!first->lookupAll({"joint0", "mNoSuch"}, nums) && nums.size() == 2 && nums[0] == 7,
          "lookupAll with one unknown name leaves the output untouched");
    check(first->lookupAll({}, nums) && nums.empty(), "lookupAll of no names");

    // A second publish (the next self avatar) replaces the current table; the first stays valid for a reader that holds it.
    pub.publish(makeTable(100, 3));
    check(pub.get() != first && pub.get()->size() == 3, "second table is current");
    check(first->find("joint4", num) && num == 14, "first table still valid after the second publish");

    // Readers on other threads see either nothing or a complete table, never a partial one.
    LLJointNumTablePublisher racing;
    std::atomic<bool> go{false};
    std::atomic<int> bad{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&]() {
            while (!go.load()) {}
            for (int i = 0; i < 200000; ++i)
            {
                const LLJointNumTable* table = racing.get();
                if (table)
                {
                    int32_t n = 0;
                    if (table->size() != 64 || !table->find("joint63", n) || n != 63)
                    {
                        ++bad;
                    }
                }
            }
        });
    }
    go = true;
    racing.publish(makeTable(0, 64));
    for (auto& reader : readers)
    {
        reader.join();
    }
    check(bad == 0, "concurrent readers see a complete table");

    if (failures == 0)
    {
        std::printf("lljointnumtable_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
