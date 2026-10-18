/**
 * @file fsdiscordconnect.cpp
 * @brief Connection to Discord
 * @author liny@pinkfox.xyz
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (C) 2019 Liny Odell @ Second Life
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
 * The Phoenix Firestorm Project, Inc., 1831 Oakwood Drive, Fairmont, Minnesota 56031-3225 USA
 * http://www.firestormviewer.org
 */

#include "llviewerprecompiledheaders.h"

#include "fsdiscordconnect.h"
#include "vsregioncontext.h"
#include "vsnativeim.h"
#include "vsavatarservices.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#endif

#include "llagent.h"
#include "llevents.h"
#include "llviewerregion.h"
#include "llavatarnamecache.h"
#include "llregioninfomodel.h"

#include "rlvactions.h"
#include "rlvhandler.h"

#include <discord-rpc/discord_rpc.h>
#include <boost/algorithm/string/case_conv.hpp>

#include "fsdiscordkey.h"

#include "llviewernetwork.h"

std::unique_ptr<LLEventPump> FSDiscordConnect::sStateWatcher = std::make_unique<LLEventStream>("DiscordConnectState");
std::unique_ptr<LLEventPump> FSDiscordConnect::sInfoWatcher = std::make_unique<LLEventStream>("DiscordConnectInfo");


// Returns false when the file exists and has not our UUID
// Or, put simply, returns true if someone else is using it
bool FSDiscordConnect::checkMarkerFile() const
{
    if (!LLFile::isfile(mMarkerFilename))
    {
        return false;
    }
    LLUUID fileID;
    llifstream file(mMarkerFilename);
    file >> fileID;
    if ((fileID == gAgentID) || fileID.isNull())
    {
        return false;
    }
    return true;
}

void FSDiscordConnect::setMarkerFile()
{
    if (checkMarkerFile())
    {
        return; // dont over-write another instances file
    }
    llofstream file(mMarkerFilename.c_str());
    file << gAgentID << std::endl;
    file.close();
}

void FSDiscordConnect::clearMarkerFile()
{
    if (checkMarkerFile())
    {
        return; // dont remove another instances file
    }
    LLFile::remove(mMarkerFilename);
}

static void handleDiscordReady(const DiscordUser *request)
{
    if(!request || !FSDiscordConnect::instanceExists() || !FSDiscordConnect::instance().acceptsCallbacks())return;
    LLSD info;
    info["name"] = request->username;
    FSDiscordConnect::getInstance()->storeInfo(info);
    if(FSDiscordConnect::instanceExists() && FSDiscordConnect::instance().acceptsCallbacks())
        FSDiscordConnect::instance().setConnectionState(FSDiscordConnect::DISCORD_CONNECTED);
}

static void handleDiscordError(int errorCode, const char* message)
{
    LL_WARNS("DiscordConnect") << "Discord error, errorCode: \"" << errorCode << "\", message: \"" << message << "\"" << LL_ENDL;
}

static void handleDiscordDisconnected(int errorCode, const char* message)
{
    if(!FSDiscordConnect::instanceExists() || !FSDiscordConnect::instance().acceptsCallbacks())return;
    LL_INFOS("DiscordConnect") << "Discord disconnected, errorCode: \"" << errorCode << "\", message: \"" << message << "\"" << LL_ENDL;
    FSDiscordConnect::getInstance()->setConnectionState(FSDiscordConnect::DISCORD_NOT_CONNECTED);
}

///////////////////////////////////////////////////////////////////////////////
//
void FSDiscordConnect::discordConnectCoro()
{
    if(mRetiring || mRpcActive)return;
    mCurrent=vs_native_im_guard();
    if(!mCurrent())return;
    mRpcActive=true;
    DiscordEventHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.ready = handleDiscordReady;
    handlers.errored = handleDiscordError;
    handlers.disconnected = handleDiscordDisconnected;
    //TODO: Get this working and sending TPs
    /*handlers.joinGame = handleDiscordJoinGame;
    handlers.spectateGame = handleDiscordSpectateGame;
    handlers.joinRequest = handleDiscordJoinRequest;*/

    Discord_Initialize(DISCORD_API_KEY, &handlers, 1, "");
}

