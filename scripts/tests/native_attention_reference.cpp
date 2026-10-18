// Frozen reference attention methods at 6be74629db; LGPL-2.1.
void LLHUDEffectPointAt::packData(LLMessageSystem *mesgsys)
{
    LLViewerObject* source_object = (LLViewerObject*)mSourceObject;
    if (!source_object)
    {
        markDead();
        return;
    }
    else if (!source_object->isAvatar())
    {
        LL_DEBUGS("HUDEffect")<<"Non-Avatar HUDEffectPointAt message for ID: "
        <<  source_object->getID().asString()<< LL_ENDL;
        markDead();
        return;
    }
    else
    {
        LLVOAvatar* source_avatar = (LLVOAvatar*)source_object;
        if (!source_avatar->isSelf())
        {
            LL_DEBUGS("HUDEffect")<<"Non-self HUDEffectPointAt message for ID: "
            << source_avatar->getID().asString()<< LL_ENDL;
            markDead();
            return;
        }
    }
    // Pack the default data
    LLHUDEffect::packData(mesgsys);

    // Pack the type-specific data.  Uses a fun packed binary format.  Whee!
    U8 packed_data[PKT_SIZE];
    memset(packed_data, 0, PKT_SIZE);

    if (mSourceObject)
    {
        htolememcpy(&(packed_data[SOURCE_AVATAR]), mSourceObject->mID.mData, MVT_LLUUID, 16);
    }
    else
    {
        htolememcpy(&(packed_data[SOURCE_AVATAR]), LLUUID::null.mData, MVT_LLUUID, 16);
    }

    // pack both target object and position
    // position interpreted as offset if target object is non-null
    if (mTargetObject)
    {
        htolememcpy(&(packed_data[TARGET_OBJECT]), mTargetObject->mID.mData, MVT_LLUUID, 16);
    }
    else
    {
        htolememcpy(&(packed_data[TARGET_OBJECT]), LLUUID::null.mData, MVT_LLUUID, 16);
    }

    htolememcpy(&(packed_data[TARGET_POS]), mTargetOffsetGlobal.mdV, MVT_LLVector3d, 24);

    U8 pointAtTypePacked = (U8)mTargetType;
    htolememcpy(&(packed_data[POINTAT_TYPE]), &pointAtTypePacked, MVT_U8, 1);

    mesgsys->addBinaryDataFast(_PREHASH_TypeData, packed_data, PKT_SIZE);

    mLastSendTime = mTimer.getElapsedTimeF32();
}
void LLHUDEffectLookAt::update()
{
    // If the target object is dead, set the target object to NULL
    if (!mTargetObject.isNull() && mTargetObject->isDead())
    {
        clearLookAtTarget();
    }

    // if source avatar is null or dead, mark self as dead and return
    if (mSourceObject.isNull() || mSourceObject->isDead())
    {
        markDead();
        return;
    }

    // make sure the proper set of avatar attention are currently being used.
    LLVOAvatar* source_avatar = (LLVOAvatar*)(LLViewerObject*)mSourceObject;
    // for now the first cut will just switch on sex. future development could adjust
    // timeouts according to avatar age and/or other features.
    mAttentions = (source_avatar->getSex() == SEX_MALE) ? &gBoyAttentions : &gGirlAttentions;
    //printf("updated to %s\n", (source_avatar->getSex() == SEX_MALE) ? "male" : "female");

    F32 time = mTimer.getElapsedTimeF32();

    // clear out the effect if time is up
    if (mKillTime != 0.f && time > mKillTime)
    {
        if (mTargetType != LOOKAT_TARGET_NONE)
        {
            clearLookAtTarget();
            // look at timed out (only happens on own avatar), so tell everyone
            setNeedsSendToSim(true);
        }
    }

    if (mTargetType != LOOKAT_TARGET_NONE)
    {
        if (calcTargetPosition())
        {
            static LLCachedControl<bool> disable_look_at(gSavedSettings, "DisableLookAtAnimation", true);
            LLMotion* head_motion = ((LLVOAvatar*)(LLViewerObject*)mSourceObject)->findMotion(ANIM_AGENT_HEAD_ROT);
            if (disable_look_at())
            {
                if (head_motion)
                {
                    ((LLVOAvatar*)(LLViewerObject*)mSourceObject)->stopMotion(ANIM_AGENT_HEAD_ROT);
                }
            }
            else if (!head_motion || head_motion->isStopped())
            {
                ((LLVOAvatar*)(LLViewerObject*)mSourceObject)->startMotion(ANIM_AGENT_HEAD_ROT);
            }
        }
    }

    // <FS:Ansariel> FIRE-12878: Don't render debug text for lookats
    //if (mDebugLookAt)
    //{
    //  ((LLVOAvatar*)(LLViewerObject*)mSourceObject)->addDebugText((*mAttentions)[mTargetType].mName);
    //}
    // </FS:Ansariel>
}