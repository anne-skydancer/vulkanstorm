/**
 * @file llaudioengine_vks.cpp
 * @brief The viewer's audio engine on vkstorm-audio (Rust).
 *
 * The engine keeps the structure and the call sequences of the SoLoud
 * adapter it replaces (the mixer is a port of SoLoud's core): voices start
 * paused and fully configured, stops are declicked over 20 ms, synced starts
 * seek into the master's loop and fade in over 20 ms, loops are detected by
 * the mixer's loop counter, inaudible voices keep ticking, UI sounds play
 * flat. Device selection, restart and the device list refresh behave as
 * before; see doc/rust_port.md, rung six.
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
#include "lldir.h"

#include "llaudioengine_vks.h"
#include "lllistener_vks.h"
#include "llstreamingaudio_vks.h"

#include "vkstorm_ffi.h"

#include <cmath>

VksAudio* LLAudioEngine_VKS::sAudio = nullptr;
bool LLAudioEngine_VKS::sBackendReady = false;

namespace
{
    // String results of the C ABI arrive through this callback; it must not
    // throw across the boundary.
    void append_string(void* user, const char* s, size_t len)
    {
        try
        {
            static_cast<std::string*>(user)->assign(s, len);
        }
        catch (...)
        {
        }
    }

    void add_device(void* user, const uint8_t* uuid, const char* name, size_t name_len)
    {
        try
        {
            LLUUID id;
            memcpy(id.mData, uuid, UUID_BYTES);
            static_cast<LLAudioEngine::output_device_map_t*>(user)->insert(
                std::make_pair(id, std::string(name, name_len)));
        }
        catch (...)
        {
        }
    }
}

// ------------ engine ------------

LLAudioEngine_VKS::LLAudioEngine_VKS()
{
}

LLAudioEngine_VKS::~LLAudioEngine_VKS()
{
}

void LLAudioEngine_VKS::backendStarted()
{
    sBackendReady = true;
    vks_audio_set_global_volume(sAudio, llmax(mInternalGain, 0.f));
    mListenerp->commitDeferredChanges();
}

bool LLAudioEngine_VKS::init(void* userdata, const std::string &app_title)
{
    LLAudioEngine::init(userdata, app_title);

    sBackendReady = false;
    std::unique_lock<std::timed_mutex> lock(gAudioDeviceMutex, std::chrono::seconds(1));
    if (lock.owns_lock())
    {
        // The default endpoint first; the viewer selects the saved one next
        sAudio = vks_audio_create(nullptr);
        if (!sAudio)
        {
            LL_WARNS() << "LLAudioEngine_VKS output init failed: " << vks_audio_last_error() << LL_ENDL;
        }
    }
    if (!sAudio)
    {
        delete mListenerp;
        mListenerp = nullptr;
        return false;
    }
    backendStarted();

    LL_INFOS() << "LLAudioEngine_VKS::init() audio engine initialized: "
               << getDriverName(true) << LL_ENDL;

    return true;
}

bool LLAudioEngine_VKS::initNull(unsigned int rate, unsigned int channels)
{
    sBackendReady = false;
    sAudio = vks_audio_create_null(rate, channels, 0, 0);
    if (!sAudio)
    {
        LL_WARNS() << "LLAudioEngine_VKS null output init failed: " << vks_audio_last_error() << LL_ENDL;
        return false;
    }
    backendStarted();
    return true;
}

// <FS:Ansariel> Output device selection
//virtual
LLAudioEngine_VKS::output_device_map_t LLAudioEngine_VKS::getDevices()
{
    output_device_map_t device_map;

    std::unique_lock<std::timed_mutex> lock(gAudioDeviceMutex, std::chrono::seconds(1));
    if (!lock.owns_lock())
    {
        return mKnownDevices;
    }
    if (vks_audio_devices(add_device, &device_map) < 0)
    {
        LL_WARNS() << "Output device enumeration failed: " << vks_audio_last_error() << LL_ENDL;
        return output_device_map_t();
    }
    for (const auto& device : device_map)
    {
        LL_DEBUGS("AppInit") << "LLAudioEngine_VKS::getDevices(): name=\"" << device.second
                             << "\" - uuid: " << device.first << LL_ENDL;
    }
    return device_map;
}

//virtual
void LLAudioEngine_VKS::setDevice(const LLUUID& device_uuid)
{
    if (!sAudio)
    {
        return;
    }

    mRequestedDeviceUUID = device_uuid;
    if (sBackendReady && device_uuid == mSelectedDeviceUUID)
    {
        mDeviceChangePending = false;
        return;
    }

    if (!device_uuid.isNull())
    {
        const output_device_map_t devices = getDevices();
        if (devices.find(device_uuid) == devices.end())
        {
            LL_WARNS() << "Requested output device " << device_uuid
                       << " not found; retaining the current output device" << LL_ENDL;
            mDeviceChangePending = false;
            return;
        }
    }

    // Share the device access mutex with the voice subsystem (FIRE-36022)
    std::unique_lock<std::timed_mutex> lock(gAudioDeviceMutex, std::chrono::seconds(1));
    if (!lock.owns_lock())
    {
        LL_WARNS() << "Could not access the audio device mutex; device change deferred" << LL_ENDL;
        mDeviceChangePending = true;
        mDeviceRetryTimer.reset();
        return;
    }
    mDeviceChangePending = false;

    // Voice handles do not survive the restart
    sBackendReady = false;
    for (U32 i = 0; i < LL_MAX_AUDIO_CHANNELS; ++i)
    {
        if (mChannels[i])
        {
            ((LLAudioChannelVKS*)mChannels[i])->resetVoiceHandle();
        }
    }

    const S32 result = vks_audio_set_device(sAudio, device_uuid.isNull() ? nullptr : device_uuid.mData);
    switch (result)
    {
    case 0: // already there
    case 2: // switched
        mSelectedDeviceUUID = device_uuid;
        backendStarted();
        LL_INFOS() << "LLAudioEngine_VKS::setDevice() now on: " << getDriverName(true) << LL_ENDL;
        break;
    case 1:
        LL_WARNS() << "Requested output device " << device_uuid
                   << " not found; retaining the current output device" << LL_ENDL;
        backendStarted();
        break;
    case 3:
        LL_WARNS() << "Requested device failed (" << vks_audio_last_error()
                   << "); restored previous output device" << LL_ENDL;
        backendStarted();
        break;
    default:
        LL_WARNS() << "Requested device failed and the previous device could not be restored; audio unavailable"
                   << LL_ENDL;
        break;
    }
}
// </FS:Ansariel>

std::string LLAudioEngine_VKS::getDriverName(bool verbose)
{
    // The mixer is a port of SoLoud: the name and version come from the
    // crate's record of that baseline. The verbose form is the one the
    // SoLoud backend printed, with the channel mask, the form factor and the
    // device appended.
    if (!verbose)
    {
        return vks_audio_engine_info();
    }

    std::ostringstream version;
    version << "SoLoud (Rust), version " << vks_audio_engine_version();

    if (sAudio)
    {
        VksAudioFormat format{};
        std::string names;
        if (vks_audio_format(sAudio, &format, append_string, &names))
        {
            const size_t split = names.find('\0');
            const std::string backend = names.substr(0, split);
            const std::string device = split == std::string::npos ? std::string() : names.substr(split + 1);
            version << ", backend " << backend << " @ " << format.rate << " Hz, "
                    << format.channels << " channels, " << format.period_frames << " frame buffer"
                    << ", mask 0x" << std::hex << format.channel_mask << std::dec << ", "
                    << (format.headphones ? "headphones" : "speakers")
                    << (device.empty() ? "" : ", device ") << device;
        }
        else
        {
            version << ", no output";
        }
    }

    return version.str();
}

LLStreamingAudioInterface* LLAudioEngine_VKS::createDefaultStreamingAudioImpl() const
{
    return new LLStreamingAudio_VKS();
}

void LLAudioEngine_VKS::allocateListener()
{
    mListenerp = (LLListener *) new LLListener_VKS();
    if (!mListenerp)
    {
        LL_WARNS() << "LLAudioEngine_VKS::allocateListener() Listener creation failed" << LL_ENDL;
    }
}

void LLAudioEngine_VKS::shutdown()
{
    // Deletes the streaming impl, the channels (declicked) and the buffers
    // (their voices stop) while the engine still exists
    LLAudioEngine::shutdown();

    std::lock_guard<std::timed_mutex> lock(gAudioDeviceMutex);
    sBackendReady = false;
    mDeviceChangePending = false;
    if (sAudio)
    {
        vks_audio_destroy(sAudio);
        sAudio = nullptr;
    }

    LL_INFOS() << "LLAudioEngine_VKS::shutdown() audio engine shut down" << LL_ENDL;

    delete mListenerp;
    mListenerp = nullptr;
}

void LLAudioEngine_VKS::idle()
{
    if (mDeviceChangePending && mDeviceRetryTimer.getElapsedTimeF32() > 1.f)
    {
        setDevice(mRequestedDeviceUUID);
    }
    if (mDeviceListTimer.getElapsedTimeF32() > 5.f)
    {
        mDeviceListTimer.reset();
        const auto devices = getDevices();
        if (devices != mKnownDevices)
        {
            mKnownDevices = devices;
            OnOutputDeviceListChanged(devices);
        }
    }
    if (sAudio)
    {
        // Report a lost endpoint or a failed output once; the output stays
        // silent until the user selects a device again, as before
        const S32 state = vks_audio_poll(sAudio);
        if (state != mLastDeviceState)
        {
            if (state == 2)
            {
                LL_WARNS() << "The selected audio output device was lost" << LL_ENDL;
            }
            else if (state == 3)
            {
                LL_WARNS() << "The audio output device failed" << LL_ENDL;
            }
            else if (state == 1)
            {
                LL_INFOS() << "No default audio output device; waiting for one" << LL_ENDL;
            }
            mLastDeviceState = state;
        }
    }
    if (!sBackendReady)
    {
        updateInternetStream();
        return;
    }
    LLAudioEngine::idle();
}

LLAudioBuffer *LLAudioEngine_VKS::createBuffer()
{
    return new LLAudioBufferVKS();
}

LLAudioChannel *LLAudioEngine_VKS::createChannel()
{
    return new LLAudioChannelVKS();
}

void LLAudioEngine_VKS::setInternalGain(F32 gain)
{
    if (sBackendReady)
    {
        vks_audio_set_global_volume(sAudio, gain);
    }
}

bool LLAudioEngine_VKS::initWind()
{
    if (!sBackendReady)
    {
        return false;
    }

    if (!mWindStarted)
    {
        mWindStarted = vks_audio_wind_init(sAudio) != 0;
    }

    return mWindStarted;
}

void LLAudioEngine_VKS::cleanupWind()
{
    if (sAudio && mWindStarted)
    {
        vks_audio_wind_cleanup(sAudio);
    }
    mWindStarted = false;
}

void LLAudioEngine_VKS::updateWind(LLVector3 wind_vec, F32 camera_altitude)
{
    if (!mEnableWind || !mWindStarted)
    {
        return;
    }

    if (mWindUpdateTimer.checkExpirationAndReset(LL_WIND_UPDATE_INTERVAL))
    {
        // wind comes in as Linden coordinate (+X = forward, +Y = left, +Z = up)
        // need to convert this to the conventional orientation DS3D and OpenAL use
        // where +X = right, +Y = up, +Z = backwards
        wind_vec.setVec(-wind_vec.mV[1], wind_vec.mV[2], -wind_vec.mV[0]);

        F64 pitch = 1.0 + mapWindVecToPitch(wind_vec);
        F64 center_freq = 80.0 * pow(pitch, 2.5 * (mapWindVecToGain(wind_vec) + 1.0));

        vks_audio_wind_targets(sAudio,
                               (F32)center_freq,
                               (F32)mapWindVecToGain(wind_vec) * mMaxWindGain,
                               (F32)mapWindVecToPan(wind_vec));
    }
}

// ------------ channel ------------

// Stop with a short fade instead of truncating mid-sample, which pops
static void stop_voice_declicked(VksAudio* audio, U32 voice_handle)
{
    const double DECLICK_FADE_SEC = 0.02;
    vks_audio_voice_fade_volume(audio, voice_handle, 0.f, DECLICK_FADE_SEC);
    vks_audio_voice_schedule_stop(audio, voice_handle, DECLICK_FADE_SEC);
}

LLAudioChannelVKS::LLAudioChannelVKS()
    : mHandle(0),
      mLastLoopCount(0),
      mLastGain(-1.f),
      mLastLoop(false)
{
}

LLAudioChannelVKS::~LLAudioChannelVKS()
{
    cleanup();
}

void LLAudioChannelVKS::cleanup()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (audio && mHandle)
    {
        stop_voice_declicked(audio, mHandle);
    }
    mHandle = 0;
    mLastLoopCount = 0;
    mLastGain = -1.f;

    mCurrentBufferp = nullptr;
}

// Every mixer call takes the mixer lock that the device callback holds while
// it renders, so per-frame calls are limited to actual state changes
void LLAudioChannelVKS::applyVoiceParams()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (!audio || !mHandle || !mCurrentSourcep)
    {
        return;
    }

    F32 gain = mCurrentSourcep->getGain() * getSecondaryGain();
    if (gain != mLastGain)
    {
        vks_audio_voice_set_volume(audio, mHandle, gain);
        mLastGain = gain;
    }

    bool loop = mCurrentSourcep->isLoop();
    if (loop != mLastLoop)
    {
        vks_audio_voice_set_looping(audio, mHandle, loop);
        mLastLoop = loop;
    }
}

// Create the voice PAUSED and fully configure it before it can reach the
// mixer: any parameter applied after an unpaused create races the audio
// thread, and the first granules would mix with defaults (worst: unattenuated
// full-volume starts of distant sounds - an audible thump per sound start)
void LLAudioChannelVKS::startVoicePaused()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    LLAudioBufferVKS *bufferp = (LLAudioBufferVKS *)mCurrentBufferp;
    if (!audio || !bufferp || !bufferp->getWav() || !mCurrentSourcep)
    {
        return;
    }

    F32 gain = mCurrentSourcep->getGain() * getSecondaryGain();

    if (mCurrentSourcep->isForcedPriority())
    {
        // UI / preview sounds play flat, no spatialization
        mHandle = vks_audio_play(audio, bufferp->getWav(), gain, 1 /*paused*/);
    }
    else
    {
        LLVector3 pos;
        pos.setVec(mCurrentSourcep->getPositionGlobal());

        const LLVector3& velocity = mCurrentSourcep->getVelocity();
        mHandle = vks_audio_play_3d(audio, bufferp->getWav(), pos.mV, velocity.mV, gain, 1 /*paused*/);
        vks_audio_voice_set_doppler_factor(audio, mHandle, gAudiop->getDopplerFactor());
        F32 rolloff = gAudiop->getRolloffFactor();
        if (rolloff != 1.f)
        {
            vks_audio_voice_set_inverse_distance_rolloff(audio, mHandle, rolloff);
        }
    }

    mLastGain = gain;
    mLastLoop = mCurrentSourcep->isLoop();
    vks_audio_voice_set_looping(audio, mHandle, mLastLoop);
    // Keep inaudible voices ticking so loop timing and completion stay correct
    vks_audio_voice_set_inaudible_behavior(audio, mHandle, 1, 0);
    if (!mCurrentSourcep->isForcedPriority())
    {
        vks_audio_update_3d(audio);
    }
    mLastLoopCount = 0;
}

