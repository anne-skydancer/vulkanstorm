// Native image payload accounting, independent of admission limits. LGPL-2.1.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
namespace VSImageUsage
{
struct State
{
    std::atomic<std::uint64_t> gpu{0},snapshot{0};
};
class Charge
{
    std::shared_ptr<State> owner;
    std::uint64_t gpu=0,snapshot=0;
public:
    Charge()=default;
    Charge(const Charge&)=delete;
    Charge& operator=(const Charge&)=delete;
    void attach(const std::shared_ptr<State>& state,std::uint64_t gpuBytes,std::uint64_t snapshotBytes)
    {
        if(owner || !state) throw std::logic_error("Invalid native image accounting owner");
        owner=state;gpu=gpuBytes;snapshot=snapshotBytes;
        owner->gpu.fetch_add(gpu);owner->snapshot.fetch_add(snapshot);
    }
    ~Charge()
    {
        if(owner) {owner->gpu.fetch_sub(gpu);owner->snapshot.fetch_sub(snapshot);}
    }
};
}
