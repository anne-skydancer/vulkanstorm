// Shared native resource accounting. LGPL-2.1, like the viewer.
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

// A cache eviction does not free a resource still referenced by a scene or an
// in-flight submission. Charge its bytes until the last actual owner retires.
class VSResourceBudget
{
    struct State
    {
        explicit State(std::size_t maximum) : limit(maximum) {}
        const std::size_t limit;
        std::atomic<std::size_t> used{0};
    };
public:
    class Reservation
    {
        friend class VSResourceBudget;
        Reservation(std::shared_ptr<State> state, std::size_t bytes)
            : mState(std::move(state)), mBytes(bytes) {}
        std::shared_ptr<State> mState;
        std::size_t mBytes;
    public:
        ~Reservation() { mState->used.fetch_sub(mBytes); }
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        std::size_t bytes() const { return mBytes; }
    };
    explicit VSResourceBudget(std::size_t maximum) : mState(std::make_shared<State>(maximum)) {}
    std::size_t used() const { return mState->used.load(); }
    std::size_t limit() const { return mState->limit; }
    std::shared_ptr<const Reservation> reserve(std::size_t bytes)
    {
        auto used = mState->used.load();
        do
        {
            if (!bytes || used > mState->limit || bytes > mState->limit - used) return {};
        } while (!mState->used.compare_exchange_weak(used, used + bytes));
        Reservation* reservation;
        try { reservation = new Reservation(mState, bytes); }
        catch (...) { mState->used.fetch_sub(bytes); throw; }
        // shared_ptr deletes the reservation itself if control-block allocation
        // fails; the byte charge is released exactly once in either case.
        return std::shared_ptr<const Reservation>(reservation);
    }
private:
    std::shared_ptr<State> mState;
};
