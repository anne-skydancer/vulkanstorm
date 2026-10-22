/**
 * @file llaudiodecodemgr.cpp
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#include "linden_common.h"

#include "llaudiodecodemgr.h"

#include "llaudioengine.h"
#include "lllfsthread.h"
#include "llfilesystem.h"
#include "llstring.h"
#include "lldir.h"
#include "llassetstorage.h"
#include "llrefcount.h"
#include "threadpool.h"
#include "workqueue.h"

#include "llvorbisencode.h"

// Ogg Vorbis decoding and the assembly of the decoded sound file are Rust
// (vkstorm-audio, rung six): bit-identical to libvorbisfile's ov_read output
// and to the WAV this file assembled around it. The work queues, the asset
// cache read and the sound cache write stay here.
#include "vkstorm_ffi.h"

#include <iterator>
#include <deque>

extern LLAudioEngine *gAudiop;


//////////////////////////////////////////////////////////////////////////////


class LLVorbisDecodeState : public LLThreadSafeRefCount
{
public:
    class WriteResponder : public LLLFSThread::Responder
    {
    public:
        WriteResponder(LLVorbisDecodeState* decoder) : mDecoder(decoder) {}
        ~WriteResponder() {}
        void completed(S32 bytes)
        {
            mDecoder->ioComplete(bytes);
        }
        LLPointer<LLVorbisDecodeState> mDecoder;
    };

    LLVorbisDecodeState(const LLUUID &uuid, const std::string &out_filename);

    bool initDecode();
    bool decodeSection(); // Return true if done.
    bool finishDecode();

    void flushBadFile();

    void ioComplete(S32 bytes)          { mBytesRead = bytes; }
    bool isValid() const                { return mValid; }
    bool isDone() const                 { return mDone; }
    const LLUUID &getUUID() const       { return mUUID; }

protected:
    virtual ~LLVorbisDecodeState();

    bool mValid;
    bool mDone;
    LLAtomicS32 mBytesRead;
    LLUUID mUUID;

    std::vector<U8> mWAVBuffer;
    std::string mOutFilename;
    LLLFSThread::handle_t mFileHandle;

    LLFileSystem *mInFilep;
    // The decoder's verdict (vks_audio_decode_asset): 0 decoded, 7 a decode
    // error in the stream, 8 no samples; the early rejections end initDecode
    S32 mDecodeResult;
    S64 mDecodeDetail;
};

namespace
{
    void take_vendor(void* user, const char* s, size_t len)
    {
        try
        {
            static_cast<std::string*>(user)->assign(s, len);
        }
        catch (...)
        {
        }
    }
}

LLVorbisDecodeState::LLVorbisDecodeState(const LLUUID &uuid, const std::string &out_filename)
{
    mDone = false;
    mValid = false;
    mBytesRead = -1;
    mUUID = uuid;
    mInFilep = NULL;
    mDecodeResult = 0;
    mDecodeDetail = 0;
    mOutFilename = out_filename;
    mFileHandle = LLLFSThread::nullHandle();
}

LLVorbisDecodeState::~LLVorbisDecodeState()
{
    if (!mDone)
    {
        delete mInFilep;
        mInFilep = NULL;
    }
}


bool LLVorbisDecodeState::initDecode()
{
    LL_DEBUGS("AudioEngine") << "Initing decode from vfile: " << mUUID << LL_ENDL;

    mInFilep = new LLFileSystem(mUUID, LLAssetType::AT_SOUND);
    if (!mInFilep || !mInFilep->getSize())
    {
        LL_WARNS("AudioEngine") << "unable to open vorbis source vfile for reading" << LL_ENDL;
        delete mInFilep;
        mInFilep = NULL;
        return false;
    }

    // The decoder reads the whole asset from memory (a sound asset is at
    // most a few hundred kilobytes of Ogg)
    std::vector<U8> asset;
    try
    {
        asset.resize(mInFilep->getSize());
    }
    catch (std::bad_alloc&)
    {
        LL_WARNS("AudioEngine") << "Out of memory when trying to alloc buffer: " << mInFilep->getSize() << LL_ENDL;
        delete mInFilep;
        mInFilep = NULL;
        return false;
    }
    S32 bytes_read = 0;
    if (mInFilep->read(asset.data(), (S32)asset.size()))  /*Flawfinder: ignore*/
    {
        bytes_read = mInFilep->getLastBytesRead();
    }
    asset.resize(llmax(bytes_read, 0));

    U8* wav = nullptr;
    size_t wav_len = 0;
    int64_t detail = 0;
    mDecodeResult = vks_audio_decode_asset(asset.data(), asset.size(), &wav, &wav_len, &detail);
    mDecodeDetail = detail;
    if (wav)
    {
        try
        {
            mWAVBuffer.assign(wav, wav + wav_len);
        }
        catch (std::bad_alloc&)
        {
            vks_audio_bytes_free(wav, wav_len);
            LL_WARNS("AudioEngine") << "Out of memory when trying to alloc buffer: " << wav_len << LL_ENDL;
            delete mInFilep;
            mInFilep = NULL;
            return false;
        }
        vks_audio_bytes_free(wav, wav_len);
    }

    switch (mDecodeResult)
    {
    case 0: // decoded
    case 7: // a decode error in the stream: decodeSection reports it
    case 8: // no samples: finishDecode reports it
        return true;
    case 1:
        LL_WARNS("AudioEngine") << "unable to open vorbis source vfile for reading" << LL_ENDL;
        delete mInFilep;
        mInFilep = NULL;
        return false;
    case 2:
        LL_WARNS("AudioEngine") << mDecodeDetail << " Input to vorbis decode does not appear to be an Ogg bitstream: " << mUUID << LL_ENDL;
        return false;
    default:
        break;
    }

    switch (mDecodeResult)
    {
    case 3:
        LL_WARNS("AudioEngine") << "Bad channel count: " << mDecodeDetail << LL_ENDL;
        break;
    case 4:
        LL_WARNS("AudioEngine") << "No default bitstream found" << LL_ENDL;
        break;
    case 5:
        LL_WARNS("AudioEngine") << "Illegal sample count: " << mDecodeDetail << LL_ENDL;
        break;
    case 6:
        LL_WARNS("AudioEngine") << "Illegal sample size: " << mDecodeDetail << LL_ENDL;
        break;
    default:
        break;
    }
    LL_WARNS("AudioEngine") << "Canceling initDecode. Bad asset: " << mUUID << LL_ENDL;
    std::string vendor;
    if (vks_audio_asset_vendor(asset.data(), asset.size(), take_vendor, &vendor) && !vendor.empty())
    {
        LL_WARNS("AudioEngine") << "Bad asset encoded by: " << vendor << LL_ENDL;
    }
    delete mInFilep;
    mInFilep = NULL;
    return false;
}

