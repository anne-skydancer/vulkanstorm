/**
 * @file lllistener_vks.h
 * @brief The listener of the vkstorm-audio engine.
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
