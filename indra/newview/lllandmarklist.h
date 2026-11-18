/**
 * @file lllandmarklist.h
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

#ifndef LL_LLLANDMARKLIST_H
#define LL_LLLANDMARKLIST_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <boost/signals2.hpp>
#include "lllandmark.h"
#include "lluuid.h"
#include "llassetstorage.h"

class LLMessageSystem;
class LLLineEditor;
class LLInventoryItem;

class LLLandmarkList
{
public:
    typedef std::function<void(LLLandmark*)> loaded_callback_t;

    LLLandmarkList() {}
    ~LLLandmarkList();

    //S32                   getLength() { return mList.getLength(); }
    //const LLLandmark* getFirst()  { return mList.getFirstData(); }
    //const LLLandmark* getNext()   { return mList.getNextData(); }

    bool assetExists(const LLUUID& asset_uuid);
    // <Vulkanstorm> async: for a landmark that is not loaded yet, read the disk cache on the
    // General thread instead of on the caller's thread (the first call still returns NULL);
    // the reads are batched (one General job per ASYNC_BATCH_SIZE landmarks, a partial batch is
    // posted on the next idle pass).
    LLLandmark* getAsset(const LLUUID& asset_uuid, loaded_callback_t cb = nullptr, bool async = false);
    // The landmarks-loaded signal fires on the main thread whenever no async read is pending
    // after one finished (every empty transition), and after a region handle or a network load
    // completed (on the next idle pass) while none is pending. Handlers may rescan, which only re-posts uuids that are
    // neither loaded, bad nor backed off.
    typedef boost::signals2::signal<void()> landmarks_loaded_signal_t;
    // Bumped each time the signal fires: lets a once-per-frame scan guard know that new data arrived.
    U32 getLoadedGeneration() const { return mLoadedGeneration; }
    boost::signals2::connection setLandmarksLoadedCallback(const landmarks_loaded_signal_t::slot_type& cb)
    {
        return mLandmarksLoadedSignal.connect(cb);
    }
    // </Vulkanstorm>
    static void processGetAssetReply(
        const LLUUID& uuid,
        LLAssetType::EType type,
        void* user_data,
        S32 status,
        LLExtStat ext_status );

    // Returns true if loading the landmark with given asset_uuid has been requested
    // but is not complete yet.
    bool isAssetInLoadedCallbackMap(const LLUUID& asset_uuid);

protected:
    void onRegionHandle(const LLUUID& landmark_id);
    void eraseCallbacks(const LLUUID& landmark_id);
    void makeCallbacks(const LLUUID& landmark_id);
    // <Vulkanstorm>
    void onLandmarkData(const LLUUID& uuid, const std::string& data);
    void onCacheRead(const std::vector<std::pair<LLUUID, std::string>>& results);
    void flushAsyncBatch();
    void fireLandmarksLoaded();
    void scheduleLandmarksLoaded();
    // </Vulkanstorm>

    typedef std::map<LLUUID, LLLandmark*> landmark_list_t;
    landmark_list_t mList;

    typedef std::set<LLUUID> landmark_uuid_list_t;
    landmark_uuid_list_t mBadList;
    landmark_uuid_list_t mRetryList;

    typedef std::map<LLUUID,F32> landmark_requested_list_t;
    landmark_requested_list_t mRequestedList;

    // *TODO: make the callback multimap a template class and make use of it
    // here and in LLLandmark.
    typedef std::multimap<LLUUID, loaded_callback_t> loaded_callback_map_t;
    loaded_callback_map_t mLoadedCallbackMap;

    // <Vulkanstorm> cache reads in flight on the General thread
    landmark_uuid_list_t mAsyncPending;
    static constexpr size_t ASYNC_BATCH_SIZE = 64;
    std::vector<LLUUID> mAsyncBatch; // accumulated, not posted yet (also in mAsyncPending)
    bool mFlushScheduled = false;
    bool mSignalScheduled = false;
    U32 mLoadedGeneration = 0;
    landmarks_loaded_signal_t mLandmarksLoadedSignal;
};


extern LLLandmarkList gLandmarkList;

#endif  // LL_LLLANDMARKLIST_H
