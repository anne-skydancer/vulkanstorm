/**
 * @file lldiskcache.cpp
 * @brief The disk cache implementation.
 *
 * Note: Rather than keep the top level function comments up
 * to date in both the source and header files, I elected to
 * only have explicit comments about each function and variable
 * in the header - look there for details. The same is true for
 * description of how this code is supposed to work.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Linden Research, Inc.
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

#include "linden_common.h"

#include "llapp.h"
#include "llassettype.h"
#include "lldir.h"
#include "llerror.h"
#include <chrono>

#include "lldiskcache.h"

#include "vkstorm_ffi.h"

// The cache file naming, the purge (with the high and low water marks and
// the static asset skip list), the directory scans and the clear are
// vkstorm-assets (vkstorm-rust rung five, crates/vkstorm-assets/src/diskcache);
// this file keeps the singleton, the viewer-facing API and the static asset
// prepopulation, which needs the viewer's directories.

std::string LLDiskCache::sCacheDir;

namespace
{
    void vks_assets_viewer_log(int32_t level, const char* msg, size_t len)
    {
        std::string text(msg, len);
        switch (level)
        {
        case 0:
            LL_DEBUGS("VksAssets") << text << LL_ENDL;
            break;
        case 1:
            LL_INFOS("VksAssets") << text << LL_ENDL;
            break;
        default:
            LL_WARNS("VksAssets") << text << LL_ENDL;
            break;
        }
    }
}

void vks_assets_install_viewer_log()
{
    static bool installed = false;
    if (!installed)
    {
        vks_assets_set_log(&vks_assets_viewer_log);
        installed = true;
    }
}

LLDiskCache::LLDiskCache(const std::string& cache_dir,
                         const uintmax_t max_size_bytes,
                         const bool enable_cache_debug_info
// <FS:Beq> Add High/Low water mark support
                         ,const F32 highwater_mark_percent
                         ,const F32 lowwater_mark_percent
// </FS:Beq>
                         ) :
    mCache(NULL)
{
    vks_assets_install_viewer_log();
    sCacheDir = cache_dir;
    const std::string& delim = gDirUtilp->getDirDelimiter();
    // Creates the cache directory and its 16 subdirectories.
    mCache = vks_diskcache_new(cache_dir.data(), cache_dir.size(), delim.data(), delim.size(),
                               (uint64_t)max_size_bytes, enable_cache_debug_info ? 1 : 0,
                               highwater_mark_percent, lowwater_mark_percent);
    llassert_always(mCache);

    // <FS:Beq> add static assets into the new cache after clear.
    // Only missing entries are copied on init, skiplist is setup
    // For everything we populate FS specific assets to allow future updates
    prepopulateCacheWithStatic();
    // </FS:Beq>
}

// WARNING: purge() is called by LLPurgeDiskCacheThread. The cache object
// has its own lock: the purge works on a copy of the configuration and
// writes the scan result back, so the main thread's calls do not wait for
// the file system work. Interaction through the file system itself is safe
// (a file being read cannot be removed; one removed first fails to open and
// the asset is re-requested).
void LLDiskCache::purge()
{
    // <VulkanStorm> Stop as soon as the viewer is shutting down (upstream
    // d527adf68c): checked in the scan, the delete loop and the debug list.
    vks_diskcache_purge_while(mCache, []() -> int32_t { return LLApp::isRunning() ? 1 : 0; });
    // </VulkanStorm>
}

const std::string LLDiskCache::metaDataToFilepath(const LLUUID& id, LLAssetType::EType at)
{
    // <FS:Ansariel> Store assets in subfolders: <dir>/<first hex>/sl_cache_<id>_0.asset
    const std::string& delim = gDirUtilp->getDirDelimiter();
    return vks_ffi_string([&](char* buf, size_t cap)
        { return vks_diskcache_metadata_to_filepath(sCacheDir.data(), sCacheDir.size(), delim.data(), delim.size(), id.mData, buf, cap); });
}

const std::string LLDiskCache::getCacheInfo()
{
    LL_PROFILE_ZONE_SCOPED; // <FS:Beq/> add some instrumentation
    return vks_ffi_string([&](char* buf, size_t cap) { return vks_diskcache_info(mCache, buf, cap); });
}

// <FS:Beq> Copy static items into cache and add to the skip list that prevents their purging
// Note that there is no de-duplication nor other validation of the list.
void LLDiskCache::prepopulateCacheWithStatic()
{
    vks_diskcache_skip_list_clear(mCache);
    std::vector<std::string> from_folders;
    from_folders.emplace_back(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "fs_static_assets"));
#ifdef OPENSIM
    from_folders.emplace_back(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "static_assets"));
#endif
    for (const auto& from_folder : from_folders)
    {
        if (gDirUtilp->fileExists(from_folder))
        {
            auto assets_to_copy = gDirUtilp->getFilesInDir(from_folder);
            for (auto from_asset_file : assets_to_copy)
            {
                from_asset_file = from_folder + gDirUtilp->getDirDelimiter() + from_asset_file;
                // we store static assets as UUID.asset_type the asset_type is not used in the current simple cache format
                vks_diskcache_add_static(mCache, from_folder.data(), from_folder.size(), from_asset_file.data(), from_asset_file.size());
            }
        }
    }
}
// </FS:Beq>

void LLDiskCache::clearCache()
{
    vks_diskcache_clear(mCache);
    // <FS:Beq> add static assets into the new cache after clear
    LL_INFOS() << "prepopulating new cache " << LL_ENDL;
    prepopulateCacheWithStatic();
}

void LLDiskCache::removeOldVFSFiles()
{
    //VFS files won't be created, so consider removing this code later
    const std::string cache_path(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, ""));
    vks_diskcache_remove_old_vfs_files(cache_path.data(), cache_path.size());
}

void LLDiskCache::setMaxSizeBytes(uintmax_t size)
{
    vks_diskcache_set_max_size(mCache, (uint64_t)size);
}

void LLDiskCache::setHighWaterPercentage(F32 HiPct)
{
    vks_diskcache_set_high_water(mCache, HiPct);
}

void LLDiskCache::setLowWaterPercentage(F32 LowPct)
{
    vks_diskcache_set_low_water(mCache, LowPct);
}

LLPurgeDiskCacheThread::LLPurgeDiskCacheThread() :
    LLThread("PurgeDiskCacheThread", nullptr)
{
}

void LLPurgeDiskCacheThread::run()
{
    constexpr std::chrono::seconds CHECK_INTERVAL{60};

    while (LLApp::instance()->sleep(CHECK_INTERVAL))
    {
        LLDiskCache::instance().purge();
    }
}
