/**
* @file media_plugin_libvlc.cpp
* @brief LibVLC plugin for LLMedia API plugin system
*
* @cond
* $LicenseInfo:firstyear=2008&license=viewerlgpl$
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
* @endcond
*/

#include "linden_common.h"

#include "llgl.h"
#include "llplugininstance.h"
#include "llpluginmessage.h"
#include "llpluginmessageclasses.h"
#include "media_plugin_base.h"

#if defined(_MSC_VER)
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#include "vlc/vlc.h"
#include "vlc/libvlc_version.h"

#include <atomic>
#include <cmath>
#include <charconv>
#include <chrono>

#if LL_WINDOWS
#include "llvlcspeakerfillconfig.h"
// needed for waveOut call - see below for description
#include <mmsystem.h>
#endif

////////////////////////////////////////////////////////////////////////////////
//
class MediaPluginLibVLC :
    public MediaPluginBase
{
public:
    MediaPluginLibVLC(LLPluginInstance::sendMessageFunction host_send_func, void *host_user_data);
    ~MediaPluginLibVLC();

    /*virtual*/ void receiveMessage(const char* message_string);

private:
    bool init();

    void initVLC();
    void playMedia();
    void resetVLC();
    void stopPlayer();
    void idle();
    void setVolume(const F64 volume);
    void setVolumeVLC();
    void updateTitle(const char* title);
    void updateStreamMetadata(int meta_type);

    std::string mLastMetadataSent;

    static void* lock(void* data, void** p_pixels);
    static void unlock(void* data, void* id, void* const* raw_pixels);
    static void display(void* data, void* id);

    /*virtual*/ void setDirty(int left, int top, int right, int bottom) /* override, but that is not supported in gcc 4.6 */;
    void setDurationDirty();

    static void eventCallbacks(const libvlc_event_t* event, void* ptr);

    enum EventBits : unsigned
    {
        EventPlaying = 1, EventTime = 2, EventDuration = 4, EventTitle = 8,
        EventNowPlaying = 16, EventArtist = 32, EventMetaTitle = 64, EventEnd = 128
    };
    std::atomic<unsigned> mEvents{0};
    std::atomic<EStatus> mEventStatus{STATUS_NONE};
    std::atomic<std::int64_t> mEventTime{0};
    std::atomic<std::int64_t> mEventDuration{0};
    std::atomic<float> mBuffering{100.f};
    std::atomic<bool> mFrameReady{false};
    unsigned mAttachedPlayerEvents = 0;
    bool mAttachedMediaEvent = false;

    libvlc_instance_t* mLibVLC;
    libvlc_media_t* mLibVLCMedia;
    libvlc_media_player_t* mLibVLCMediaPlayer;

    struct mLibVLCContext
    {
        unsigned char* texture_pixels;
        libvlc_media_player_t* mp;
        MediaPluginLibVLC* parent;
    };
    struct mLibVLCContext mLibVLCCallbackContext;

    std::string mURL;
    S32 mMusicSpeakerFill = 0;
    bool mMusic = false;
#if LL_WINDOWS
    void receiveNativeMusic(const LLPluginMessage& message);
    void idleNativeMusic();
    void nativeMusicState(const std::string& state, const std::string& detail, std::uint32_t serial = 0);
    std::uint64_t mNativeGeneration = 0;
    std::uint32_t mNativeSerial = 0;
    std::uint32_t mNativeAcknowledged = 0;
    float mNativeInitial = 1.f;
    float mNativeTarget = 1.f;
    float mNativeDuration = 0.f;
    bool mNativeMute = false;
    bool mNativeConfigured = false;
    std::string mNativeFailure;
    std::string mNativeLastState;
    std::chrono::steady_clock::time_point mNativeFadeStarted{};
#endif
    F64 mCurVolume;

    bool mIsLooping;

    F64 mCurTime;
    F64 mDuration;
    EStatus mVlcStatus;
};

////////////////////////////////////////////////////////////////////////////////
//
MediaPluginLibVLC::MediaPluginLibVLC(LLPluginInstance::sendMessageFunction host_send_func, void *host_user_data) :
MediaPluginBase(host_send_func, host_user_data)
{
    mTextureWidth = 0;
    mTextureHeight = 0;
    mWidth = 0;
    mHeight = 0;
    mDepth = 4;
    mPixels = 0;

    mLibVLC = 0;
    mLibVLCMedia = 0;
    mLibVLCMediaPlayer = 0;

    mCurVolume = 0.0;

    mIsLooping = false;

    mCurTime = 0.0;
    mDuration = 0.0;

    mURL = std::string();

    mVlcStatus = STATUS_NONE;
    setStatus(STATUS_NONE);
}

