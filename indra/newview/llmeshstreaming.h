// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_MESH_STREAMING_H
#define LL_MESH_STREAMING_H
#include <cstdint>
namespace LLMeshStreaming
{
inline bool retainLoadedLevel(unsigned level, unsigned target, unsigned previous, unsigned ready)
{
    return level <= target || (previous & (1u << level)) || !ready;
}
template<class Resolve, class Ready>
int firstMissingLevel(int desired, Resolve resolve, Ready ready)
{
    for (int level=0; level<=desired; ++level)
    {
        const int actual = resolve(level);
        if (actual < 0) return -1;
        if (!ready(actual)) return actual;
    }
    return desired;
}
inline unsigned residentLevel(unsigned ready, unsigned desired)
{
    for (int level=int(desired); level>=0; --level) if (ready & (1u<<level)) return level;
    for (unsigned level=0; level<4; ++level) if (ready & (1u<<level)) return level;
    return 4; // no drawable level
}
inline unsigned visibleLevel(unsigned ready, unsigned desired, int previous)
{
    unsigned best = residentLevel(ready, desired);
    if (previous >= 0 && previous < 4 && (ready & (1u<<previous)) &&
        (best == 4 || unsigned(previous)>best)) best = unsigned(previous);
    return best;
}
// Process each initially queued entry at most once; an unfinished entry moves
// behind its peers. New work appended by a callback waits for a later batch.
template<class Queue, class Immediate>
Queue extractImmediate(Queue& queue, Immediate immediate)
{
    Queue result;
    for (auto it=queue.begin(); it!=queue.end();)
    {
        auto current = it++;
        if (immediate(*current)) result.splice(result.end(), queue, current);
    }
    return result;
}
template<class Queue, class Callback, class Exhausted>
unsigned rebuildBatch(Queue& queue, Callback callback, Exhausted exhausted)
{
    const auto initial_count = queue.size();
    unsigned processed = 0;
    for (auto it=queue.begin(); it!=queue.end() && processed<initial_count && processed<64; ++processed)
    {
        if (processed && exhausted()) break;
        auto current = it++;
        if (callback(*current)) queue.erase(current);
        else queue.splice(queue.end(), queue, current);
    }
    return processed;
}
// The map remains authoritative across frames, including object unregistration.
// Remove a waiter before calling it; callbacks may mutate or erase the map.
template<class Map, class Key, class Callback, class Exhausted>
bool notifyWaiters(Map& map, const Key& id, Callback callback, Exhausted exhausted)
{
    for (unsigned count=0; count<8 && !exhausted(); ++count)
    {
        auto it = map.find(id);
        if (it == map.end()) return true;
        auto& volumes = it->second.mVolumes;
        if (volumes.empty()) { map.erase(it); return true; }
        auto first = volumes.begin();
        auto* object = *first;
        volumes.erase(first);
        const bool finished = volumes.empty();
        if (finished) map.erase(it);
        if (object && !object->isDead()) callback(*object);
        if (finished) return true;
    }
    return false;
}
}
#endif
