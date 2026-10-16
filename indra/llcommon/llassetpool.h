// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_ASSET_POOL_H
#define LL_ASSET_POOL_H
#include <atomic>
#include <cstdint>
#include <cassert>

namespace LLAssetPool
{
// Transfer a decoded image only when the releasing consumer is its sole
// owner. Shared/mutable consumers still require an independent snapshot.
inline std::atomic<std::uint64_t> pixelTransferredBytes{0}, pixelCopiedBytes{0};
inline bool canTransferPixels(unsigned references) { return references == 1; }

inline bool reclaimSource(unsigned leases, unsigned references, double idleSeconds, bool pressure)
{
    return leases == 0 && references == 1 && idleSeconds >= (pressure ? 5.0 : 60.0);
}

// Reserved output not yet allocated. Already allocated staging stays charged
// to the renderer's resident counter, avoiding a second charge at publication.
class Reservations
{
    std::uint64_t mBytes = 0;
public:
    std::uint64_t bytes() const { return mBytes; }
    bool acquire(std::uint64_t used, std::uint64_t requested, std::uint64_t limit)
    {
        if (used > limit || mBytes > limit-used || requested > limit-used-mBytes) return false;
        mBytes += requested;
        return true;
    }
    void release(std::uint64_t n) { assert(n <= mBytes); mBytes -= n; }
};
}
#endif
