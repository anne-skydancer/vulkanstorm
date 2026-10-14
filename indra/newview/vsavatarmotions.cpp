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

#include "llviewerprecompiledheaders.h"
#include "vsavatarmotions.h"
#include "llviewercontrol.h"
#include "llcharacter.h"
#include "llanimationstates.h"
#include "llkeyframestandmotion.h"
#include "llkeyframewalkmotion.h"
#include "llkeyframefallmotion.h"
#include "llkeyframemotion.h"
#include "llheadrotmotion.h"
#include "llhandmotion.h"
#include "lleditingmotion.h"
#include "lltargetingmotion.h"
#include "llphysicsmotion.h"
#include "llemote.h"
#include "noise.h"

namespace
{

constexpr F32 TORSO_NOISE_AMOUNT = 1.0f, TORSO_NOISE_SPEED = .2f, BREATHE_ROT_MOTION_STRENGTH = .05f;
constexpr S32 MIN_REQUIRED_PIXEL_AREA_BODY_NOISE = 10000, MIN_REQUIRED_PIXEL_AREA_BREATHE = 10000, MIN_REQUIRED_PIXEL_AREA_PELVIS_FIX = 40;
class LLBodyNoiseMotion : public LLMotion
{
public:
    // Constructor
    LLBodyNoiseMotion(const LLUUID& id) : LLMotion(id)
    {
        mName       = "body_noise";
        mTorsoState = new LLJointState;
    }

    // Destructor
    virtual ~LLBodyNoiseMotion() {}

public:
    //-------------------------------------------------------------------------
    // functions to support MotionController and MotionRegistry
    //-------------------------------------------------------------------------
    // static constructor
    // all subclasses must implement such a function and register it
    static LLMotion* create(const LLUUID& id) { return new LLBodyNoiseMotion(id); }

public:
    //-------------------------------------------------------------------------
    // animation callbacks to be implemented by subclasses
    //-------------------------------------------------------------------------

    // motions must specify whether or not they loop
    virtual bool getLoop() { return true; }

    // motions must report their total duration
    virtual F32 getDuration() { return 0.0; }

    // motions must report their "ease in" duration
    virtual F32 getEaseInDuration() { return 0.0; }

    // motions must report their "ease out" duration.
    virtual F32 getEaseOutDuration() { return 0.0; }

    // motions must report their priority
    virtual LLJoint::JointPriority getPriority() { return LLJoint::HIGH_PRIORITY; }

    virtual LLMotionBlendType getBlendType() { return ADDITIVE_BLEND; }

    // called to determine when a motion should be activated/deactivated based on avatar pixel coverage
    virtual F32 getMinPixelArea() { return MIN_REQUIRED_PIXEL_AREA_BODY_NOISE; }

    // run-time (post constructor) initialization,
    // called after parameters have been set
    // must return true to indicate success and be available for activation
    virtual LLMotionInitStatus onInitialize(LLCharacter* character)
    {
        if (!mTorsoState->setJoint(character->getJoint("mTorso")))
        {
            return STATUS_FAILURE;
        }

        mTorsoState->setUsage(LLJointState::ROT);

        addJointState(mTorsoState);
        return STATUS_SUCCESS;
    }

    // called when a motion is activated
    // must return true to indicate success, or else
    // it will be deactivated
    virtual bool onActivate() { return true; }

    // called per time step
    // must return true while it is active, and
    // must return false when the motion is completed.
    virtual bool onUpdate(F32 time, U8* joint_mask)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
        F32 nx[2];
        nx[0] = time * TORSO_NOISE_SPEED;
        nx[1] = 0.0f;
        F32 ny[2];
        ny[0]      = 0.0f;
        ny[1]      = time * TORSO_NOISE_SPEED;
        F32 noiseX = noise2(nx);
        F32 noiseY = noise2(ny);

