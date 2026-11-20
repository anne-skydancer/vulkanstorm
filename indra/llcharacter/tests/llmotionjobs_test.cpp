// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Multi-core C6: the two-step motion update (LLCharacter::updateMotionsBegin / updateMotionsCore) on LL::FrameJobs.
//
//   1  the character's random stream (LLCharacter::motionRandom): fixed by the id, in range, apart for two ids
//   2  the deferred effects of a controller: stop requests, emote starts and deletions replayed in the recorded order
//   3  two crowds of synthetic characters with the same ids, motions and events: one animated by updateMotions() in list
//      order (the serial path), one by Begin on the calling thread, Core as frame jobs and the deferred effects after the
//      batch. Joints (local transforms and world matrices), visual params, morphed "meshes", motion counts and stop
//      requests are compared bit for bit after every frame, on 0 to 7 helpers and in the job system's serial mode.
//      Real LLKeyframeMotion (with a ground constraint and an emote), LLHeadRotMotion and LLEyeMotion take part, with
//      restarts (deprecated instances, deleted inside the core), stops, self-stopping motions, pauses, hidden and
//      forced updates and LOD fades.
//   4  deliberate breaks: a motion that draws the thread's random numbers (ll_frand) and the deferred effects replayed
//      in the wrong order must each make the compare of test 3 fail
//   5  opt-in bench (LL_MOTION_JOBS_BENCH=1): 52 characters at 0 to 6 helpers

#include "linden_common.h"

#include "llcharacter.h"
#include "llcriticaldamp.h"
#include "llframejobs.h"
#include "llframetimer.h"
#include "llheadrotmotion.h"
#include "llkeyframemotion.h"
#include "llmotioncontroller.h"
#include "llquaternion.h"
#include "llrand.h"
#include "m4math.h"
#include "v3dmath.h"
#include "v3math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test/lltut.h"

namespace
{
    using LL::FrameJobs;

    // Stops the job system when a test leaves, also by a failed ensure.
    struct Started
    {
        explicit Started(size_t helpers) { FrameJobs::start(helpers); }
        ~Started() { FrameJobs::setSerial(false); FrameJobs::stop(); }
    };

    // The frame clock every LLFrameTimer reads, moved by hand: both crowds see the same time, and a run takes the same
    // course on every machine.
    struct TestClock : public LLFrameTimer
    {
        static void advance(F64 seconds) { sFrameTime += seconds; }
    };

    struct Lcg
    {
        explicit Lcg(U32 seed) : mState(seed * 2654435761u + 12345u) { }
        U32 next() { mState = mState * 1664525u + 1013904223u; return mState >> 8; }
        F32 unit() { return (F32)(next() & 0xffff) / 65536.f; } // [0, 1)
        U32 mState;
    };

    LLUUID test_id(U32 kind, U32 index)
    {
        LLUUID id;
        memset(id.mData, 0, UUID_BYTES);
        id.mData[0] = 0xc6;
        id.mData[1] = (U8)kind;
        id.mData[14] = (U8)(index >> 8);
        id.mData[15] = (U8)index;
        return id;
    }

    enum { ID_CHARACTER = 1, ID_KEYFRAME = 2, ID_HEAD = 3, ID_EYE = 4, ID_STOP = 5, ID_EMOTE = 6, ID_REMOVER = 7 };

    const U32 NUM_ANIMS = 6; // synthetic keyframe animations
    const S32 MESH_SIZE = 8;

    class TestCharacter;

    //-------------------------------------------------------------------------
    // a visual param that "morphs" a small array of its character
    //-------------------------------------------------------------------------
    struct TestParamInfo : public LLVisualParamInfo
    {
        TestParamInfo(S32 id, const std::string& name)
        {
            mID = id;
            mName = name;
            mMinWeight = 0.f;
            mMaxWeight = 1.f;
            mDefaultWeight = 0.f;
            mSex = SEX_BOTH;
        }
    };

    class TestParam : public LLVisualParam
    {
    public:
        TestParam(TestCharacter* owner, S32 id, const std::string& name, F32 gain)
            : mOwner(owner), mTestInfo(id, name), mGain(gain)
        {
            // what a derived param's setInfo() does
            mInfo = &mTestInfo;
            mID = id;
            setWeight(getDefaultWeight(), false);
        }
        void apply(ESex avatar_sex) override;

        TestCharacter*  mOwner;
        TestParamInfo   mTestInfo;
        F32             mGain;
    };

    //-------------------------------------------------------------------------
    // the character
    //-------------------------------------------------------------------------
    struct StopEvent
    {
        U32     mFrame;
        U32     mCharacter;
        LLUUID  mMotion;
        bool operator==(const StopEvent& o) const { return mFrame == o.mFrame && mCharacter == o.mCharacter && mMotion == o.mMotion; }
    };

