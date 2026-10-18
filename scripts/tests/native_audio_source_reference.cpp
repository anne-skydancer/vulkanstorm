// Frozen actual source mute/position/cutoff policy. LGPL-2.1.
void OriginalAudio::checkCutOffRadius()
{
    if (mSourceMuted // already muted by something, will be recalculated on update()
        || !mObjectp)
    {
        return;
    }

    F32 cutoff = mObjectp->getSoundCutOffRadius();
    if (cutoff < 0.1f)
    {
        // consider cutoff below 0.1m as off (to avoid near zero comparison)
        return;
    }

    LLVector3d pos_global = getPosGlobal();
    if (!isInCutOffRadius(pos_global, cutoff))
    {
        mSourceMuted = true;
    }
}
LLVector3d OriginalAudio::getPosGlobal() const
{
    if (mObjectp->isAttachment())
    {
        LLViewerObject* parent = mObjectp;
        while (parent && !parent->isAvatar())
        {
            parent = (LLViewerObject*)parent->getParent();
        }
        if (parent)
        {
            return parent->getPositionGlobal();
        }
    }
    else
    {
        return mObjectp->getPositionGlobal();
    }
    return LLVector3d();
}
bool OriginalAudio::isInCutOffRadius(const LLVector3d pos_global, const F32 cutoff) const
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
void OriginalAudio::updateMute()
{
    if (!mObjectp || mObjectp->isDead())
    {
        mSourceMuted = true;
        return;
    }

    bool mute = false;
    LLVector3d pos_global = getPosGlobal();

    F32 cutoff = mObjectp->getSoundCutOffRadius();
    // Object can specify radius at which it turns off
    // consider cutoff below 0.1m as 'cutoff off'
    if (cutoff > 0.1f && !isInCutOffRadius(pos_global, cutoff))
    {
        mute = true;
    }
    // check if parcel allows sounds to pass border
    else if (!LLViewerParcelMgr::getInstance()->canHearSound(pos_global))
    {
        if (isAgentAvatarValid() && gAgentAvatarp->getParent())
        {
            // Check if agent is riding this object
            // Agent can ride something out of region border and canHearSound
            // will treat object as not being part of agent's parcel.
            LLViewerObject *sound_root = (LLViewerObject*)mObjectp->getRoot();
            LLViewerObject *agent_root = (LLViewerObject*)gAgentAvatarp->getRoot();
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
        if (LLMuteList::getInstance()->isMuted(mObjectp->getID()))
        {
            mute = true;
        }
        else if (LLMuteList::getInstance()->isMuted(mOwnerID, LLMute::flagObjectSounds))
        {
            mute = true;
        }
        else if (mObjectp->isAttachment())
        {
            LLViewerObject* parent = mObjectp;
            while (parent && !parent->isAvatar())
            {
                parent = (LLViewerObject*)parent->getParent();
            }
            if (parent
                && LLMuteList::getInstance()->isMuted(parent->getID()))
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
void OriginalAudio::update()
{
    updateMute();

    if (!mObjectp)
    {
        return;
    }

    if (mObjectp->isDead())
    {
        mObjectp = NULL;
        return;
    }

    if (mSourceMuted)
    {
        return;
    }

    if (mObjectp->isHUDAttachment())
    {
        mPositionGlobal = gAgentCamera.getCameraPositionGlobal();
    }
    else
    {
        mPositionGlobal = mObjectp->getPositionGlobal();
    }
    if (mObjectp->getSubParent())
    {
        mVelocity = mObjectp->getSubParent()->getVelocity();
    }
    else
    {
        mVelocity = mObjectp->getVelocity();
    }

    LLAudioSource::update();
}
