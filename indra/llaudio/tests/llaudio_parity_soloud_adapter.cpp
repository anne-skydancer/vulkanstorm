/**
 * @file llaudio_parity_soloud_adapter.cpp
 * @brief ENGINE ADAPTER of INTEGRATION_TEST_llaudio_parity for the SoLoud
 *        backend (LLAudioEngine_SoLoud), the C++ engine the corpora were
 *        recorded from before the port of the engine to Rust.
 *
 * The null driver is SoLoud's own headless backend: no thread, no device; the
 * test pulls every frame with Soloud::mix. SoLoud picks 44100 Hz for it
 * (Soloud::init with AUTO), which is the rate the corpora were recorded at.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llaudioengine_soloud.h"
#include "llaudio_parity_adapter.h"

#include <sstream>

// Friend of LLAudioEngine_SoLoud: opens the null backend instead of miniaudio.
struct LLSoLoudTestAccess
{
    static bool init(LLAudioEngine_SoLoud& engine, unsigned int channels)
    {
        engine.LLAudioEngine::init(nullptr, "llaudio parity");
        engine.sSoloud = new SoLoud::Soloud();
        return engine.startBackend(SoLoud::Soloud::NULLDRIVER, channels);
    }
};

namespace vks_audio_parity
{
    LLAudioEngine* adapterCreateEngine(unsigned channels, unsigned rate, EngineInfo& info)
    {
        auto* engine = new LLAudioEngine_SoLoud();
        gAudiop = engine;
        if (!LLSoLoudTestAccess::init(*engine, channels))
        {
            adapterDestroyEngine(engine);
            return nullptr;
        }
        SoLoud::Soloud* soloud = LLAudioEngine_SoLoud::getSoloud();
        info.rate = soloud->getBackendSamplerate();
        info.channels = soloud->getBackendChannels();
        std::ostringstream name;
        name << "SoLoud " << soloud->getVersion() << " " << ll_safe_string(soloud->getBackendString())
             << " buffer " << soloud->getBackendBufferSize();
        info.name = name.str();
        if (info.rate != rate || info.channels != channels)
        {
            adapterDestroyEngine(engine);
            return nullptr;
        }
        return engine;
    }

    void adapterRender(LLAudioEngine*, float* out, unsigned frames)
    {
        LLAudioEngine_SoLoud::getSoloud()->mix(out, frames);
    }

    unsigned adapterMaxRenderFrames()
    {
        // Soloud::mix renders into a scratch buffer of max(4096, buffer size)
        // frames; the null driver's buffer is 2048 frames.
        return 2048;
    }

    void adapterIdle(LLAudioEngine* engine)
    {
        // LLAudioEngine_SoLoud::idle adds the output device list refresh
        // (miniaudio enumeration every 5 s) to the base update.
        engine->LLAudioEngine::idle();
    }

    int adapterVoiceCount(LLAudioEngine*)
    {
        // getVoiceCount only counts; getActiveVoiceCount would recompute the
        // active set ahead of the mixer and is not read for that reason.
        SoLoud::Soloud* soloud = LLAudioEngine_SoLoud::getSoloud();
        return soloud ? (int)soloud->getVoiceCount() : -1;
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