bool LLVorbisDecodeState::decodeSection()
{
    if (!mInFilep)
    {
        LL_WARNS("AudioEngine") << "No cache file to decode in vorbis!" << LL_ENDL;
        return true;
    }
    if (mDone)
    {
        return true;
    }

    if (mDecodeResult == 7)
    {
        /* error in the stream.  Not a problem, just reporting it in
           case we (the app) cares.  In this case, we don't. */
        LL_WARNS("AudioEngine") << "BAD vorbis decode in decodeSection." << LL_ENDL;
        mValid = false;
        mDone = true;
        return true;
    }

    // End of stream: initDecode decoded all of it
    mDone = true;
    mValid = true;
    return true;
}

bool LLVorbisDecodeState::finishDecode()
{
    if (!isValid())
    {
        LL_WARNS("AudioEngine") << "Bogus vorbis decode state for " << getUUID() << ", aborting!" << LL_ENDL;
        return true; // We've finished
    }

    if (mFileHandle == LLLFSThread::nullHandle())
    {
        // The RIFF and data lengths and the loop-point fade are part of the
        // decoder's output
        if (mDecodeResult == 8)
        {
            LL_WARNS("AudioEngine") << "BAD Vorbis decode in finishDecode!" << LL_ENDL;
            mValid = false;
            return true; // we've finished
        }
        mBytesRead = -1;
        mFileHandle = LLLFSThread::sLocal->write(mOutFilename, &mWAVBuffer[0], 0, static_cast<S32>(mWAVBuffer.size()),
                             new WriteResponder(this));
    }

    if (mFileHandle != LLLFSThread::nullHandle())
    {
        if (mBytesRead >= 0)
        {
            if (mBytesRead == 0)
            {
                LL_WARNS("AudioEngine") << "Unable to write file in LLVorbisDecodeState::finishDecode" << LL_ENDL;
                mValid = false;
                return true; // we've finished
            }
        }
        else
        {
            return false; // not done
        }
    }

    mDone = true;

    LL_DEBUGS("AudioEngine") << "Finished decode for " << getUUID() << LL_ENDL;

    return true;
}

void LLVorbisDecodeState::flushBadFile()
{
    if (mInFilep)
    {
        LL_WARNS("AudioEngine") << "Flushing bad vorbis file from cache for " << mUUID << LL_ENDL;
        mInFilep->remove();

        // <FS:ND> FIRE-15975; Delete the current file, or we might end with stale locks during the re-transfer
        delete mInFilep;
        mInFilep = NULL;
        // </FS:ND>
    }
}

