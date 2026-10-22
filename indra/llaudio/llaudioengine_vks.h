/**
 * @file llaudioengine_vks.h
 * @brief The viewer's audio engine on vkstorm-audio (Rust): the output
 *        device, the effects mixer, wind and parcel music.
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

#ifndef LL_AUDIOENGINE_VKS_H
#define LL_AUDIOENGINE_VKS_H

#include "llaudioengine.h"
#include "lllistener_vks.h"

struct VksAudio;
struct VksAudioWav;

// The default audio backend (vkstorm-rust rung six). The mixer is a port of
// the SoLoud core the viewer used before (bit-identical output), behind the
// same calls LLAudioEngine_SoLoud made; see doc/rust_port.md.
class LLAudioEngine_VKS : public LLAudioEngine
{
public:
    LLAudioEngine_VKS();
    virtual ~LLAudioEngine_VKS();

    virtual bool init(void *user_data, const std::string &app_title);
    virtual std::string getDriverName(bool verbose);
    virtual LLStreamingAudioInterface* createDefaultStreamingAudioImpl() const;
    virtual void allocateListener();

    virtual void shutdown();

    void setInternalGain(F32 gain);

    LLAudioBuffer* createBuffer();
    LLAudioChannel* createChannel();

    /*virtual*/ bool initWind();
    /*virtual*/ void cleanupWind();
    /*virtual*/ void updateWind(LLVector3 direction, F32 camera_altitude);

    virtual void idle();

    // <FS:Ansariel> Output device selection
    virtual output_device_map_t getDevices();
    virtual void setDevice(const LLUUID& device_uuid);
    // </FS:Ansariel>

    // The engine while its output runs (null otherwise): every mixer call
    // goes through it.
    static VksAudio* getAudio() { return sBackendReady ? sAudio : nullptr; }
    // The engine object whether or not its output runs (buffers release
    // through it; the music player lives in it).
    static VksAudio* getAudioObject() { return sAudio; }

private:
    friend struct LLVKSAudioTestAccess;
    // The null device instead of hardware (tests, offline rendering).
    bool initNull(unsigned int rate, unsigned int channels);
    void backendStarted();

    static VksAudio* sAudio;
    static bool sBackendReady;

    bool mWindStarted = false;
    LLUUID mSelectedDeviceUUID;
    LLUUID mRequestedDeviceUUID;
    bool mDeviceChangePending = false;
    S32 mLastDeviceState = 0;
    LLFrameTimer mDeviceRetryTimer;
    LLFrameTimer mDeviceListTimer;
    output_device_map_t mKnownDevices;
};

class LLAudioChannelVKS : public LLAudioChannel
{
public:
    LLAudioChannelVKS();
    virtual ~LLAudioChannelVKS();

    // Voice handles do not survive an output restart (device switch); the
    // engine calls this on every channel to drop them
    void resetVoiceHandle() { cleanup(); }
protected:
    /*virtual*/ void play();
    /*virtual*/ void playSynced(LLAudioChannel *channelp);
    /*virtual*/ void cleanup();
    /*virtual*/ bool isPlaying();

    /*virtual*/ bool updateBuffer();
    /*virtual*/ void update3DPosition();
    /*virtual*/ void updateLoop();

    void applyVoiceParams();
    void startVoicePaused();

    U32 mHandle;
    U32 mLastLoopCount;
    F32 mLastGain;
    bool mLastLoop;
};

class LLAudioBufferVKS : public LLAudioBuffer
{
public:
    LLAudioBufferVKS();
    virtual ~LLAudioBufferVKS();

    bool loadWAV(const std::string& filename);
    U32 getLength();

    friend class LLAudioChannelVKS;
    friend class LLAudioEngine_VKS;
protected:
    VksAudioWav* getWav() const { return mWav; }

    VksAudioWav* mWav;
};

#endif