////////////////////////////////////////////////////////////////////////////////
//
MediaPluginLibVLC::~MediaPluginLibVLC()
{
    resetVLC();
}

/////////////////////////////////////////////////////////////////////////////////
//
void* MediaPluginLibVLC::lock(void* data, void** p_pixels)
{
    struct mLibVLCContext* context = (mLibVLCContext*)data;

    *p_pixels = context->texture_pixels;

    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::unlock(void* data, void* id, void* const* raw_pixels)
{
    // nothing to do here for the moment
    // we *could* modify pixels here to, for example, Y flip, but this is done with
    // a VLC video filter transform.
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::display(void* data, void* id)
{
    struct mLibVLCContext* context = (mLibVLCContext*)data;

    context->parent->mFrameReady.store(true, std::memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::initVLC()
{
    char const* vlc_argv[] =
    {
        "--no-xlib",
        "--video-filter=transform{type=vflip}",  // MAINT-6578 Y flip textures in plugin vs client
#if defined(__FreeBSD__)
        // FreeBSD's VLC build ships only the OSS audio output (no PulseAudio
        // module), so pin it explicitly; it coexists with the viewer's own OSS
        // audio engine via the kernel's vchan mixing.
        "--aout=oss",
#endif
    };

#if LL_DARWIN
    setenv("VLC_PLUGIN_PATH", "./plugins", 1);
#endif

    int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
    mLibVLC = libvlc_new(vlc_argc, vlc_argv);

    if (!mLibVLC)
    {
        mEventStatus.store(STATUS_ERROR);
        setStatus(STATUS_ERROR);
    }
}

namespace
{
const libvlc_event_e playerEvents[] =
{
    libvlc_MediaPlayerOpening, libvlc_MediaPlayerPlaying, libvlc_MediaPlayerPaused,
    libvlc_MediaPlayerStopped, libvlc_MediaPlayerEndReached,
    libvlc_MediaPlayerEncounteredError, libvlc_MediaPlayerTimeChanged,
    libvlc_MediaPlayerPositionChanged, libvlc_MediaPlayerLengthChanged,
    libvlc_MediaPlayerTitleChanged, libvlc_MediaPlayerBuffering
};
}

void MediaPluginLibVLC::stopPlayer()
{
    if (mLibVLCMediaPlayer)
    {
        auto* events = libvlc_media_player_event_manager(mLibVLCMediaPlayer);
        if (events)
        {
            unsigned index = 0;
            for (const auto type : playerEvents)
            {
                if (mAttachedPlayerEvents & (1u << index)) libvlc_event_detach(events, type, eventCallbacks, this);
                ++index;
            }
        }
        if (mLibVLCMedia && mAttachedMediaEvent)
        {
            auto* metadata = libvlc_media_event_manager(mLibVLCMedia);
            if (metadata) libvlc_event_detach(metadata, libvlc_MediaMetaChanged, eventCallbacks, this);
        }
        libvlc_media_player_stop(mLibVLCMediaPlayer);
        libvlc_media_player_release(mLibVLCMediaPlayer);
        mLibVLCMediaPlayer = nullptr;
    }
    if (mLibVLCMedia)
    {
        libvlc_media_release(mLibVLCMedia);
        mLibVLCMedia = nullptr;
    }
    mLibVLCCallbackContext = {};
    mAttachedPlayerEvents = 0;
    mAttachedMediaEvent = false;
    mFrameReady.store(false);
    mEvents.store(0);
    mEventStatus.store(STATUS_DONE);
    mVlcStatus = STATUS_DONE;
    mLastMetadataSent.clear();
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::resetVLC()
{
    stopPlayer();
    if (mLibVLC) libvlc_release(mLibVLC);
    mLibVLC = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// *virtual*
void MediaPluginLibVLC::setDirty(int left, int top, int right, int bottom)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "updated");

    message.setValueS32("left", left);
    message.setValueS32("top", top);
    message.setValueS32("right", right);
    message.setValueS32("bottom", bottom);

    message.setValueReal("current_time", mCurTime);
    message.setValueReal("duration", mDuration);
    message.setValueReal("current_rate", 1.0f);

    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
// *virtual*
void MediaPluginLibVLC::setDurationDirty()
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "updated");

    message.setValueReal("current_time", mCurTime);
    message.setValueReal("duration", mDuration);
    message.setValueReal("current_rate", 1.0f);

    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::eventCallbacks(const libvlc_event_t* event, void* ptr)
{
    MediaPluginLibVLC* parent = (MediaPluginLibVLC*)ptr;
    if (parent == 0)
    {
        return;
    }

    unsigned flags = 0;
    switch (event->type)
    {
    case libvlc_MediaPlayerOpening:
        parent->mEventStatus.store(STATUS_LOADING);
        break;

    case libvlc_MediaPlayerPlaying:
        parent->mEventStatus.store(STATUS_PLAYING);
        flags = EventPlaying;
        break;

    case libvlc_MediaPlayerPaused:
        parent->mEventStatus.store(STATUS_PAUSED);
        break;

    case libvlc_MediaPlayerStopped:
        parent->mEventStatus.store(STATUS_DONE);
        break;

    case libvlc_MediaPlayerEndReached:
        parent->mEventStatus.store(STATUS_DONE);
        flags = EventEnd;
        break;

    case libvlc_MediaPlayerEncounteredError:
        parent->mEventStatus.store(STATUS_ERROR);
        break;

    case libvlc_MediaPlayerTimeChanged:
        parent->mEventTime.store(event->u.media_player_time_changed.new_time);
        flags = EventTime;
        break;

    case libvlc_MediaPlayerBuffering:
        parent->mBuffering.store(event->u.media_player_buffering.new_cache);
        break;

    case libvlc_MediaPlayerLengthChanged:
        parent->mEventDuration.store(event->u.media_player_length_changed.new_length);
        flags = EventDuration;
        break;

    case libvlc_MediaPlayerTitleChanged:
        flags = EventTitle;
        break;
    case libvlc_MediaMetaChanged:
    {
        const auto meta = event->u.media_meta_changed.meta_type;
        if (meta == libvlc_meta_NowPlaying) flags = EventNowPlaying;
        else if (meta == libvlc_meta_Title) flags = EventMetaTitle;
        else if (meta == libvlc_meta_Artist) flags = EventArtist;
        break;
    }
    default: break;
    }
    parent->mEvents.fetch_or(flags, std::memory_order_release);
}

void MediaPluginLibVLC::idle()
{
    const auto flags = mEvents.exchange(0, std::memory_order_acq_rel);
    mVlcStatus = mEventStatus.load();
    if (flags & EventPlaying)
    {
        if (mLibVLCMedia) mDuration = libvlc_media_get_duration(mLibVLCMedia) / 1000.;
        setVolumeVLC();
    }
    if (flags & EventTime) mCurTime = mEventTime.load() / 1000.;
    if (flags & EventDuration) mDuration = mEventDuration.load() / 1000.;
    if (flags & EventEnd) mCurTime = mDuration;
    if (flags & (EventPlaying | EventTime | EventDuration | EventEnd)) setDurationDirty();
    if ((flags & EventTitle) && mLibVLCMedia)
    {
        char* title = libvlc_media_get_meta(mLibVLCMedia, libvlc_meta_Title);
        if (title) { updateTitle(title); libvlc_free(title); }
    }
    if (flags & EventNowPlaying) updateStreamMetadata(libvlc_meta_NowPlaying);
    if (flags & EventMetaTitle) updateStreamMetadata(libvlc_meta_Title);
    if (flags & EventArtist) updateStreamMetadata(libvlc_meta_Artist);
    if (mFrameReady.exchange(false, std::memory_order_acq_rel)) setDirty(0, 0, mWidth, mHeight);
#if LL_WINDOWS
    idleNativeMusic();
#endif
    setStatus(mVlcStatus);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::playMedia()
{
    if (mURL.empty() || !mLibVLC)
    {
        return;
    }

    // A new call to play the media is received after the initial one. Typically
    // this is due to a size change request either as the media naturally resizes
    // to the size of the prim container, or else, as a 2D window is resized by the
    // user.  Stopping the media, helps avoid a race condition where the media pixel
    // buffer size is out of sync with the declared size (width/height) for a frame
    // or two and the plugin crashes as VLC tries to decode a frame into unallocated
    // memory.
    stopPlayer();
    mBuffering.store(100.f);
    mEventStatus.store(STATUS_LOADING);
    mVlcStatus = STATUS_LOADING;

    mLibVLCMedia = libvlc_media_new_location(mLibVLC, mURL.c_str());
    if (!mLibVLCMedia)
    {
        mEventStatus.store(STATUS_ERROR);
        setStatus(STATUS_ERROR);
        return;
    }

#if LL_WINDOWS
    if (mMusic)
    {
        libvlc_media_add_option(mLibVLCMedia, ":no-video");
    }
#endif
    mLibVLCMediaPlayer = libvlc_media_player_new_from_media(mLibVLCMedia);
    if (!mLibVLCMediaPlayer)
    {
        libvlc_media_release(mLibVLCMedia);
        mLibVLCMedia = nullptr;
        mEventStatus.store(STATUS_ERROR);
        setStatus(STATUS_ERROR);
        return;
    }

#if LL_WINDOWS
    if (mMusic &&
        (!configureMusicGain(mLibVLCMediaPlayer, mMusicSpeakerFill, static_cast<float>(mCurVolume)) ||
         !configureMusicFade(mLibVLCMediaPlayer, mNativeConfigured ? mNativeInitial : 1.f) ||
         (mNativeConfigured && mNativeSerial &&
          !setMusicFade(mLibVLCMediaPlayer, mNativeTarget, mNativeDuration, mNativeSerial)) ||
         libvlc_audio_output_set(mLibVLCMediaPlayer, "mmdevice") != 0))
    {
        stopPlayer();
        mEventStatus.store(STATUS_ERROR);
        setStatus(STATUS_ERROR);
        return;
    }
#endif

    // listen to events
    libvlc_event_manager_t* em = libvlc_media_player_event_manager(mLibVLCMediaPlayer);
    bool eventsAttached = em != nullptr;
    if (em)
    {
        unsigned index = 0;
        for (const auto type : playerEvents)
        {
            if (libvlc_event_attach(em, type, eventCallbacks, this) != 0) eventsAttached = false;
            else mAttachedPlayerEvents |= 1u << index;
            ++index;
        }
    }

    // <FS:ND> Stream metadata (ICY StreamTitle etc.) arrives on the media's
    // own event manager, not the player's
    libvlc_event_manager_t* mem = libvlc_media_event_manager(mLibVLCMedia);
    if (mem)
    {
        if (libvlc_event_attach(mem, libvlc_MediaMetaChanged, eventCallbacks, this) != 0) eventsAttached = false;
        else mAttachedMediaEvent = true;
    }
    else eventsAttached = false;
    // </FS:ND>

    if (!eventsAttached)
    {
        stopPlayer();
        mEventStatus.store(STATUS_ERROR);
        setStatus(STATUS_ERROR);
        return;
    }

    libvlc_video_set_callbacks(mLibVLCMediaPlayer, lock, unlock, display, &mLibVLCCallbackContext);
    libvlc_video_set_format(mLibVLCMediaPlayer, "RV32", mWidth, mHeight, mWidth * mDepth);

    mLibVLCCallbackContext.parent = this;
    mLibVLCCallbackContext.texture_pixels = mPixels;
    mLibVLCCallbackContext.mp = mLibVLCMediaPlayer;

    // Send a "navigate begin" event.
    // This is really a browser message but the QuickTime plugin did it and
    // the media system relies on this message to update internal state so we must send it too
    // Note: see "navigate_complete" message below too
    // https://jira.secondlife.com/browse/MAINT-6528
    LLPluginMessage message_begin(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_begin");
    message_begin.setValue("uri", mURL);
    message_begin.setValueBoolean("history_back_available", false);
    message_begin.setValueBoolean("history_forward_available", false);
    sendMessage(message_begin);

    // volume level gets set before VLC is initialized (thanks media system) so we have to record
    // it in mCurVolume and set it again here so that volume levels are correctly initialized
    setVolumeVLC();

    setStatus(STATUS_LOADED);

    // note this relies on the "set_loop" message arriving before the "start" (play) one
    // but that appears to always be the case
    if (mIsLooping)
    {
        libvlc_media_add_option(mLibVLCMedia, "input-repeat=65535");
    }

    // <FS> The streaming-audio instance is created 1x1 with no visible
    // surface. Radio doesn't care about latency, and SLPlugin runs at
    // background priority where VLC's default ~1s network cache underruns
    // (audible pops/thuds) whenever the viewer loads the machine — buffer
    // deep. Visible parcel media keeps the default for responsiveness.
    if (mWidth <= 1 && mHeight <= 1)
    {
        libvlc_media_add_option(mLibVLCMedia, ":network-caching=5000");
        libvlc_media_add_option(mLibVLCMedia, ":no-video");
    }
    // </FS>

    if (libvlc_media_player_play(mLibVLCMediaPlayer) != 0)
    {
        stopPlayer();
        mEventStatus.store(STATUS_ERROR);
        setStatus(STATUS_ERROR);
        return;
    }

    // send a "location_changed" message - this informs the media system
    // that a new URL is the 'current' one and is used extensively.
    // Again, this is really a browser message but we will use it here.
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "location_changed");
    message.setValue("uri", mURL);
    sendMessage(message);

    // Send a "navigate complete" event.
    // This is really a browser message but the QuickTime plugin did it and
    // the media system relies on this message to update internal state so we must send it too
    // Note: see "navigate_begin" message above too
    // https://jira.secondlife.com/browse/MAINT-6528
    LLPluginMessage message_complete(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_complete");
    message_complete.setValue("uri", mURL);
    message_complete.setValueS32("result_code", 200);
    message_complete.setValue("result_string", "OK");
    sendMessage(message_complete);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::updateTitle(const char* title)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
    message.setValue("name", title);
    sendMessage(message);
}

// <FS:ND> Report stream metadata to the viewer, same message the gstreamer
// plugin sends. Which meta field carries the track depends on the stream:
// MP3/AAC ICY updates arrive in NowPlaying ("Artist - Title"), while
// Ogg/Icecast chains update Title/Artist from in-band Vorbis comments (and
// often park the static station name in NowPlaying, so a fixed precedence
// would mask per-track updates). React to the field that actually changed.
void MediaPluginLibVLC::updateStreamMetadata(int meta_type)
{
    if (!mLibVLCMedia)
    {
        return;
    }

    std::string artist;
    std::string title;

    if (meta_type == libvlc_meta_NowPlaying)
    {
        char* meta = libvlc_media_get_meta(mLibVLCMedia, libvlc_meta_NowPlaying);
        if (meta)
        {
            std::string now_playing = meta;
            libvlc_free(meta);

            size_t sep = now_playing.find(" - ");
            if (sep != std::string::npos)
            {
                artist = now_playing.substr(0, sep);
                title = now_playing.substr(sep + 3);
            }
            else
            {
                title = now_playing;
            }
        }
    }
    else // Title or Artist changed
    {
        char* meta = libvlc_media_get_meta(mLibVLCMedia, libvlc_meta_Artist);
        if (meta)
        {
            artist = meta;
            libvlc_free(meta);
        }

        meta = libvlc_media_get_meta(mLibVLCMedia, libvlc_meta_Title);
        if (meta)
        {
            title = meta;
            libvlc_free(meta);
        }

        // Before any real meta arrives VLC uses the URL's filename as the
        // title placeholder; that is not worth announcing
        if (artist.empty() && !title.empty() && mURL.find(title) != std::string::npos)
        {
            return;
        }
    }

    if (artist.empty() && title.empty())
    {
        return;
    }

    // Both metas of a track boundary land before the first event is
    // delivered, so Title and Artist events would send the same pair twice
    std::string dedup_key = artist + "\x01" + title;
    if (dedup_key == mLastMetadataSent)
    {
        return;
    }
    mLastMetadataSent = dedup_key;

    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "ndMediadata_change");
    message.setValue("title", title);
    message.setValue("artist", artist);
    sendMessage(message);
}
// </FS:ND>

void MediaPluginLibVLC::setVolumeVLC()
{
#if LL_WINDOWS
    if (mMusic && mLibVLCMediaPlayer)
    {
        if (!setMusicGain(mLibVLCMediaPlayer, static_cast<float>(mCurVolume), mNativeMute) ||
            libvlc_audio_set_volume(mLibVLCMediaPlayer, 100) != 0 ||
            waveOutSetVolume(NULL, 0xffffffff) != MMSYSERR_NOERROR)
            mEventStatus.store(STATUS_ERROR);
        return;
    }
#endif
    if (mLibVLCMediaPlayer)
    {
        int vlc_vol = (int)(mCurVolume * 100);

        int result = libvlc_audio_set_volume(mLibVLCMediaPlayer, vlc_vol);
        if (result == 0)
        {
            // volume change was accepted by LibVLC
        }
        else
        {
            mEventStatus.store(STATUS_ERROR);
        }

#if LL_WINDOWS
        // https ://jira.secondlife.com/browse/MAINT-8119
        // CEF media plugin uses code in media_plugins/cef/windows_volume_catcher.cpp to
        // set the actual output volume of the plugin process since there is no API in
        // CEF to otherwise do this.
        // There are explicit calls to change the volume in LibVLC but sometimes they
        // are ignored SLPlugin.exe process volume is set to 0 so you never heard audio
        // from the VLC media stream.
        // The right way to solve this is to move the volume catcher stuff out of
        // the CEF plugin and into it's own folder under media_plugins and have it referenced
        // by both CEF and VLC. That's for later. The code there boils down to this so for
                // now, as we approach a release, the less risky option is to do it directly vs
                // calls to volume catcher code.
        DWORD left_channel = (DWORD)(mCurVolume * 65535.0f);
        DWORD right_channel = (DWORD)(mCurVolume * 65535.0f);
        DWORD hw_volume = left_channel << 16 | right_channel;
        if (waveOutSetVolume(NULL, hw_volume) != MMSYSERR_NOERROR) mEventStatus.store(STATUS_ERROR);
#endif
    }
    else
    {
        // volume change was requested but VLC wasn't ready.
        // that's okay though because we saved the value in mCurVolume and
        // the next volume change after the VLC system is initilzied  will set it
    }
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::setVolume(const F64 volume)
{
    if (!std::isfinite(volume) || volume < 0. || volume > 1.)
    {
        return;
    }
    mCurVolume = volume;

#if LL_WINDOWS
    if (mMusic && mLibVLCMediaPlayer)
    {
        if (!setMusicGain(mLibVLCMediaPlayer, static_cast<float>(volume)))
            mEventStatus.store(STATUS_ERROR);
        return;
    }
#endif
    setVolumeVLC();
}

#if LL_WINDOWS
namespace
{
bool nativeMusicId(const LLSD& value, std::uint64_t& result)
{
    if (!value.isString()) return false;
    const auto text = value.asString();
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0')) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
}

void MediaPluginLibVLC::nativeMusicState(const std::string& state, const std::string& detail, std::uint32_t serial)
{
    LLPluginMessage message("media_audio", "state");
    message.setValue("generation", std::to_string(mNativeGeneration));
    message.setValue("serial", std::to_string(serial));
    message.setValue("state", state);
    message.setValue("detail", detail);
    sendMessage(message);
}

void MediaPluginLibVLC::receiveNativeMusic(const LLPluginMessage& message)
{
    std::uint64_t generation = 0;
    if (!nativeMusicId(message.getValueLLSD("generation"), generation) || !generation) return;
    if (message.getName() == "configure")
    {
        if (message.getValue("role") != "music" || generation <= mNativeGeneration) return;
        stopPlayer();
        mURL.clear();
        mNativeGeneration = generation;
        mNativeSerial = mNativeAcknowledged = 0;
        mNativeInitial = mNativeTarget = 1.f;
        mNativeDuration = 0.f;
        mNativeMute = false;
        mNativeConfigured = true;
        mNativeFailure.clear();
        mNativeLastState.clear();
        return;
    }
    if (!mNativeConfigured || generation != mNativeGeneration || !mNativeFailure.empty()) return;
    const auto targetValue = message.getValueLLSD("target");
    const auto durationValue = message.getValueLLSD("duration");
    const double target = targetValue.asReal();
    const double duration = durationValue.asReal();
    if (!targetValue.isReal() || !durationValue.isReal() || !std::isfinite(target) ||
        !std::isfinite(duration) || target < 0. || target > 1. || duration < 0. || duration > 60.)
    { mNativeFailure = "invalid_native_music_control"; return; }
    if (message.getName() == "set_gain")
    {
        if (!message.getValueLLSD("hard_mute").isBoolean())
        { mNativeFailure = "invalid_native_music_mute"; return; }
        mCurVolume = target;
        mNativeMute = message.getValueBoolean("hard_mute");
        if (mLibVLCMediaPlayer && !setMusicGain(mLibVLCMediaPlayer, static_cast<float>(target), mNativeMute))
            mNativeFailure = "native_music_gain_failed";
    }
    else if (message.getName() == "transition")
    {
        std::uint64_t serial = 0;
        if (!nativeMusicId(message.getValueLLSD("serial"), serial) || serial <= mNativeSerial) return;
        if (serial > 0x7fffffff) { mNativeFailure = "native_music_serial_overflow"; return; }
        mNativeSerial = static_cast<std::uint32_t>(serial);
        mNativeTarget = static_cast<float>(target);
        mNativeDuration = static_cast<float>(duration);
        mNativeFadeStarted = std::chrono::steady_clock::now();
        if (!mLibVLCMediaPlayer && duration == 0.) mNativeInitial = mNativeTarget;
        if (mLibVLCMediaPlayer && !setMusicFade(mLibVLCMediaPlayer, mNativeTarget, mNativeDuration, mNativeSerial))
            mNativeFailure = "native_music_fade_failed";
    }
}

void MediaPluginLibVLC::idleNativeMusic()
{
    if (!mNativeConfigured) return;
    if (mLibVLCMediaPlayer && (mVlcStatus == STATUS_ERROR || musicOutputFailed(mLibVLCMediaPlayer)))
        mNativeFailure = "native_music_output_failed";
    if (mNativeSerial != mNativeAcknowledged && mNativeFailure.empty())
    {
        if (mLibVLCMediaPlayer && musicFadeComplete(mLibVLCMediaPlayer, mNativeSerial))
        {
            mNativeAcknowledged = mNativeSerial;
            nativeMusicState(mNativeTarget == 0.f ? "silent" : "running", "vlc_playback_clock", mNativeSerial);
        }
        else if (std::chrono::duration<double>(std::chrono::steady_clock::now() - mNativeFadeStarted).count() >
                 mNativeDuration + 30.)
            mNativeFailure = "native_music_playback_clock_timeout";
    }
    const std::string state = !mNativeFailure.empty() ? "failed" : !mLibVLCMediaPlayer ? "priming" :
        mVlcStatus == STATUS_PAUSED ? "paused" : mBuffering.load() < 100.f ? "buffering" : "running";
    if (state != mNativeLastState)
    {
        nativeMusicState(state, mNativeFailure.empty() ? "vlc_native_music" : mNativeFailure,
                         state == "failed" ? mNativeSerial : 0);
        mNativeLastState = state;
    }
}
#endif

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginLibVLC::receiveMessage(const char* message_string)
{
    LLPluginMessage message_in;

    if (message_in.parse(message_string) >= 0)
    {
        std::string message_class = message_in.getClass();
        std::string message_name = message_in.getName();
        if (message_class == LLPLUGIN_MESSAGE_CLASS_BASE)
        {
            if (message_name == "init")
            {
                initVLC();

                LLPluginMessage message("base", "init_response");
                LLSD versions = LLSD::emptyMap();
                versions[LLPLUGIN_MESSAGE_CLASS_BASE] = LLPLUGIN_MESSAGE_CLASS_BASE_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA] = LLPLUGIN_MESSAGE_CLASS_MEDIA_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME] = LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME_VERSION;
#if LL_WINDOWS
                versions["media_music"] = "1.0";
#endif
                message.setValueLLSD("versions", versions);

                std::ostringstream s;
                s << "LibVLC plugin ";
                s << LIBVLC_VERSION_MAJOR;
                s << ".";
                s << LIBVLC_VERSION_MINOR;
                s << ".";
                s << LIBVLC_VERSION_REVISION;

                message.setValue("plugin_version", s.str());
                sendMessage(message);
            }
            else if (message_name == "idle")
            {
                idle();
            }
            else if (message_name == "cleanup")
            {
                resetVLC();
            }
            else if (message_name == "force_exit")
            {
                resetVLC();
                mDeleteMe = true;
            }
            else if (message_name == "shm_added")
            {
                SharedSegmentInfo info;
                info.mAddress = message_in.getValuePointer("address");
                info.mSize = (size_t)message_in.getValueS32("size");
                std::string name = message_in.getValue("name");

                mSharedSegments.insert(SharedSegmentMap::value_type(name, info));

            }
            else if (message_name == "shm_remove")
            {
                std::string name = message_in.getValue("name");

                SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                if (iter != mSharedSegments.end())
                {
                    if (mPixels == iter->second.mAddress)
                    {
                        stopPlayer();

                        mPixels = NULL;
                        mTextureSegmentName.clear();
                    }
                    mSharedSegments.erase(iter);
                }
                else
                {
                    //std::cerr << "MediaPluginWebKit::receiveMessage: unknown shared memory region!" << std::endl;
                }

                // Send the response so it can be cleaned up.
                LLPluginMessage message("base", "shm_remove_response");
                message.setValue("name", name);
                sendMessage(message);
            }
            else
            {
                //std::cerr << "MediaPluginWebKit::receiveMessage: unknown base message: " << message_name << std::endl;
            }
        }
    #if LL_WINDOWS
        else if (message_class == "media_audio")
        {
            receiveNativeMusic(message_in);
        }
    #endif
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA)
        {
            if (message_name == "init")
            {
                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "texture_params");
                message.setValueS32("default_width", 1024);
                message.setValueS32("default_height", 1024);
                message.setValueS32("depth", mDepth);
                message.setValueU32("internalformat", GL_RGB);
                message.setValueU32("format", GL_BGRA_EXT);
                message.setValueU32("type", GL_UNSIGNED_BYTE);
                message.setValueBoolean("coords_opengl", true);
                sendMessage(message);
            }
            else if (message_name == "size_change")
            {
                std::string name = message_in.getValue("name");
                S32 width = message_in.getValueS32("width");
                S32 height = message_in.getValueS32("height");
                S32 texture_width = message_in.getValueS32("texture_width");
                S32 texture_height = message_in.getValueS32("texture_height");

                if (!name.empty())
                {
                    // Find the shared memory region with this name
                    SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                    if (iter != mSharedSegments.end())
                    {
                        stopPlayer();
                        mPixels = (unsigned char*)iter->second.mAddress;
                        mWidth = width;
                        mHeight = height;
                        mTextureWidth = texture_width;
                        mTextureHeight = texture_height;

                        libvlc_time_t time = (libvlc_time_t)(1000.0 * mCurTime);

                        playMedia();

                        if (mLibVLCMediaPlayer)
                        {
                            libvlc_media_player_set_time(mLibVLCMediaPlayer, time);
                            time = libvlc_media_player_get_time(mLibVLCMediaPlayer);
                            if (time < 0)
                            {
                                // -1 if there is no media
                                mCurTime = 0;
                            }
                            else
                            {
                                mCurTime = (F64)time / 1000.0;
                            }
                        }
                    };
                };

                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "size_change_response");
                message.setValue("name", name);
                message.setValueS32("width", width);
                message.setValueS32("height", height);
                message.setValueS32("texture_width", texture_width);
                message.setValueS32("texture_height", texture_height);
                sendMessage(message);
            }
            else if (message_name == "load_uri")
            {
                mURL = message_in.getValue("uri");
#if LL_WINDOWS
                if (message_in.getValue("audio_generation") != std::to_string(mNativeGeneration))
                    mNativeConfigured = false;
#endif
                mMusic = message_in.getValue("audio_role") == "music";
                const S32 layout = message_in.getValueLLSD("speaker_fill").isInteger() ?
                    message_in.getValueS32("speaker_fill") : 0;
                mMusicSpeakerFill = message_in.getValue("audio_role") == "music" &&
                    (layout == 21 || layout == 41 || layout == 51 || layout == 71) ? layout : 0;
                playMedia();
            }
        }
        else
            if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME)
            {
                if (message_name == "stop")
                {
                    stopPlayer();
                }
                else if (message_name == "start")
                {
                    if (!mLibVLCMediaPlayer || mVlcStatus == STATUS_DONE)
                    {
                        playMedia();
                    }
                    else
                    {
                        if (libvlc_media_player_play(mLibVLCMediaPlayer) != 0) mEventStatus.store(STATUS_ERROR);
                    }
                }
                else if (message_name == "pause")
                {
                    if (mLibVLCMediaPlayer)
                    {
                        libvlc_media_player_set_pause(mLibVLCMediaPlayer, 1);
                    }
                }
                else if (message_name == "seek")
                {
                    if (mLibVLCMediaPlayer)
                    {
                        libvlc_time_t time = (libvlc_time_t)(1000.0 * message_in.getValueReal("time"));
                        libvlc_media_player_set_time(mLibVLCMediaPlayer, time);
                        time = libvlc_media_player_get_time(mLibVLCMediaPlayer);
                        if (time < 0)
                        {
                            // -1 if there is no media
                            mCurTime = 0;
                        }
                        else
                        {
                            mCurTime = (F64)time / 1000.0;
                        }

                        if (!libvlc_media_player_is_playing(mLibVLCMediaPlayer))
                        {
                            // if paused, won't trigger update, update now
                            setDurationDirty();
                        }
                    }
                }
                else if (message_name == "set_loop")
                {
                    bool loop = message_in.getValueBoolean("loop");
                    mIsLooping = loop;
                }
                else if (message_name == "set_volume")
                {
                    // volume comes in 0 -> 1.0
                    F64 volume = message_in.getValueReal("volume");
                    setVolume(volume);
                }
            }
    }
}

////////////////////////////////////////////////////////////////////////////////
//
bool MediaPluginLibVLC::init()
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
    message.setValue("name", "LibVLC Plugin");
    sendMessage(message);

    return true;
};

////////////////////////////////////////////////////////////////////////////////
//
int init_media_plugin(LLPluginInstance::sendMessageFunction host_send_func,
    void* host_user_data,
    LLPluginInstance::sendMessageFunction *plugin_send_func,
    void **plugin_user_data)
{
    MediaPluginLibVLC* self = new MediaPluginLibVLC(host_send_func, host_user_data);
    *plugin_send_func = MediaPluginLibVLC::staticReceiveMessage;
    *plugin_user_data = (void*)self;

    return 0;
}
