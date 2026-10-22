/**
 * @file llstreamingaudio_vks.h
 * @brief Parcel music through the vkstorm-audio engine: in-process decoding,
 *        mixed on the device clock with sample-timed fades and speaker fill.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
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
 * $/LicenseInfo$
 */

#ifndef LL_STREAMINGAUDIO_VKS_H
#define LL_STREAMINGAUDIO_VKS_H

#include "stdtypes.h"
#include "llsd.h"
#include "llstreamingaudio.h"
#include "lltimer.h"

struct VksAudio;

// The audio engine's own streaming slot (createDefaultStreamingAudioImpl).
// The player (vkstorm-audio's stream module) connects, demuxes and decodes on
// its own threads; its output feeds the mixer's music bus, which applies the
// fade envelope, the user gain (volume^3 with a 50 ms ramp), the hard mute
// and the speaker fill.
class LLStreamingAudio_VKS : public LLStreamingAudioInterface
{
public:
    LLStreamingAudio_VKS();
    virtual ~LLStreamingAudio_VKS();

    /*virtual*/ void start(const std::string& url);
    /*virtual*/ void stop();
    /*virtual*/ void pause(int pause);
    /*virtual*/ void update();
    /*virtual*/ int isPlaying();
    /*virtual*/ void setGain(F32 vol);
    /*virtual*/ F32 getGain();
    /*virtual*/ std::string getURL();

    // Sample-timed fades on the mixer: a fade completes when its final
    // sample has passed the device clock
    /*virtual*/ bool hasAudioFade() const { return true; }
    /*virtual*/ bool beginAudioFade(F32 target, F32 duration);
    /*virtual*/ bool isAudioFadeComplete() const;
    /*virtual*/ AudioFadeResult getAudioFadeResult() const;
    /*virtual*/ void setAudioHardMute(bool muted);
    /*virtual*/ void setMusicSpatialLayout(S32 layout);

    /*virtual*/ LLSD getCurrentMetadata() const noexcept { return mMetadata; }

private:
    VksAudio* audio() const;
    bool ensurePlayer();

    std::string mURL;
    F32 mGain = 1.f;
    bool mHardMuted = false;
    S32 mSpatialLayout = 0;

    // A fade requested before a stream exists attaches to the next start
    bool mPendingFade = false;
    F32 mFadeTarget = 1.f;
    F32 mFadeDuration = 0.f;
    // The transition in flight (its serial), and when a pending one must
    // have completed before it counts as failed (the stream stalled)
    U32 mFadeSerial = 0;
    LLTimer mFadeTimer;
    F32 mFadeDeadline = 0.f;

    bool mPlayerCreated = false;
    S32 mLastState = -1;
    LLSD mMetadata;
};

#endif
