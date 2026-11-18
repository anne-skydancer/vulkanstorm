/**
 * @file lllandmarklist.cpp
 * @brief Landmark asset list class
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "lllandmarklist.h"

#include "message.h"
#include "llassetstorage.h"

#include "llappviewer.h"
#include "llagent.h"
#include "llfilesystem.h"
#include "llviewerstats.h"
#include "workqueue.h"

// Globals
LLLandmarkList gLandmarkList;

////////////////////////////////////////////////////////////////////////////
// LLLandmarkList

LLLandmarkList::~LLLandmarkList()
{
    std::for_each(mList.begin(), mList.end(), DeletePairedPointer());
    mList.clear();
}

LLLandmark* LLLandmarkList::getAsset(const LLUUID& asset_uuid, loaded_callback_t cb, bool async)
{
    LLLandmark* landmark = get_ptr_in_map(mList, asset_uuid);
    if(landmark)
    {
        LLVector3d dummy;
        if(cb && !landmark->getGlobalPos(dummy))
        {
            // landmark is not completely loaded yet
            loaded_callback_map_t::value_type vt(asset_uuid, cb);
            mLoadedCallbackMap.insert(vt);
        }
        return landmark;
    }
    else
    {
        if ( mBadList.find(asset_uuid) != mBadList.end() )
        {
            return NULL;
        }

        if (cb)
        {
            // Multiple different sources can request same landmark,
            // mLoadedCallbackMap is a multimap that allows multiple pairs with same key
            // Todo: this might need to be improved to not hold identical callbacks multiple times
            loaded_callback_map_t::value_type vt(asset_uuid, cb);
            mLoadedCallbackMap.insert(vt);
        }

        landmark_requested_list_t::iterator iter = mRequestedList.find(asset_uuid);
        if (iter != mRequestedList.end())
        {
            const F32 rerequest_time = 30.f; // 30 seconds between requests
            if (gFrameTimeSeconds - iter->second < rerequest_time)
            {
                return NULL;
            }
        }

        // <Vulkanstorm> bulk scans: read the cache file on the General thread
        if (async)
        {
            if (mAsyncPending.count(asset_uuid) || mRequestedList.count(asset_uuid))
            {
                // already on its way; a scan never re-posts, or the loaded signal could loop
                return NULL;
            }
            LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
            LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
            if (main_queue && general_queue)
            {
                mAsyncPending.insert(asset_uuid);
                main_queue->postTo(
                    general_queue,
                    [asset_uuid]() // General thread: only the uuid is captured
                    {
                        std::string data;
                        LLFileSystem file(asset_uuid, LLAssetType::AT_LANDMARK, LLFileSystem::READ);
                        S32 size = file.getSize();
                        if (size > 0)
                        {
                            data.resize(size);
                            if (!file.read((U8*)&data[0], size))
                            {
                                data.clear();
                            }
                        }
                        return data;
                    },
                    [asset_uuid](std::string data) // main thread
                    {
                        gLandmarkList.onCacheRead(asset_uuid, data);
                    });
                return NULL;
            }
            // no queues: fall through to the synchronous path
        }
        // </Vulkanstorm>

        mRequestedList[asset_uuid] = gFrameTimeSeconds;

        // Note that getAssetData can callback immediately and cleans mRequestedList
        gAssetStorage->getAssetData(asset_uuid,
                                    LLAssetType::AT_LANDMARK,
                                    LLLandmarkList::processGetAssetReply,
                                    NULL);
    }
    return NULL;
}

// <Vulkanstorm> Factored out of processGetAssetReply: builds the landmark from its text.
void LLLandmarkList::onLandmarkData(const LLUUID& uuid, const std::string& data)
{
    if (!data.empty())
    {
        LLLandmark* landmark = LLLandmark::constructFromString(data.c_str(), static_cast<S32>(data.size() + 1));
        if (landmark)
        {
            mList[uuid] = landmark;
            mRequestedList.erase(uuid);

            LLVector3d pos;
            if (!landmark->getGlobalPos(pos))
            {
                LLUUID region_id;
                if (landmark->getRegionID(region_id))
                {
                    LLLandmark::requestRegionHandle(
                        gMessageSystem,
                        gAgent.getRegionHost(),
                        region_id,
                        boost::bind(&LLLandmarkList::onRegionHandle, this, uuid));
                }

                // the callback will be called when we get the region handle.
            }
            else
            {
                makeCallbacks(uuid);
            }
        }
        else
        {
            // failed to parse, shouldn't happen
            LL_WARNS("Landmarks") << "Failed to parse landmark " << uuid << LL_ENDL;
            eraseCallbacks(uuid);
        }
    }
    else
    {
        // got a good status, but no file, shouldn't happen
        LL_WARNS("Landmarks") << "Empty buffer for landmark " << uuid << LL_ENDL;
        eraseCallbacks(uuid);
    }
}

// Main thread: a cache read posted by getAsset(..., async = true) is done.
void LLLandmarkList::onCacheRead(const LLUUID& uuid, const std::string& data)
{
    mAsyncPending.erase(uuid);

    if (mList.count(uuid) == 0 && mBadList.count(uuid) == 0)
    {
        if (!data.empty())
        {
            onLandmarkData(uuid, data);
        }
        else if (mRequestedList.count(uuid) == 0)
        {
            // not in the cache: today's network path
            mRequestedList[uuid] = gFrameTimeSeconds;
            gAssetStorage->getAssetData(uuid,
                                        LLAssetType::AT_LANDMARK,
                                        LLLandmarkList::processGetAssetReply,
                                        NULL);
        }
    }

    if (mAsyncPending.empty())
    {
        mLandmarksLoadedSignal();
    }
}
// </Vulkanstorm>

// static
void LLLandmarkList::processGetAssetReply(
    const LLUUID& uuid,
    LLAssetType::EType type,
    void* user_data,
    S32 status,
    LLExtStat ext_status )
{
    if( status == 0 )
    {
        LLFileSystem file(uuid, type);
        S32 file_length = file.getSize();

        std::string data;
        if (file_length > 0)
        {
            data.resize(file_length);
            file.read((U8*)&data[0], file_length);
        }
        gLandmarkList.onLandmarkData(uuid, data); // <Vulkanstorm> factored out

        // We got this asset, remove it from retry and bad lists.
        gLandmarkList.mRetryList.erase(uuid);
        gLandmarkList.mBadList.erase(uuid);
    }
    else
    {
        if (LL_ERR_NO_CAP == status)
        {
            // A problem with asset cap, always allow retrying.
            // Todo: should this reschedule?
            gLandmarkList.mRequestedList.erase(uuid);
            gLandmarkList.eraseCallbacks(uuid);
            // If there was a previous request, it likely failed due to an obsolete cap
            // so clear the retry marker to allow multiple retries.
            gLandmarkList.mRetryList.erase(uuid);
            return;
        }
        if (gLandmarkList.mBadList.find(uuid) != gLandmarkList.mBadList.end())
        {
            // Already on the 'bad' list, ignore
            gLandmarkList.mRequestedList.erase(uuid);
            gLandmarkList.eraseCallbacks(uuid);
            return;
        }
        if (LL_ERR_ASSET_REQUEST_FAILED == status
            && gLandmarkList.mRetryList.find(uuid) == gLandmarkList.mRetryList.end())
        {
            // There is a number of reasons why an asset request can fail,
            // like a cap being obsolete due to user teleporting.
            // Let viewer rerequest at least once more.
            // Todo: should this reshchedule?
            gLandmarkList.mRetryList.emplace(uuid);
            gLandmarkList.mRequestedList.erase(uuid);
            gLandmarkList.eraseCallbacks(uuid);
            return;
        }

        if (LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE == status)
        {
            LL_WARNS("Landmarks") << "Missing Landmark " << uuid << LL_ENDL;
        }
        else
        {
            LL_WARNS("Landmarks") << "Unable to load Landmark " << uuid
                << ". asset status: " << status
                << ". Extended status: " << (S64)ext_status << LL_ENDL;
        }

        gLandmarkList.mRetryList.erase(uuid);
        gLandmarkList.mBadList.insert(uuid);
        gLandmarkList.mRequestedList.erase(uuid); //mBadList effectively blocks any load, so no point keeping id in requests
        gLandmarkList.eraseCallbacks(uuid);
    }
}

bool LLLandmarkList::isAssetInLoadedCallbackMap(const LLUUID& asset_uuid)
{
    return mLoadedCallbackMap.find(asset_uuid) != mLoadedCallbackMap.end();
}

bool LLLandmarkList::assetExists(const LLUUID& asset_uuid)
{
    return mList.count(asset_uuid) != 0 || mBadList.count(asset_uuid) != 0;
}

void LLLandmarkList::onRegionHandle(const LLUUID& landmark_id)
{
    LLLandmark* landmark = getAsset(landmark_id);
    if (!landmark)
    {
        LL_WARNS() << "Got region handle but the landmark " << landmark_id << " not found." << LL_ENDL;
        eraseCallbacks(landmark_id);
        return;
    }

    // Calculate landmark global position.
    // This should succeed since the region handle is available.
    LLVector3d pos;
    if (!landmark->getGlobalPos(pos))
    {
        LL_WARNS() << "Got region handle but the landmark " << landmark_id << " global position is still unknown." << LL_ENDL;
        eraseCallbacks(landmark_id);
        return;
    }

    // Call this even if no landmark exists to clean mLoadedCallbackMap
    makeCallbacks(landmark_id);
}

void LLLandmarkList::eraseCallbacks(const LLUUID& landmark_id)
{
    mLoadedCallbackMap.erase(landmark_id);
}

void LLLandmarkList::makeCallbacks(const LLUUID& landmark_id)
{
    LLLandmark* landmark = getAsset(landmark_id);

    if (!landmark)
    {
        LL_WARNS() << "Landmark " << landmark_id << " to make callbacks for not found." << LL_ENDL;
    }

    // make all the callbacks here.
    loaded_callback_map_t::iterator it;
    while((it = mLoadedCallbackMap.find(landmark_id)) != mLoadedCallbackMap.end())
    {
        if (landmark)
            (*it).second(landmark);

        mLoadedCallbackMap.erase(it);
    }
}