        F32          rx = TORSO_NOISE_AMOUNT * DEG_TO_RAD * noiseX / 0.42f;
        F32          ry = TORSO_NOISE_AMOUNT * DEG_TO_RAD * noiseY / 0.42f;
        LLQuaternion tQn;
        tQn.setQuat(rx, ry, 0.0f);
        mTorsoState->setRotation(tQn);

        return true;
    }

    // called when a motion is deactivated
    virtual void onDeactivate() {}

private:
    //-------------------------------------------------------------------------
    // joint states to be animated
    //-------------------------------------------------------------------------
    LLPointer<LLJointState> mTorsoState;
};

//-----------------------------------------------------------------------------
// class LLBreatheMotionRot
//-----------------------------------------------------------------------------
class LLBreatheMotionRot : public LLMotion
{
public:
    // Constructor
    LLBreatheMotionRot(const LLUUID& id) : LLMotion(id), mBreatheRate(1.f), mCharacter(NULL)
    {
        mName       = "breathe_rot";
        mChestState = new LLJointState;
    }

    // Destructor
    virtual ~LLBreatheMotionRot() {}

public:
    //-------------------------------------------------------------------------
    // functions to support MotionController and MotionRegistry
    //-------------------------------------------------------------------------
    // static constructor
    // all subclasses must implement such a function and register it
    static LLMotion* create(const LLUUID& id) { return new LLBreatheMotionRot(id); }

public:
    //-------------------------------------------------------------------------
    // animation callbacks to be implemented by subclasses
    //-------------------------------------------------------------------------

    // motions must specify whether or not they loop
    virtual bool getLoop() { return true; }

    // motions must report their total duration
    virtual F32 getDuration() { return 0.0; }

    // motions must report their "ease in" duration
    virtual F32 getEaseInDuration() { return 0.0; }

    // motions must report their "ease out" duration.
    virtual F32 getEaseOutDuration() { return 0.0; }

    // motions must report their priority
    virtual LLJoint::JointPriority getPriority() { return LLJoint::MEDIUM_PRIORITY; }

    virtual LLMotionBlendType getBlendType() { return NORMAL_BLEND; }

    // called to determine when a motion should be activated/deactivated based on avatar pixel coverage
    virtual F32 getMinPixelArea() { return MIN_REQUIRED_PIXEL_AREA_BREATHE; }

    // run-time (post constructor) initialization,
    // called after parameters have been set
    // must return true to indicate success and be available for activation
    virtual LLMotionInitStatus onInitialize(LLCharacter* character)
    {
        mCharacter   = character;
        bool success = true;

        if (!mChestState->setJoint(character->getJoint("mChest")))
        {
            success = false;
        }

        if (success)
        {
            mChestState->setUsage(LLJointState::ROT);
            addJointState(mChestState);
        }

        if (success)
        {
            return STATUS_SUCCESS;
        }
        else
        {
            return STATUS_FAILURE;
        }
    }

    // called when a motion is activated
    // must return true to indicate success, or else
    // it will be deactivated
    virtual bool onActivate() { return true; }

    // called per time step
    // must return true while it is active, and
    // must return false when the motion is completed.
    virtual bool onUpdate(F32 time, U8* joint_mask)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
        mBreatheRate = 1.f;

        F32 breathe_amt = (sinf(mBreatheRate * time) * BREATHE_ROT_MOTION_STRENGTH);

        mChestState->setRotation(LLQuaternion(breathe_amt, LLVector3(0.f, 1.f, 0.f)));

        return true;
    }

    // called when a motion is deactivated
    virtual void onDeactivate() {}

private:
    //-------------------------------------------------------------------------
    // joint states to be animated
    //-------------------------------------------------------------------------
    LLPointer<LLJointState> mChestState;
    F32                     mBreatheRate;
    LLCharacter*            mCharacter;
};

