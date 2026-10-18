/**
 * @File llvoavatar.cpp
 * @brief Implementation of LLVOAvatar class which is a derivation of LLViewerObject
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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
// Shared original avatar speaking, gesture and lipsync policy. LGPL-2.1.
#pragma once
#include "llagent.h"
#include "llagentcamera.h"
#include "llviewercontrol.h"
#include "llgesturemgr.h"
#include "llvoiceclient.h"
#include "llvoicevisualizer.h"
struct VSAvatarVoicePolicy
{
    template<class Avatar> static void update(Avatar& avatar, bool voice_enabled, const LLVector3 &position)
{
    bool render_visualizer = voice_enabled;

    // Don't render the user's own voice visualizer when in mouselook, or when opening the mic is disabled.
    if(avatar.isSelf())
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

    avatar.mVoiceVisualizer->setVoiceEnabled(render_visualizer);

    if ( voice_enabled )
    {
        //----------------------------------------------------------------
        // Only do gesture triggering for your own avatar, and only when you're in a proximal channel.
        //----------------------------------------------------------------
        if( avatar.isSelf() )
        {
            //----------------------------------------------------------------------------------------
            // The following takes the voice signal and uses that to trigger gesticulations.
            //----------------------------------------------------------------------------------------
            int lastGesticulationLevel = avatar.mCurrentGesticulationLevel;
            avatar.mCurrentGesticulationLevel = avatar.mVoiceVisualizer->getCurrentGesticulationLevel();

            //---------------------------------------------------------------------------------------------------
            // If "current gesticulation level" changes, we catch this, and trigger the new gesture
            //---------------------------------------------------------------------------------------------------
            if ( lastGesticulationLevel != avatar.mCurrentGesticulationLevel )
            {
                if ( avatar.mCurrentGesticulationLevel != VOICE_GESTICULATION_LEVEL_OFF )
                {
                    std::string gestureString = "unInitialized";
                    if ( avatar.mCurrentGesticulationLevel == 0 )  { gestureString = "/voicelevel1";   }
                    else    if ( avatar.mCurrentGesticulationLevel == 1 )  { gestureString = "/voicelevel2";   }
                    else    if ( avatar.mCurrentGesticulationLevel == 2 )  { gestureString = "/voicelevel3";   }
                    else    { LL_INFOS() << "oops - CurrentGesticulationLevel can be only 0, 1, or 2"  << LL_ENDL; }

                    // this is the call that Karl S. created for triggering gestures from within the code.
                    LLGestureMgr::instance().triggerAndReviseString( gestureString );
                }
            }

        } //if( avatar.isSelf() )

        //-----------------------------------------------------------------------------------------------------------------
        // If the avatar is speaking, then the voice amplitude signal is passed to the voice visualizer.
        // Also, here we trigger voice visualizer start and stop speaking, so it can animate the voice symbol.
        //
        // Notice the calls to "gAwayTimer.reset()". This resets the timer that determines how long the avatar has been
        // "away", so that the avatar doesn't lapse into away-mode (and slump over) while the user is still talking.
        //-----------------------------------------------------------------------------------------------------------------
        if (LLVoiceClient::getInstance()->getIsSpeaking( avatar.getID() ) && (!avatar.isInMuteList() || avatar.isSelf()))
        {
            if (!avatar.mVoiceVisualizer->getCurrentlySpeaking())
            {
                avatar.mVoiceVisualizer->setStartSpeaking();

                //printf( "gAwayTimer.reset();\n" );
            }

            avatar.mVoiceVisualizer->setSpeakingAmplitude( LLVoiceClient::getInstance()->getCurrentPower( avatar.getID() ) );

            if( avatar.isSelf() )
            {
                gAgent.clearAFK();
            }
        }
        else
        {
            if ( avatar.mVoiceVisualizer->getCurrentlySpeaking() )
            {
                avatar.mVoiceVisualizer->setStopSpeaking();

                if ( avatar.mLipSyncActive )
                {
                    // <FS:Ansariel> [Legacy Bake]
                    //if( avatar.mOohMorph ) avatar.mOohMorph->setWeight(avatar.mOohMorph->getMinWeight());
                    //if( avatar.mAahMorph ) avatar.mAahMorph->setWeight(avatar.mAahMorph->getMinWeight());
                    if( avatar.mOohMorph ) avatar.mOohMorph->setWeight(avatar.mOohMorph->getMinWeight(), false);
                    if( avatar.mAahMorph ) avatar.mAahMorph->setWeight(avatar.mAahMorph->getMinWeight(), false);
                    // </FS:Ansariel> [Legacy Bake]

                    avatar.mLipSyncActive = false;
                    avatar.LLCharacter::updateVisualParams();
                    avatar.dirtyMesh();
                }
            }
        }
        avatar.mVoiceVisualizer->setPositionAgent(position);
    }//if ( voiceEnabled )
}
    template<class Avatar> static void lipSync(Avatar& avatar, bool voice_enabled)
{
    // Use the Lipsync_Ooh and Lipsync_Aah morphs for lip sync
    if (voice_enabled
        && avatar.mLastRezzedStatus > 0 // no point updating lip-sync for clouds
        && LLVoiceVisualizer::getLipSyncEnabled()
        && LLVoiceClient::getInstance()->getIsSpeaking(avatar.getID()))
    {
        F32 ooh_morph_amount = 0.0f;
        F32 aah_morph_amount = 0.0f;

        avatar.mVoiceVisualizer->lipSyncOohAah( ooh_morph_amount, aah_morph_amount );

        if (avatar.mOohMorph)
        {
            F32 ooh_weight = avatar.mOohMorph->getMinWeight()
                + ooh_morph_amount * (avatar.mOohMorph->getMaxWeight() - avatar.mOohMorph->getMinWeight());

            // <FS:Ansariel> [Legacy Bake]
            //avatar.mOohMorph->setWeight( ooh_weight);
            avatar.mOohMorph->setWeight( ooh_weight, false);
        }

        if (avatar.mAahMorph)
        {
            F32 aah_weight = avatar.mAahMorph->getMinWeight()
                + aah_morph_amount * (avatar.mAahMorph->getMaxWeight() - avatar.mAahMorph->getMinWeight());

            // <FS:Ansariel> [Legacy Bake]
            //avatar.mAahMorph->setWeight( aah_weight);
            avatar.mAahMorph->setWeight( aah_weight, false);
        }

        avatar.mLipSyncActive = true;
        avatar.LLCharacter::updateVisualParams();
        avatar.dirtyMesh();
    }
}
};
