/**
 * @file   llcond_test.cpp
 * @author Nat Goodspeed
 * @date   2019-07-18
 * @brief  Test for llcond.
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 * Copyright (c) 2019, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llcond.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "../test/lltut.h"
#include <thread>

/*****************************************************************************
*   TUT
*****************************************************************************/
namespace tut
{
    struct llcond_data
    {
        LLScalarCond<int> cond{0};
    };
    typedef test_group<llcond_data> llcond_group;
    typedef llcond_group::object object;
    llcond_group llcondgrp("llcond");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("Immediate gratification");
        cond.set_one(1);
        ensure("wait_for_equal() failed",
               cond.wait_for_equal(F32Milliseconds(1), 1));
        ensure("wait_for_unequal() should have failed",
               ! cond.wait_for_unequal(F32Milliseconds(1), 1));
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("Simple two-thread test");
        int seenFirst = -1, seenSecond = -1;
        // Don't use ensure() on the thread: a failure there would not reach the test driver.
        std::thread worker(
            [this, &seenFirst, &seenSecond]()
            {
                seenFirst = cond.get();
                cond.set_all(1);
                cond.wait_equal(2);
                seenSecond = cond.get();
                cond.set_all(3);
            });
        cond.wait_equal(1);
        cond.set_all(2);
        cond.wait_equal(3);
        worker.join();
        ensure_equals(seenFirst, 0);
        ensure_equals(seenSecond, 2);
    }
} // namespace tut