    class TestCharacter : public LLCharacter
    {
    public:
        TestCharacter(U32 index, S32 joint_count, std::vector<StopEvent>* world_log)
            : mIndex(index), mID(test_id(ID_CHARACTER, index)), mWorldLog(world_log)
        {
            static const char* const NAMES[][2] =
            {
                { "mRoot", "" }, { "mPelvis", "mRoot" }, { "mTorso", "mPelvis" }, { "mChest", "mTorso" }, { "mNeck", "mChest" },
                { "mHead", "mNeck" }, { "mEyeLeft", "mHead" }, { "mEyeRight", "mHead" }, { "mFaceEyeAltLeft", "mHead" },
                { "mFaceEyeAltRight", "mHead" }, { "mCollarLeft", "mChest" }, { "mShoulderLeft", "mCollarLeft" },
                { "mElbowLeft", "mShoulderLeft" }, { "mWristLeft", "mElbowLeft" }, { "mCollarRight", "mChest" },
                { "mShoulderRight", "mCollarRight" }, { "mElbowRight", "mShoulderRight" }, { "mWristRight", "mElbowRight" },
                { "mHipLeft", "mPelvis" }, { "mKneeLeft", "mHipLeft" }, { "mAnkleLeft", "mKneeLeft" }, { "mFootLeft", "mAnkleLeft" },
                { "mHipRight", "mPelvis" }, { "mKneeRight", "mHipRight" }, { "mAnkleRight", "mKneeRight" }, { "mFootRight", "mAnkleRight" },
            };
            const S32 named = (S32)LL_ARRAY_SIZE(NAMES);
            for (S32 i = 0; i < joint_count; ++i)
            {
                std::string name = i < named ? NAMES[i][0] : "mExtra" + std::to_string(i);
                LLJoint* parent = nullptr;
                if (i > 0)
                {
                    // the extra joints hang in chains of four below the chest
                    parent = i < named ? find(NAMES[i][1]) : (((i - named) % 4) == 0 ? find("mChest") : mJoints.back().get());
                }
                mJoints.emplace_back(new LLJoint(name, parent));
                LLJoint* joint = mJoints.back().get();
                joint->setJointNum(i);
                joint->setPosition(LLVector3(0.01f * (F32)(i % 7), 0.02f * (F32)(i % 3), 0.05f + 0.01f * (F32)(i % 5)));
            }
            // a collision volume under the left ankle, for the ground constraint
            mJoints.emplace_back(new LLJoint("LEFT_FOOT_VOL", find("mAnkleLeft")));
            mVolume = mJoints.back().get();
            mVolume->setPosition(LLVector3(0.05f, 0.f, -0.04f));
            mAnimated = joint_count;

            addVisualParam(new TestParam(this, 1, "Blink_Left", 0.25f));
            addVisualParam(new TestParam(this, 2, "Blink_Right", 0.5f));
            addVisualParam(new TestParam(this, 3, "Test_Morph", 1.f));
            addVisualParam(new TestParam(this, 4, "Test_Emote", 2.f));
            memset(mMesh, 0, sizeof(mMesh));
        }

        ~TestCharacter() override
        {
            mPause = nullptr;
            // children before parents
            while (!mJoints.empty())
            {
                mJoints.pop_back();
            }
        }

        LLJoint* find(const std::string& name)
        {
            for (auto& joint : mJoints)
            {
                if (joint->getName() == name)
                {
                    return joint.get();
                }
            }
            return nullptr;
        }

        const char* getAnimationPrefix() override { return "test"; }
        LLJoint* getRootJoint() override { return mJoints[0].get(); }
        LLJoint* getJoint(std::string_view name) override { return find(std::string(name)); }
        LLVector3 getCharacterPosition() override { return mPosition; }
        LLQuaternion getCharacterRotation() override { return LLQuaternion::DEFAULT; }
        LLVector3 getCharacterVelocity() override { return mVelocity; }
        LLVector3 getCharacterAngularVelocity() override { return LLVector3::zero; }
        void getGround(const LLVector3& in_pos, LLVector3& out_pos, LLVector3& out_norm) override
        {
            out_pos = in_pos;
            out_pos.mV[VZ] = -0.2f;
            out_norm.setVec(0.f, 0.f, 1.f);
        }
        LLJoint* getCharacterJoint(U32 i) override { return i < mJoints.size() ? mJoints[i].get() : nullptr; }
        F32 getTimeDilation() override { return 1.f; }
        F32 getPixelArea() const override { return mPixelArea; }
        LLPolyMesh* getHeadMesh() override { return nullptr; }
        LLPolyMesh* getUpperBodyMesh() override { return nullptr; }
        LLVector3d getPosGlobalFromAgent(const LLVector3& position) override { return LLVector3d(position); }
        LLVector3 getPosAgentFromGlobal(const LLVector3d& position) override { return LLVector3(position); }
        void addDebugText(const std::string&) override { }
        const LLUUID& getID() const override { return mID; }
        LLVector3 getVolumePos(S32, LLVector3& volume_offset) override
        {
            return mVolume->getWorldPosition() + volume_offset * mVolume->getWorldRotation();
        }
        LLJoint* findCollisionVolume(S32 volume_id) override { return volume_id == 0 ? mVolume : nullptr; }

        void requestStopMotion(LLMotion* motion) override
        {
            // the main thread only: the core defers it when it runs as a job
            StopEvent event = { sFrame, mIndex, motion->getID() };
            mStops.push_back(event);
            mWorldLog->push_back(event);
        }

        static U32 sFrame;

        U32         mIndex;
        LLUUID      mID;
        std::vector<std::unique_ptr<LLJoint> > mJoints;
        LLJoint*    mVolume = nullptr;
        S32         mAnimated = 0;
        LLVector3   mPosition;
        LLVector3   mVelocity;
        F32         mPixelArea = 100000.f;
        F32         mMesh[MESH_SIZE];
        U32         mDeactivations = 0;
        std::vector<StopEvent>  mStops;
        std::vector<StopEvent>* mWorldLog;
        LLAnimPauseRequest      mPause;
    };

    U32 TestCharacter::sFrame = 0;

    void TestParam::apply(ESex)
    {
        const F32 delta = mCurWeight - mLastWeight;
        mLastWeight = mCurWeight;
        for (S32 i = 0; i < MESH_SIZE; ++i)
        {
            mOwner->mMesh[i] += delta * mGain * (F32)(i + 1);
        }
    }

    //-------------------------------------------------------------------------
    // a motion that stops itself, draws random numbers, asks for interpolants and drives a visual param
    //-------------------------------------------------------------------------
    bool sBreakThreadRandom = false; // deliberate break: the thread's stream instead of the character's
    S32  sStopMotionsAlive = 0;

    class TestStopMotion : public LLMotion
    {
    public:
        explicit TestStopMotion(const LLUUID& id) : LLMotion(id) { mName = "test_stop"; ++sStopMotionsAlive; }
        ~TestStopMotion() override { --sStopMotionsAlive; }
        static LLMotion* create(const LLUUID& id) { return new TestStopMotion(id); }

