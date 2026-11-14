/**
 * @file llaudio_parity_vks_adapter.cpp
 * @brief ENGINE ADAPTER of INTEGRATION_TEST_llaudio_parity for the
 *        vkstorm-audio engine (LLAudioEngine_VKS), compared against the
 *        corpora recorded from LLAudioEngine_SoLoud before the port.
 *
 * The engine runs on vkstorm-audio's null device: no thread, no hardware;
 * the test pulls every frame as one device callback would. The corpora were
 * recorded at 44100 Hz (SoLoud's null driver rate), so the null device runs
 * at that rate. The null device hands back the mixer's output unclamped, as
 * SoLoud's Soloud::mix did.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#include "linden_common.h"
#include "llaudioengine_vks.h"
#include "llaudio_parity_adapter.h"
#include "vkstorm_ffi.h"

#include <sstream>

// Friend of LLAudioEngine_VKS: opens the null device instead of hardware.
struct LLVKSAudioTestAccess
{
    static bool init(LLAudioEngine_VKS& engine, unsigned int rate, unsigned int channels)
    {
        engine.LLAudioEngine::init(nullptr, "llaudio parity");
        return engine.initNull(rate, channels);
    }
};

namespace vks_audio_parity
{
    LLAudioEngine* adapterCreateEngine(unsigned channels, unsigned rate, EngineInfo& info)
    {
        auto* engine = new LLAudioEngine_VKS();
        gAudiop = engine;
        if (!LLVKSAudioTestAccess::init(*engine, rate, channels))
        {
            adapterDestroyEngine(engine);
            return nullptr;
        }
        VksAudioFormat format{};
        vks_audio_format(LLAudioEngine_VKS::getAudio(), &format, nullptr, nullptr);
        info.rate = format.rate;
        info.channels = format.channels;
        info.name = engine->getDriverName(true);
        if (info.rate != rate || info.channels != channels)
        {
            adapterDestroyEngine(engine);
            return nullptr;
        }
        return engine;
    }

    void adapterRender(LLAudioEngine*, float* out, unsigned frames)
    {
        vks_audio_render_null(LLAudioEngine_VKS::getAudio(), out, frames);
    }

    unsigned adapterMaxRenderFrames()
    {
        // The device contract: at most 1024 frames per callback (the bound
        // the viewer's SoLoud package put on miniaudio's callbacks).
        return 1024;
    }

    void adapterIdle(LLAudioEngine* engine)
    {
        // LLAudioEngine_VKS::idle adds the output device list refresh and the
        // device state poll to the base update.
        engine->LLAudioEngine::idle();
    }

    int adapterVoiceCount(LLAudioEngine*)
    {
        VksAudio* audio = LLAudioEngine_VKS::getAudio();
        return audio ? (int)vks_audio_voice_count(audio) : -1;
    }

    void adapterDestroyEngine(LLAudioEngine* engine)
    {
        if (engine)
        {
            engine->shutdown();
            delete engine;
        }
        gAudiop = nullptr;
    }
}
