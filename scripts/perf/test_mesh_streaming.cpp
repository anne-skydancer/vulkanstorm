// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llmeshstreaming.h"
#include <cassert>
#include <map>
#include <list>
#include <set>
#include <vector>
#include <iostream>
struct Object { int id; bool dead=false; bool isDead() const { return dead; } };
struct Waiters { std::set<Object*> mVolumes; };
int main()
{
    using Lane = LLMeshStreaming::RequestLane;
    assert(LLMeshStreaming::requestBefore(0.f, 10.f, 4.f, 1.f));
    assert(LLMeshStreaming::requestBefore(5.f, 0.f, 0.f, 100.f));
    assert(LLMeshStreaming::requestBefore(8.f, 0.f, 6.f, 100.f));
    assert(!LLMeshStreaming::requestBefore(6.f, 100.f, 8.f, 0.f));
    assert(!LLMeshStreaming::requestBefore(8.f, 10.f, 8.f, 10.f));
    // Fairness holds across frame boundaries, not just within large batches.
    for (unsigned mask=1; mask<16; ++mask)
    {
        LLMeshStreaming::RequestScheduler scheduler;
        std::array<bool,4> available{};
        for (unsigned i=0; i<4; ++i) available[i] = (mask & (1u<<i)) != 0;
        for (unsigned round=0; round<8; ++round)
        {
            unsigned served=0;
            for (unsigned slot=0; slot<8; ++slot)
            {
                unsigned lane=scheduler.select(available);
                assert(lane<4 && available[lane]);
                served |= 1u<<lane;
            }
            assert(served==mask);
        }
    }
    LLMeshStreaming::RequestScheduler scheduler;
    assert(scheduler.select({false,false,false,false})==4);
    std::array<unsigned,4> shares{};
    for (unsigned i=0; i<80; ++i) ++shares[scheduler.select({true,true,true,true})];
    assert((shares==std::array<unsigned,4>{30,20,20,10}));
    // Authored levels can alias, fail, or already be resident. Test every ready
    // mask and desired LOD with both direct and compute consumers, including HUD.
    const std::array<std::array<int,4>,5> mappings{{{0,1,2,3},{3,3,3,3},
        {1,1,1,3},{-1,1,2,3},{-1,-1,-1,-1}}};
    for (const auto& mapping:mappings)
        for (unsigned ready=0; ready<16; ++ready)
            for (int desired=0; desired<4; ++desired)
                for (bool hud:{false,true})
                    for (bool residency:{false,true})
                    {
                        std::vector<std::pair<int,Lane>> requests;
                        LLMeshStreaming::planRequests(desired,hud,residency,
                            [&](int i){return mapping[i];},
                            [&](int i){return (ready & (1u<<i))!=0;},
                            [&](int i,Lane lane){requests.emplace_back(i,lane);});
                        unsigned seen=0;
                        for (const auto& request:requests)
                        {
                            assert(request.first>=0 && request.first<4);
                            unsigned bit=1u<<request.first;
                            assert(!(seen & bit) && !(ready & bit));
                            seen |= bit;
                            if (hud) assert(request.first==mapping[desired]);
                            if (!residency || hud) assert(request.second!=Lane::RESIDENCY);
                        }
                        if (mapping[desired]>=0 && !(ready & (1u<<mapping[desired])))
                            assert(seen & (1u<<mapping[desired]));
                    }
    std::vector<std::pair<int,Lane>> cold;
    LLMeshStreaming::planRequests(3,false,true,[](int i){return i;},[](int){return false;},
        [&](int i,Lane lane){cold.emplace_back(i,lane);});
    assert((cold==std::vector<std::pair<int,Lane>>{{0,Lane::FIRST_GEOMETRY},
        {3,Lane::VISIBLE_DETAIL},{1,Lane::RESIDENCY},{2,Lane::RESIDENCY}}));
    using Admission = LLMeshStreaming::AdmissionController;
    Admission admission;
    assert(admission.room(64,0,0,0,0,0.)==64);
    assert(admission.room(64,0,256,0,0,0.)==32 && admission.throttled);
    assert(admission.room(64,0,200,0,0,0.)==32 && admission.throttled); // hysteresis
    assert(admission.room(64,0,128,0,0,0.)>32 && !admission.throttled);
    assert(admission.room(64,0,768,0,0,0.)==16);
    assert(admission.room(64,40,256,0,0,0.)==0); // in-flight work exceeds reduced target
    assert(admission.room(64,30,256,0,0,0.)==2);
    assert(admission.room(64,32,256,0,0,0.)==0); // no repeated per-frame allowance
    assert(admission.room(64,32,0,0,0,0.)==32);
    assert(admission.room(0,0,0,0,0,0.)==0);
    assert(admission.room(64,0,UINT64_MAX,UINT64_MAX,0,0.)==1);
    // Regression: observed healthy backlog was reducing 80 slots to 54 even
    // though admission_throttled was false and drain capacity was ample.
    Admission healthy;
    assert(healthy.room(80,0,117,1187408,0,0.)==80 && !healthy.throttled);
    assert(healthy.room(80,37,117,1187408,588,1.)==43 && !healthy.throttled);
    assert(healthy.limit==80);
    assert(healthy.room(80,77,1,6360342,588,1.)==3 && !healthy.throttled);
    Admission memory;
    assert(memory.room(64,0,1,Admission::HIGH_BYTES,0,0.)==32 && memory.throttled);
    assert(memory.room(64,0,1,Admission::LOW_BYTES+1,0,0.)==32 && memory.throttled);
    assert(memory.room(64,0,1,Admission::LOW_BYTES,0,0.)>32 && !memory.throttled);
    Admission stalled;
    stalled.room(64,0,100,0,0,0.);
    assert(stalled.room(64,0,100,0,0,1.)<32 && stalled.rateKnown && stalled.throttled);
    assert(stalled.drainPerSecond==0.);
    assert(stalled.room(64,0,10,0,100,2.)>32 && !stalled.throttled);
    assert(stalled.room(64,0,0,0,100,3.)==64 && !stalled.rateKnown);
    Admission fast;
    fast.room(64,0,100,0,0,0.);
    assert(fast.room(64,0,100,0,100,1.)>32 && !fast.throttled);
    assert(fast.rateKnown && fast.drainPerSecond==100.);
    // A long delay cannot manufacture extra capacity; the measured rate is per second.
    Admission paused;
    paused.room(64,0,100,0,0,0.);
    assert(paused.room(64,0,100,0,10,10.)<32 && paused.drainPerSecond==1.);

    // Cheap completions use the time allowance rather than stopping at eight.
    unsigned cursor = 0, elapsed = 0;
    unsigned remaining[3] = {100, 1, 1};
    std::vector<unsigned> serviced;
    auto service = [&](unsigned q) {
        if (!remaining[q]) return false;
        --remaining[q]; ++elapsed; serviced.push_back(q); return true;
    };
    assert(LLMeshStreaming::completionBatch(cursor, 3, service, [&]{return elapsed>=20;}) == 20);
    assert(serviced[0]==0 && serviced[1]==1 && serviced[2]==2);
    assert(remaining[0]==82 && remaining[1]==0 && remaining[2]==0);
    // No work or unavailable locks cannot spin until the deadline.
    unsigned probes = 0;
    assert(LLMeshStreaming::completionBatch(cursor, 6, [&](unsigned){++probes;return false;}, []{return false;})==0);
    assert(probes==6);
    probes=0;
    assert(LLMeshStreaming::completionBatch(cursor, 6, [&](unsigned){++probes;return true;}, []{return true;})==0);
    assert(probes==0);
    // Expiration preserves round-robin position; slow callbacks get no extra turn.
    cursor=0; elapsed=0;
    assert(LLMeshStreaming::completionBatch(cursor,3,[&](unsigned q){assert(q==0);elapsed=10;return true;},[&]{return elapsed>=1;})==1);
    assert(cursor==1);
    elapsed=0;
    assert(LLMeshStreaming::completionBatch(cursor,3,[&](unsigned q){assert(q==1);++elapsed;return true;},[&]{return elapsed>=1;})==1);
    // Requeued partial notifications can continue, still sharing the time budget.
    elapsed=0;
    assert(LLMeshStreaming::completionBatch(cursor,1,[&](unsigned){++elapsed;return true;},[&]{return elapsed>=12;})==12);

    // A direct-draw fallback must catch up before a finer rigged generation
    // becomes resident. Later invalidation must not expose the original Lowest.
    unsigned cpu_level = 0;
    auto authored = [](unsigned level) { return int(level); };
    for (unsigned loaded=0; loaded<4; ++loaded)
    {
        const unsigned ready = (1u << (loaded+1))-1;
        const int promotion = LLMeshStreaming::fallbackPromotion(cpu_level, ready, authored);
        if (promotion >= 0) cpu_level = unsigned(promotion);
        assert(cpu_level == loaded);
        assert(LLMeshStreaming::fallbackPromotion(cpu_level, ready, authored) == -1);
    }
    assert(LLMeshStreaming::fallbackPromotion(cpu_level, 1, authored) == -1); // never demote a warm fallback
    assert(LLMeshStreaming::fallbackPromotion(0, 8, authored) == 3); // authored High only
    assert(LLMeshStreaming::fallbackPromotion(0, 0, authored) == -1); // no source yet
    // Nominal High may alias the already active authored Low. Promoting to the
    // nominal slot would rebuild forever because the source remains Low.
    auto low_only = [](unsigned) { return 1; };
    assert(LLMeshStreaming::fallbackPromotion(1, 15, low_only) == -1);
    assert(LLMeshStreaming::fallbackPromotion(0, 15, low_only) == 1);
    for (unsigned mask=1; mask<16; ++mask)
        assert(LLMeshStreaming::fallbackPromotion(1, mask, low_only) == -1);
    // A Medium refinement after High residency must retain High, even when a
    // 16-bit packed subset previously omitted Medium.
    assert(LLMeshStreaming::retainLoadedLevel(3, 2, 8, 7));
    assert(!LLMeshStreaming::retainLoadedLevel(3, 2, 0, 7));
    assert(LLMeshStreaming::retainLoadedLevel(3, 0, 0, 0));
    // Display the best available level while concurrent requests complete.
    unsigned checks=0;
    for (unsigned ready=1; ready<16; ++ready)
        for (unsigned desired=0; desired<4; ++desired)
        {
            unsigned chosen=LLMeshStreaming::residentLevel(ready,desired);
            assert(chosen<4 && (ready & (1u<<chosen)));
            if (ready & ((1u<<(desired+1))-1))
            {
                assert(chosen<=desired);
                for (unsigned i=chosen+1; i<=desired; ++i) assert(!(ready & (1u<<i)));
            }
            else for (unsigned i=0; i<chosen; ++i) assert(!(ready & (1u<<i)));
            ++checks;
        }
    assert(LLMeshStreaming::residentLevel(0,3)==4);
    for (unsigned available=0; available<4; ++available)
        assert(LLMeshStreaming::visibleLevel((1u<<(available+1))-1,3,0)==available);
    assert(LLMeshStreaming::visibleLevel(8,0,3)==3); // keep warm High
    assert(LLMeshStreaming::visibleLevel(2,0,-1)==1); // no authored Lowest
    assert(LLMeshStreaming::visibleLevel(0,3,-1)==4); // no geometry yet

    std::vector<Object> objects; for (int i=0;i<25;++i) objects.push_back({i});
    std::map<int,Waiters> pending;
    for (auto& object:objects) pending[1].mVolumes.insert(&object);
    unsigned calls=0;
    auto callback=[&](Object&){++calls;};
    assert(!LLMeshStreaming::notifyWaiters(pending,1,callback,[]{return false;}));
    assert(calls==8 && pending[1].mVolumes.size()==17);
    // Deletion/unregistration while a completion is spread over frames.
    pending[1].mVolumes.erase(&objects[10]); objects[11].dead=true;
    unsigned before=calls;
    assert(!LLMeshStreaming::notifyWaiters(pending,1,callback,[&]{return calls==before+2;}));
    assert(calls==before+2);
    while (!LLMeshStreaming::notifyWaiters(pending,1,callback,[]{return false;})) {}
    assert(calls==23 && pending.empty());
    // Callback may invalidate the remainder; no iterator survives the callback.
    pending[2].mVolumes={&objects[0],&objects[1]};
    assert(LLMeshStreaming::notifyWaiters(pending,2,[&](Object&){pending.erase(2);},[]{return false;}));
    // A new request created by the final callback belongs to a new completion.
    pending[3].mVolumes={&objects[0]};
    assert(LLMeshStreaming::notifyWaiters(pending,3,[&](Object&){pending[3].mVolumes.insert(&objects[1]);},[]{return false;}));
    assert(pending[3].mVolumes.size()==1);
    std::list<int> rebuilds={1,2,3};
    // Particle animation must be serviced even behind more than a frame's
    // worth of mesh work, without consuming or reordering that mesh budget.
    std::list<int> crowded;
    for (int i=1; i<=100; ++i) crowded.push_back(i);
    crowded.push_back(-1); crowded.push_back(-2);
    auto particles = LLMeshStreaming::extractImmediate(crowded, [](int i){return i<0;});
    assert((std::vector<int>(particles.begin(),particles.end())==std::vector<int>{-1,-2}));
    assert(crowded.size()==100 && crowded.front()==1 && crowded.back()==100);
    unsigned rebuilt=0;
    assert(LLMeshStreaming::rebuildBatch(rebuilds,[&](int i){++rebuilt;return i!=1;},[&]{return rebuilt==2;})==2);
    assert((std::vector<int>(rebuilds.begin(),rebuilds.end())==std::vector<int>{3,1}));
    assert(LLMeshStreaming::rebuildBatch(rebuilds,[](int){return false;},[]{return false;})==2);
    assert(rebuilds.size()==2); // no repeated processing of the rotated tail
    rebuilds.clear(); for (int i=0;i<100;++i) rebuilds.push_back(i);
    assert(LLMeshStreaming::rebuildBatch(rebuilds,[](int){return true;},[]{return false;})==64);
    assert(rebuilds.size()==36);
    rebuilds={1};
    assert(LLMeshStreaming::rebuildBatch(rebuilds,[&](int){rebuilds.push_back(2);return true;},[]{return false;})==1);
    assert(rebuilds.front()==2 && rebuilds.size()==1);
    std::cout << "PASS: " << checks << " resident cases; 1280 request plans; weighted admission; bounded fan-out; deletion; callback reentrancy; rebuild time/count limits and fairness\n";
}