void LLAudioChannelVKS::play()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (!audio)
    {
        return;
    }

    if (isPlaying())
    {
        return;
    }

    if (!mCurrentBufferp || !mCurrentSourcep)
    {
        LL_WARNS() << "Playing without a buffer or source, aborting" << LL_ENDL;
        return;
    }

    startVoicePaused();
    if (mHandle)
    {
        vks_audio_voice_set_pause(audio, mHandle, 0);
    }
    mCurrentSourcep->setPlayedOnce(true);
}

void LLAudioChannelVKS::playSynced(LLAudioChannel *channelp)
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (audio && channelp)
    {
        LLAudioChannelVKS *masterchannelp = (LLAudioChannelVKS*)channelp;
        if (masterchannelp->mHandle && vks_audio_voice_is_valid(audio, masterchannelp->mHandle))
        {
            // Stream position is monotonic across loops; reduce it to the
            // position within the master's current loop before seeking.
            double master_offset = vks_audio_voice_stream_position(audio, masterchannelp->mHandle);
            LLAudioBufferVKS *master_bufferp = (LLAudioBufferVKS *)masterchannelp->mCurrentBufferp;
            if (master_bufferp && master_bufferp->getWav())
            {
                double master_length = vks_audio_wav_length(master_bufferp->getWav());
                if (master_length > 0.0)
                {
                    master_offset = fmod(master_offset, master_length);
                }
            }
            startVoicePaused();
            if (mHandle)
            {
                // Seek and arm the fade-in while still paused: the seek
                // lands mid-waveform past the PCM head fade, so an
                // unramped start (or any mixing before the seek) thumps
                float target_volume = mLastGain;
                vks_audio_voice_set_volume(audio, mHandle, 0.f);
                vks_audio_voice_seek(audio, mHandle, master_offset);
                vks_audio_voice_fade_volume(audio, mHandle, target_volume, 0.02);
                vks_audio_voice_set_pause(audio, mHandle, 0);
            }
            if (mCurrentSourcep)
            {
                mCurrentSourcep->setPlayedOnce(true);
            }
            return;
        }
    }
    play();
}

