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
    // Cold loads advance one level at a time; no new higher request before its
    // first missing predecessor. Every availability pattern is exercised.
    unsigned checks=0;
    assert(LLMeshStreaming::firstMissingLevel(3, [](int){return -1;}, [](int){return false;})==-1);
    for (unsigned ready=0; ready<16; ++ready)
        for (int desired=0; desired<4; ++desired)
        {
            int result=LLMeshStreaming::firstMissingLevel(desired, [](int i){ return i; },
                [&](int i){ return bool(ready & (1u<<i)); });
            assert(result>=0 && result<=desired);
            for (int i=0; i<result; ++i) assert(ready & (1u<<i));
            if (result<desired) assert(!(ready & (1u<<result)));
            ++checks;
        }
    // An asset with only High: request it directly, never a nonexistent Lowest.
    assert(LLMeshStreaming::firstMissingLevel(3, [](int){return 3;}, [](int){return false;})==3);
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
    std::cout << "PASS: " << checks << " progressive/resident cases; bounded fan-out; deletion; callback reentrancy; rebuild time/count limits and fairness\n";
}
