void LLVOAvatar::idleUpdateLipSync(bool voice_enabled)
{
    // Use the Lipsync_Ooh and Lipsync_Aah morphs for lip sync
    if (voice_enabled
        && mLastRezzedStatus > 0 // no point updating lip-sync for clouds
        && LLVoiceVisualizer::getLipSyncEnabled()
        && LLVoiceClient::getInstance()->getIsSpeaking(mID))
    {
        F32 ooh_morph_amount = 0.0f;
        F32 aah_morph_amount = 0.0f;

        mVoiceVisualizer->lipSyncOohAah( ooh_morph_amount, aah_morph_amount );

        if (mOohMorph)
        {
            F32 ooh_weight = mOohMorph->getMinWeight()
                + ooh_morph_amount * (mOohMorph->getMaxWeight() - mOohMorph->getMinWeight());

            // <FS:Ansariel> [Legacy Bake]
            //mOohMorph->setWeight( ooh_weight);
            mOohMorph->setWeight( ooh_weight, false);
        }

        if (mAahMorph)
        {
            F32 aah_weight = mAahMorph->getMinWeight()
                + aah_morph_amount * (mAahMorph->getMaxWeight() - mAahMorph->getMinWeight());

            // <FS:Ansariel> [Legacy Bake]
            //mAahMorph->setWeight( aah_weight);
            mAahMorph->setWeight( aah_weight, false);
        }

        mLipSyncActive = true;
        LLCharacter::updateVisualParams();
        dirtyMesh();
    }
}