/**
 * @file llaudiosourcevo.cpp
 * @author Douglas Soo, James Cook
 * @brief Audio sources attached to viewer objects
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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
 */

#include "llviewerprecompiledheaders.h"

#include "llaudiosourcevo.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llmutelist.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"
#include "llvoavatarself.h"


namespace
{
struct ReferenceAudioObject
{
    LLPointer<LLViewerObject>& object;
    bool valid() const { return bool(object); }
    bool dead() const { return object->isDead(); }
    bool attachment() const { return object->isAttachment(); }
    bool hud() const { return object->isHUDAttachment(); }
    LLUUID id() const { return object->getID(); }
    F32 cutoff() const { return object->getSoundCutOffRadius(); }
    LLVector3d position() const { return object->getPositionGlobal(); }
    LLVector3 velocity() const { return object->getSubParent() ? object->getSubParent()->getVelocity() : object->getVelocity(); }
    LLViewerObject* avatar() const
    {
        LLViewerObject* parent = object;
        while (parent && !parent->isAvatar()) parent = (LLViewerObject*)parent->getParent();
        return parent;
    }
    LLUUID avatarRoot() const { auto* parent = avatar(); return parent ? parent->getID() : LLUUID::null; }
    LLVector3d avatarRootPosition() const { auto* parent = avatar(); return parent ? parent->getPositionGlobal() : LLVector3d(); }
    bool riding() const { return isAgentAvatarValid() && gAgentAvatarp->getParent(); }
    LLUUID root() const { return ((LLViewerObject*)object->getRoot())->getID(); }
    LLUUID agentRoot() const { return ((LLViewerObject*)gAgentAvatarp->getRoot())->getID(); }
    void clear() { object = nullptr; }
};
struct NativeAudioObject
{
    std::optional<VSAudioObjectState> state;
    bool valid() const { return state.has_value(); }
    bool dead() const { return state->dead; }
    bool attachment() const { return state->attachment; }
    bool hud() const { return state->hud; }
    LLUUID id() const { return state->id; }
    F32 cutoff() const { return state->cutoff; }
    LLVector3d position() const { return state->position; }
    LLVector3 velocity() const { return state->velocity; }
    LLUUID avatarRoot() const { return state->avatarRoot; }
    LLVector3d avatarRootPosition() const { return state->avatarRootPosition; }
    bool riding() const { return state->agentRiding; }
    LLUUID root() const { return state->root; }
    LLUUID agentRoot() const { return state->agentRoot; }
    void clear() { state.reset(); }
};
}

LLAudioSourceVO::LLAudioSourceVO(const LLUUID &sound_id, const LLUUID& owner_id, const F32 gain, LLViewerObject *objectp)
// <FS:CR> FIRE-10512 - Sound explorer fix by Sei Lisa
//  :   LLAudioSource(sound_id, owner_id, gain, LLAudioEngine::AUDIO_TYPE_SFX),
    :   LLAudioSource(sound_id, owner_id, gain, LLAudioEngine::AUDIO_TYPE_SFX, objectp->getID(), false),
// </FS:CR>
    mObjectp(objectp)
{
    update();
}

LLAudioSourceVO::LLAudioSourceVO(const LLUUID& sound_id, const LLUUID& owner_id, F32 gain, VSAudioObjectQuery object)
    : LLAudioSource(sound_id, owner_id, gain, LLAudioEngine::AUDIO_TYPE_SFX, sound_id, false),
      mNativeObject(std::move(object))
{
    update();
}

LLAudioSourceVO::~LLAudioSourceVO()
{
    if (mObjectp)
    {
        mObjectp->clearAttachedSound();
    }
    mObjectp = NULL;
}

void LLAudioSourceVO::setGain(const F32 gain)
{
    mGain = llclamp(gain, 0.f, 1.f);
}

template<class Peer>
void LLAudioSourceVO::checkCutOffRadiusFor(const Peer& object)
{
    if (mSourceMuted // already muted by something, will be recalculated on update()
        || !object.valid())
    {
        return;
    }

    F32 cutoff = object.cutoff();
    if (cutoff < 0.1f)
    {
        // consider cutoff below 0.1m as off (to avoid near zero comparison)
        return;
    }

    LLVector3d pos_global = getPosGlobalFor(object);
    if (!isInCutOffRadius(pos_global, cutoff))
    {
        mSourceMuted = true;
    }
}

