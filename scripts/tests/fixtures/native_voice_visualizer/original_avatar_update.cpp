void LLVOAvatar::idleUpdateVoiceVisualizer(bool voice_enabled, const LLVector3 &position)
{
    bool render_visualizer = voice_enabled;

    // Don't render the user's own voice visualizer when in mouselook, or when opening the mic is disabled.
    if(isSelf())
    {
        static LLCachedControl<bool> voice_disable_mic(gSavedSettings, "VoiceDisableMic");
        static LLCachedControl<bool> fsShowMyOwnVoiceVisualizer(gSavedSettings, "FSShowMyOwnVoiceVisualizer"); // <FS:PP> FIRE-21210: Don't show my voice visualizer
        if (gAgentCamera.cameraMouselook() || voice_disable_mic || !fsShowMyOwnVoiceVisualizer)
        {
            render_visualizer = false;
        }
    }

    // <FS:Ansariel> FIRE-1916: Hide voice dots over avatars
    static LLCachedControl<bool> voiceVisualizerEnabled(gSavedSettings, "VoiceVisualizerEnabled");
    if (!voiceVisualizerEnabled)
    {
        render_visualizer = false;
    }
    // </FS:Ansariel>

    mVoiceVisualizer->setVoiceEnabled(render_visualizer);

    if ( voice_enabled )
    {
        //----------------------------------------------------------------
        // Only do gesture triggering for your own avatar, and only when you're in a proximal channel.
        //----------------------------------------------------------------
        if( isSelf() )
        {
            //----------------------------------------------------------------------------------------
            // The following takes the voice signal and uses that to trigger gesticulations.
            //----------------------------------------------------------------------------------------
            int lastGesticulationLevel = mCurrentGesticulationLevel;
            mCurrentGesticulationLevel = mVoiceVisualizer->getCurrentGesticulationLevel();

            //---------------------------------------------------------------------------------------------------
            // If "current gesticulation level" changes, we catch this, and trigger the new gesture
            //---------------------------------------------------------------------------------------------------
            if ( lastGesticulationLevel != mCurrentGesticulationLevel )
            {
                if ( mCurrentGesticulationLevel != VOICE_GESTICULATION_LEVEL_OFF )
                {
                    std::string gestureString = "unInitialized";
                    if ( mCurrentGesticulationLevel == 0 )  { gestureString = "/voicelevel1";   }
                    else    if ( mCurrentGesticulationLevel == 1 )  { gestureString = "/voicelevel2";   }
                    else    if ( mCurrentGesticulationLevel == 2 )  { gestureString = "/voicelevel3";   }
                    else    { LL_INFOS() << "oops - CurrentGesticulationLevel can be only 0, 1, or 2"  << LL_ENDL; }

                    // this is the call that Karl S. created for triggering gestures from within the code.
                    LLGestureMgr::instance().triggerAndReviseString( gestureString );
                }
            }

        } //if( isSelf() )

        //-----------------------------------------------------------------------------------------------------------------
        // If the avatar is speaking, then the voice amplitude signal is passed to the voice visualizer.
        // Also, here we trigger voice visualizer start and stop speaking, so it can animate the voice symbol.
        //
        // Notice the calls to "gAwayTimer.reset()". This resets the timer that determines how long the avatar has been
        // "away", so that the avatar doesn't lapse into away-mode (and slump over) while the user is still talking.
        //-----------------------------------------------------------------------------------------------------------------
        if (LLVoiceClient::getInstance()->getIsSpeaking( mID ) && (!isInMuteList() || isSelf()))
        {
            if (!mVoiceVisualizer->getCurrentlySpeaking())
            {
                mVoiceVisualizer->setStartSpeaking();

                //printf( "gAwayTimer.reset();\n" );
            }

            mVoiceVisualizer->setSpeakingAmplitude( LLVoiceClient::getInstance()->getCurrentPower( mID ) );

            if( isSelf() )
            {
                gAgent.clearAFK();
            }
        }
        else
        {
            if ( mVoiceVisualizer->getCurrentlySpeaking() )
            {
                mVoiceVisualizer->setStopSpeaking();

                if ( mLipSyncActive )
                {
                    // <FS:Ansariel> [Legacy Bake]
                    //if( mOohMorph ) mOohMorph->setWeight(mOohMorph->getMinWeight());
                    //if( mAahMorph ) mAahMorph->setWeight(mAahMorph->getMinWeight());
                    if( mOohMorph ) mOohMorph->setWeight(mOohMorph->getMinWeight(), false);
                    if( mAahMorph ) mAahMorph->setWeight(mAahMorph->getMinWeight(), false);
                    // </FS:Ansariel> [Legacy Bake]

                    mLipSyncActive = false;
                    LLCharacter::updateVisualParams();
                    dirtyMesh();
                }
            }
        }
        mVoiceVisualizer->setPositionAgent(position);
    }//if ( voiceEnabled )
}