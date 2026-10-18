void LLVoiceVisualizer::render()
{
    static LLCachedControl<bool> show_visualizer(gSavedSettings, "VoiceVisualizerEnabled", true);
    if (!mVoiceEnabled || !show_visualizer)
    {
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if ( mSoundSymbol.mActive )
    {
        mPreviousTime = mCurrentTime;
        mCurrentTime = mTimer.getTotalSeconds();

        //---------------------------------------------------------------
        // set the sound symbol position over the source (avatar's head)
        //---------------------------------------------------------------
        mSoundSymbol.mPosition = gAgent.getPosAgentFromGlobal(mPositionGlobal) + WORLD_UPWARD_DIRECTION * HEIGHT_ABOVE_HEAD;

        //---------------------------------------------------------------
        // some gl state
        //---------------------------------------------------------------
        LLGLSPipelineAlpha alpha_blend;
        LLGLDepthTest depth(GL_TRUE, GL_FALSE);
        //LLGLDisable gls_stencil(GL_STENCIL_TEST);

        //-------------------------------------------------------------
        // create coordinates of the geometry for the dot
        //-------------------------------------------------------------
        LLViewerCamera* camera = LLViewerCamera::getInstance();
        LLVector3 l = camera->getLeftAxis() * DOT_SIZE;
        LLVector3 u = camera->getUpAxis()   * DOT_SIZE;

        LLVector3 bottomLeft    = mSoundSymbol.mPosition + l - u;
        LLVector3 bottomRight   = mSoundSymbol.mPosition - l - u;
        LLVector3 topLeft       = mSoundSymbol.mPosition + l + u;
        LLVector3 topRight      = mSoundSymbol.mPosition - l + u;

        //-----------------------------
        // bind texture 0 (the dot)
        //-----------------------------
        gGL.getTexUnit(0)->bind(mSoundSymbol.mTexture[0]);

        // <FS:PP> FIRE-22570: Option to show visualizers as waves without the dot
        static LLCachedControl<bool> fsShowVoiceVisualizerWithDot(gSavedSettings, "FSShowVoiceVisualizerWithDot");
        if (fsShowVoiceVisualizerWithDot)
        {
        // </FS:PP>

            //-------------------------------------------------------------
            // now render the dot
            //-------------------------------------------------------------
            gGL.color4fv( LLColor4( 1.0f, 1.0f, 1.0f, DOT_OPACITY ).mV );

            gGL.begin( LLRender::TRIANGLE_STRIP );
                gGL.texCoord2i( 0,  0   ); gGL.vertex3fv( bottomLeft.mV );
                gGL.texCoord2i( 1,  0   ); gGL.vertex3fv( bottomRight.mV );
                gGL.texCoord2i( 0,  1   ); gGL.vertex3fv( topLeft.mV );
            gGL.end();

            gGL.begin( LLRender::TRIANGLE_STRIP );
                gGL.texCoord2i( 1,  0   ); gGL.vertex3fv( bottomRight.mV );
                gGL.texCoord2i( 1,  1   ); gGL.vertex3fv( topRight.mV );
                gGL.texCoord2i( 0,  1   ); gGL.vertex3fv( topLeft.mV );
            gGL.end();

        // <FS:PP> FIRE-22570: Option to show visualizers as waves without the dot
        }
        // </FS:PP>

        //--------------------------------------------------------------------------------------
        // if currently speaking, trigger waves (1 through 6) based on speaking amplitude
        //--------------------------------------------------------------------------------------
        if ( mCurrentlySpeaking )
        {
            F32 min = 0.2f;
            F32 max = 0.7f;
            F32 fraction = ( mSpeakingAmplitude - min ) / ( max - min );

            // in case mSpeakingAmplitude > max....
            if ( fraction > 1.0f )
            {
                fraction = 1.0f;
            }

            S32 level = 1 + (int)( fraction * ( NUM_VOICE_SYMBOL_WAVES - 2 ) );

            for (int i=0; i<level+1; i++)
            {
                mSoundSymbol.mWaveActive            [i] = true;
                mSoundSymbol.mWaveOpacity           [i] = 1.0f;
                mSoundSymbol.mWaveFadeOutStartTime  [i] = mCurrentTime;
            }

        } // if currently speaking

        //---------------------------------------------------
        // determine color
        //---------------------------------------------------
        F32 red     = 0.0f;
        F32 green   = 0.0f;
        F32 blue    = 0.0f;
        if ( mSpeakingAmplitude < RED_THRESHOLD )
        {
            if ( mSpeakingAmplitude < GREEN_THRESHOLD )
            {
                red     = BASE_BRIGHTNESS;
                green   = BASE_BRIGHTNESS;
                blue    = BASE_BRIGHTNESS;
            }
            else
            {
                //---------------------------------------------------
                // fade from gray to bright green
                //---------------------------------------------------
                F32 fraction = ( mSpeakingAmplitude - GREEN_THRESHOLD ) / ( 1.0f - GREEN_THRESHOLD );
                red     = BASE_BRIGHTNESS - ( fraction * BASE_BRIGHTNESS );
                green   = BASE_BRIGHTNESS +   fraction * ( 1.0f - BASE_BRIGHTNESS );
                blue    = BASE_BRIGHTNESS - ( fraction * BASE_BRIGHTNESS );
            }
        }
        else
        {
            //---------------------------------------------------
            // redish
            //---------------------------------------------------
            red     = 1.0f;
            green   = 0.2f;
            blue    = 0.2f;
        }

        for (int i=0; i<NUM_VOICE_SYMBOL_WAVES; i++)
        {
            if ( mSoundSymbol.mWaveActive[i] )
            {
                F32 fadeOutFraction = (F32)( mCurrentTime - mSoundSymbol.mWaveFadeOutStartTime[i] ) / FADE_OUT_DURATION;

                mSoundSymbol.mWaveOpacity[i] = 1.0f - fadeOutFraction;

                if ( mSoundSymbol.mWaveOpacity[i] < 0.0f )
                {
                    mSoundSymbol.mWaveFadeOutStartTime  [i] = mCurrentTime;
                    mSoundSymbol.mWaveOpacity           [i] = 0.0f;
                    mSoundSymbol.mWaveActive            [i] = false;
                }

                //----------------------------------------------------------------------------------
                // This is where we calculate the expansion of the waves - that is, the
                // rate at which they are scaled greater than 1.0 so that they grow over time.
                //----------------------------------------------------------------------------------
                F32 timeSlice = (F32)( mCurrentTime - mPreviousTime );
                F32 waveSpeed = mSpeakingAmplitude * WAVE_MOTION_RATE;
                mSoundSymbol.mWaveExpansion[i] *= ( 1.0f + EXPANSION_RATE * timeSlice * waveSpeed );

                if ( mSoundSymbol.mWaveExpansion[i] > EXPANSION_MAX )
                {
                    mSoundSymbol.mWaveExpansion[i] = 1.0f;
                }

                //----------------------------------------------------------------------------------
                // create geometry for the wave billboard textures
                //----------------------------------------------------------------------------------
                F32 width   = i * WAVE_WIDTH_SCALE  * mSoundSymbol.mWaveExpansion[i];
                F32 height  = i * WAVE_HEIGHT_SCALE * mSoundSymbol.mWaveExpansion[i];

                LLVector3 l = camera->getLeftAxis() * width;
                LLVector3 u = camera->getUpAxis()   * height;

                LLVector3 bottomLeft    = mSoundSymbol.mPosition + l - u;
                LLVector3 bottomRight   = mSoundSymbol.mPosition - l - u;
                LLVector3 topLeft       = mSoundSymbol.mPosition + l + u;
                LLVector3 topRight      = mSoundSymbol.mPosition - l + u;

                gGL.color4fv( LLColor4( red, green, blue, mSoundSymbol.mWaveOpacity[i] ).mV );
                gGL.getTexUnit(0)->bind(mSoundSymbol.mTexture[i]);


                //---------------------------------------------------
                // now, render the mofo
                //---------------------------------------------------
                gGL.begin( LLRender::TRIANGLE_STRIP );
                    gGL.texCoord2i( 0, 0 ); gGL.vertex3fv( bottomLeft.mV );
                    gGL.texCoord2i( 1, 0 ); gGL.vertex3fv( bottomRight.mV );
                    gGL.texCoord2i( 0, 1 ); gGL.vertex3fv( topLeft.mV );
                gGL.end();

                gGL.begin( LLRender::TRIANGLE_STRIP );
                    gGL.texCoord2i( 1, 0 ); gGL.vertex3fv( bottomRight.mV );
                    gGL.texCoord2i( 1, 1 ); gGL.vertex3fv( topRight.mV );
                    gGL.texCoord2i( 0, 1 ); gGL.vertex3fv( topLeft.mV );
                gGL.end();

            } //if ( mSoundSymbol.mWaveActive[i] )

        }// for loop

    }//if ( mSoundSymbol.mActive )

}