template<class Peer>
LLVector3d LLAudioSourceVO::getPosGlobalFor(const Peer& object) const
{
    if (object.attachment()) return object.avatarRootPosition();
    return object.position();
}

bool LLAudioSourceVO::isInCutOffRadius(const LLVector3d pos_global, const F32 cutoff) const
{
    static LLCachedControl<S32> ear_mode(gSavedSettings, "MediaSoundsEarLocation", 0);

    LLVector3d pos_ear;

    switch (ear_mode())
    {
        case 0: // camera
            pos_ear = gAgentCamera.getCameraPositionGlobal();
            break;

        case 1: // avatar
            pos_ear = gAgent.getPositionGlobal();
            break;

        default:
            pos_ear = gAgentCamera.getCameraPositionGlobal();
            break;
    }
    LLVector3d to_vec = pos_global - pos_ear;

    F32 dist = (F32)to_vec.magVec();

    return dist < cutoff;
}

template<class Peer>
void LLAudioSourceVO::updateMuteFor(const Peer& object)
{
    if (!object.valid() || object.dead())
    {
        mSourceMuted = true;
        return;
    }

    bool mute = false;
    LLVector3d pos_global = getPosGlobalFor(object);

    F32 cutoff = object.cutoff();
    // Object can specify radius at which it turns off
    // consider cutoff below 0.1m as 'cutoff off'
    if (cutoff > 0.1f && !isInCutOffRadius(pos_global, cutoff))
    {
        mute = true;
    }
    // check if parcel allows sounds to pass border
    else if (!LLViewerParcelMgr::getInstance()->canHearSound(pos_global))
    {
        if (object.riding())
        {
            // Check if agent is riding this object
            // Agent can ride something out of region border and canHearSound
            // will treat object as not being part of agent's parcel.
            const LLUUID sound_root = object.root();
            const LLUUID agent_root = object.agentRoot();
            if (sound_root != agent_root)
            {
                mute = true;
            }
            else
            {
                LL_INFOS() << "roots identical" << LL_ENDL;
            }
        }
        else
        {
            mute = true;
        }
    }

    if (!mute)
    {
        if (LLMuteList::getInstance()->isMuted(object.id()))
        {
            mute = true;
        }
        else if (LLMuteList::getInstance()->isMuted(mOwnerID, LLMute::flagObjectSounds))
        {
            mute = true;
        }
        else if (object.attachment())
        {
            const LLUUID parent = object.avatarRoot();
            if (parent.notNull()
                && LLMuteList::getInstance()->isMuted(parent))
            {
                mute = true;
            }
        }
    }

    if (mute != mSourceMuted)
    {
        mSourceMuted = mute;
        if (mSourceMuted)
        {
            // Stop the sound.
            this->play(LLUUID::null);
        }
        else
        {
            // Muted sounds keep there data at all times, because
            // it's the place where the audio UUID is stored.
            // However, it's possible that mCurrentDatap is
            // NULL when this source did only preload sounds.
            if (mCurrentDatap)
            {
                // Restart the sound.
                this->play(mCurrentDatap->getID());
            }
        }
    }
}

template<class Peer>
void LLAudioSourceVO::updateFor(Peer& object)
{
    updateMuteFor(object);

    if (!object.valid())
    {
        return;
    }

    if (object.dead())
    {
        object.clear();
        return;
    }

    if (mSourceMuted)
    {
        return;
    }

    if (object.hud())
    {
        mPositionGlobal = gAgentCamera.getCameraPositionGlobal();
    }
    else
    {
        mPositionGlobal = object.position();
    }
    mVelocity = object.velocity();

    LLAudioSource::update();
}

void LLAudioSourceVO::update()
{
    if (mNativeObject)
    {
        NativeAudioObject object{mNativeObject()};
        updateFor(object);
    }
    else
    {
        ReferenceAudioObject object{mObjectp};
        updateFor(object);
    }
}

void LLAudioSourceVO::checkCutOffRadius()
{
    if (mNativeObject)
    {
        NativeAudioObject object{mNativeObject()};
        checkCutOffRadiusFor(object);
    }
    else
    {
        ReferenceAudioObject object{mObjectp};
        checkCutOffRadiusFor(object);
    }
}