        bool getLoop() override { return false; }
        F32 getDuration() override { return 0.f; }
        F32 getEaseInDuration() override { return 0.1f; }
        F32 getEaseOutDuration() override { return 0.2f; }
        LLJoint::JointPriority getPriority() override { return LLJoint::HIGH_PRIORITY; }
        LLMotionBlendType getBlendType() override { return NORMAL_BLEND; }
        F32 getMinPixelArea() override { return 20.f; }
        LLMotionInitStatus onInitialize(LLCharacter* character) override
        {
            mCharacter = static_cast<TestCharacter*>(character);
            mState = new LLJointState;
            mState->setJoint(character->getJoint("mWristRight"));
            mState->setUsage(LLJointState::ROT);
            addJointState(mState);
            return STATUS_SUCCESS;
        }
        bool onActivate() override { return true; }
        bool onUpdate(F32 time, U8*) override
        {
            const F32 r = sBreakThreadRandom ? ll_frand() : mCharacter->motionRandom();
            // a time constant the interpolant cache does not hold yet in most frames
            const F32 amount = LLSmoothInterpolation::getInterpolant(0.13f + 0.0001f * (F32)(TestCharacter::sFrame % 1000));
            mAngle = lerp(mAngle, r, amount);
            mState->setRotation(LLQuaternion(mAngle, LLVector3(0.f, 1.f, 0.f)));
            mCharacter->setVisualParamWeight("Test_Morph", r);
            mCharacter->updateVisualParams();
            return time < 0.6f + 0.1f * (F32)(mCharacter->mIndex % 5);
        }
        void onDeactivate() override { ++mCharacter->mDeactivations; }

        TestCharacter*          mCharacter = nullptr;
        LLPointer<LLJointState> mState;
        F32                     mAngle = 0.f;
    };

    // an additive motion with a duration, started by a keyframe motion as its emote
    class TestEmoteMotion : public LLMotion
    {
    public:
        explicit TestEmoteMotion(const LLUUID& id) : LLMotion(id) { mName = "test_emote"; }
        static LLMotion* create(const LLUUID& id) { return new TestEmoteMotion(id); }

        bool getLoop() override { return false; }
        F32 getDuration() override { return 1.5f; }
        F32 getEaseInDuration() override { return 0.2f; }
        F32 getEaseOutDuration() override { return 0.3f; }
        LLJoint::JointPriority getPriority() override { return LLJoint::ADDITIVE_PRIORITY; }
        LLMotionBlendType getBlendType() override { return ADDITIVE_BLEND; }
        F32 getMinPixelArea() override { return 20.f; }
        LLMotionInitStatus onInitialize(LLCharacter* character) override
        {
            mCharacter = static_cast<TestCharacter*>(character);
            mState = new LLJointState;
            mState->setJoint(character->getJoint("mHead"));
            mState->setUsage(LLJointState::ROT);
            addJointState(mState);
            return STATUS_SUCCESS;
        }
        bool onActivate() override { return true; }
        bool onUpdate(F32 time, U8*) override
        {
            mState->setRotation(LLQuaternion(0.05f * sinf(3.f * time), LLVector3(1.f, 0.f, 0.f)));
            mCharacter->setVisualParamWeight("Test_Emote", mPose.getWeight());
            mCharacter->updateVisualParams();
            return true;
        }
        void onDeactivate() override
        {
            mCharacter->setVisualParamWeight("Test_Emote", 0.f);
            mCharacter->updateVisualParams();
            ++mCharacter->mDeactivations;
        }

        TestCharacter*          mCharacter = nullptr;
        LLPointer<LLJointState> mState;
    };

    // a motion that, when asked, removes the stop motion during its own update, after the stop motion was added to the
    // blend (H6: the removed motion's joint states must last until the blend)
    bool sRemoveStopMotion = false;
    S32  sStopMotionsAliveAfterRemove = -1;

    class TestRemoverMotion : public LLMotion
    {
    public:
        explicit TestRemoverMotion(const LLUUID& id) : LLMotion(id) { mName = "test_remover"; }
        static LLMotion* create(const LLUUID& id) { return new TestRemoverMotion(id); }

        bool getLoop() override { return true; }
        F32 getDuration() override { return 0.f; }
        F32 getEaseInDuration() override { return 0.f; }
        F32 getEaseOutDuration() override { return 0.f; }
        LLJoint::JointPriority getPriority() override { return LLJoint::LOW_PRIORITY; }
        LLMotionBlendType getBlendType() override { return NORMAL_BLEND; }
        F32 getMinPixelArea() override { return 20.f; }
        LLMotionInitStatus onInitialize(LLCharacter* character) override
        {
            mCharacter = static_cast<TestCharacter*>(character);
            mState = new LLJointState;
            mState->setJoint(character->getJoint("mWristLeft"));
            mState->setUsage(LLJointState::ROT);
            addJointState(mState);
            return STATUS_SUCCESS;
        }
        bool onActivate() override { return true; }
        bool onUpdate(F32 time, U8*) override
        {
            mState->setRotation(LLQuaternion(0.1f * time, LLVector3(0.f, 0.f, 1.f)));
            if (sRemoveStopMotion)
            {
                sRemoveStopMotion = false;
                mCharacter->removeMotion(test_id(ID_STOP, 0));
                sStopMotionsAliveAfterRemove = sStopMotionsAlive;
            }
            return true;
        }
        void onDeactivate() override { }

        TestCharacter*          mCharacter = nullptr;
        LLPointer<LLJointState> mState;
    };

