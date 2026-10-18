// Original LLViewerObject attached sound state machine shared by real peers.
// LGPL-2.1-or-later.
#pragma once
#include "llaudioengine.h"
#include "fsassetblacklist.h"
#include "llagent.h"
#include "lldefs.h"

template<class Source, class IsAttachment, class Position, class EnsureSource>
void vs_set_attached_sound(Source*& source, F32& savedGain, const LLUUID& audio_uuid,
    const LLUUID& owner_id, F32 gain, U8 flags, IsAttachment&& attachment,
    Position&& position, EnsureSource&& ensureSource)
{
    if (!gAudiop)
    {
        return;
    }

    if (audio_uuid.isNull())
    {
        if (!source)
        {
            return;
        }
        if (source->isLoop() && !source->hasPendingPreloads())
        {
            // We don't clear the sound if it's a loop, it'll go away on its own.
            // At least, this appears to be how the scripts work.
            // The attached sound ID is set to NULL to avoid it playing back when the
            // object rezzes in on non-looping sounds.
            //LL_INFOS() << "Clearing attached sound " << source->getCurrentData()->getID() << LL_ENDL;
            gAudiop->cleanupAudioSource(source);
            source = NULL;
        }
        else if (flags & LL_SOUND_FLAG_STOP)
        {
            // Just shut off the sound
            source->stop();
        }
        return;
    }

    // <FS:Ansariel> Asset blacklist
    FSAssetBlacklist& blacklist = FSAssetBlacklist::instance();
    if (blacklist.isBlacklisted(audio_uuid, LLAssetType::AT_SOUND))
    {
        return;
    }
    else if (attachment() && blacklist.isBlacklisted(owner_id, LLAssetType::AT_SOUND, FSAssetBlacklist::eBlacklistFlag::WORN))
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
        && source && source->isLoop() && source->getCurrentData()
        && source->getCurrentData()->getID() == audio_uuid)
    {
        //LL_INFOS() << "Already playing this sound on a loop, ignoring" << LL_ENDL;
        return;
    }

    // don't clean up before previous sound is done. Solves: SL-33486
    if ( source && source->isDone() )
    {
        gAudiop->cleanupAudioSource(source);
        source = NULL;
    }

    if (source && source->isMuted() &&
        source->getCurrentData() && source->getCurrentData()->getID() == audio_uuid)
    {
        //LL_INFOS() << "Already having this sound as muted sound, ignoring" << LL_ENDL;
        return;
    }

    ensureSource(owner_id);

    if (source)
    {
        bool queue = flags & LL_SOUND_FLAG_QUEUE;
        savedGain = gain;
        source->setGain(gain);
        source->setLoop(flags & LL_SOUND_FLAG_LOOP);
        source->setSyncMaster(flags & LL_SOUND_FLAG_SYNC_MASTER);
        source->setSyncSlave(flags & LL_SOUND_FLAG_SYNC_SLAVE);
        source->setQueueSounds(queue);
        if(!queue) // stop any current sound first to avoid "farts of doom" (SL-1541) -MG
        {
            source->stop();
        }

        // Play this sound if region maturity permits
        if( gAgent.canAccessMaturityAtGlobal(position()) )
        {
            //LL_INFOS() << "Playing attached sound " << audio_uuid << LL_ENDL;
            // recheck cutoff radius in case this update was an object-update with new value
            source->checkCutOffRadius();
            source->play(audio_uuid);
        }
    }
}
