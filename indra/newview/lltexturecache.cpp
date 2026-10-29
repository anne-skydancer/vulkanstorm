/**
 * @file lltexturecache.cpp
 * @brief Object which handles local texture caching
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#include "lltexturecache.h"

#include "lldir.h"
#include "llimage.h"
#include "llimagej2c.h" // for version control
#include "llviewercontrol.h"
#include "llmemory.h"
#include "lldiskcache.h" // vks_assets_install_viewer_log

#include "vkstorm_ffi.h"

// Cache organization:
// cache/texture.entries
//  Unordered array of Entry structs
// cache/texture.cache
//  First TEXTURE_CACHE_ENTRY_SIZE bytes of each texture in texture.entries in same order
// cache/textures/[0-F]/UUID.texture
//  Actual texture body files
//
// The index, the files and the purge rules are vkstorm-assets (vkstorm-rust
// rung five, crates/vkstorm-assets/src/texcache); the on-disk format is the
// one the C++ wrote. This file keeps the worker thread that serialises the
// reads and writes, the responders and the viewer-facing API.

const S32 TEXTURE_CACHE_ENTRY_SIZE = FIRST_PACKET_SIZE;//1024;
const S32 TEXTURE_FAST_CACHE_DATA_SIZE = 16 * 16 * 4;
const F32 TEXTURE_LAZY_PURGE_TIME_LIMIT = .004f; // 4ms. Would be better to autoadjust, but there is a major cache rework in progress.

class LLTextureCacheWorker : public LLWorkerClass
{
    friend class LLTextureCache;

public:
    LLTextureCacheWorker(LLTextureCache* cache, const LLUUID& id,
                         const U8* data, S32 datasize, S32 offset,
                         S32 imagesize, // for writes
                         LLTextureCache::Responder* responder)
        : LLWorkerClass(cache, "LLTextureCacheWorker"),
          mID(id),
          mCache(cache),
          mReadData(NULL),
          mWriteData(data),
          mDataSize(datasize),
          mOffset(offset),
          mImageSize(imagesize),
          mImageFormat(IMG_CODEC_J2C),
          mImageLocal(false),
          mResponder(responder)
    {
    }
    ~LLTextureCacheWorker()
    {
        llassert_always(!haveWork());
        ll_aligned_free_16(mReadData);
    }

    // override this interface
    virtual bool doRead() = 0;
    virtual bool doWrite() = 0;

    virtual bool doWork(S32 param); // Called from LLWorkerThread::processRequest()

    handle_t read() { addWork(0); return mRequestHandle; }
    handle_t write() { addWork(1); return mRequestHandle; }
    bool complete() { return checkWork(); }

private:
    virtual void startWork(S32 param); // called from addWork() (MAIN THREAD)
    virtual void finishWork(S32 param, bool completed); // called from finishRequest() (WORK THREAD)
    virtual void endWork(S32 param, bool aborted); // called from doWork() (MAIN THREAD)

protected:
    LLTextureCache* mCache;
    LLUUID  mID;

    U8* mReadData;
    const U8* mWriteData;
    S32 mDataSize;
    S32 mOffset;
    S32 mImageSize;
    EImageCodec mImageFormat;
    bool mImageLocal;
    LLPointer<LLTextureCache::Responder> mResponder;
};

class LLTextureCacheLocalFileWorker : public LLTextureCacheWorker
{
public:
    LLTextureCacheLocalFileWorker(LLTextureCache* cache, const std::string& filename, const LLUUID& id,
                         U8* data, S32 datasize, S32 offset,
                         S32 imagesize, // for writes
                         LLTextureCache::Responder* responder)
            : LLTextureCacheWorker(cache, id, data, datasize, offset, imagesize, responder),
            mFileName(filename)

    {
    }

    virtual bool doRead();
    virtual bool doWrite();

private:
    std::string mFileName;
};

bool LLTextureCacheLocalFileWorker::doRead()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    S32 local_size = vks_file_size(mFileName.data(), mFileName.size());

    if (local_size > 0 && mFileName.size() > 4)
    {
        mDataSize = local_size; // Only a complete file is valid

        std::string extension = mFileName.substr(mFileName.size() - 3, 3);

        mImageFormat = LLImageBase::getCodecFromExtension(extension);

        if (mImageFormat == IMG_CODEC_INVALID)
        {
//          LL_WARNS() << "Unrecognized file extension " << extension << " for local texture " << mFileName << LL_ENDL;
            mDataSize = 0; // no data
            return true;
        }
    }
    else
    {
        // file doesn't exist
        mDataSize = 0; // no data
        return true;
    }

    if (!mDataSize || mDataSize > local_size)
    {
        mDataSize = local_size;
    }
    mReadData = (U8*)ll_aligned_malloc_16(mDataSize);

    S32 bytes_read = vks_file_read_at(mFileName.data(), mFileName.size(), mOffset, mReadData, (size_t)mDataSize);

    if (bytes_read != mDataSize)
    {
//      LL_WARNS() << "Error reading file from local cache: " << mFileName
//              << " Bytes: " << mDataSize << " Offset: " << mOffset
//              << " / " << mDataSize << LL_ENDL;
        mDataSize = 0;
        ll_aligned_free_16(mReadData);
        mReadData = NULL;
    }
    else
    {
        mImageSize = local_size;
        mImageLocal = true;
    }
    return true;
}

bool LLTextureCacheLocalFileWorker::doWrite()
{
    // no writes for local files
    return false;
}

class LLTextureCacheRemoteWorker : public LLTextureCacheWorker
{
public:
    LLTextureCacheRemoteWorker(LLTextureCache* cache, const LLUUID& id,
                         const U8* data, S32 datasize, S32 offset,
                         S32 imagesize, // for writes
                         LLPointer<LLImageRaw> raw, S32 discardlevel,
                         LLTextureCache::Responder* responder)
            : LLTextureCacheWorker(cache, id, data, datasize, offset, imagesize, responder),
            mRawImage(raw),
            mRawDiscardLevel(discardlevel)
    {
    }

    virtual bool doRead();
    virtual bool doWrite();

private:
    LLPointer<LLImageRaw> mRawImage;
    S32 mRawDiscardLevel;
};


//virtual
void LLTextureCacheWorker::startWork(S32 param)
{
}

// This is where a texture is read from the cache system (header and body)
// Current assumption are:
// - the whole data are in a raw form, will be stored at mReadData
// - the size of this raw data is mDataSize and can be smaller than TEXTURE_CACHE_ENTRY_SIZE (the size of a record in the header cache)
// - the code supports offset reading but this is actually never exercised in the viewer
bool LLTextureCacheRemoteWorker::doRead()
{
    LL_PROFILE_ZONE_SCOPED;
    if (mDataSize <= 0)
    {
        mDataSize = 0; // nothing asked for
        return true;
    }
    // The buffer the responder takes over: the cache fills it with the
    // header record and the body, the data size tells how much.
    mReadData = (U8*)ll_aligned_malloc_16(mDataSize);
    if (!mReadData)
    {
        LL_WARNS() << "LLTextureCacheWorker: "  << mID
            << " failed to allocate memory for reading: " << mDataSize << LL_ENDL;
        mDataSize = -1; // failed
        return true;
    }
    S32 image_size = 0;
    S32 data_size = vks_texcache_read(mCache->getCache(), mID.mData, mOffset, mDataSize, mReadData, &image_size);
    if (data_size <= 0)
    {
        // 0: not cached, -1: failed (the entry is removed from endWork)
        ll_aligned_free_16(mReadData);
        mReadData = NULL;
        mDataSize = data_size;
    }
    else
    {
        mDataSize = data_size;
        mImageSize = image_size;
    }
    return true;
}

// This is where *everything* about a texture is written down in the cache system (entry map, header and body)
// Current assumption are:
// - the whole data are in a raw form, starting at mWriteData
// - the size of this raw data is mDataSize and can be smaller than TEXTURE_CACHE_ENTRY_SIZE (the size of a record in the header cache)
// - the code *does not* support offset writing so there are no difference between buffer addresses and start of data
bool LLTextureCacheRemoteWorker::doWrite()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    // First state / stage : check that what we're trying to cache is in an OK shape
    if ((mOffset != 0) // We currently do not support write offsets
        || (mDataSize <= 0) // Things will go badly wrong if mDataSize is nul or negative...
        || (mImageSize < mDataSize)
        || (mRawDiscardLevel < 0)
        || (mRawImage->isBufferInvalid())) // decode failed or malfunctioned, don't write
    {
        LL_WARNS() << "INIT state check failed for image: " << mID << " Size: " << mImageSize << " DataSize: " << mDataSize << " Discard:" << mRawDiscardLevel << LL_ENDL;
        mDataSize = -1; // failed
        mRawImage = NULL;
        return true;
    }

    // The fast cache record: the raw image scaled to the level that fits
    // into a record (the search and the record layout are the cache's).
    LLPointer<LLImageRaw> raw = mRawImage;
    S32 w = raw->getWidth();
    S32 h = raw->getHeight();
    S32 c = raw->getComponents();
    S32 discardlevel = mRawDiscardLevel;
    S32 i = vks_texcache_fast_level(w, h, c);
    bool ok = true;
    if (i)
    {
        w >>= i;
        h >>= i;
        if (w * h * c > 0) //valid
        {
            // Make a duplicate to keep the original raw image untouched.
            raw = raw->duplicate();

            if (raw->isBufferInvalid())
            {
                LL_WARNS() << "Invalid image duplicate buffer" << LL_ENDL;
                ok = false;
            }
            else
            {
                raw->scale(w, h);
                discardlevel += i;
            }
        }
    }
    if (ok)
    {
        LLImageDataSharedLock lock(raw);
        VksFastEntry fast;
        fast.width = w;
        fast.height = h;
        fast.components = c;
        fast.discard_level = discardlevel;
        S32 copy_size = w * h * c;
        fast.data = copy_size > 0 ? raw->getData() : NULL;
        fast.data_len = copy_size > 0 ? (size_t)llmin(copy_size, TEXTURE_FAST_CACHE_DATA_SIZE) : 0;
        ok = vks_texcache_write(mCache->getCache(), mID.mData, mWriteData, (size_t)mDataSize, mImageSize, mRawDiscardLevel, &fast) != 0;
    }
    if (!ok)
    {
        mDataSize = -1; // failed
    }
    mRawImage = NULL;
    return true;
}

//virtual
bool LLTextureCacheWorker::doWork(S32 param)
{
    LL_PROFILE_ZONE_SCOPED;
    bool res = false;
    if (param == 0) // read
    {
        res = doRead();
    }
    else if (param == 1) // write
    {
        res = doWrite();
    }
    else
    {
        llassert_always(0);
    }
    return res;
}

//virtual (WORKER THREAD)
void LLTextureCacheWorker::finishWork(S32 param, bool completed)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (mResponder.notNull())
    {
        bool success = (completed && mDataSize > 0);
        if (param == 0)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("tcwfw - read");
            // read
            if (success)
            {
                mResponder->setData(mReadData, mDataSize, mImageSize, mImageFormat, mImageLocal);
                mReadData = NULL; // responder owns data
                mDataSize = 0;
            }
            else
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("tcwfw - read fail");
                ll_aligned_free_16(mReadData);
                mReadData = NULL;
            }
        }
        else
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("tcwfw - write");
            // write
            mWriteData = NULL; // we never owned data
            mDataSize = 0;
        }
        mCache->addCompleted(mResponder, success);
    }
}

//virtual (MAIN THREAD)
void LLTextureCacheWorker::endWork(S32 param, bool aborted)
{
    LL_PROFILE_ZONE_SCOPED;
    if (aborted)
    {
        // Let the destructor handle any cleanup
        return;
    }
    switch(param)
    {
      default:
      case 0: // read
      case 1: // write
      {
          if (mDataSize < 0)
          {
              // failed
              mCache->removeFromCache(mID);
          }
          break;
      }
    }
}

//////////////////////////////////////////////////////////////////////////////

//static
U32 (*LLTextureCache::sClock)() = NULL;

namespace
{
    uint32_t vks_texcache_clock()
    {
        return LLTextureCache::now();
    }

    // The cache location as the Rust cache takes it: the expanded cache
    // directory without its trailing delimiter.
    std::string cache_location(ELLPath location)
    {
        std::string dir = gDirUtilp->getExpandedFilename(location, "");
        const std::string& delim = gDirUtilp->getDirDelimiter();
        if (dir.size() > delim.size() && dir.compare(dir.size() - delim.size(), delim.size(), delim) == 0)
        {
            dir.erase(dir.size() - delim.size());
        }
        return dir;
    }

    VksTexCachePaths cache_paths(const std::string& location)
    {
        const std::string& delim = gDirUtilp->getDirDelimiter();
        VksTexCachePaths paths;
        paths.location = location.data();
        paths.location_len = location.size();
        paths.delim = delim.data();
        paths.delim_len = delim.size();
        return paths;
    }
}

LLTextureCache::LLTextureCache(bool threaded)
    : LLWorkerThread("TextureCache", threaded),
      mWorkersMutex(),
      mListMutex(),
      mCache(NULL)
{
    vks_assets_install_viewer_log();
    const std::string encoder = LLImageJ2C::getEngineInfo();
#if defined(ADDRESS_SIZE)
    const U32 address_size = ADDRESS_SIZE;
#else
    const U32 address_size = 32;
#endif
    mCache = vks_texcache_new(encoder.data(), encoder.size(), address_size, &vks_texcache_clock);
    llassert_always(mCache);
    //do not allow to change the texture cache until setReadOnly() is called.
    vks_texcache_set_read_only(mCache, 1);
}

LLTextureCache::~LLTextureCache()
{
    clearDeleteList() ;
    vks_texcache_shutdown(mCache); // writes the updated entries
    vks_texcache_free(mCache);
    mCache = NULL;
}

//////////////////////////////////////////////////////////////////////////////

//virtual
size_t LLTextureCache::update(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static LLFrameTimer timer ;
    static const F32 MAX_TIME_INTERVAL = 300.f ; //seconds.

    size_t res;
    res = LLWorkerThread::update(max_time_ms);

    mListMutex.lock();
    handle_list_t priorty_list = mPrioritizeWriteList; // copy list
    mPrioritizeWriteList.clear();
    responder_list_t completed_list = mCompletedList; // copy list
    mCompletedList.clear();
    mListMutex.unlock();

    // call 'completed' with workers list unlocked (may call readComplete() or writeComplete()
    for (responder_list_t::iterator iter1 = completed_list.begin();
         iter1 != completed_list.end(); ++iter1)
    {
        Responder *responder = iter1->first;
        bool success = iter1->second;
        responder->completed(success);
    }

    if(!res && timer.getElapsedTimeF32() > MAX_TIME_INTERVAL)
    {
        timer.reset() ;
        vks_texcache_write_updated_entries(mCache);
    }

    return res;
}

//////////////////////////////////////////////////////////////////////////////
// search for local copy of UUID-based image file
std::string LLTextureCache::getLocalFileName(const LLUUID& id)
{
    // Does not include extension
    std::string idstr = id.asString();
    // TODO: should we be storing cached textures in skin directory?
    std::string filename = gDirUtilp->getExpandedFilename(LL_PATH_LOCAL_ASSETS, idstr);
    return filename;
}

std::string LLTextureCache::getTextureFileName(const LLUUID& id)
{
    return vks_ffi_string([&](char* buf, size_t cap) { return vks_texcache_texture_file_name(mCache, id.mData, buf, cap); });
}

//debug
bool LLTextureCache::isInCache(const LLUUID& id)
{
    return vks_texcache_is_in_cache(mCache, id.mData) != 0;
}

//debug
bool LLTextureCache::isInLocal(const LLUUID& id)
{
    std::string filename = getLocalFileName(id);
    // Is it a JPEG2000 file? If not, is it a jpeg file? Hmm... What about a
    // targa file? (used for UI texture mostly)
    static const char* extensions[] = { ".j2c", ".jpg", ".tga" };
    for (const char* ext : extensions)
    {
        std::string local_filename = filename + ext;
        if (vks_file_size(local_filename.data(), local_filename.size()) > 0)
        {
            return true ;
        }
    }
    return false ;
}

S64Bytes LLTextureCache::getUsage()
{
    return S64Bytes(vks_texcache_usage(mCache));
}

S64Bytes LLTextureCache::getMaxUsage()
{
    return S64Bytes(vks_texcache_max_usage(mCache));
}

U32 LLTextureCache::getEntries()
{
    return vks_texcache_entries(mCache);
}

U32 LLTextureCache::getMaxEntries()
{
    return vks_texcache_max_entries(mCache);
}

//////////////////////////////////////////////////////////////////////////////

void LLTextureCache::purgeCache(ELLPath location, bool remove_dir)
{
    const std::string dir = cache_location(location);
    VksTexCachePaths paths = cache_paths(dir);
    vks_texcache_purge_cache(mCache, &paths, remove_dir ? 1 : 0);
}

//is called in the main thread before initCache(...) is called.
void LLTextureCache::setReadOnly(bool read_only)
{
    vks_texcache_set_read_only(mCache, read_only ? 1 : 0);
}

// Called in the main thread.
// Returns the unused amount of max_size if any
S64 LLTextureCache::initCache(ELLPath location, S64 max_size, bool texture_cache_mismatch)
{
    LL_PROFILE_ZONE_SCOPED;

    llassert_always(getPending() == 0) ; //should not start accessing the texture cache before initialized.

    const std::string dir = cache_location(location);
    VksTexCachePaths paths = cache_paths(dir);

    // One 256th of the body files is validated per start, round robin.
    const U32 validate_idx = gSavedSettings.getU32("CacheValidateCounter");
    const bool read_only = vks_texcache_is_read_only(mCache) != 0;

    S64 unused = vks_texcache_init(mCache, &paths, max_size, texture_cache_mismatch ? 1 : 0, (S32)validate_idx);

    // The counter advanced when the purge ran (a writable cache with
    // entries), as the C++ purgeTextures did.
    if (!read_only && !(texture_cache_mismatch && vks_texcache_is_read_only(mCache)) && vks_texcache_entries(mCache) > 0)
    {
        gSavedSettings.setU32("CacheValidateCounter", (validate_idx + 1) % 256);
    }

    llassert_always(getPending() == 0) ; //should not start accessing the texture cache before initialized.

    return unused; // unused cache space
}

//////////////////////////////////////////////////////////////////////////////

// call lockWorkers() first!
LLTextureCacheWorker* LLTextureCache::getReader(handle_t handle)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLTextureCacheWorker* res = NULL;
    handle_map_t::iterator iter = mReaders.find(handle);
    if (iter != mReaders.end())
    {
        res = iter->second;
    }
    return res;
}

LLTextureCacheWorker* LLTextureCache::getWriter(handle_t handle)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLTextureCacheWorker* res = NULL;
    handle_map_t::iterator iter = mWriters.find(handle);
    if (iter != mWriters.end())
    {
        res = iter->second;
    }
    return res;
}

//////////////////////////////////////////////////////////////////////////////

// Calls from texture pipeline thread (i.e. LLTextureFetch)

LLTextureCache::handle_t LLTextureCache::readFromCache(const std::string& filename, const LLUUID& id,
                                                       S32 offset, S32 size, ReadResponder* responder)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // Note: checking to see if an entry exists can cause a stall,
    //  so let the thread handle it
    LLMutexLock lock(&mWorkersMutex);
    LLTextureCacheWorker* worker = new LLTextureCacheLocalFileWorker(this, filename, id,
                                                                     NULL, size, offset, 0,
                                                                     responder);
    handle_t handle = worker->read();
    mReaders[handle] = worker;
    return handle;
}

LLTextureCache::handle_t LLTextureCache::readFromCache(const LLUUID& id,
                                                       S32 offset, S32 size, ReadResponder* responder)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // Note: checking to see if an entry exists can cause a stall,
    //  so let the thread handle it
    LLMutexLock lock(&mWorkersMutex);
    LLTextureCacheWorker* worker = new LLTextureCacheRemoteWorker(this, id,
                                                                  NULL, size, offset,
                                                                  0, NULL, 0, responder);
    handle_t handle = worker->read();
    mReaders[handle] = worker;
    return handle;
}


bool LLTextureCache::readComplete(handle_t handle, bool abort)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    lockWorkers();
    handle_map_t::iterator iter = mReaders.find(handle);
    LLTextureCacheWorker* worker = NULL;
    bool complete = false;
    if (iter != mReaders.end())
    {
        worker = iter->second;
        complete = worker->complete();

        if(!complete && abort)
        {
            abortRequest(handle, true) ;
        }
    }
    if (worker && (complete || abort))
    {
        mReaders.erase(iter);
        unlockWorkers();
        worker->scheduleDelete();
    }
    else
    {
        unlockWorkers();
    }
    return (complete || abort);
}

LLTextureCache::handle_t LLTextureCache::writeToCache(const LLUUID& id,
                                                      const U8* data, S32 datasize, S32 imagesize,
                                                      LLPointer<LLImageRaw> rawimage, S32 discardlevel,
                                                      WriteResponder* responder)
{
    if (vks_texcache_is_read_only(mCache))
    {
        delete responder;
        return LLWorkerThread::nullHandle();
    }
    // A pending lazy purge runs a slice here, on the control thread.
    vks_texcache_lazy_purge_tick(mCache, TEXTURE_LAZY_PURGE_TIME_LIMIT);

    // <FS:ND> There seems to be an edge case of KDU failing to decode images and then we end with null data here.
    // This should be bettered handled up where it fails, but at least this stops the crashes.
    if( rawimage.isNull() || !rawimage->getData() )
    {
        delete responder;
        return LLWorkerThread::nullHandle();
    }
    // </FS:ND>

    LLMutexLock lock(&mWorkersMutex);
    LLTextureCacheWorker* worker = new LLTextureCacheRemoteWorker(this, id,
                                                                  data, datasize, 0,
                                                                  imagesize, rawimage, discardlevel, responder);
    handle_t handle = worker->write();
    mWriters[handle] = worker;
    return handle;
}

//called in the main thread
LLPointer<LLImageRaw> LLTextureCache::readFromFastCache(const LLUUID& id, S32& discardlevel)
{
    S32 head[4] = { 0, 0, 0, 0 };
    U8 record[TEXTURE_FAST_CACHE_DATA_SIZE];
    if (!vks_texcache_read_fast(mCache, id.mData, &head[0], &head[1], &head[2], &head[3], record))
    {
        return NULL; //not in the cache, corrupted or under thread race condition
    }
    discardlevel = head[3];
    S32 image_size = head[0] * head[1] * head[2];
    U8* data = (U8*)ll_aligned_malloc_16(image_size);
    memcpy(data, record, image_size);
    LLPointer<LLImageRaw> raw = new LLImageRaw(data, head[0], head[1], head[2], true);

    // <FS:minerjr>
    // This fixes the invalid discard values from being created which cause the decoder code to fail when trying to handle 6 and 7's which are above the MAX_DISCARD_LEVEL of 5
    // especially on load
    // We will expand the 16x16 texture to the actual MAX_DISCARD_LEVEL texture size, it may be blurry until the user gets closer but 5 discard value should be objects far from the camera.
    // So a 1024x1024 texture with a dicard of 6 will become 32x32 and a 2048x2048 texture with a discard of 7 will become a 64x64 texture.
    if (discardlevel > MAX_DISCARD_LEVEL)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("FixBadDiscardLevel");

        S32 w = head[0]; // Get the current width from the header (16)
        S32 h = head[1]; // Get the current height from the header (16)

        // Expand the width and height by teh difference between the discard and MAX_DISCARD_LEVEL bit shifted to the left. (Expand power of 2 textures)
        w <<= discardlevel - MAX_DISCARD_LEVEL;
        h <<= discardlevel - MAX_DISCARD_LEVEL;

        // Set the discard level to the MAX_DISCARD_LEVEL
        discardlevel = MAX_DISCARD_LEVEL;

        // Scale up the texture and scale the actual data, as we just created it above, it should be fine.
        raw->scale(w, h, true);
    }
    // </FS:minerjr>

    return raw;
}

bool LLTextureCache::writeComplete(handle_t handle, bool abort)
{
    lockWorkers();
    handle_map_t::iterator iter = mWriters.find(handle);
    llassert(iter != mWriters.end());
    if (iter != mWriters.end())
    {
        LLTextureCacheWorker* worker = iter->second;
        if (worker->complete() || abort)
        {
            mWriters.erase(handle);
            unlockWorkers();
            worker->scheduleDelete();
            return true;
        }
    }
    unlockWorkers();
    return false;
}

void LLTextureCache::prioritizeWrite(handle_t handle)
{
    // Don't prioritize yet, we might be working on this now
    //   which could create a deadlock
    LLMutexLock lock(&mListMutex);
    mPrioritizeWriteList.push_back(handle);
}

void LLTextureCache::addCompleted(Responder* responder, bool success)
{
    LLMutexLock lock(&mListMutex);
    mCompletedList.push_back(std::make_pair(responder,success));
}

//////////////////////////////////////////////////////////////////////////////

bool LLTextureCache::removeFromCache(const LLUUID& id)
{
    //LL_WARNS() << "Removing texture from cache: " << id << LL_ENDL;
    return vks_texcache_remove(mCache, id.mData) != 0;
}

//////////////////////////////////////////////////////////////////////////////

LLTextureCache::ReadResponder::ReadResponder()
    : mImageSize(0),
      mImageLocal(false)
{
}

void LLTextureCache::ReadResponder::setData(U8* data, S32 datasize, S32 imagesize, S32 imageformat, bool imagelocal)
{
    if (mFormattedImage.notNull())
    {
        llassert_always(mFormattedImage->getCodec() == imageformat);
        mFormattedImage->appendData(data, datasize);
    }
    else
    {
        mFormattedImage = LLImageFormatted::createFromType(imageformat);
        mFormattedImage->setData(data,datasize);
    }
    mImageSize = imagesize;
    mImageLocal = imagelocal;
}

//////////////////////////////////////////////////////////////////////////////
