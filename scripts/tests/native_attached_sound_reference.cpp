// Frozen original shared LLViewerObject attached-sound controller. LGPL-2.1.
void ReferenceObject::setAttachedSound(const LLUUID &audio_uuid, const LLUUID& owner_id, const F32 gain, const U8 flags)
{
    if (!gAudiop)
    {
        return;
    }

    if (audio_uuid.isNull())
    {
        if (!mAudioSourcep)
        {
            return;
        }
        if (mAudioSourcep->isLoop() && !mAudioSourcep->hasPendingPreloads())
        {
            // We don't clear the sound if it's a loop, it'll go away on its own.
            // At least, this appears to be how the scripts work.
            // The attached sound ID is set to NULL to avoid it playing back when the
            // object rezzes in on non-looping sounds.
            //LL_INFOS() << "Clearing attached sound " << mAudioSourcep->getCurrentData()->getID() << LL_ENDL;
            gAudiop->cleanupAudioSource(mAudioSourcep);
            mAudioSourcep = NULL;
        }
        else if (flags & LL_SOUND_FLAG_STOP)
        {
            // Just shut off the sound
            mAudioSourcep->stop();
        }
        return;
    }

    // <FS:Ansariel> Asset blacklist
    FSAssetBlacklist& blacklist = FSAssetBlacklist::instance();
    if (blacklist.isBlacklisted(audio_uuid, LLAssetType::AT_SOUND))
    {
        return;
    }
    else if (isAttachment() && blacklist.isBlacklisted(owner_id, LLAssetType::AT_SOUND, FSAssetBlacklist::eBlacklistFlag::WORN))
    {
        // Attachment sound
        return;
    }
    else if (blacklist.isBlacklisted(owner_id, LLAssetType::AT_SOUND, FSAssetBlacklist::eBlacklistFlag::REZZED))
    {
        // Rezzed object sound
        return;
    }
    // </FS:Ansariel>

    if (flags & LL_SOUND_FLAG_LOOP
        && mAudioSourcep && mAudioSourcep->isLoop() && mAudioSourcep->getCurrentData()
        && mAudioSourcep->getCurrentData()->getID() == audio_uuid)
    {
        //LL_INFOS() << "Already playing this sound on a loop, ignoring" << LL_ENDL;
        return;
    }

    // don't clean up before previous sound is done. Solves: SL-33486
    if ( mAudioSourcep && mAudioSourcep->isDone() )
    {
        gAudiop->cleanupAudioSource(mAudioSourcep);
        mAudioSourcep = NULL;
    }

    if (mAudioSourcep && mAudioSourcep->isMuted() &&
        mAudioSourcep->getCurrentData() && mAudioSourcep->getCurrentData()->getID() == audio_uuid)
    {
        //LL_INFOS() << "Already having this sound as muted sound, ignoring" << LL_ENDL;
        return;
    }

    getAudioSource(owner_id);

    if (mAudioSourcep)
    {
        bool queue = flags & LL_SOUND_FLAG_QUEUE;
        mAudioGain = gain;
        mAudioSourcep->setGain(gain);
        mAudioSourcep->setLoop(flags & LL_SOUND_FLAG_LOOP);
        mAudioSourcep->setSyncMaster(flags & LL_SOUND_FLAG_SYNC_MASTER);
        mAudioSourcep->setSyncSlave(flags & LL_SOUND_FLAG_SYNC_SLAVE);
        mAudioSourcep->setQueueSounds(queue);
        if(!queue) // stop any current sound first to avoid "farts of doom" (SL-1541) -MG
        {
            mAudioSourcep->stop();
        }

        // Play this sound if region maturity permits
        if( gAgent.canAccessMaturityAtGlobal(this->getPositionGlobal()) )
        {
            //LL_INFOS() << "Playing attached sound " << audio_uuid << LL_ENDL;
            // recheck cutoff radius in case this update was an object-update with new value
            mAudioSourcep->checkCutOffRadius();
            mAudioSourcep->play(audio_uuid);
        }
    }
}
