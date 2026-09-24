// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_COMPUTE_DEPENDENCIES_H
#define LL_COMPUTE_DEPENDENCIES_H
#include <unordered_map>
#include <utility>
namespace LLComputeMesh
{
enum Dependency : unsigned { MESH = 1, SKIN = 2, MATERIAL = 4, DRAWABLE = 8, RESOURCES = 16 };

// Check as many resource waiters as the shared frame allowance permits.
// next() advances a persistent cursor and returns false at the end of a sweep.
template<class Next, class Expired>
unsigned wakeResourceBatch(Next next, Expired expired)
{
    unsigned checked = 0;
    while (!expired() && next()) ++checked;
    return checked;
}

// Main-thread wait registry. One job per object; an event removes it before
// returning it to the runnable queue, so repeated notifications coalesce.
template<class Key, class Value>
class DependencyWaits
{
    struct Wait { unsigned dependencies; Value job; };
    std::unordered_map<Key, Wait> mWaiting;
public:
    void park(Key key, unsigned dependencies, Value job)
    { mWaiting.insert_or_assign(key, Wait{dependencies, std::move(job)}); }
    Value wake(Key key, unsigned changed)
    {
        auto found = mWaiting.find(key);
        if (found == mWaiting.end() || !(found->second.dependencies & changed)) return {};
        auto job = std::move(found->second.job);
        mWaiting.erase(found);
        return job;
    }
    void erase(Key key) { mWaiting.erase(key); }
    void clear() { mWaiting.clear(); }
    auto size() const { return mWaiting.size(); }
    template<class Callback> void forEach(Callback callback)
    { for (auto& item : mWaiting) callback(item.second.job); }
};
}
#endif
