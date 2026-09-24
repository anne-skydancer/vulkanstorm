// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llassetpool.h"
#include <cassert>
#include <limits>
#include <iostream>

int main()
{
    using namespace LLAssetPool;
    // External consumers forbid zero-copy transfer. Sole ownership transfers
    // only inside the unscaled save branch; resized variants remain independent.
    assert(canTransferPixels(1));
    for (unsigned refs : {0u,2u,3u,100u}) assert(!canTransferPixels(refs));
    // One active sibling does not protect an idle LOD, but its own lease and
    // external LLPointer borrowers each prevent collection even under pressure.
    assert(reclaimSource(0,1,60,false));
    assert(!reclaimSource(0,1,59.9,false));
    assert(reclaimSource(0,1,5,true));
    assert(!reclaimSource(0,1,4.9,true));
    for (bool pressure : {false,true})
    {
        assert(!reclaimSource(1,1,600,pressure));
        assert(!reclaimSource(0,2,600,pressure));
    }
    // Two individually affordable jobs must not collectively overcommit.
    Reservations pool;
    assert(pool.acquire(400,200,768));
    assert(!pool.acquire(400,200,768));
    assert(pool.bytes()==200);
    // Allocation consumes the reservation as it becomes actual resident bytes.
    pool.release(80);
    assert(pool.bytes()==120);
    assert(!pool.acquire(480,200,768));
    // Cancellation releases the unallocated reservation and the owner releases
    // its 80 allocated bytes; another full replacement can now be admitted.
    pool.release(120);
    assert(pool.acquire(400,300,768));
    pool.release(300);
    assert(pool.bytes()==0);
    assert(!pool.acquire(769,1,768));
    assert(!pool.acquire(0,769,768));
    assert(pool.acquire(0,768,768));
    assert(!pool.acquire(0,1,768));
    pool.release(768);
    // No overflow in available-capacity arithmetic.
    const auto max=std::numeric_limits<std::uint64_t>::max();
    assert(!pool.acquire(max-1,2,max));
    assert(pool.acquire(max-1,1,max));
    assert(!pool.acquire(max-1,1,max));
    pool.release(1);
    std::cout << "PASS: pixel ownership, idle LOD leases, reservation admission/conversion/cancellation and overflow\n";
}
