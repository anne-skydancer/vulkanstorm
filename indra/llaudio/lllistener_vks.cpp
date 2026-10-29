/**
 * @file lllistener_vks.cpp
 * @brief The listener of the vkstorm-audio engine.
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
#include "llaudioengine_vks.h"

#include "lllistener_vks.h"
#include "vkstorm_ffi.h"

LLListener_VKS::LLListener_VKS()
    : mDopplerFactor(1.0f),
      mRolloffFactor(1.0f)
{
    init();
}

LLListener_VKS::~LLListener_VKS()
{
}

void LLListener_VKS::commitDeferredChanges()
{
    VksAudio* audio = LLAudioEngine_VKS::getAudio();
    if (!audio)
    {
        return;
    }

    vks_audio_set_listener(audio, mPosition.mV, mListenAt.mV, mListenUp.mV, mVelocity.mV);
    vks_audio_update_3d(audio);
}

void LLListener_VKS::setDopplerFactor(F32 factor)
{
    mDopplerFactor = factor;
}

F32 LLListener_VKS::getDopplerFactor()
{
    return mDopplerFactor;
}

void LLListener_VKS::setRolloffFactor(F32 factor)
{
    mRolloffFactor = factor;
}

F32 LLListener_VKS::getRolloffFactor()
{
    return mRolloffFactor;
}