    //-------------------------------------------------------------------------
    // synthetic keyframe animations, put into the keyframe cache so that LLKeyframeMotion needs no asset
    //-------------------------------------------------------------------------
    // LLKeyframeMotion keeps its constraint data protected: a derived class may name it
    struct ConstraintMaker : public LLKeyframeMotion
    {
        // the left foot held on the ground: chain ankle (joint motion 0), knee (1), hip (2)
        static void addGroundConstraint(JointMotionList* list)
        {
            JointConstraintSharedData* constraint = new JointConstraintSharedData;
            constraint->mChainLength = 2;
            constraint->mJointStateIndices = new S32[3]{ 0, 1, 2 };
            constraint->mSourceConstraintVolume = 0;
            constraint->mSourceConstraintOffset.setVec(0.f, 0.f, -0.02f);
            constraint->mConstraintType = CONSTRAINT_TYPE_POINT;
            constraint->mConstraintTargetType = CONSTRAINT_TARGET_TYPE_GROUND;
            constraint->mEaseInStartTime = 0.f;
            constraint->mEaseInStopTime = 0.2f;
            constraint->mEaseOutStartTime = 0.7f * list->mDuration;
            constraint->mEaseOutStopTime = 0.8f * list->mDuration;
            list->mConstraints.push_front(constraint);
        }
    };

    void add_keyframe_anim(U32 anim, S32 joint_count, S32 joints_used, bool for_bench)
    {
        const LLUUID id = test_id(ID_KEYFRAME, anim + (for_bench ? 100 : 0));
        if (LLKeyframeDataCache::getKeyframeData(id))
        {
            return;
        }
        Lcg lcg(977 * (anim + 1) + (for_bench ? 31 : 0));
        LLKeyframeMotion::JointMotionList* list = new LLKeyframeMotion::JointMotionList;
        const bool loop = for_bench || (anim % 3) != 2;
        list->mDuration = for_bench ? 4.f : 1.2f + 0.7f * (F32)anim;
        list->mLoop = loop;
        list->mLoopInPoint = loop ? 0.2f : 0.f;
        list->mLoopOutPoint = loop ? list->mDuration - (anim % 2 ? 0.3f : 0.f) : list->mDuration;
        list->mEaseInDuration = 0.1f * (F32)(anim % 4);
        list->mEaseOutDuration = 0.15f * (F32)((anim + 1) % 3);
        list->mBasePriority = (LLJoint::JointPriority)(anim % 4);
        list->mMaxPriority = list->mBasePriority;
        if (!for_bench && anim == 4)
        {
            list->mEmoteID = test_id(ID_EMOTE, 0);
        }

        // The joints this animation moves. Animation 1 starts with the left leg, ankle first, for its constraint.
        std::vector<std::string> names;
        if (!for_bench && anim == 1)
        {
            names = { "mAnkleLeft", "mKneeLeft", "mHipLeft" };
        }
        TestCharacter probe(0, joint_count, nullptr);
        for (S32 k = 0; (S32)names.size() < joints_used && k < 4 * joint_count; ++k)
        {
            const std::string name = probe.mJoints[1 + lcg.next() % (joint_count - 1)]->getName();
            if (std::find(names.begin(), names.end(), name) == names.end())
            {
                names.push_back(name);
            }
        }

        for (const std::string& name : names)
        {
            LLKeyframeMotion::JointMotion* jm = new LLKeyframeMotion::JointMotion;
            jm->mJointName = name;
            jm->mPriority = (lcg.next() % 5) == 0 ? LLJoint::HIGHER_PRIORITY : LLJoint::USE_MOTION_PRIORITY;
            jm->mUsage = LLJointState::ROT;
            const S32 keys = 4 + (S32)(lcg.next() % 5);
            for (S32 k = 0; k < keys; ++k)
            {
                const F32 time = list->mDuration * (F32)k / (F32)(keys - 1);
                LLVector3 axis(lcg.unit() - 0.5f, lcg.unit() - 0.5f, lcg.unit() + 0.1f);
                jm->mRotationCurve.mKeys[time] = LLKeyframeMotion::RotationKey(time, LLQuaternion(1.5f * (lcg.unit() - 0.5f), axis));
            }
            jm->mRotationCurve.mNumKeys = (S32)jm->mRotationCurve.mKeys.size();
            if (name == "mPelvis" || (lcg.next() % 6) == 0)
            {
                jm->mUsage |= LLJointState::POS;
                for (S32 k = 0; k < 3; ++k)
                {
                    const F32 time = list->mDuration * (F32)k / 2.f;
                    jm->mPositionCurve.mKeys[time] = LLKeyframeMotion::PositionKey(
                        time, LLVector3(0.1f * (lcg.unit() - 0.5f), 0.1f * (lcg.unit() - 0.5f), 0.1f * lcg.unit()));
                }
                jm->mPositionCurve.mNumKeys = (S32)jm->mPositionCurve.mKeys.size();
            }
            if ((lcg.next() % 9) == 0)
            {
                jm->mUsage |= LLJointState::SCALE;
                for (S32 k = 0; k < 2; ++k)
                {
                    const F32 time = list->mDuration * (F32)k;
                    jm->mScaleCurve.mKeys[time] = LLKeyframeMotion::ScaleKey(time, LLVector3(1.f, 1.f + 0.1f * lcg.unit(), 1.f));
                }
                jm->mScaleCurve.mNumKeys = (S32)jm->mScaleCurve.mKeys.size();
            }
            list->mJointMotionArray.push_back(jm);
        }

        if (!for_bench && anim == 1)
        {
            ConstraintMaker::addGroundConstraint(list);
        }

        LLKeyframeDataCache::addKeyframeData(id, list);
    }

    void register_motions(S32 joint_count, S32 bench_joint_count)
    {
        static bool done = false;
        if (!done)
        {
            done = true;
            LLMotionController registry_access;
            registry_access.registerMotion(test_id(ID_HEAD, 0), LLHeadRotMotion::create);
            registry_access.registerMotion(test_id(ID_EYE, 0), LLEyeMotion::create);
            registry_access.registerMotion(test_id(ID_STOP, 0), TestStopMotion::create);
            registry_access.registerMotion(test_id(ID_EMOTE, 0), TestEmoteMotion::create);
            registry_access.registerMotion(test_id(ID_REMOVER, 0), TestRemoverMotion::create);
            for (U32 anim = 0; anim < NUM_ANIMS; ++anim)
            {
                registry_access.registerMotion(test_id(ID_KEYFRAME, anim), LLKeyframeMotion::create);
            }
            for (U32 anim = 0; anim < 16; ++anim)
            {
                registry_access.registerMotion(test_id(ID_KEYFRAME, anim + 100), LLKeyframeMotion::create);
            }
        }
        for (U32 anim = 0; anim < NUM_ANIMS; ++anim)
        {
            add_keyframe_anim(anim, joint_count, 10 + 3 * (S32)anim, false);
        }
        if (bench_joint_count > 0)
        {
            for (U32 anim = 0; anim < 16; ++anim)
            {
                add_keyframe_anim(anim, bench_joint_count, 40, true);
            }
        }
    }