//////////////////////////////////////////////////////////////////////////////

class LLAudioDecodeMgr::Impl
{
    friend class LLAudioDecodeMgr;
    Impl();
  public:

    void processQueue();

    void startMoreDecodes();
    void enqueueFinishAudio(const LLUUID &decode_id, LLPointer<LLVorbisDecodeState>& decode_state);
    void checkDecodesFinished();

  protected:
    std::deque<LLUUID> mDecodeQueue;
    std::map<LLUUID, LLPointer<LLVorbisDecodeState>> mDecodes;
};

LLAudioDecodeMgr::Impl::Impl()
{
}

// Returns the in-progress decode_state, which may be an empty LLPointer if
// there was an error and there is no more work to be done.
LLPointer<LLVorbisDecodeState> beginDecodingAndWritingAudio(const LLUUID &decode_id);

// Return true if finished
bool tryFinishAudio(const LLUUID &decode_id, LLPointer<LLVorbisDecodeState> decode_state);

void LLAudioDecodeMgr::Impl::processQueue()
{
    // First, check if any audio from in-progress decodes are ready to play. If
    // so, mark them ready for playback (or errored, in case of error).
    checkDecodesFinished();

    // Second, start as many decodes from the queue as permitted
    startMoreDecodes();
}

void LLAudioDecodeMgr::Impl::startMoreDecodes()
{
    llassert_always(gAudiop);

    LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
    // *NOTE: main_queue->postTo casts this refcounted smart pointer to a weak
    // pointer
    LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
    const LL::ThreadPool::ptr_t general_thread_pool = LL::ThreadPool::getInstance("General");
    llassert_always(main_queue);
    llassert_always(general_queue);
    llassert_always(general_thread_pool);
    // Set max decodes to double the thread count of the general work queue.
    // This ensures the general work queue is full, but prevents theoretical
    // buildup of buffers in memory due to disk writes once the
    // LLVorbisDecodeState leaves the worker thread (see
    // LLLFSThread::sLocal->write). This is probably as fast as we can get it
    // without modifying/removing LLVorbisDecodeState, at which point we should
    // consider decoding the audio during the asset download process.
    // -Cosmic,2022-05-11
    const size_t max_decodes = general_thread_pool->getWidth() * 2;

    while (!mDecodeQueue.empty() && mDecodes.size() < max_decodes)
    {
        const LLUUID decode_id = mDecodeQueue.front();
        mDecodeQueue.pop_front();

        // Don't decode the same file twice
        if (mDecodes.find(decode_id) != mDecodes.end())
        {
            continue;
        }
        if (gAudiop->hasDecodedFile(decode_id))
        {
            continue;
        }

        // Kick off a decode
        mDecodes[decode_id] = LLPointer<LLVorbisDecodeState>(NULL);
        bool posted = main_queue->postTo(
            general_queue,
            [decode_id]() // Work done on general queue
            {
                LLPointer<LLVorbisDecodeState> decode_state = beginDecodingAndWritingAudio(decode_id);

                if (!decode_state)
                {
                    if (gAudiop)
                        gAudiop->markSoundCorrupt(decode_id);

                    // Audio decode has errored
                    return decode_state;
                }

                // Disk write of decoded audio is now in progress off-thread
                return decode_state;
            },
            [decode_id, this](LLPointer<LLVorbisDecodeState> decode_state) // Callback to main thread
            mutable {
                if (!gAudiop)
                {
                    // There is no LLAudioEngine anymore. This might happen if
                    // an audio decode is enqueued just before shutdown.
                    return;
                }

                // At this point, we can be certain that the pointer to "this"
                // is valid because the lifetime of "this" is dependent upon
                // the lifetime of gAudiop.

                enqueueFinishAudio(decode_id, decode_state);
            });
        if (! posted)
        {
            // Shutdown
            // Consider making processQueue() do a cleanup instead
            // of starting more decodes
            LL_WARNS() << "Tried to start decoding on shutdown" << LL_ENDL;
        }
    }
}