///////////////////////////////////////////////////////////////////////////////
//
void FSDiscordConnect::discordDisconnectCoro()
{
    const bool active=mRpcActive;
    mRpcActive=false;
    mCurrent=[] {return false;};
    if(active){Discord_ClearPresence();Discord_Shutdown();}
    setConnectionState(FSDiscordConnect::DISCORD_NOT_CONNECTED);
}

///////////////////////////////////////////////////////////////////////////////
//
void FSDiscordConnect::discordConnectedCoro(bool autoConnect)
{
    const auto current=vs_native_im_guard();
    if (autoConnect && !mRetiring && current())
    {
        if(mRpcActive)
        {
            if(isConnected() || mConnectionState==DISCORD_CONNECTION_IN_PROGRESS)return;
            disconnectFromDiscord();
            if(!current())return;
        }
        setConnectionState(FSDiscordConnect::DISCORD_CONNECTION_IN_PROGRESS);
        if(!current() || mRetiring)return;
        if (!checkMarkerFile())
        {
            connectToDiscord();
        }
        else
        {
            setConnectionState(FSDiscordConnect::DISCORD_CONNECTION_FAILED);
        }
    }

}

static bool isRegionVisible(const VSRegionContext* region)
{
    U8 rating = region->getSimAccess();
    bool visible = true;
    if (rating > gSavedPerAccountSettings.getU32("FSMaxSharedMaturity"))
    {
        visible = false;
    }
    else
    {
        std::string name = region->getName();
        LLSD list = gSavedPerAccountSettings.getLLSD("FSBlacklistedRegionNames");
        for (LLSD::array_const_iterator iter = list.beginArray();
            iter != list.endArray();
            iter++)
        {
            if (boost::algorithm::to_lower_copy((*iter).asString()) == boost::algorithm::to_lower_copy(name))
            {
                visible = false;
                break;
            }
        }
    }
    return visible;
}

void FSDiscordConnect::updateRichPresence() const
{
    const auto region=vs_current_region_context();
    if (!acceptsCallbacks() || !isConnected() || !region)
    {
        return;
    }
    std::string region_name;
    if (RlvActions::canShowLocation() && isRegionVisible(&*region))
    {
        region_name = region->getName();
        region_name += " ";
        LLVector3 pos = gAgent.getPositionAgent();
#if VS_NATIVE_VULKAN
        if(VSNativeSession::active())
        {
            const auto pose=vs_avatar_voice_pose();if(!pose)return;
            pos=region->getPosRegionFromGlobal(pose->position);
        }
#endif

        region_name += llformat("(%.0f, %.0f, %.0f)", pos.mV[VX], pos.mV[VY], pos.mV[VZ]);
    }
    else
    {
        region_name = "Hidden Region";
    }

    DiscordRichPresence discordPresence;
    memset(&discordPresence, 0, sizeof(discordPresence));
    discordPresence.state = region_name.c_str();

    std::string name;
    if (RlvActions::canShowName(RlvActions::SNC_DEFAULT, gAgentID) && gSavedPerAccountSettings.getBOOL("FSShareNameToDiscord"))
    {
        LLAvatarName av_name;
        if (LLAvatarNameCache::get(gAgentID, &av_name))
        {
            name = av_name.getCompleteName(true, true);
        }
        else
        {
            name = gAgentUsername;
        }
        discordPresence.details = name.c_str();
    }

    discordPresence.startTimestamp = mConnectTime;

#ifdef OPENSIM
    if (LLGridManager::getInstance()->isInSecondLife())
    {
        discordPresence.largeImageKey = "secondlife_512";
    }
    else
    {
        discordPresence.largeImageKey = "opensimulator_512";
    }
#else
    discordPresence.largeImageKey = "secondlife_512";
#endif

    auto gridLabel = LLGridManager::getInstance()->getGridLabel();
    discordPresence.largeImageText = gridLabel.c_str();
    discordPresence.smallImageKey = "firestorm_512";
    std::string appName = std::string("via " + APP_NAME);
    discordPresence.smallImageText = appName.c_str();

    std::string regionId = region->getRegionID().asString();
    discordPresence.partyId = regionId.c_str();
    #if VS_NATIVE_VULKAN
    if(const auto session=VSNativeSession::active())
    {
        const auto count=session->coarseAvatarCount(region->getHost());
        if(!count || !region->current())return;
        discordPresence.partySize=static_cast<int>(*count);
    }
    else
#endif
        discordPresence.partySize = static_cast<int>(gAgent.getRegion()->mMapAvatars.size());
    discordPresence.partyMax = LLRegionInfoModel::instance().mAgentLimit;
    Discord_UpdatePresence(&discordPresence);
}

