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
#include "llcallbacklist.h"
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

        // <Vulkanstorm> bulk scans: read the cache file on the General thread, in batches
        if (async)
        {
            if (mAsyncPending.count(asset_uuid))
            {
                // already on its way; the expired-request gate above handles the retries
                return NULL;
            }
            if (LL::WorkQueue::getInstance("mainloop") && LL::WorkQueue::getInstance("General"))
            {
                mAsyncPending.insert(asset_uuid);
                mAsyncBatch.push_back(asset_uuid);
                if (mAsyncBatch.size() >= ASYNC_BATCH_SIZE)
                {
                    flushAsyncBatch();
                }
                else if (!mFlushScheduled)
                {
                    // post the partial batch once per frame (idle callbacks run on the main thread)
                    mFlushScheduled = true;
                    doOnIdleOneTime([]() { gLandmarkList.flushAsyncBatch(); });
                }
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

// Main thread: posts the pending async cache reads as one job on the General thread.
void LLLandmarkList::flushAsyncBatch()
{
    mFlushScheduled = false;
    if (mAsyncBatch.empty())
    {
        return;
    }

    std::vector<LLUUID> batch;
    batch.swap(mAsyncBatch);

    LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
    LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
    bool posted = false;
    if (main_queue && general_queue)
    {
        posted = main_queue->postTo(
            general_queue,
            [batch]() // General thread: only the uuids are captured
            {
                std::vector<std::pair<LLUUID, std::string>> results;
                results.reserve(batch.size());
                for (const LLUUID& uuid : batch)
                {
                    std::string data;
                    LLFileSystem file(uuid, LLAssetType::AT_LANDMARK, LLFileSystem::READ);
                    S32 size = file.getSize();
                    if (size > 0)
                    {
                        data.resize(size);
                        if (!file.read((U8*)&data[0], size))
                        {
                            data.clear();
                        }
                    }
                    results.emplace_back(uuid, std::move(data));
                }
                return results;
            },
            [](std::vector<std::pair<LLUUID, std::string>> results) // main thread
            {
                gLandmarkList.onCacheRead(results);
            });
    }
    if (!posted)
    {
        // could not post: nothing may stay pending, load them the synchronous way
        for (const LLUUID& uuid : batch)
        {
            mAsyncPending.erase(uuid);
        }
        for (const LLUUID& uuid : batch)
        {
            getAsset(uuid);
        }
        scheduleLandmarksLoaded();
    }
}

void LLLandmarkList::fireLandmarksLoaded()
{
    ++mLoadedGeneration;
    mLandmarksLoadedSignal();
}

// Fires the signal on the next idle pass, never from inside a caller's scan (getAssetData can
// call processGetAssetReply back immediately) or from the middle of a callback.
void LLLandmarkList::scheduleLandmarksLoaded()
{
    if (mSignalScheduled)
    {
        return;
    }
    mSignalScheduled = true;
    doOnIdleOneTime([]()
    {
        gLandmarkList.mSignalScheduled = false;
        if (gLandmarkList.mAsyncPending.empty())
        {
            gLandmarkList.fireLandmarksLoaded();
        }
    });
}

// Main thread: a batch of cache reads posted by flushAsyncBatch() is done.
void LLLandmarkList::onCacheRead(const std::vector<std::pair<LLUUID, std::string>>& results)
{
    const F32 rerequest_time = 30.f; // same gate as getAsset()
    for (const auto& result : results)
    {
        const LLUUID& uuid = result.first;
        const std::string& data = result.second;
        mAsyncPending.erase(uuid);

        if (mList.count(uuid) != 0 || mBadList.count(uuid) != 0)
        {
            continue;
        }

        // A request entry that is still fresh was made after our post (getAsset() does not
        // post while one is), so a synchronous or network request for this uuid is in flight:
        // leave it to that request, a partial read must not erase its callbacks.
        landmark_requested_list_t::iterator req = mRequestedList.find(uuid);
        if (req != mRequestedList.end() && gFrameTimeSeconds - req->second < rerequest_time)
        {
            continue;
        }

        if (!data.empty())
        {
            onLandmarkData(uuid, data);
            if (mList.count(uuid) == 0)
            {
                // unparsable: back off like a failed request, or the scan would re-post it every frame
                mRequestedList[uuid] = gFrameTimeSeconds;
            }
        }
        else
        {
            // Missing file and failed read both end here: fall back to the network path
            mRequestedList[uuid] = gFrameTimeSeconds;
            gAssetStorage->getAssetData(uuid,
                                        LLAssetType::AT_LANDMARK,
                                        LLLandmarkList::processGetAssetReply,
                                        NULL);
        }
    }

    if (mAsyncPending.empty())
    {
        fireLandmarksLoaded();
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
        // <Vulkanstorm> the network path finished: refresh what depends on it
        gLandmarkList.scheduleLandmarksLoaded();
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
    // <Vulkanstorm> the position is known now: refresh what depends on it
    scheduleLandmarksLoaded();
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