bool LLAudioChannelVKS::isPlaying()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (audio && mHandle)
    {
        return vks_audio_voice_is_valid(audio, mHandle) != 0;
    }
    return false;
}

bool LLAudioChannelVKS::updateBuffer()
{
    if (!mCurrentSourcep)
    {
        // This channel isn't associated with any source, nothing to update
        return false;
    }

    if (LLAudioChannel::updateBuffer())
    {
        // The source is switching to a different buffer; stop the old voice
        // so play() starts the new sound.
        VksAudio* audio = LLAudioEngine_VKS::getAudio();
        if (audio && mHandle)
        {
            stop_voice_declicked(audio, mHandle);
        }
        mHandle = 0;
        mLastLoopCount = 0;
        mLastGain = -1.f;
    }

    applyVoiceParams();

    return true;
}

void LLAudioChannelVKS::updateLoop()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (!audio || !mHandle)
    {
        return;
    }

    // The mixer's stream position is monotonic and never wraps on loop, so
    // the OpenAL-style position heuristic can't work here; the engine's sync
    // master/slave and queued-sound logic depend on this flag, so use the
    // mixer's per-voice loop counter instead.
    U32 cur_loops = vks_audio_voice_loop_count(audio, mHandle);
    if (cur_loops != mLastLoopCount)
    {
        mLoopedThisFrame = true;
    }
    mLastLoopCount = cur_loops;
}