LLPointer<LLVorbisDecodeState> beginDecodingAndWritingAudio(const LLUUID &decode_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEDIA;

    LL_DEBUGS() << "Decoding " << decode_id << " from audio queue!" << LL_ENDL;

    // <FS:Ansariel> Sound cache
    //std::string                    d_path       = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, decode_id.asString()) + ".dsf";
    std::string                    d_path       = gDirUtilp->getExpandedFilename(LL_PATH_FS_SOUND_CACHE, decode_id.asString()) + ".dsf";
    // </FS:Ansariel>
    LLPointer<LLVorbisDecodeState> decode_state = new LLVorbisDecodeState(decode_id, d_path);

    if (!decode_state->initDecode())
    {
        return NULL;
    }

    // Decode in a loop until we're done
    while (!decode_state->decodeSection())
    {
        // decodeSection does all of the work above
    }

    if (!decode_state->isDone())
    {
        // Decode stopped early, or something bad happened to the file
        // during decoding.
        LL_WARNS("AudioEngine") << decode_id << " has invalid vorbis data or decode has been canceled, aborting decode" << LL_ENDL;
        decode_state->flushBadFile();
        return NULL;
    }

    if (!decode_state->isValid())
    {
        // We had an error when decoding, abort.
        LL_WARNS("AudioEngine") << decode_id << " has invalid vorbis data, aborting decode" << LL_ENDL;
        decode_state->flushBadFile();
        return NULL;
    }

    // Kick off the writing of the decoded audio to the disk cache.
    // The receiving thread can then cheaply call finishDecode() again to check
    // if writing has finished. Someone has to hold on to the refcounted
    // decode_state to prevent it from getting destroyed during write.
    decode_state->finishDecode();

    return decode_state;
}

void LLAudioDecodeMgr::Impl::enqueueFinishAudio(const LLUUID &decode_id, LLPointer<LLVorbisDecodeState>& decode_state)
{
    // Assumed fast
    if (tryFinishAudio(decode_id, decode_state))
    {
        // Done early!
        auto decode_iter = mDecodes.find(decode_id);
        llassert(decode_iter != mDecodes.end());
        mDecodes.erase(decode_iter);
        return;
    }

    // Not done yet... enqueue it
    mDecodes[decode_id] = decode_state;
}

void LLAudioDecodeMgr::Impl::checkDecodesFinished()
{
    auto decode_iter = mDecodes.begin();
    while (decode_iter != mDecodes.end())
    {
        const LLUUID& decode_id = decode_iter->first;
        const LLPointer<LLVorbisDecodeState>& decode_state = decode_iter->second;
        if (tryFinishAudio(decode_id, decode_state))
        {
            decode_iter = mDecodes.erase(decode_iter);
        }
        else
        {
            ++decode_iter;
        }
    }
}

bool tryFinishAudio(const LLUUID &decode_id, LLPointer<LLVorbisDecodeState> decode_state)
{
    // decode_state is a file write in progress unless finished is true
    bool finished = decode_state && decode_state->finishDecode();
    if (!finished)
    {
        return false;
    }

    llassert_always(gAudiop);

    LLAudioData *adp = gAudiop->getAudioData(decode_id);
    if (!adp)
    {
        LL_WARNS("AudioEngine") << "Missing LLAudioData for decode of " << decode_id << LL_ENDL;
        return true;
    }

    bool valid = decode_state && decode_state->isValid();
    // Mark current decode finished regardless of success or failure
    adp->setHasCompletedDecode(true);
    // Flip flags for decoded data
    adp->setHasDecodeFailed(!valid);
    adp->setHasDecodedData(valid);
    // When finished decoding, there will also be a decoded wav file cached on
    // disk with the .dsf extension
    if (valid)
    {
        adp->setHasWAVLoadFailed(false);
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////////

LLAudioDecodeMgr::LLAudioDecodeMgr()
{
    mImpl = new Impl();
}

LLAudioDecodeMgr::~LLAudioDecodeMgr()
{
    delete mImpl;
    mImpl = nullptr;
}

void LLAudioDecodeMgr::processQueue()
{
    mImpl->processQueue();
}

bool LLAudioDecodeMgr::addDecodeRequest(const LLUUID &uuid)
{
    // <FS:ND> Protect against corrupted sounds. Just do a quit exit instead of trying to decode over and over.
    if (gAudiop && gAudiop->isCorruptSound(uuid))
        return false;
    // </FS:ND>

    if (gAudiop && gAudiop->hasDecodedFile(uuid))
    {
        // Already have a decoded version, don't need to decode it.
        LL_DEBUGS("AudioEngine") << "addDecodeRequest for " << uuid << " has decoded file already" << LL_ENDL;
        return true;
    }

    if (gAssetStorage->hasLocalAsset(uuid, LLAssetType::AT_SOUND))
    {
        // Just put it on the decode queue it if it's not already in the queue
        LL_DEBUGS("AudioEngine") << "addDecodeRequest for " << uuid << " has local asset file already" << LL_ENDL;
        if (std::find(mImpl->mDecodeQueue.begin(), mImpl->mDecodeQueue.end(), uuid) == mImpl->mDecodeQueue.end())
        {
            mImpl->mDecodeQueue.emplace_back(uuid);
        }
        return true;
    }

    LL_DEBUGS("AudioEngine") << "addDecodeRequest for " << uuid << " no file available" << LL_ENDL;
    return false;
}
