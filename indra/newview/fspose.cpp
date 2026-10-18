/*
 * @file fspose.cpp
 * @brief Pose stuff
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Cinder Roxley wrote this file. As long as you retain this notice you can do
 * whatever you want with this stuff. If we meet some day, and you think this
 * stuff is worth it, you can buy me a beer in return <cinder.roxley@phoenixviewer.com>
 * ----------------------------------------------------------------------------
 */

#include "llviewerprecompiledheaders.h"
#include "fspose.h"

#include "llagent.h"
#include "vsavatarservices.h"
#include "vsnativeim.h"
#include "llvoavatarself.h"

FSPose::FSPose()
:   mCurrentPose(LLUUID::null)
{
}

FSPose::~FSPose()
{
}

void FSPose::setPose(const std::string& new_pose, bool save_state)
{
    if (vs_avatar_character() && LLUUID::validate(new_pose))
    {
        if (!mCurrent()) mCurrentPose.setNull();
        if (mCurrentPose.notNull())
        {
            gAgent.sendAnimationRequest(mCurrentPose, ANIM_REQUEST_STOP);
        }
        if (save_state)
        {
            mCurrentPose.set(new_pose);
            const auto account = vs_native_im_guard();
            auto* character = vs_avatar_character();
            const U64 generation = vs_avatar_generation();
            mCurrent = [account, character, generation]
            { return account() && vs_avatar_character() == character && vs_avatar_generation() == generation; };

        }
        gAgent.sendAnimationRequest(LLUUID(new_pose), ANIM_REQUEST_START);
    }
}

void FSPose::stopPose()
{
    if (!mCurrent()) { mCurrentPose.setNull(); return; }
    if (vs_avatar_character() && mCurrentPose.notNull())
    {
        gAgent.sendAnimationRequest(mCurrentPose, ANIM_REQUEST_STOP);
        mCurrentPose.setNull();
    }
}

void FSPose::retireAccount()
{
    mCurrentPose.setNull();
    mCurrent = [] { return false; };
}