    //-------------------------------------------------------------------------
    // a crowd
    //-------------------------------------------------------------------------
    const S32 TEST_JOINTS = 44;

    struct World
    {
        World(U32 count, S32 joint_count)
        {
            for (U32 i = 0; i < count; ++i)
            {
                mCharacters.emplace_back(new TestCharacter(i, joint_count, &mLog));
            }
            mNeedCore.resize(count);
            mTypes.resize(count);
        }

        std::vector<std::unique_ptr<TestCharacter> > mCharacters;
        std::vector<StopEvent>                       mLog;
        std::vector<U8>                              mNeedCore;
        std::vector<LLCharacter::e_update_t>         mTypes;
    };

    void start_motions(World& world)
    {
        for (auto& character : world.mCharacters)
        {
            const U32 i = character->mIndex;
            character->startMotion(test_id(ID_HEAD, 0));
            character->startMotion(test_id(ID_EYE, 0));
            character->startMotion(test_id(ID_KEYFRAME, i % NUM_ANIMS));
            character->startMotion(test_id(ID_KEYFRAME, (i + 1) % NUM_ANIMS), 0.3f);
            character->startMotion(test_id(ID_KEYFRAME, (i + 3) % NUM_ANIMS));
            if (i % 2)
            {
                character->startMotion(test_id(ID_STOP, 0));
            }
        }
    }

    // what the rest of a viewer does to a character between two updates: the same in both crowds
    void apply_events(World& world, U32 frame)
    {
        for (auto& character : world.mCharacters)
        {
            const U32 i = character->mIndex;
            Lcg lcg(frame * 7919u + i * 104729u);
            const U32 roll = lcg.next() % 400;
            if (roll < 10)
            {
                const U32 pick = lcg.next() % (NUM_ANIMS + 2);
                const LLUUID id = pick < NUM_ANIMS ? test_id(ID_KEYFRAME, pick) : test_id(pick == NUM_ANIMS ? ID_STOP : ID_EMOTE, 0);
                character->startMotion(id, 0.1f * (F32)(lcg.next() % 4));
            }
            else if (roll < 18)
            {
                const U32 pick = lcg.next() % (NUM_ANIMS + 1);
                const LLUUID id = pick < NUM_ANIMS ? test_id(ID_KEYFRAME, pick) : test_id(ID_STOP, 0);
                character->stopMotion(id, (lcg.next() & 1) != 0);
            }
            else if (roll == 18)
            {
                // pause, or let go of the pause
                if (character->mPause.isNull())
                {
                    character->mPause = character->requestPause();
                }
                else
                {
                    character->mPause = nullptr;
                }
            }
            if (i % 5 == 0 && frame % 97 == 0)
            {
                character->mPixelArea = character->mPixelArea > 1000.f ? 10.f : 100000.f; // LOD: motions fade out and in
            }
            character->mVelocity.setVec(0.5f * sinf(0.01f * (F32)(frame + i)), 0.f, 0.f);
            character->mPosition += character->mVelocity * (1.f / 45.f);

            LLCharacter::e_update_t type = LLCharacter::NORMAL_UPDATE;
            if (i % 7 == 3 && (frame / 50) % 2 == 1)
            {
                type = LLCharacter::HIDDEN_UPDATE;
            }
            else if ((frame + i) % 211 == 0)
            {
                type = LLCharacter::FORCE_UPDATE;
            }
            world.mTypes[i] = type;
        }
    }

    // the serial path: one character after the other, as LLCharacter::updateMotions always ran
    void step_serial(World& world)
    {
        for (auto& character : world.mCharacters)
        {
            const LLCharacter::e_update_t type = world.mTypes[character->mIndex];
            character->updateMotions(type);
            if (type != LLCharacter::HIDDEN_UPDATE)
            {
                character->getRootJoint()->updateWorldMatrixChildren();
            }
        }
    }

