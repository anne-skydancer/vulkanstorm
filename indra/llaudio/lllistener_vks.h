/**
 * @file lllistener_vks.h
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

#ifndef LL_LISTENER_VKS_H
#define LL_LISTENER_VKS_H

#include "lllistener.h"

class LLListener_VKS : public LLListener
{
public:
    LLListener_VKS();
    virtual ~LLListener_VKS();

    virtual void commitDeferredChanges();

    // The mixer reads Doppler and rolloff per voice (the engine forwards
    // them in startVoicePaused/update3DPosition), as SoLoud's adapter did
    virtual void setDopplerFactor(F32 factor);
    virtual F32 getDopplerFactor();
    virtual void setRolloffFactor(F32 factor);
    virtual F32 getRolloffFactor();

protected:
    F32 mDopplerFactor;
    F32 mRolloffFactor;
};

#endif