//-----------------------------------------------------------------------------
// class LLPelvisFixMotion
//-----------------------------------------------------------------------------
class LLPelvisFixMotion : public LLMotion
{
public:
    // Constructor
    LLPelvisFixMotion(const LLUUID& id) : LLMotion(id), mCharacter(NULL)
    {
        mName = "pelvis_fix";

        mPelvisState = new LLJointState;
    }

    // Destructor
    virtual ~LLPelvisFixMotion() {}

public:
    //-------------------------------------------------------------------------
    // functions to support MotionController and MotionRegistry
    //-------------------------------------------------------------------------
    // static constructor
    // all subclasses must implement such a function and register it
    static LLMotion* create(const LLUUID& id) { return new LLPelvisFixMotion(id); }

public:
    //-------------------------------------------------------------------------
    // animation callbacks to be implemented by subclasses
    //-------------------------------------------------------------------------

    // motions must specify whether or not they loop
    virtual bool getLoop() { return true; }

    // motions must report their total duration
    virtual F32 getDuration() { return 0.0; }

    // motions must report their "ease in" duration
    virtual F32 getEaseInDuration() { return 0.5f; }

    // motions must report their "ease out" duration.
    virtual F32 getEaseOutDuration() { return 0.5f; }

    // motions must report their priority
    virtual LLJoint::JointPriority getPriority() { return LLJoint::LOW_PRIORITY; }

    virtual LLMotionBlendType getBlendType() { return NORMAL_BLEND; }

    // called to determine when a motion should be activated/deactivated based on avatar pixel coverage
    virtual F32 getMinPixelArea() { return MIN_REQUIRED_PIXEL_AREA_PELVIS_FIX; }

    // run-time (post constructor) initialization,
    // called after parameters have been set
    // must return true to indicate success and be available for activation
    virtual LLMotionInitStatus onInitialize(LLCharacter* character)
    {
        mCharacter = character;

        if (!mPelvisState->setJoint(character->getJoint("mPelvis")))
        {
            return STATUS_FAILURE;
        }

        mPelvisState->setUsage(LLJointState::POS);

        addJointState(mPelvisState);
        return STATUS_SUCCESS;
    }

    // called when a motion is activated
    // must return true to indicate success, or else
    // it will be deactivated
    virtual bool onActivate() { return true; }

    // called per time step
    // must return true while it is active, and
    // must return false when the motion is completed.
    virtual bool onUpdate(F32 time, U8* joint_mask)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
        mPelvisState->setPosition(LLVector3::zero);

        return true;
    }

    // called when a motion is deactivated
    virtual void onDeactivate() {}