    // the job path: the first step in list order, the cores as one batch, the deferred effects in list order
    void step_jobs(World& world, bool reversed_effects = false)
    {
        const size_t count = world.mCharacters.size();
        for (size_t i = 0; i < count; ++i)
        {
            TestCharacter* character = world.mCharacters[i].get();
            world.mNeedCore[i] = character->updateMotionsBegin(world.mTypes[i]) ? 1 : 0;
            if (world.mNeedCore[i])
            {
                character->getMotionController().beginDeferredEffects();
            }
        }
        FrameJobs::parallelFor(count, 1,
            [&world](size_t begin, size_t end, size_t /*chunk*/)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    if (world.mNeedCore[i])
                    {
                        TestCharacter* character = world.mCharacters[i].get();
                        character->updateMotionsCore(world.mTypes[i]);
                        if (world.mTypes[i] != LLCharacter::HIDDEN_UPDATE)
                        {
                            character->getRootJoint()->updateWorldMatrixChildren();
                        }
                    }
                }
            });
        for (size_t k = 0; k < count; ++k)
        {
            const size_t i = reversed_effects ? count - 1 - k : k;
            if (world.mNeedCore[i])
            {
                world.mCharacters[i]->getMotionController().applyDeferredEffects();
            }
        }
    }

    template <typename T>
    void put(std::vector<U8>& out, const T& value)
    {
        const U8* bytes = reinterpret_cast<const U8*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    // everything the update leaves behind in a character, as bytes
    void snapshot(TestCharacter& character, std::vector<U8>& out)
    {
        out.clear();
        for (auto& joint : character.mJoints)
        {
            put(out, joint->getPosition().mV);
            put(out, joint->getRotation().mQ);
            put(out, joint->getScale().mV);
            put(out, joint->getWorldMatrix().mMatrix);
        }
        for (S32 id = 1; id <= 4; ++id)
        {
            LLVisualParam* param = character.getVisualParam(id);
            put(out, param->getCurrentWeight());
            put(out, param->getLastWeight());
        }
        put(out, character.mMesh);
        put(out, character.mDeactivations);
        S32 counts[5] = { 0, 0, 0, 0, 0 };
        character.getMotionController().incMotionCounts(counts[0], counts[1], counts[2], counts[3], counts[4]);
        put(out, counts);
        put(out, character.getMotionController().getAnimTime());
        for (const StopEvent& event : character.mStops)
        {
            put(out, event.mFrame);
            put(out, event.mMotion.mData);
        }
    }

    struct CompareResult
    {
        U32 mMismatches = 0;       // character snapshots that differ, over all frames
        U32 mLogMismatches = 0;    // frames after which the crowds' stop-request logs differ
        U32 mStops = 0;
        U32 mDeactivations = 0;
        U32 mBlendersLeft = 0;     // characters whose blender still held a joint state after an update (H6)
        std::string mFirst;
    };

    CompareResult run_compare(U32 characters, U32 frames, bool reversed_effects = false)
    {
        register_motions(TEST_JOINTS, 0);
        World serial(characters, TEST_JOINTS);
        World jobs(characters, TEST_JOINTS);
        start_motions(serial);
        start_motions(jobs);

        CompareResult result;
        std::vector<U8> a, b;
        for (U32 frame = 0; frame < frames; ++frame)
        {
            TestCharacter::sFrame = frame;
            TestClock::advance(1.0 / 45.0);
            LLSmoothInterpolation::updateInterpolants();
            apply_events(serial, frame);
            apply_events(jobs, frame);

            // the jobs first, so that they meet the time constants of this frame before the serial crowd has cached them
            step_jobs(jobs, reversed_effects);
            step_serial(serial);

            for (U32 i = 0; i < characters; ++i)
            {
                snapshot(*serial.mCharacters[i], a);
                snapshot(*jobs.mCharacters[i], b);
                if (a != b)
                {
                    if (result.mMismatches == 0)
                    {
                        size_t at = 0;
                        while (at < a.size() && at < b.size() && a[at] == b[at])
                        {
                            ++at;
                        }
                        result.mFirst = "frame " + std::to_string(frame) + " character " + std::to_string(i) + " byte "
                            + std::to_string(at) + " of " + std::to_string(a.size()) + "/" + std::to_string(b.size());
                    }
                    ++result.mMismatches;
                }
                if (!jobs.mCharacters[i]->getMotionController().blendersEmpty()
                    || !serial.mCharacters[i]->getMotionController().blendersEmpty())
                {
                    ++result.mBlendersLeft;
                }
            }
            if (!(serial.mLog == jobs.mLog))
            {
                ++result.mLogMismatches;
            }
        }
        result.mStops = (U32)serial.mLog.size();
        for (auto& character : serial.mCharacters)
        {
            result.mDeactivations += character->mDeactivations;
        }
        return result;
    }
    // One character with a self-stopping motion restarted while it blends in: the first instance is deprecated, stops and
    // is deleted inside a core; the second instance then stops itself (a stop request). Runs the two-step update with
    // deferred effects until both have happened; returns the frame of the deletion.
    // With leave_at set, the effects of that frame are not replayed and the character is destroyed with them recorded.
    U32 run_deferred_delete(U32 leave_at, bool& replayed_stop, S32& alive_at_end)
    {
        const U32 NEVER = 0xffffffff;
        U32 deleted_at = NEVER;
        const S32 alive_before = sStopMotionsAlive;
        {
            World world(1, TEST_JOINTS);
            TestCharacter* character = world.mCharacters[0].get();
            LLMotionController& controller = character->getMotionController();
            character->startMotion(test_id(ID_STOP, 0));
            character->updateMotions(LLCharacter::NORMAL_UPDATE);
            TestClock::advance(0.05);
            LLSmoothInterpolation::updateInterpolants();
            character->updateMotions(LLCharacter::NORMAL_UPDATE);
            character->startMotion(test_id(ID_STOP, 0));
            tut::ensure_equals("two instances", sStopMotionsAlive, alive_before + 2);

            for (U32 frame = 0; frame < 300 && (deleted_at == NEVER || !replayed_stop); ++frame)
            {
                TestClock::advance(1.0 / 45.0);
                LLSmoothInterpolation::updateInterpolants();
                tut::ensure("the core is needed", character->updateMotionsBegin(LLCharacter::NORMAL_UPDATE));
                controller.beginDeferredEffects();
                const size_t stops_in = character->mStops.size();
                const S32 alive_in = sStopMotionsAlive;
                character->updateMotionsCore(LLCharacter::NORMAL_UPDATE);
                tut::ensure_equals("no stop request reaches the character from the core", character->mStops.size(), stops_in);
                tut::ensure_equals("nothing is deleted in the core", sStopMotionsAlive, alive_in);
                tut::ensure("the blender is empty after the core (H6)", controller.blendersEmpty());
                if (frame == leave_at)
                {
                    break;
                }
                controller.applyDeferredEffects();
                tut::ensure("the deferral is over", !controller.isDeferringEffects());
                replayed_stop = replayed_stop || character->mStops.size() > stops_in;
                if (sStopMotionsAlive < alive_in && deleted_at == NEVER)
                {
                    deleted_at = frame;
                }
            }
        }
        alive_at_end = sStopMotionsAlive - alive_before;
        return deleted_at;
    }
}

namespace tut
{
    struct llmotionjobs_data
    {
    };
    typedef test_group<llmotionjobs_data> llmotionjobs_test;
    typedef llmotionjobs_test::object llmotionjobs_object;
    tut::llmotionjobs_test llmotionjobs_testcase("LLMotionJobs");

