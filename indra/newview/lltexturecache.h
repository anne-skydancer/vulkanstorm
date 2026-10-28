/**
 * @file lltexturecache.h
 * @brief Object for managing texture cachees.
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

#ifndef LL_LLTEXTURECACHE_H
#define LL_LLTEXTURECACHE_H

#include "lldir.h"
#include "llstl.h"
#include "llstring.h"
#include "lluuid.h"

#include "llworkerthread.h"

class LLImageFormatted;
class LLTextureCacheWorker;
class LLImageRaw;
struct VksTexCache;

// The texture cache's index, entries file, header-data file, body files and
// fast cache are vkstorm-assets (vkstorm-rust rung five), byte-compatible
// with the files the C++ wrote. This class keeps the worker thread, the
// responders and the viewer-facing API; see doc/rust_port.md.
class LLTextureCache : public LLWorkerThread
{
    friend class LLTextureCacheWorker;
    friend class LLTextureCacheRemoteWorker;
    friend class LLTextureCacheLocalFileWorker;

public:
    // The clock the entries are stamped with (seconds since 1/1/1970);
    // NULL for time(NULL). Tests set it for reproducible files.
    static U32 (*sClock)();
    static U32 now() { return sClock ? sClock() : (U32)time(NULL); }

    class Responder : public LLResponder
    {
    public:
        virtual void setData(U8* data, S32 datasize, S32 imagesize, S32 imageformat, bool imagelocal) = 0;
    };

    class ReadResponder : public Responder
    {
    public:
        ReadResponder();
        void setData(U8* data, S32 datasize, S32 imagesize, S32 imageformat, bool imagelocal);
        void setImage(LLImageFormatted* image) { mFormattedImage = image; }
    protected:
        LLPointer<LLImageFormatted> mFormattedImage;
        S32 mImageSize;
        bool mImageLocal;
    };

    class WriteResponder : public Responder
    {
        void setData(U8* data, S32 datasize, S32 imagesize, S32 imageformat, bool imagelocal)
        {
            // not used
        }
    };

    LLTextureCache(bool threaded);
    ~LLTextureCache();

    /*virtual*/ size_t update(F32 max_time_ms);

    void purgeCache(ELLPath location, bool remove_dir = true);
    void setReadOnly(bool read_only) ;
    S64 initCache(ELLPath location, S64 maxsize, bool texture_cache_mismatch);

    handle_t readFromCache(const std::string& local_filename, const LLUUID& id, S32 offset, S32 size,
                           ReadResponder* responder);

    handle_t readFromCache(const LLUUID& id, S32 offset, S32 size,
                           ReadResponder* responder);
    bool readComplete(handle_t handle, bool abort);
    handle_t writeToCache(const LLUUID& id, const U8* data, S32 datasize, S32 imagesize, LLPointer<LLImageRaw> rawimage, S32 discardlevel,
                          WriteResponder* responder);
    LLPointer<LLImageRaw> readFromFastCache(const LLUUID& id, S32& discardlevel);
    bool writeComplete(handle_t handle, bool abort = false);
    void prioritizeWrite(handle_t handle);

    bool removeFromCache(const LLUUID& id);

    // For LLTextureCacheWorker::Responder
    LLTextureCacheWorker* getReader(handle_t handle);
    LLTextureCacheWorker* getWriter(handle_t handle);
    void lockWorkers() { mWorkersMutex.lock(); }
    void unlockWorkers() { mWorkersMutex.unlock(); }

    // debug
    S32 getNumReads() { return static_cast<S32>(mReaders.size()); }
    S32 getNumWrites() { return static_cast<S32>(mWriters.size()); }
    S64Bytes getUsage();
    S64Bytes getMaxUsage();
    U32 getEntries();
    U32 getMaxEntries();
    bool isInCache(const LLUUID& id) ;
    bool isInLocal(const LLUUID& id) ; //not thread safe at the moment
    LLMutex* getFastCacheMutex() { return &mFastCacheMutex; }
protected:
    // Accessed by LLTextureCacheWorker
    std::string getLocalFileName(const LLUUID& id);
    std::string getTextureFileName(const LLUUID& id);
    void addCompleted(Responder* responder, bool success);
    VksTexCache* getCache() { return mCache; }

private:
    // Internal
    LLMutex mWorkersMutex;
    LLMutex mListMutex;
    // Batch guard for LLViewerTextureList::updateImagesLoadingFastCache
    // (upstream #5356). The fast cache itself is in vkstorm-assets, which
    // locks it per call; no worker takes this mutex, so the try-lock there
    // always succeeds and a busy fast cache blocks inside the read, as
    // before #5356.
    LLMutex mFastCacheMutex;

    typedef std::map<handle_t, LLTextureCacheWorker*> handle_map_t;
    handle_map_t mReaders;
    handle_map_t mWriters;

    typedef std::vector<handle_t> handle_list_t;
    handle_list_t mPrioritizeWriteList;

    typedef std::vector<std::pair<LLPointer<Responder>, bool> > responder_list_t;
    responder_list_t mCompletedList;

    VksTexCache* mCache;
};

extern const S32 TEXTURE_CACHE_ENTRY_SIZE;

#endif // LL_LLTEXTURECACHE_H