void LLAudioChannelVKS::update3DPosition()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (!audio || !mHandle || !mCurrentSourcep)
    {
        return;
    }

    if (!mCurrentSourcep->isForcedPriority())
    {
        LLVector3 pos;
        pos.setVec(mCurrentSourcep->getPositionGlobal());
        const LLVector3& velocity = mCurrentSourcep->getVelocity();
        vks_audio_voice_set_3d(audio, mHandle, pos.mV, velocity.mV);
        vks_audio_voice_set_doppler_factor(audio, mHandle, gAudiop->getDopplerFactor());
        vks_audio_voice_set_inverse_distance_rolloff(audio, mHandle, gAudiop->getRolloffFactor());
    }

    applyVoiceParams();
}

// ------------ buffer ------------

LLAudioBufferVKS::LLAudioBufferVKS()
    : mWav(nullptr)
{
}

LLAudioBufferVKS::~LLAudioBufferVKS()
{
    // Stops any voices still playing this buffer
    vks_audio_wav_free(LLAudioEngine_VKS::getAudioObject(), mWav);
    mWav = nullptr;
}

bool LLAudioBufferVKS::loadWAV(const std::string& filename)
{
    // As SoLoud's Wav::load: the previous sound (and its voices) goes first
    vks_audio_wav_free(LLAudioEngine_VKS::getAudioObject(), mWav);
    mWav = nullptr;

    // The decoded data carries the viewer's source defaults (inverse distance
    // attenuation, min/max distance 1/512 m: voices inherit them at creation,
    // so no first granule plays unattenuated) and a 64-sample fade baked into
    // the head and tail of the PCM, as the SoLoud backend did.
    S32 missing = 0;
    mWav = vks_audio_wav_load(filename.c_str(), &missing);
    if (!mWav)
    {
        if (!missing)
        {
            LL_WARNS() << "LLAudioBufferVKS::loadWAV() Error loading "
                       << filename << " (" << vks_audio_last_error() << ")" << LL_ENDL;
        }
        else
        {
            // It's common for the file to not actually exist.
            LL_DEBUGS() << "LLAudioBufferVKS::loadWAV() Error loading "
                        << filename << " (" << vks_audio_last_error() << ")" << LL_ENDL;
        }
        return false;
    }

    return true;
}

U32 LLAudioBufferVKS::getLength()
{
    return vks_audio_wav_sample_count(mWav);
}
