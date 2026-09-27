/** Decoded texture delivery admission. Independent of the resident texture cache. */
#ifndef LL_TEXTURE_DELIVERY_BUDGET_H
#define LL_TEXTURE_DELIVERY_BUDGET_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class LLTextureDeliveryBudget
{
public:
    static constexpr std::uint64_t MiB = 1024 * 1024;
    static constexpr std::uint64_t Ceiling = 2048 * MiB;

    class Reservation
    {
    public:
        ~Reservation() { release(); }
        void release()
        {
            shrinkTo(0);
        }
        // Once a stage has finished, return its unused allowance without
        // revoking ownership of the output. Never grow or resurrect a lease.
        void shrinkTo(std::uint64_t bytes)
        {
            // Also makes destruction of an uncharged reservation safe while
            // reserve() holds sMutex (shared_ptr allocation failure).
            if (mBytes.load() <= bytes) return;
            std::lock_guard<std::mutex> lock(sMutex);
            const auto previous = mBytes.load();
            if (bytes < previous)
            {
                mBytes.store(bytes);
                sUsed -= previous - bytes;
            }
        }
        std::uint64_t bytes() const { return mBytes.load(); }
    private:
        friend class LLTextureDeliveryBudget;
        explicit Reservation(std::uint64_t bytes) : mBytes(bytes) {}
        std::atomic<std::uint64_t> mBytes;
    };
    using Lease = std::shared_ptr<Reservation>;

    static Lease reserve(std::uint64_t bytes, bool first_visible)
    {
        std::lock_guard<std::mutex> lock(sMutex);
        // A quarter of capacity is reserved for first visible images. Ordinary
        // work can use only the other three quarters.
        const auto limit = first_visible ? sLimit : sLimit * 3 / 4;
        if (!bytes) return {};
        if (bytes > limit || sUsed > limit - bytes)
        {
            return {};
        }
        auto result = Lease(new Reservation(0));
        sUsed += bytes;
        result->mBytes.store(bytes);
        return result;
    }

    static void updateAvailableMemory(std::uint64_t available)
    {
        std::lock_guard<std::mutex> lock(sMutex);
        // Leave 512 MiB free and use at most half the remaining headroom.
        // Do not add sUsed: some reservations have not allocated their output
        // yet, and memory samples can be stale. Never revoke existing leases.
        const auto headroom = available > 512 * MiB ? available - 512 * MiB : 0;
        sLimit = std::min(Ceiling, std::max(256 * MiB, headroom / 2));
    }
    static std::uint64_t used() { std::lock_guard<std::mutex> lock(sMutex); return sUsed; }
    static std::uint64_t limit() { std::lock_guard<std::mutex> lock(sMutex); return sLimit; }

private:
    inline static std::mutex sMutex;
    inline static std::uint64_t sUsed = 0;
    inline static std::uint64_t sLimit = Ceiling;
};
#endif
