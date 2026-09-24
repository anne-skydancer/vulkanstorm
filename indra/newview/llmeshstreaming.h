// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_MESH_STREAMING_H
#define LL_MESH_STREAMING_H
#include <cstdint>
#include <algorithm>
namespace LLMeshStreaming
{
// Admission uses completed payload estimates, backlog and measured drain rate.
// Hysteresis prevents toggling near a threshold. Limits apply to total in-flight
// plus queued network work, never to an independent allowance each frame.
class AdmissionController
{
public:
    static constexpr std::uint64_t HIGH_BYTES = 64ull*1024*1024;
    static constexpr std::uint64_t LOW_BYTES = HIGH_BYTES/2;
    bool throttled = false, rateKnown = false;
    double drainPerSecond = 0.;
    unsigned limit = 0;

    unsigned room(unsigned high, unsigned active, std::uint64_t backlog,
                  std::uint64_t bytes, std::uint64_t completed, double now)
    {
        if (sampleTime < 0. || now < sampleTime || completed < sampleCompleted)
        { sampleTime = now; sampleCompleted = completed; rateKnown = false; }
        if (!backlog)
        {
            sampleTime = now; sampleCompleted = completed;
            rateKnown = false; drainPerSecond = 0.;
        }
        else if (now-sampleTime >= 1.)
        {
            const double measured = double(completed-sampleCompleted)/(now-sampleTime);
            drainPerSecond = rateKnown ? 0.5*drainPerSecond + 0.5*measured : measured;
            rateKnown = true; sampleTime = now; sampleCompleted = completed;
        }
        const double rateHigh = rateKnown ? std::max(32., 2.*drainPerSecond) : 256.;
        const double rateLow = rateKnown ? std::max(16., drainPerSecond) : 128.;
        if (!throttled && (backlog>=256 || bytes>=HIGH_BYTES || double(backlog)>rateHigh)) throttled=true;
        else if (throttled && backlog<=128 && bytes<=LOW_BYTES && double(backlog)<=rateLow) throttled=false;
        double pressure = std::max(double(backlog)/256., double(bytes)/HIGH_BYTES);
        if (rateKnown) pressure = std::max(pressure, double(backlog)/rateHigh);
        // Keep pressure engaged until the lower thresholds are satisfied.
        if (throttled) pressure = std::max(1., pressure);
        // A healthy, draining queue must retain normal request concurrency.
        // Pressure scales capacity only after an entry threshold has fired.
        limit = !throttled ? high : (high ? std::max(1u, unsigned(double(high)/(1.+pressure))) : 0);
        return active<limit ? limit-active : 0;
    }
private:
    double sampleTime = -1.;
    std::uint64_t sampleCompleted = 0;
};

// Fair completion service bounded by elapsed time, not a fixed number of items.
// A failed probe (empty queue or unavailable lock) advances to the next queue.
// Stop after a whole idle round; retain the cursor across frames.
template<class Service, class Exhausted>
unsigned completionBatch(unsigned& next, unsigned queues, Service service, Exhausted exhausted)
{
    if (!queues) return 0;
    unsigned processed = 0, idle = 0;
    next %= queues;
    while (idle < queues && !exhausted())
    {
        const unsigned current = next;
        next = (next + 1) % queues;
        if (service(current)) { ++processed; idle = 0; }
        else ++idle;
    }
    return processed;
}

inline bool retainLoadedLevel(unsigned level, unsigned target, unsigned previous, unsigned ready)
{
    return level <= target || (previous & (1u << level)) || !ready;
}
template<class Resolve>
int fallbackPromotion(unsigned current, unsigned ready, Resolve resolve)
{
    int finest = int(current);
    for (unsigned level=0; level<4; ++level)
        if (ready & (1u << level))
        {
            const int actual = resolve(level);
            if (actual > finest) finest = actual;
        }
    return finest > int(current) ? finest : -1;
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
