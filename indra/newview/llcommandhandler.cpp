/**
 * @file llcommandhandler.cpp
 * @brief Central registry for text-driven "commands", most of
 * which manipulate user interface.  For example, the command
 * "agent (uuid) about" will open the UI for an avatar's profile.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
#include "llviewerprecompiledheaders.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#endif

#include "llcommandhandler.h"
#include "lluuid.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "llcommanddispatcherlistener.h"
#include "llstartup.h"
#include "stringize.h"

// system includes
#include <boost/tokenizer.hpp>

#define THROTTLE_PERIOD    5    // required seconds between throttled commands

static LLCommandDispatcherListener sCommandDispatcherListener;
const std::string LLCommandHandler::NAV_TYPE_CLICKED = "clicked";
const std::string LLCommandHandler::NAV_TYPE_EXTERNAL = "external";
const std::string LLCommandHandler::NAV_TYPE_NAVIGATED = "navigated";

//---------------------------------------------------------------------------
// Underlying registry for command handlers, not directly accessible.
//---------------------------------------------------------------------------
struct LLCommandHandlerInfo
{
    LLCommandHandler::EUntrustedAccess mUntrustedBrowserAccess;
    LLCommandHandler* mHandler; // safe, all of these are static objects
};

class LLCommandHandlerRegistry
{
public:
    static LLCommandHandlerRegistry& instance();
    void add(const char* cmd,
             LLCommandHandler::EUntrustedAccess untrusted_access,
             LLCommandHandler* handler);
    bool dispatch(const std::string& cmd,
                  const LLSD& params,
                  const LLSD& query_map,
                  const std::string& grid,
                  LLMediaCtrl* web,
                  const std::string& nav_type,
                  bool trusted_browser);

private:
    void notifySlurlBlocked();
    void notifySlurlThrottled();

    friend LLSD LLCommandDispatcher::enumerate();
    std::map<std::string, LLCommandHandlerInfo> mMap;
};

// static
LLCommandHandlerRegistry& LLCommandHandlerRegistry::instance()
{
    // Force this to be initialized on first call, because we're going
    // to be adding items to the std::map before main() and we can't
    // rely on a global being initialized in the right order.
    static LLCommandHandlerRegistry instance;
    return instance;
}

void LLCommandHandlerRegistry::add(const char* cmd,
                                   LLCommandHandler::EUntrustedAccess untrusted_access,
                                   LLCommandHandler* handler)
{
    LLCommandHandlerInfo info;
    info.mUntrustedBrowserAccess = untrusted_access;
    info.mHandler = handler;

    mMap[cmd] = info;
}

bool LLCommandHandlerRegistry::dispatch(const std::string& cmd,
                                        const LLSD& params,
                                        const LLSD& query_map,
                                        const std::string& grid,
                                        LLMediaCtrl* web,
                                        const std::string& nav_type,
                                        bool trusted_browser)
{
    static F64 last_throttle_time = 0.0;
    F64 cur_time = 0.0;
    std::map<std::string, LLCommandHandlerInfo>::iterator it = mMap.find(cmd);
    if (it == mMap.end()) return false;
    const LLCommandHandlerInfo& info = it->second;
    if (!trusted_browser)
    {
        switch (info.mUntrustedBrowserAccess)
        {
        case LLCommandHandler::UNTRUSTED_ALLOW:
            // fall through and let the command be handled
            break;

        case LLCommandHandler::UNTRUSTED_BLOCK:
            // block request from external browser, but report as
            // "handled" because it was well formatted.
            LL_WARNS_ONCE("SLURL") << "Blocked SLURL command from untrusted browser" << LL_ENDL;
            notifySlurlBlocked();
            return true;

        case LLCommandHandler::UNTRUSTED_CLICK_ONLY:
            if (nav_type == LLCommandHandler::NAV_TYPE_CLICKED
                && info.mHandler->canHandleUntrusted(params, query_map, web, nav_type))
            {
                break;
            }
            LL_WARNS_ONCE("SLURL") << "Blocked SLURL click-only command " << cmd << " from untrusted browser" << LL_ENDL;
            notifySlurlBlocked();
            return true;

        case LLCommandHandler::UNTRUSTED_THROTTLE:
            //skip initial request from external browser before STATE_BROWSER_INIT
            if (LLStartUp::getStartupState() == STATE_FIRST)
            {
                return true;
            }
            if (!info.mHandler->canHandleUntrusted(params, query_map, web, nav_type))
            {
                LL_WARNS_ONCE("SLURL") << "Blocked SLURL command from untrusted browser" << LL_ENDL;
                notifySlurlBlocked();
                return true;
            }
            // if users actually click on a link, we don't need to throttle it
            // (throttling mechanism is used to prevent an avalanche of clicks via
            // javascript
            if (nav_type == LLCommandHandler::NAV_TYPE_CLICKED)
            {
                break;
            }
            cur_time = LLTimer::getElapsedSeconds();
            if (cur_time < last_throttle_time + THROTTLE_PERIOD)
            {
                // block request from external browser if it happened
                // within THROTTLE_PERIOD seconds of the last command
                LL_WARNS_ONCE("SLURL") << "Throttled SLURL command from untrusted browser" << LL_ENDL;
                notifySlurlThrottled();
                return true;
            }
            last_throttle_time = cur_time;
            break;
        }
    }
    if (!info.mHandler) return false;
    return info.mHandler->handle(params, query_map, grid, web);
}

void LLCommandHandlerRegistry::notifySlurlBlocked()
{
    static bool slurl_blocked = false;
    if (!slurl_blocked)
    {
        if (LLStartUp::getStartupState() >= STATE_BROWSER_INIT)
        {
            // Note: commands can arrive before we initialize everything we need for Notification.
            LLNotificationsUtil::add("BlockedSLURL");
        }
        slurl_blocked = true;
    }
}

void LLCommandHandlerRegistry::notifySlurlThrottled()
{
    static bool slurl_throttled = false;
    if (!slurl_throttled)
    {
        if (LLStartUp::getStartupState() >= STATE_BROWSER_INIT)
        {
            // Note: commands can arrive before we initialize everything we need for Notification.
            LLNotificationsUtil::add("ThrottledSLURL");
        }
        slurl_throttled = true;
    }
}

//---------------------------------------------------------------------------
// Automatic registration of commands, runs before main()
//---------------------------------------------------------------------------

LLCommandHandler::LLCommandHandler(const char* cmd,
                                   EUntrustedAccess untrusted_access)
{
    LLCommandHandlerRegistry::instance().add(cmd, untrusted_access, this);
}

LLCommandHandler::~LLCommandHandler()
{
    // Don't care about unregistering these, all the handlers
    // should be static objects.
}

//---------------------------------------------------------------------------
// Public interface
//---------------------------------------------------------------------------

// static
bool LLCommandDispatcher::isNativeConnectedCommand(const std::string& cmd, const LLSD& params)
{
    if (cmd == "teleport") return true; // Ordinary handler validates positions/grid and asks for confirmation.
    if (cmd == "group" && params.size() == 1) return params[0].asString() == "create" || params[0].asString() == "list" || params[0].asString() == "show";
    if (params.size() < 2 || !LLUUID::validate(params[0].asString()) || LLUUID(params[0].asString()).isNull()) return false;
    const std::string verb = params[1].asString();
    if (cmd == "agent") return verb == "about" || verb == "mention" || verb == "inspect" || verb == "im" || verb == "pay" || verb == "requestfriend" || verb == "removefriend" || verb == "mute" || verb == "unmute" || verb == "block" || verb == "unblock";
    if (cmd == "group") return verb == "about" || verb == "inspect";
    if (cmd == "firestorm") return verb == "addtocontactset" || verb == "blockavatar" || verb == "viewlog" || verb == "groupjoin" || verb == "groupleave" || verb == "groupactivate";
    return false;
}

bool LLCommandDispatcher::dispatch(const std::string& cmd,
                                   const LLSD& params,
                                   const LLSD& query_map,
                                   const std::string& grid,
                                   LLMediaCtrl* web,
                                   const std::string& nav_type,
                                   bool trusted_browser)
{
#if VS_NATIVE_VULKAN
    if (auto owner = VSNativeSession::active(); owner && !(cmd == "login" && owner->phase() == VSNativeSession::Phase::Login)
        && !(owner->phase() == VSNativeSession::Phase::Connected && isNativeConnectedCommand(cmd, params)))
    {
        LLSD args; args["ERROR_MESSAGE"] = LLTrans::getString("NativeSessionCommandUnavailable");
        LLNotificationsUtil::add("ErrorMessage", args); return true;
    }
#endif

    return LLCommandHandlerRegistry::instance().dispatch(
        cmd, params, query_map, grid, web, nav_type, trusted_browser);
}

static std::string lookup(LLCommandHandler::EUntrustedAccess value);

LLSD LLCommandDispatcher::enumerate()
{
    LLSD response;
    LLCommandHandlerRegistry& registry(LLCommandHandlerRegistry::instance());
    for (std::map<std::string, LLCommandHandlerInfo>::const_iterator chi(registry.mMap.begin()),
                                                                     chend(registry.mMap.end());
         chi != chend; ++chi)
    {
        LLSD info;
        info["untrusted"] = chi->second.mUntrustedBrowserAccess;
        info["untrusted_str"] = lookup(chi->second.mUntrustedBrowserAccess);
        response[chi->first] = info;
    }
    return response;
}

/*------------------------------ lookup stuff ------------------------------*/
struct symbol_info
{
    const char* name;
    LLCommandHandler::EUntrustedAccess value;
};

#define ent(SYMBOL)                                     \
    {                                                   \
        &#SYMBOL[28], /* skip "LLCommandHandler::UNTRUSTED_" prefix */  \
        SYMBOL                                          \
    }

symbol_info symbols[] =
{
    ent(LLCommandHandler::UNTRUSTED_ALLOW),       // allow commands from untrusted browsers
    ent(LLCommandHandler::UNTRUSTED_BLOCK),       // ignore commands from untrusted browsers
    ent(LLCommandHandler::UNTRUSTED_CLICK_ONLY),  // allow untrusted, but only if clicked
    ent(LLCommandHandler::UNTRUSTED_THROTTLE)     // allow untrusted, but only a few per min.
};

#undef ent

static std::string lookup(LLCommandHandler::EUntrustedAccess value)
{
    for (symbol_info *sii(symbols), *siend(symbols + (sizeof(symbols)/sizeof(symbols[0])));
         sii != siend; ++sii)
    {
        if (sii->value == value)
        {
            return sii->name;
        }
    }
    return STRINGIZE("UNTRUSTED_" << value);
}
