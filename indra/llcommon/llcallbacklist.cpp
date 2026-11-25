/**
 * @file llcallbacklist.cpp
 * @brief A simple list of callback functions to call.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "llcallbacklist.h"
#include <utility>
#include "lleventtimer.h"
#include "llerrorlegacy.h"

// Globals
//
LLCallbackList gIdleCallbacks;

//
// Member functions
//

LLCallbackList::LLCallbackList()
{
    // nothing
}

LLCallbackList::~LLCallbackList()
= default;


void LLCallbackList::addFunction( callback_t func, void *data)
{
    if (!func)
    {
        return;
    }

    // only add one callback per func/data pair
    //
    if (containsFunction(func, data))
    {
        return;
    }

    callback_pair_t t(func, data);
    mCallbackList.push_back(t);
}

bool LLCallbackList::containsFunction( callback_t func, void *data)
{
    callback_pair_t t(func, data);
    auto iter = find(func,data);
    if (iter != mCallbackList.end())
    {
        return true;
    }
    else
    {
        return false;
    }
}


bool LLCallbackList::deleteFunction( callback_t func, void *data)
{
    auto iter = find(func,data);
    if (iter != mCallbackList.end())
    {
        // <FS:TJ> [FIRE-36314] Fix crash caused by possible callback erasure during callFunctions()
        //mCallbackList.erase(iter);
        if (mCallbackDepth > 0)
        {
            iter->first = nullptr;
            mHasPendingDeletions = true;
        }
        else
        {
            mCallbackList.erase(iter);
        }
        // </FS:TJ>
        return true;
    }
    else
    {
        return false;
    }
}

inline
LLCallbackList::callback_list_t::iterator
LLCallbackList::find(callback_t func, void *data)
{
    callback_pair_t t(func, data);
    return std::find(mCallbackList.begin(), mCallbackList.end(), t);
}

void LLCallbackList::deleteAllFunctions()
{
    // <FS:TJ> [FIRE-36314] Fix crash caused by possible callback erasure during callFunctions()
    if (mCallbackDepth > 0)
    {
        for (auto& entry : mCallbackList)
        {
            entry.first = nullptr;
        }
        mHasPendingDeletions = !mCallbackList.empty();
        return;
    }
    // </FS:TJ>
    mCallbackList.clear();
}


void LLCallbackList::callFunctions()
{
    ++mCallbackDepth; // <FS:TJ/> [FIRE-36314] Fix crash caused by possible callback erasure during callFunctions()
    for (auto iter = mCallbackList.begin(); iter != mCallbackList.end(); )
    {
        auto curiter = iter++;
        // <FS:TJ> [FIRE-36314] Fix crash caused by possible callback erasure during callFunctions()
        //curiter->first(curiter->second);
        if (curiter->first)
        {
            curiter->first(curiter->second);
        }
        // </FS:TJ>
    }

    // <FS:TJ> [FIRE-36314] Fix crash caused by possible callback erasure during callFunctions()
    llassert(mCallbackDepth >= 0);
    if (--mCallbackDepth <= 0 && mHasPendingDeletions)
    {
        mCallbackDepth = 0;
        mHasPendingDeletions = false;
        mCallbackList.remove_if([](const auto& entry) { return !entry.first; });
    }
    // </FS:TJ>
}

// Shim class to allow arbitrary boost::bind
// expressions to be run as one-time idle callbacks.
class OnIdleCallbackOneTime
{
public:
    OnIdleCallbackOneTime(nullary_func_t callable):
        mCallable(std::move(callable))
    {
    }
    static void onIdle(void *data)
    {
        gIdleCallbacks.deleteFunction(onIdle, data);
        auto* self = reinterpret_cast<OnIdleCallbackOneTime*>(data);
        self->call();
        delete self;
    }
    void call()
    {
        mCallable();
    }
private:
    nullary_func_t mCallable;
};

void doOnIdleOneTime(nullary_func_t callable)
{
    auto* cb_functor = new OnIdleCallbackOneTime(std::move(callable));
    gIdleCallbacks.addFunction(&OnIdleCallbackOneTime::onIdle,cb_functor);
}

// Shim class to allow generic boost functions to be run as
// recurring idle callbacks.  Callable should return true when done,
// false to continue getting called.
class OnIdleCallbackRepeating
{
public:
    OnIdleCallbackRepeating(bool_func_t callable):
        mCallable(std::move(callable))
    {
    }
    // Will keep getting called until the callable returns true.
    static void onIdle(void *data)
    {
        auto* self = reinterpret_cast<OnIdleCallbackRepeating*>(data);
        bool done = self->call();
        if (done)
        {
            gIdleCallbacks.deleteFunction(onIdle, data);
            delete self;
        }
    }
    bool call()
    {
        return mCallable();
    }
private:
    bool_func_t mCallable;
};

void doOnIdleRepeating(bool_func_t callable)
{
    auto* cb_functor = new OnIdleCallbackRepeating(std::move(callable));
    gIdleCallbacks.addFunction(&OnIdleCallbackRepeating::onIdle,cb_functor);
// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the idle list owns the functor; onIdle() deletes it
}

class NullaryFuncEventTimer: public LLEventTimer
{
public:
    NullaryFuncEventTimer(nullary_func_t callable, F32 seconds):
        LLEventTimer(seconds),
        mCallable(std::move(callable))
    {
    }

private:
    bool tick() override
    {
        mCallable();
        return true;
    }

    nullary_func_t mCallable;
};

// Call a given callable once after specified interval.
void doAfterInterval(nullary_func_t callable, F32 seconds)
{
    new NullaryFuncEventTimer(std::move(callable), seconds);
// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the LLEventTimer base registers the timer and deletes it when tick() returns true
}

class BoolFuncEventTimer: public LLEventTimer
{
public:
    BoolFuncEventTimer(bool_func_t callable, F32 seconds):
        LLEventTimer(seconds),
        mCallable(std::move(callable))
    {
    }
private:
    bool tick() override
    {
        return mCallable();
    }

    bool_func_t mCallable;
};

// Call a given callable every specified number of seconds, until it returns true.
void doPeriodically(bool_func_t callable, F32 seconds)
{
    new BoolFuncEventTimer(std::move(callable), seconds);
// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the LLEventTimer base registers the timer and deletes it when tick() returns true
}