    template<> template<>
    void llmotionjobs_object::test<1>()
    {
        set_test_name("the character's random stream");
        std::vector<StopEvent> log;
        TestCharacter a(1, 8, &log), a_again(1, 8, &log), b(2, 8, &log);
        U32 same = 0;
        F32 lowest = 1.f, highest = 0.f;
        for (U32 i = 0; i < 20000; ++i)
        {
            const F32 x = a.motionRandom();
            const F32 y = a_again.motionRandom();
            const F32 z = b.motionRandom();
            ensure("in [0, 1)", x >= 0.f && x < 1.f && z >= 0.f && z < 1.f);
            ensure("one id, one sequence", memcmp(&x, &y, sizeof(F32)) == 0);
            same += (x == z) ? 1 : 0;
            lowest = llmin(lowest, x);
            highest = llmax(highest, x);
        }
        ensure("two ids, two sequences", same < 10);
        ensure("the range is used", lowest < 0.001f && highest > 0.999f);
        for (U32 i = 0; i < 2000; ++i)
        {
            const F32 up = a.motionRandom(2.2f);
            const F32 down = a.motionRandom(-3.f);
            ensure("motionRandom(val) in [0, val)", up >= 0.f && up < 2.2f);
            ensure("motionRandom(-val) in (-val, 0]", down <= 0.f && down > -3.f);
        }
        ensure_equals("motionRandom(0)", a.motionRandom(0.f), 0.f);
    }

    template<> template<>
    void llmotionjobs_object::test<2>()
    {
        set_test_name("deferred effects");
        register_motions(TEST_JOINTS, 0);
        TestClock::advance(1.0);
        LLSmoothInterpolation::updateInterpolants();

        bool replayed_stop = false;
        S32 alive = -1;
        const U32 deleted_at = run_deferred_delete(0xffffffff, replayed_stop, alive);
        ensure("the deprecated instance was deleted after a core", deleted_at != 0xffffffff);
        ensure("a stop request was replayed after a core", replayed_stop);
        ensure_equals("every instance is gone with the character", alive, 0);

        // the same course, left with the deletion recorded and not replayed: the controller's end deletes it
        replayed_stop = false;
        alive = -1;
        run_deferred_delete(deleted_at, replayed_stop, alive);
        ensure_equals("a recorded deletion is not lost", alive, 0);

        // an emote asked for while effects are deferred starts when they are replayed
        {
            World world(1, TEST_JOINTS);
            TestCharacter* character = world.mCharacters[0].get();
            LLMotionController& controller = character->getMotionController();
            controller.beginDeferredEffects();
            character->startMotion(test_id(ID_KEYFRAME, 4)); // the animation with the emote
            ensure("the animation is active", character->isMotionActive(test_id(ID_KEYFRAME, 4)));
            ensure("the emote waits", !character->isMotionActive(test_id(ID_EMOTE, 0)));
            controller.applyDeferredEffects();
            ensure("the emote started with the replay", character->isMotionActive(test_id(ID_EMOTE, 0)));
            // and at once without a deferral
            character->stopMotion(test_id(ID_EMOTE, 0), true);
            character->stopMotion(test_id(ID_KEYFRAME, 4), true);
            character->startMotion(test_id(ID_KEYFRAME, 4));
            ensure("the emote starts at once outside a deferral", character->isMotionActive(test_id(ID_EMOTE, 0)));
        }

        // outside a job, a motion removed during the update, after its joint states went into the blend, is deleted after
        // the blend and not before (H6: the blender's plain pointers)
        {
            const S32 alive_before = sStopMotionsAlive;
            World world(1, TEST_JOINTS);
            TestCharacter* character = world.mCharacters[0].get();
            LLMotionController& controller = character->getMotionController();
            for (U32 frame = 0; frame < 3; ++frame)
            {
                if (frame < 2)
                {
                    // the stop motion activates a frame later: ahead of the remover in the active list, updated first
                    character->startMotion(test_id(frame == 0 ? ID_REMOVER : ID_STOP, 0));
                }
                TestClock::advance(1.0 / 45.0);
                LLSmoothInterpolation::updateInterpolants();
                character->updateMotions(LLCharacter::NORMAL_UPDATE);
            }
            ensure_equals("the stop motion lives", sStopMotionsAlive, alive_before + 1);
            ensure("the stop motion is active", character->isMotionActive(test_id(ID_STOP, 0)));
            sRemoveStopMotion = true;
            sStopMotionsAliveAfterRemove = -1;
            TestClock::advance(1.0 / 45.0);
            LLSmoothInterpolation::updateInterpolants();
            character->updateMotions(LLCharacter::NORMAL_UPDATE);
            ensure("the remover ran", !sRemoveStopMotion);
            ensure_equals("the removed motion outlives the blend", sStopMotionsAliveAfterRemove, alive_before + 1);
            ensure_equals("and goes at the end of the update", sStopMotionsAlive, alive_before);
            ensure("the blender is empty after the update", controller.blendersEmpty());
            ensure("no deferral is left", !controller.isDeferringEffects());
        }

        // inside a job the interpolant cache is read, never grown, and gives the cache's value
        {
            Started started(2);
            const F32 constants[4] = { 0.4321f, 0.5432f, 0.6543f, 0.7654f };
            F32 in_job[4] = { 0.f, 0.f, 0.f, 0.f };
            FrameJobs::parallelFor(4, 1, [&](size_t begin, size_t end, size_t)
                {
                    for (size_t i = begin; i < end; ++i)
                    {
                        in_job[i] = LLSmoothInterpolation::getInterpolant(constants[i]);
                    }
                });
            for (U32 i = 0; i < 4; ++i)
            {
                const F32 cached = LLSmoothInterpolation::getInterpolant(constants[i]);
                ensure("the interpolant of a job is the cache's", memcmp(&cached, &in_job[i], sizeof(F32)) == 0);
            }
        }
    }

