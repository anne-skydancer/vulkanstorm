/**
 * @file llaudio_parity_decode_adapter.cpp
 * @brief DECODE ADAPTER of INTEGRATION_TEST_llaudio_parity: one sound asset
 *        through the viewer's decode path (llaudiodecodemgr.cpp).
 *
 * The path drives the real translation unit: it is compiled into this file so
 * the test can reach the two functions the decode manager's work queue
 * lambdas call (beginDecodingAndWritingAudio on a "General" pool thread,
 * tryFinishAudio on "mainloop"), which take LLVorbisDecodeState, a class
 * defined only in that file. The test executable therefore links this copy
 * and never pulls llaudiodecodemgr.obj out of the llaudio library.
 *
 * Why not LLAudioDecodeMgr::addDecodeRequest + processQueue: addDecodeRequest
 * asks gAssetStorage->hasLocalAsset, and an LLAssetStorage can only be
 * constructed on a live LLMessageSystem, which opens a UDP socket and loads
 * the message template. Everything after that gate is the code below: the
 * same posting from "mainloop" to "General", the same LLFileSystem reads of
 * the asset cache through libvorbisfile callbacks, the same LLLFSThread
 * write of the .dsf into the sound cache, the same completion polling and
 * LLAudioData flags.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llaudiodecodemgr.cpp"

#include "llaudio_parity_adapter.h"
#include "llfile.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace vks_audio_parity
{
    const char* adapterDecodePath()
    {
        return "llaudiodecodemgr.cpp: beginDecodingAndWritingAudio on the General pool, tryFinishAudio on mainloop "
               "(libvorbisfile over LLFileSystem, LLLFSThread write)";
    }

    DecodeOutcome adapterDecodeAsset(const LLUUID& id)
    {
        DecodeOutcome out;
        LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
        LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
        llassert_always(main_queue && general_queue && gAudiop);

        // As LLAudioDecodeMgr::Impl::startMoreDecodes posts it.
        bool returned = false;
        LLPointer<LLVorbisDecodeState> state;
        std::atomic<double> decode_ms{0.0};
        const bool posted = main_queue->postTo(
            general_queue,
            [id, &decode_ms]()
            {
                const auto t0 = std::chrono::steady_clock::now();
                LLPointer<LLVorbisDecodeState> decode_state = beginDecodingAndWritingAudio(id);
                decode_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (!decode_state)
                {
                    if (gAudiop)
                        gAudiop->markSoundCorrupt(id);
                }
                return decode_state;
            },
            [&returned, &state](LLPointer<LLVorbisDecodeState> decode_state)
            {
                state = decode_state;
                returned = true;
            });
        llassert_always(posted);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!returned && std::chrono::steady_clock::now() < deadline)
        {
            main_queue->runPending();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        llassert_always(returned);
        out.began = state.notNull();
        out.decode_ms = decode_ms;

        // As enqueueFinishAudio and checkDecodesFinished poll it, once per
        // viewer frame; the frame also updates the LFS thread (which sleeps
        // paused until LLLFSThread::updateClass sees pending requests). A
        // failed decode (null state) never finishes: the manager keeps it
        // pending and the corrupt mark stops retries.
        if (out.began)
        {
            while (!tryFinishAudio(id, state) && std::chrono::steady_clock::now() < deadline)
            {
                LLLFSThread::updateClass(1);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            out.finished = std::chrono::steady_clock::now() < deadline;
        }

        const std::string dsf = gDirUtilp->getExpandedFilename(LL_PATH_FS_SOUND_CACHE, id.asString()) + ".dsf";
        out.dsf_present = gDirUtilp->fileExists(dsf);
        if (out.dsf_present)
        {
            llifstream in(dsf.c_str(), std::ios::binary);
            out.dsf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        out.asset_present = LLFileSystem::getExists(id, LLAssetType::AT_SOUND);
        if (LLAudioData* adp = gAudiop->getAudioData(id))
        {
            out.data_completed = adp->hasCompletedDecode();
            out.data_failed = adp->hasDecodeFailed();
            out.data_decoded = adp->hasDecodedData();
        }
        return out;
    }
}