///////////////////////////////////////////////////////////////////////////////
//
FSDiscordConnect::FSDiscordConnect()
:   mConnectionState(DISCORD_NOT_CONNECTED),
    mConnected(false),
    mInfo(),
    mRefreshInfo(false),
    mConnectTime(0)
{
    mMarkerFilename = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "discord_in_use_marker");
    mMainloop=LLEventPumps::instance().obtain("mainloop").listen("FSDiscordConnect", std::bind(&FSDiscordConnect::Tick, this, std::placeholders::_1));
}

FSDiscordConnect::~FSDiscordConnect()
{
    mMainloop.disconnect();
    retireAccount();
}

void FSDiscordConnect::connectToDiscord()
{
    if(!mRetiring)discordConnectCoro();
}

void FSDiscordConnect::disconnectFromDiscord()
{
    const bool retiring=mRetiring;mRetiring=true;
    setConnectionState(FSDiscordConnect::DISCORD_DISCONNECTING);
    discordDisconnectCoro();
    mRetiring=retiring;
}

void FSDiscordConnect::checkConnectionToDiscord(bool auto_connect)
{
    if(!mRetiring)discordConnectedCoro(auto_connect);
}

bool FSDiscordConnect::Tick(const LLSD&)
{
    if(mRpcActive && !mCurrent())retireAccount();
    if(acceptsCallbacks()){Discord_RunCallbacks();if(acceptsCallbacks())updateRichPresence();}

    return false;
}

void FSDiscordConnect::storeInfo(const LLSD& info)
{
    mInfo = info;
    mRefreshInfo = false;

    sInfoWatcher->post(info);
}

const LLSD& FSDiscordConnect::getInfo() const
{
    return mInfo;
}

void FSDiscordConnect::clearInfo()
{
    mInfo = LLSD();
}

void FSDiscordConnect::setConnectionState(FSDiscordConnect::EConnectionState connection_state)
{
    if(connection_state == DISCORD_CONNECTED)
    {
        setMarkerFile();
        setConnected(true);
        mConnectTime = time_corrected();
    }
    else if(connection_state == DISCORD_NOT_CONNECTED)
    {
        clearMarkerFile();
        setConnected(false);
    }

    if (mConnectionState != connection_state)
    {
        // set the connection state before notifying watchers
        mConnectionState = connection_state;

        LLSD state_info;
        state_info["enum"] = connection_state;
        sStateWatcher->post(state_info);
    }
}

void FSDiscordConnect::setConnected(bool connected)
{
    mConnected = connected;
}

void FSDiscordConnect::retireAccount()
{
    mRetiring=true;
    disconnectFromDiscord();
    clearInfo();
    mConnectTime=0;
    mAutoCurrent=[] {return false;};
    mRetiring=false;
}

void FSDiscordConnect::onNativeSelfFullyVisible()
{
    // Source avatars can be replaced during a region crossing; this original
    // one-shot login action belongs to the authenticated account, not the mesh.
    if(mAutoCurrent())return;
    mAutoCurrent=vs_native_im_guard();
    if(!mAutoCurrent())return;
    checkConnectionToDiscord(gSavedPerAccountSettings.getBOOL("FSEnableDiscordIntegration"));
}