    template<> template<>
    void llmotionjobs_object::test<3>()
    {
        set_test_name("serial against frame jobs, bit for bit");
        const U32 CHARACTERS = 24;
        const U32 FRAMES = 1200; // 26.7 s at 45 fps
        const size_t helper_counts[] = { 0, 1, 2, 3, 5, 7 };
        for (size_t helpers : helper_counts)
        {
            Started started(helpers);
            const CompareResult result = run_compare(CHARACTERS, FRAMES);
            const std::string where = " (" + std::to_string(helpers) + " helpers)";
            ensure_equals("characters that differ" + where + ": first at " + result.mFirst, result.mMismatches, U32(0));
            ensure_equals("stop-request logs that differ" + where, result.mLogMismatches, U32(0));
            ensure_equals("blenders left with a joint state" + where, result.mBlendersLeft, U32(0));
            // the run must have exercised what it claims to
            ensure("stop requests happened" + where, result.mStops > 20);
            ensure("motions were deactivated" + where, result.mDeactivations > 50);
        }
        {
            // the job system's serial mode: the same partition on the calling thread
            Started started(3);
            FrameJobs::setSerial(true);
            const CompareResult result = run_compare(CHARACTERS, 400);
            ensure_equals("characters that differ (serial mode): first at " + result.mFirst, result.mMismatches, U32(0));
            ensure_equals("stop-request logs that differ (serial mode)", result.mLogMismatches, U32(0));
        }
    }

    template<> template<>
    void llmotionjobs_object::test<4>()
    {
        set_test_name("deliberate breaks");
        {
            // a motion that draws the thread's random numbers: the crowds no longer agree
            Started started(3);
            sBreakThreadRandom = true;
            const CompareResult result = run_compare(12, 300);
            sBreakThreadRandom = false;
            ensure("ll_frand in a motion is caught", result.mMismatches > 0);
        }
        {
            // the deferred effects replayed last character first: the stop requests arrive in another order
            Started started(3);
            const CompareResult result = run_compare(24, 1200, true);
            ensure("the wrong replay order is caught", result.mLogMismatches > 0);
            ensure_equals("the characters themselves still agree: first at " + result.mFirst, result.mMismatches, U32(0));
        }
    }

    template<> template<>
    void llmotionjobs_object::test<5>()
    {
        set_test_name("bench (opt-in: LL_MOTION_JOBS_BENCH=1)");
        const char* bench = getenv("LL_MOTION_JOBS_BENCH");
        if (!bench || bench[0] != '1')
        {
            skip("set LL_MOTION_JOBS_BENCH=1 to run");
        }
        const U32 CHARACTERS = 52;
        const S32 JOINTS = 133;
        const U32 FRAMES = 300;
        register_motions(TEST_JOINTS, JOINTS);
        using clock = std::chrono::steady_clock;
        auto micros = [](clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); };
        auto median = [](std::vector<double>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };

        std::printf("motion jobs bench: %u characters, %d joints, 12 keyframe motions of 40 joints + head + eyes, %u frames a row\n",
                    CHARACTERS, JOINTS, FRAMES);
        std::printf("%8s %12s %12s %12s %12s\n", "helpers", "begin_us", "cores_us", "effects_us", "total_us");
        for (S32 helpers = -1; helpers <= 6; ++helpers)
        {
            // -1: updateMotions() in list order, no job system
            Started started(helpers < 0 ? 0 : (size_t)helpers);
            World world(CHARACTERS, JOINTS);
            for (auto& character : world.mCharacters)
            {
                const U32 i = character->mIndex;
                character->startMotion(test_id(ID_HEAD, 0));
                character->startMotion(test_id(ID_EYE, 0));
                for (U32 k = 0; k < 12; ++k)
                {
                    character->startMotion(test_id(ID_KEYFRAME, 100 + (i + k) % 16), 0.1f * (F32)k);
                }
                world.mTypes[i] = LLCharacter::NORMAL_UPDATE;
            }
            std::vector<double> begin_us, cores_us, effects_us, total_us;
            for (U32 frame = 0; frame < FRAMES; ++frame)
            {
                TestCharacter::sFrame = frame;
                TestClock::advance(1.0 / 60.0);
                LLSmoothInterpolation::updateInterpolants();
                // the rest of a frame: long enough for the helpers to have parked
                const clock::time_point wait_until = clock::now() + std::chrono::microseconds(1000);
                while (clock::now() < wait_until) { }

                const clock::time_point t0 = clock::now();
                if (helpers < 0)
                {
                    step_serial(world);
                    total_us.push_back(micros(clock::now() - t0));
                    continue;
                }
                const size_t count = world.mCharacters.size();
                for (size_t i = 0; i < count; ++i)
                {
                    world.mNeedCore[i] = world.mCharacters[i]->updateMotionsBegin(world.mTypes[i]) ? 1 : 0;
                    world.mCharacters[i]->getMotionController().beginDeferredEffects();
                }
                const clock::time_point t1 = clock::now();
                FrameJobs::parallelFor(count, 1, [&world](size_t begin, size_t end, size_t)
                    {
                        for (size_t i = begin; i < end; ++i)
                        {
                            world.mCharacters[i]->updateMotionsCore(world.mTypes[i]);
                            world.mCharacters[i]->getRootJoint()->updateWorldMatrixChildren();
                        }
                    });
                const clock::time_point t2 = clock::now();
                for (size_t i = 0; i < count; ++i)
                {
                    world.mCharacters[i]->getMotionController().applyDeferredEffects();
                }
                const clock::time_point t3 = clock::now();
                begin_us.push_back(micros(t1 - t0));
                cores_us.push_back(micros(t2 - t1));
                effects_us.push_back(micros(t3 - t2));
                total_us.push_back(micros(t3 - t0));
            }
            if (helpers < 0)
            {
                std::printf("%8s %12s %12s %12s %12.1f\n", "serial", "-", "-", "-", median(total_us));
            }
            else
            {
                std::printf("%8d %12.1f %12.1f %12.1f %12.1f\n", helpers, median(begin_us), median(cores_us), median(effects_us),
                            median(total_us));
            }
        }
    }
}
