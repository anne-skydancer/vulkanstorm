/**
 * @file llaudio_parity_adapter.h
 * @brief The engine-specific seam of INTEGRATION_TEST_llaudio_parity.
 *
 * Everything else in the parity test drives the audio engine through the C++
 * interfaces that survive the port of the engine to Rust (vkstorm-rust rung
 * six): LLAudioEngine, LLAudioSource, LLAudioChannel, LLAudioBuffer::loadWAV,
 * LLAudioData, LLListener through LLAudioEngine::setListener. Only the
 * functions declared here know which backend is under test:
 *
 *  - ENGINE ADAPTER (llaudio_parity_vks_adapter.cpp; the corpora were recorded
 *    through llaudio_parity_soloud_adapter.cpp before the port): create the
 *    backend engine on a headless null output device with N channels at a
 *    given rate, render frames into a float buffer as one device callback,
 *    run the viewer's per-frame engine update, report voice counts, shut the
 *    engine down.
 *  - DECODE ADAPTER (llaudio_parity_decode_adapter.cpp today): run one sound
 *    asset from the asset cache through the viewer's decode path, on the
 *    viewer's work queues, and return what it wrote to the sound cache.
 *
 * Switching the test to another engine means providing these functions for
 * it, nothing else.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLAUDIO_PARITY_ADAPTER_H
#define LL_LLAUDIO_PARITY_ADAPTER_H

#include "llaudioengine.h"
#include "lluuid.h"

#include <string>
#include <vector>

namespace vks_audio_parity
{
    // ===================== ENGINE ADAPTER (engine-specific) =====================

    struct EngineInfo
    {
        std::string name;       // backend description, recorded with the corpus
        unsigned rate = 0;      // output rate the null device runs at
        unsigned channels = 0;  // output channels of the null device
    };

    // Creates the backend engine on a headless null output device with
    // `channels` interleaved output channels at `rate` Hz and sets gAudiop.
    // Returns nullptr (and leaves gAudiop null) if the backend cannot open
    // such a device; `info` describes the device actually opened.
    LLAudioEngine* adapterCreateEngine(unsigned channels, unsigned rate, EngineInfo& info);

    // Mixes `frames` frames (at most adapterMaxRenderFrames()) into `out`,
    // interleaved, frames * channels floats, as one device callback would.
    void adapterRender(LLAudioEngine* engine, float* out, unsigned frames);

    // The largest frame count one adapterRender call accepts.
    unsigned adapterMaxRenderFrames();

    // The viewer's per-frame engine update (LLAudioEngine::idle) without the
    // backend's output device polling, which depends on the machine.
    void adapterIdle(LLAudioEngine* engine);

    // Voices the backend currently holds (playing, paused, virtual or fading
    // out), or -1 if the backend has no such count.
    int adapterVoiceCount(LLAudioEngine* engine);

    // Shuts the engine down, deletes it and clears gAudiop.
    void adapterDestroyEngine(LLAudioEngine* engine);

    // ===================== DECODE ADAPTER (decode-path-specific) =====================

    struct DecodeOutcome
    {
        bool began = false;            // the decode produced a state to finish (no early failure)
        bool finished = false;         // the finish step reported completion
        bool dsf_present = false;      // <sound cache>/<uuid>.dsf exists afterwards
        std::vector<U8> dsf;           // its bytes
        bool asset_present = false;    // the encoded asset is still in the asset cache
        bool data_completed = false;   // LLAudioData::hasCompletedDecode
        bool data_failed = false;      // LLAudioData::hasDecodeFailed
        bool data_decoded = false;     // LLAudioData::hasDecodedData
        double decode_ms = 0.0;        // the decode step alone (open, decode, WAV assembly), without
                                       // the queue hops and the disk write; for the timing baseline
    };

    // Decodes the sound asset `id`, already written to the asset cache,
    // through the viewer's decode path; gAudiop must be set.
    DecodeOutcome adapterDecodeAsset(const LLUUID& id);

    // What the decode adapter drives, recorded with the corpus.
    const char* adapterDecodePath();
}

#endif
