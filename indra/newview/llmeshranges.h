// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_LLMESHRANGES_H
#define LL_LLMESHRANGES_H
#include <cassert>
#include <cstdint>
#include <map>
#include <iterator>
#include <optional>

namespace LLComputeMesh
{
// Main-thread page-local ranges. Live leases never overlap; released neighbours
// coalesce so material edits do not strand space in otherwise-live pages.
class Ranges
{
public:
    explicit Ranges(std::uint32_t capacity = 0) { if (capacity) mFree.emplace(0, capacity); }
    bool fits(std::uint32_t count) const
    {
        for (const auto& span : mFree) if (count && span.second >= count) return true;
        return false;
    }
    std::optional<std::uint32_t> take(std::uint32_t count)
    {
        if (!count) return {};
        for (auto it=mFree.begin(); it!=mFree.end(); ++it)
        {
            if (it->second < count) continue;
            const auto start=it->first, remainder=it->second-count;
            mFree.erase(it);
            if (remainder) mFree.emplace(start+count, remainder);
            return start;
        }
        return {};
    }
    void release(std::uint32_t start, std::uint32_t count)
    {
        assert(count);
        auto next=mFree.lower_bound(start);
        assert(next==mFree.end() || start+count<=next->first);
        if (next!=mFree.begin())
        {
            auto previous=std::prev(next);
            assert(previous->first+previous->second<=start);
            if (previous->first+previous->second==start)
            {
                start=previous->first;
                count+=previous->second;
                mFree.erase(previous);
            }
        }
        if (next!=mFree.end() && start+count==next->first)
        {
            count+=next->second;
            mFree.erase(next);
        }
        mFree.emplace(start,count);
    }
private:
    std::map<std::uint32_t,std::uint32_t> mFree;
};
}
#endif
