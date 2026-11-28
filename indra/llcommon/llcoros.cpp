/**
 * @file   llcoros.cpp
 * @author Nat Goodspeed
 * @date   2009-06-03
 * @brief  Implementation for llcoros.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llcoros.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "llapp.h"
#include "llthread.h"
#include "lltimer.h"
#include "llevents.h"
#include "llerror.h"
#include "stringize.h"
#include "llexception.h"

// static
bool LLCoros::on_main_coro()
{
    if (!LLCoros::instanceExists() || LLCoros::getName().empty())
    {
        return true;
    }

    return false;
}

// static
bool LLCoros::on_main_thread_main_coro()
{
    return on_main_coro() && on_main_thread();
}

// static
LLCoros::CoroData& LLCoros::get_CoroData()
{
    // a task of the executor has its own record; the default coroutine of a thread (the "main" one, which was never
    // launched) has the thread's own, whose name is empty
    return LL::currentOrThreadRecord();
}

//static
void LLCoros::set_consuming(bool consuming)
{
    CoroData& data(get_CoroData());
    // DO NOT call this on the main() coroutine.
    llassert_always(! data.mName.empty());
    data.mConsuming = consuming;
}

//static
bool LLCoros::get_consuming()
{
    return get_CoroData().mConsuming;
}

// static
void LLCoros::setStatus(const std::string& status)
{
    get_CoroData().mStatus = status;
}

// static
std::string LLCoros::getStatus()
{
    return get_CoroData().mStatus;
}

LLCoros::LLCoros()
{
    // The executor's owner is the constructing thread (LL::Executor::mOwner). LLSingleton constructs on the main
    // thread, so this holds for the instance() path; assert it so that a different construction can never leave
    // the executor owned by another thread.
    llassert_always(on_main_thread());
}

LLCoros::~LLCoros() = default;

void LLCoros::cleanupSingleton()
{
    // Some of the coroutines (like voice) will depend onto
    // origin singletons, so clean coros before deleting those.
    // LLAppViewer::cleanup() already drained them before tearing anything
    // down; this is the backstop for any that remain.
    drain("at entry to ~LLCoros()");
}

void LLCoros::drain(const std::string& when)
{
    printActiveCoroutines(when);
    // The "LLApp" status-change listeners close work queues and give every waiting coroutine the Stopping exception,
    // which forces them to notice and terminate. But by the time LLApp sets "quitting" status, the main loop has
    // stopped pumping the executor. So pump it a few times ourselves, as long as coroutines remain. Whatever still
    // waits after that is logged and destroyed (never leaked): shutdown() does both.
    mExecutor.shutdown();
    printActiveCoroutines("after pumping");
}

std::string LLCoros::generateDistinctName(const std::string& prefix) const
{
    static int unique = 0;

    // Allowing empty name would make getName()'s not-found return ambiguous.
    if (prefix.empty())
    {
        LL_ERRS("LLCoros") << "LLCoros::launch(): pass non-empty name string" << LL_ENDL;
    }

    // If the specified name isn't already in the map, just use that.
    std::string name(prefix);

    // Until we find an unused name, append a numeric suffix for uniqueness.
    while (CoroData::getInstance(name))
    {
        name = STRINGIZE(prefix << unique++);
    }
    return name;
}

/*==========================================================================*|
bool LLCoros::kill(const std::string& name)
{
    CoroMap::iterator found = mCoros.find(name);
    if (found == mCoros.end())
    {
        return false;
    }
    // Erasing the map entry destroys the coroutine record, which
    // terminates the coroutine.
    mCoros.erase(found);
    return true;
}
|*==========================================================================*/

//static
std::string LLCoros::getName()
{
    return get_CoroData().mName;
}

//static
std::string LLCoros::logname()
{
    LLCoros::CoroData& data(get_CoroData());
    return data.mName.empty()? data.getKey() : data.mName;
}

void LLCoros::rethrow()
{
    mExecutor.rethrow();
}

void LLCoros::printActiveCoroutines(const std::string& when)
{
    LL_INFOS("LLCoros") << "Number of active coroutines " << when
                        << ": " << CoroData::instanceCount() << LL_ENDL;
    if (CoroData::instanceCount() > 0)
    {
        LL_INFOS("LLCoros") << "-------------- List of active coroutines ------------";
        F64 time = LLTimer::getTotalSeconds();
        for (auto& cd : CoroData::instance_snapshot())
        {
            F64 life_time = time - cd.mCreationTime;
            LL_CONT << LL_NEWLINE
                    << cd.getKey() << ' ' << cd.mStatus << " life: " << life_time;
        }
        LL_CONT << LL_ENDL;
        LL_INFOS("LLCoros") << "-----------------------------------------------------" << LL_ENDL;
    }
}

//static
void LLCoros::checkStop()
{
    if (wasDeleted())
    {
        LLTHROW(Shutdown("LLCoros was deleted"));
    }
    // do this AFTER the check above, because getName() depends on
    // get_CoroData(), which depends on the local_ptr in our instance().
    if (getName().empty())
    {
        // Our Stop exception and its subclasses are intended to stop loitering
        // coroutines. Don't throw it from the main coroutine.
        return;
    }
    if (LLApp::isStopped())
    {
        LLTHROW(Stopped("viewer is stopped"));
    }
    if (! LLApp::isRunning())
    {
        LLTHROW(Stopping("viewer is stopping"));
    }
}