private:
    //-------------------------------------------------------------------------
    // joint states to be animated
    //-------------------------------------------------------------------------
    LLPointer<LLJointState> mPelvisState;
    LLCharacter*            mCharacter;
};

} // namespace
void vs_register_avatar_motions(LLCharacter& character)
{
    character.registerMotion(ANIM_AGENT_DO_NOT_DISTURB, LLNullMotion::create);
    character.registerMotion(ANIM_AGENT_CROUCH, LLKeyframeStandMotion::create);
    character.registerMotion(ANIM_AGENT_CROUCHWALK, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_AFRAID, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_ANGER, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_BORED, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_CRY, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_DISDAIN, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_EMBARRASSED, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_FROWN, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_KISS, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_LAUGH, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_OPEN_MOUTH, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_REPULSED, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_SAD, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_SHRUG, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_SMILE, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_SURPRISE, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_TONGUE_OUT, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_TOOTHSMILE, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_WINK, LLEmote::create);
    character.registerMotion(ANIM_AGENT_EXPRESS_WORRY, LLEmote::create);
    character.registerMotion(ANIM_AGENT_FEMALE_RUN_NEW, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_FEMALE_WALK, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_FEMALE_WALK_NEW, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_RUN, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_RUN_NEW, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_STAND, LLKeyframeStandMotion::create);
    character.registerMotion(ANIM_AGENT_STAND_1, LLKeyframeStandMotion::create);
    character.registerMotion(ANIM_AGENT_STAND_2, LLKeyframeStandMotion::create);
    character.registerMotion(ANIM_AGENT_STAND_3, LLKeyframeStandMotion::create);
    character.registerMotion(ANIM_AGENT_STAND_4, LLKeyframeStandMotion::create);
    character.registerMotion(ANIM_AGENT_STANDUP, LLKeyframeFallMotion::create);
    character.registerMotion(ANIM_AGENT_TURNLEFT, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_TURNRIGHT, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_WALK, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_WALK_NEW, LLKeyframeWalkMotion::create);

    // motions without a start/stop bit
    character.registerMotion(ANIM_AGENT_BODY_NOISE, LLBodyNoiseMotion::create);
    character.registerMotion(ANIM_AGENT_BREATHE_ROT, LLBreatheMotionRot::create);
    character.registerMotion(ANIM_AGENT_PHYSICS_MOTION, LLPhysicsMotionController::create);
    character.registerMotion(ANIM_AGENT_EDITING, LLEditingMotion::create);
    character.registerMotion(ANIM_AGENT_EYE, LLEyeMotion::create);
    character.registerMotion(ANIM_AGENT_FEMALE_WALK, LLKeyframeWalkMotion::create);
    character.registerMotion(ANIM_AGENT_FLY_ADJUST, LLFlyAdjustMotion::create);
    character.registerMotion(ANIM_AGENT_HAND_MOTION, LLHandMotion::create);
    character.registerMotion(ANIM_AGENT_HEAD_ROT, LLHeadRotMotion::create);
    character.registerMotion(ANIM_AGENT_PELVIS_FIX, LLPelvisFixMotion::create);
    character.registerMotion(ANIM_AGENT_SIT_FEMALE, LLKeyframeMotion::create);
    character.registerMotion(ANIM_AGENT_TARGET, LLTargetingMotion::create);
    character.registerMotion(ANIM_AGENT_WALK_ADJUST, LLWalkAdjustMotion::create);
}
void vs_start_avatar_default_motions(LLCharacter& character)
{
    for (const auto& motion : { ANIM_AGENT_HEAD_ROT, ANIM_AGENT_EYE, ANIM_AGENT_BODY_NOISE, ANIM_AGENT_BREATHE_ROT,
                                ANIM_AGENT_PHYSICS_MOTION, ANIM_AGENT_HAND_MOTION, ANIM_AGENT_PELVIS_FIX })
        character.startMotion(motion);
}

LLUUID vs_remap_avatar_motion(const LLUUID& id, bool female)
{
    static LLCachedControl<bool> use_new_walk_run(gSavedSettings, "UseNewWalkRun");
    LLUUID                       result = id;

    // start special case female walk for female avatars
    if (female)
    {
        if (id == ANIM_AGENT_WALK)
        {
            if (use_new_walk_run)
                result = ANIM_AGENT_FEMALE_WALK_NEW;
            else
                result = ANIM_AGENT_FEMALE_WALK;
        }
        else if (id == ANIM_AGENT_RUN)
        {
            // There is no old female run animation, so only override
            // in one case.
            if (use_new_walk_run)
                result = ANIM_AGENT_FEMALE_RUN_NEW;
        }
        else if (id == ANIM_AGENT_SIT)
        {
            result = ANIM_AGENT_SIT_FEMALE;
        }
    }
    else
    {
        // Male avatar.
        if (id == ANIM_AGENT_WALK)
        {
            if (use_new_walk_run)
                result = ANIM_AGENT_WALK_NEW;
        }
        else if (id == ANIM_AGENT_RUN)
        {
            if (use_new_walk_run)
                result = ANIM_AGENT_RUN_NEW;
        }
        // keeps in sync with setSex() related code (viewer controls sit's sex)
        else if (id == ANIM_AGENT_SIT_FEMALE)
        {
            result = ANIM_AGENT_SIT;
        }
    }

    return result;
}
