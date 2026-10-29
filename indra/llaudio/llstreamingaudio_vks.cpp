/**
 * @file llstreamingaudio_vks.cpp
 * @brief Parcel music through the vkstorm-audio engine.
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

#include "linden_common.h"

#include "llstreamingaudio_vks.h"
#include "llaudioengine_vks.h"

#include "lldir.h"
#include "vkstorm_ffi.h"

#include <cmath>

namespace
{
    // A pending transition that has not completed this long after its own
    // duration counts as failed: the stream stalled or ended before the
    // final sample was played (the VLC music path used the same 30 s bound)
    const F32 FADE_COMPLETION_GRACE = 30.f;

    void take_string(void* user, const char* s, size_t len)
    {
        try
        {
            static_cast<std::string*>(user)->assign(s, len);
        }
        catch (...)
        {
        }
    }

    struct MetadataEvent
    {
        bool seen = false;
        bool cleared = false;
        std::string artist;
        std::string title;
    };

    void take_metadata(void* user, int32_t kind, const char* artist, size_t artist_len,
                       const char* title, size_t title_len)
    {
        try
        {
            MetadataEvent* ev = static_cast<MetadataEvent*>(user);
            ev->seen = true;
            ev->cleared = kind == 0;
            ev->artist.assign(artist ? artist : "", artist ? artist_len : 0);
            ev->title.assign(title ? title : "", title ? title_len : 0);
        }
        catch (...)
        {
        }
    }

    // Where the FFmpeg libraries are staged next to the viewer.
    std::string ffmpeg_dir()
    {
#if LL_WINDOWS
        return gDirUtilp->getExecutableDir();
#else
        return gDirUtilp->getExecutableDir() + gDirUtilp->getDirDelimiter() + ".." +
               gDirUtilp->getDirDelimiter() + "lib";
#endif
    }
}

LLStreamingAudio_VKS::LLStreamingAudio_VKS()
{
}

LLStreamingAudio_VKS::~LLStreamingAudio_VKS()
{
    if (VksAudio* a = audio())
    {
        vks_music_stop(a);
    }
}

VksAudio* LLStreamingAudio_VKS::audio() const
{
    // The engine object outlives its output: music keeps its state while the
    // effects device restarts or is unavailable
    return LLAudioEngine_VKS::getAudioObject();
}

bool LLStreamingAudio_VKS::ensurePlayer()
{
    VksAudio* a = audio();
    if (!a)
    {
        return false;
    }
    if (!mPlayerCreated)
    {
        const std::string ca_file = gDirUtilp->getCAFile();
        const std::string ffmpeg = ffmpeg_dir();
        mPlayerCreated = vks_music_create(a, ca_file.c_str(), nullptr, ffmpeg.c_str()) != 0;
        if (mPlayerCreated)
        {
            vks_music_set_volume(a, llclamp(mGain, 0.f, 1.f));
            vks_music_set_muted(a, mHardMuted);
            vks_music_set_spatial_layout(a, mSpatialLayout);
        }
    }
    return mPlayerCreated;
}

void LLStreamingAudio_VKS::start(const std::string& url)
{
    if (!ensurePlayer())
    {
        return;
    }
    VksAudio* a = audio();

    if (url.empty())
    {
        LL_INFOS() << "setting stream to NULL" << LL_ENDL;
        stop();
        return;
    }

    LL_INFOS() << "Starting internet stream: " << url << LL_ENDL;
    mURL = url; // keep original url here for comparison purposes

    // Where the new stream's envelope starts, and the transition it carries:
    // - a fade requested before any stream existed attaches to this one,
    //   from exactly zero (the login hold and every faded start);
    // - a stream replacing one whose transition was requested inherits it,
    //   so the replacement is never left without a transition (which would
    //   read as cancelled and hard-stop it): a hold or fade-out keeps the
    //   new stream at zero, an unfinished fade-in restarts from zero, a
    //   finished fade-in plays at full level. This is the case of another
    //   parcel URL arriving during the login hold;
    // - otherwise (fades off, or none requested) it plays at full level.
    bool fade = false;
    F32 envelope = 1.f;
    F32 target = mFadeTarget;
    F32 duration = mFadeDuration;
    if (mPendingFade)
    {
        fade = true;
        envelope = 0.f;
    }
    else if (mFadeSerial)
    {
        fade = true;
        const bool finished = vks_music_transition_state(a, mFadeSerial) == 2;
        if (target == 0.f)
        {
            envelope = 0.f;
            duration = 0.f;
        }
        else if (finished)
        {
            envelope = target;
            duration = 0.f;
        }
        else
        {
            envelope = 0.f;
        }
    }
    mPendingFade = false;
    mFadeSerial = 0;
    vks_music_set_envelope(a, envelope);
    // The player trims the URL and drops a " label" after it, as FMOD and
    // the media plugin path did
    vks_music_start(a, url.c_str());
    if (fade && !beginAudioFade(target, duration))
    {
        LL_WARNS() << "Could not carry the music transition to the new stream" << LL_ENDL;
    }
    LL_INFOS() << "Playing stream..." << LL_ENDL;
}

void LLStreamingAudio_VKS::stop()
{
    mPendingFade = false;
    mFadeSerial = 0;
    LL_INFOS() << "Stopping internet stream." << LL_ENDL;
    if (VksAudio* a = audio())
    {
        vks_music_stop(a);
    }
    mURL.clear();
    // Drain the player's metadata events: the reset reaches the listeners
    update();
}

void LLStreamingAudio_VKS::pause(int pause)
{
    VksAudio* a = audio();
    if (!a || !mPlayerCreated)
    {
        return;
    }
    if (pause)
    {
        LL_INFOS() << "Pausing internet stream." << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "Unpausing internet stream." << LL_ENDL;
    }
    vks_music_pause(a, pause);
}

void LLStreamingAudio_VKS::update()
{
    VksAudio* a = audio();
    if (!a || !mPlayerCreated)
    {
        return;
    }

    MetadataEvent ev;
    while (vks_music_poll_metadata(a, take_metadata, &ev))
    {
        if (!ev.seen)
        {
            continue;
        }
        if (ev.cleared)
        {
            // The previous stream's information no longer applies
            mMetadata = LLSD::emptyMap();
        }
        else
        {
            mMetadata = LLSD::emptyMap();
            mMetadata["ARTIST"] = ev.artist;
            mMetadata["TITLE"] = ev.title;
            LL_INFOS("StreamMetadata") << "Stream metadata changed: artist='" << ev.artist
                                       << "' title='" << ev.title << "'" << LL_ENDL;
        }
        mMetadataUpdateSignal(mMetadata);
        ev = MetadataEvent();
    }

    std::string reason;
    const S32 state = vks_music_state(a, take_string, &reason);
    if (state != mLastState)
    {
        if (state == 6)
        {
            LL_WARNS() << "Internet stream failed: " << reason << LL_ENDL;
        }
        mLastState = state;
    }
}

int LLStreamingAudio_VKS::isPlaying()
{
    VksAudio* a = audio();
    if (!a || !mPlayerCreated)
    {
        return 0; // stopped
    }
    return vks_music_is_playing(a);
}

void LLStreamingAudio_VKS::setGain(F32 vol)
{
    mGain = vol;
    if (VksAudio* a = audio())
    {
        if (mPlayerCreated)
        {
            vks_music_set_volume(a, llclamp(vol, 0.f, 1.f));
        }
    }
}

F32 LLStreamingAudio_VKS::getGain()
{
    return mGain;
}

std::string LLStreamingAudio_VKS::getURL()
{
    return mURL;
}

bool LLStreamingAudio_VKS::beginAudioFade(F32 target, F32 duration)
{
    if (!std::isfinite(target) || !std::isfinite(duration) ||
        target < 0.f || target > 1.f || duration < 0.f || duration > 60.f)
    {
        return false;
    }
    mFadeTarget = target;
    mFadeDuration = duration;
    VksAudio* a = audio();
    if (!a || !mPlayerCreated || mURL.empty())
    {
        // Applied when the next stream starts
        mPendingFade = true;
        return true;
    }
    const U32 serial = vks_music_transition(a, target, duration);
    if (!serial)
    {
        return false;
    }
    mFadeSerial = serial;
    mFadeTimer.reset();
    mFadeDeadline = duration + FADE_COMPLETION_GRACE;
    return true;
}

bool LLStreamingAudio_VKS::isAudioFadeComplete() const
{
    return getAudioFadeResult() == AudioFadeResult::Complete;
}

LLStreamingAudioInterface::AudioFadeResult LLStreamingAudio_VKS::getAudioFadeResult() const
{
    VksAudio* a = audio();
    if (!a || !mPlayerCreated || mURL.empty() || !mFadeSerial)
    {
        return mPendingFade ? AudioFadeResult::Pending : AudioFadeResult::Cancelled;
    }

    switch (vks_music_transition_state(a, mFadeSerial))
    {
    case 2:
        return AudioFadeResult::Complete;
    case 1:
    {
        // The final sample can never play once the stream failed, nor once
        // a finished stream's last frames have played out (a second after
        // the fade's own duration), nor while it stalls past the grace period
        const S32 state = vks_music_state(a, nullptr, nullptr);
        const F32 elapsed = mFadeTimer.getElapsedTimeF32();
        if (state == 6 || (state == 5 && elapsed > mFadeDuration + 1.f) || elapsed > mFadeDeadline)
        {
            return AudioFadeResult::Failed;
        }
        return AudioFadeResult::Pending;
    }
    default:
        return AudioFadeResult::Cancelled;
    }
}

void LLStreamingAudio_VKS::setAudioHardMute(bool muted)
{
    mHardMuted = muted;
    if (VksAudio* a = audio())
    {
        if (mPlayerCreated)
        {
            vks_music_set_muted(a, muted);
        }
    }
}

void LLStreamingAudio_VKS::setMusicSpatialLayout(S32 layout)
{
    if (layout == mSpatialLayout)
    {
        return;
    }
    mSpatialLayout = layout;
    if (VksAudio* a = audio())
    {
        if (mPlayerCreated && !vks_music_set_spatial_layout(a, layout))
        {
            LL_WARNS() << "Unsupported FSMusicSpatialSound value " << layout << LL_ENDL;
        }
    }
}
