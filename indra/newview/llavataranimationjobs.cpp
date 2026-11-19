// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Multi-core stage C6: the avatars' animation cores as frame jobs. See llavataranimationjobs.h.

#include "llviewerprecompiledheaders.h"

#include "llavataranimationjobs.h"

#include "llagent.h"
#include "lldrawable.h"
#include "llframejobs.h"
#include "lljoint.h"
#include "llphysicsmotion.h"
#include "lltimer.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llvoavatar.h"
#include "xform.h"

#include <cstring>
#include <vector>

namespace
{
    // LLViewerObject::mAnimBatchFlags, valid while mAnimBatchStamp is the current batch's number
    enum : U8
    {
        MARK_PENDING = 1,       // an avatar whose core and End are held back: the root of its attachments
        MARK_RELATED = 2,       // the root of the linkset a pending avatar sits on, or of the one its animated object is
        MARK_HELD = 4,          // an object of this root's tree is held back
        MARK_ANIMATED_ROOT = 8, // the root (linkset or wearer) of a pending animated object's avatar
    };

    // batches below this run on the calling thread: not worth waking the helpers (llframejobs.h: about 50 microseconds)
    const size_t MIN_JOB_BATCH = 4;

    struct Held
    {
        LLViewerObject* mObject;
        LLVOAvatar*     mAvatar;    // null: a plain object, its whole idleUpdate is held
        bool            mInBatch;   // the avatar's core is in the batch (otherwise it runs at the End's turn)
    };

    std::vector<Held>        sHeld;   // in list order
    std::vector<LLVOAvatar*> sBatch;  // the avatars whose cores run as jobs
    U32                      sStamp = 1;

    inline U8 marks(const LLViewerObject* object)
    {
        return object->mAnimBatchStamp == sStamp ? object->mAnimBatchFlags : 0;
    }

    inline void mark(LLViewerObject* object, U8 flags)
    {
        if (object->mAnimBatchStamp != sStamp)
        {
            object->mAnimBatchStamp = sStamp;
            object->mAnimBatchFlags = 0;
        }
        object->mAnimBatchFlags |= flags;
    }

#if VKS_FRAMEJOBS_COMPARE
    struct Stats
    {
        U32 frames = 0;             // frames in the window
        U32 job_frames = 0;         // frames on the job path
        U32 objects = 0;            // active objects
        U32 avatars = 0;            // avatars held back (pending)
        U32 batched = 0;            // of them, cores run in a batch
        U32 main_thread = 0;        // avatars updated whole at their place (the agent's, early ways out, UI)
        U32 batches = 0;            // flushes with at least one batched core
        U32 job_batches = 0;        // of them, dispatched to the helpers
        U32 early_flushes = 0;      // flushes before the end of the list
        U32 held_objects = 0;       // plain objects held back
        U32 dead = 0;               // pending avatars found dead at the flush
        F64 loop_s = 0.0;           // the active objects' loop, either path
        F64 cores_s = 0.0;          // the batches' cores, on the main thread's clock
        U32 joints_compared = 0;
        U32 joint_mismatches = 0;
        U32 isolation_checked = 0;
        U32 isolation_mismatches = 0;
    };
    Stats sStats;
    U32   sJointMismatchTotal = 0;
    U32   sIsolationMismatchTotal = 0;
    U32   sWarnings = 0;
    S32   sCompare = 0;

    struct ScopedSeconds
    {
        explicit ScopedSeconds(F64* sum) : mSum(sum), mStart(LLTimer::getTotalSeconds().value()) { }
        ~ScopedSeconds() { *mSum += LLTimer::getTotalSeconds().value() - mStart; }
        F64* const mSum;
        const F64  mStart;
    };
#define AAJ_STAT(statement) statement
#else
#define AAJ_STAT(statement)
#endif

    void run_range(LLVOAvatar* const* avatars, size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            // an avatar that died since its Begin is left alone (the flush cancels its job)
            if (!avatars[i]->isDead())
            {
                avatars[i]->updateCharacterCore();
            }
        }
    }

    // LLVOAvatar::idleUpdateBegin moves the root drawable of what the avatar sits on, once a frame (the first sitter's
    // Begin does, EARLY_MOVE tells the others). A pending sitter's core reads that drawable (its root joint hangs on
    // it), so a later sitter's Begin that would still move it must wait for the flush.
    bool begin_moves_seat(LLVOAvatar* avatar, const LLViewerObject* seat)
    {
        return avatar->mDrawable.notNull() && avatar->isSitting()
            && seat->mDrawable.notNull() && !seat->mDrawable->isState(LLDrawable::EARLY_MOVE);
    }

    // The cores of `count` avatars: one avatar a chunk, on the frame job helpers when the batch is worth a dispatch.
    void run_cores(LLVOAvatar* const* avatars, size_t count)
    {
        if (count >= MIN_JOB_BATCH)
        {
            AAJ_STAT(++sStats.job_batches);
            LL::FrameJobs::parallelFor(count, 1,
                [avatars](size_t begin, size_t end, size_t /*chunk*/)
                {
                    run_range(avatars, begin, end);
                });
        }
        else
        {
            run_range(avatars, 0, count);
        }
    }

#if VKS_FRAMEJOBS_COMPARE
    //-------------------------------------------------------------------------
    // the compare mode (AvatarAnimationJobsCompare)
    //-------------------------------------------------------------------------
    // The update changes the avatar, so a core cannot be run a second time for a reference. What can be checked in a
    // running viewer:
    //  - the joints' world transforms, computed again on the main thread from the joints' local transforms the job left
    //    and compared bit for bit (the helpers' floating-point state, a write that slipped in behind the job);
    //  - isolation: the batch runs as two halves drawn by a coin, and while one half's cores run, every joint of the
    //    other half's avatars must keep its bits.
    // The proof that a core gives the serial result is INTEGRATION_TEST_llmotionjobs (llcharacter/tests).

    inline void mix(U64& hash, const void* data, size_t bytes)
    {
        const U8* p = static_cast<const U8*>(data);
        for (; bytes >= 4; bytes -= 4, p += 4)
        {
            U32 word;
            memcpy(&word, p, 4);
            hash = (hash ^ word) * 0x9E3779B97F4A7C15ULL;
            hash ^= hash >> 29;
        }
    }

    void digest_joint(LLJoint* joint, U64& hash)
    {
        LLXformMatrix* xform = joint->getXform();
        mix(hash, xform->getPosition().mV, sizeof(F32) * 3);
        mix(hash, xform->getRotation().mQ, sizeof(F32) * 4);
        mix(hash, xform->getScale().mV, sizeof(F32) * 3);
        mix(hash, joint->getWorldMatrixNoUpdate().mMatrix, sizeof(F32) * 16);
        const U32 dirty = joint->isWorldMatrixDirty() ? 1 : 0;
        mix(hash, &dirty, sizeof(dirty));
        for (LLJoint* child : joint->mChildren)
        {
            digest_joint(child, hash);
        }
    }

    U64 digest(LLVOAvatar* avatar)
    {
        U64 hash = 0xcbf29ce484222325ULL;
        if (LLJoint* root = avatar->getRootJoint())
        {
            digest_joint(root, hash);
        }
        return hash;
    }

    void warn_mismatch(const char* what, LLVOAvatar* avatar, const std::string& detail)
    {
        if (sWarnings < 20)
        {
            ++sWarnings;
            LL_WARNS("AvatarAnimJobs") << what << " mismatch, frame " << LLFrameTimer::getFrameCount() << ", avatar "
                << avatar->getID() << detail << LL_ENDL;
        }
    }

    size_t count_joints(LLJoint* joint)
    {
        size_t count = 1;
        if (joint->mUpdateXform)
        {
            for (LLJoint* child : joint->mChildren)
            {
                count += count_joints(child);
            }
        }
        return count;
    }

    // One joint's world transform again, from its local transform and the scratch parent, by the code that made it
    // (LLXformMatrix::updateMatrix); then its children, as LLJoint::updateWorldMatrixChildren walks them.
    void recompute_joint(LLJoint* joint, LLXformMatrix* scratch_parent, std::vector<LLXformMatrix>& scratch, size_t& next,
                         LLVOAvatar* avatar, bool& fault)
    {
        if (!joint->mUpdateXform)
        {
            return; // updateWorldMatrixChildren skips this joint and what hangs below it
        }
        LLXformMatrix& mine = scratch[next++];
        mine = *joint->getXform();
        if (scratch_parent)
        {
            mine.setParent(scratch_parent);
        }
        // else the root: it keeps its own parent (none, or the drawable of what the avatar sits on)
        mine.updateMatrix(false);
        bool differs = joint->isWorldMatrixDirty()
            || memcmp(mine.getWorldMatrix().mMatrix, joint->getWorldMatrixNoUpdate().mMatrix, sizeof(F32) * 16) != 0;
        if (fault)
        {
            // AvatarAnimationJobsCompare 2: a deliberate fault at the first joint of the batch, to see the count move
            fault = false;
            differs = true;
        }
        ++sStats.joints_compared;
        if (differs)
        {
            ++sStats.joint_mismatches;
            ++sJointMismatchTotal;
            warn_mismatch("joint transform", avatar, ", joint " + joint->getName());
        }
        for (LLJoint* child : joint->mChildren)
        {
            recompute_joint(child, &mine, scratch, next, avatar, fault);
        }
    }

    void compare_joints(LLVOAvatar* avatar, bool& fault)
    {
        LLJoint* root = avatar->getRootJoint();
        if (!root || avatar->isDead() || !avatar->animationCoreRefreshesJoints())
        {
            return;
        }
        static std::vector<LLXformMatrix> scratch;
        const size_t joints = count_joints(root);
        if (scratch.size() < joints)
        {
            scratch.resize(joints);
        }
        size_t next = 0;
        recompute_joint(root, nullptr, scratch, next, avatar, fault);
    }

    void run_cores_compared()
    {
        static std::vector<LLVOAvatar*> halves[2];
        static std::vector<U64> digests;
        static U32 coin = 0x2545F491u;
        halves[0].clear();
        halves[1].clear();
        for (LLVOAvatar* avatar : sBatch)
        {
            coin = coin * 1664525u + 1013904223u;
            halves[(coin >> 16) & 1].push_back(avatar);
        }
        for (S32 half = 0; half < 2; ++half)
        {
            const std::vector<LLVOAvatar*>& running = halves[half];
            const std::vector<LLVOAvatar*>& still = halves[1 - half];
            digests.resize(still.size());
            for (size_t i = 0; i < still.size(); ++i)
            {
                digests[i] = digest(still[i]);
            }
            run_cores(running.data(), running.size());
            for (size_t i = 0; i < still.size(); ++i)
            {
                ++sStats.isolation_checked;
                if (digest(still[i]) != digests[i])
                {
                    ++sStats.isolation_mismatches;
                    ++sIsolationMismatchTotal;
                    warn_mismatch("isolation", still[i], half == 0 ? " (before its core)" : " (after its core)");
                }
            }
        }
        bool fault = sCompare == 2;
        for (LLVOAvatar* avatar : sBatch)
        {
            compare_joints(avatar, fault);
        }
    }
#endif // VKS_FRAMEJOBS_COMPARE

    // Runs the pending cores as one batch, then the Ends and the held objects in list order.
    void flush(LLAgent& agent, const F64& frame_time)
    {
        if (sHeld.empty())
        {
            return;
        }

        if (!sBatch.empty())
        {
            AAJ_STAT(++sStats.batches);
            AAJ_STAT(ScopedSeconds cores_timer(&sStats.cores_s));
#if VKS_FRAMEJOBS_COMPARE
            if (sCompare != 0)
            {
                run_cores_compared();
            }
            else
#endif
            {
                run_cores(sBatch.data(), sBatch.size());
            }
        }

        for (const Held& held : sHeld)
        {
            if (!held.mAvatar)
            {
                held.mObject->idleUpdate(agent, frame_time);
            }
            else if (held.mAvatar->isDead())
            {
                // it died after its Begin: the serial loop would have finished it before; nothing is left to update
                AAJ_STAT(++sStats.dead);
                if (held.mInBatch)
                {
                    held.mAvatar->cancelAnimationJob();
                }
            }
            else
            {
                if (!held.mInBatch)
                {
                    held.mAvatar->updateCharacterCore();
                }
                held.mAvatar->idleUpdateEnd();
            }
        }

        sHeld.clear();
        sBatch.clear();
        // a new number: every mark of this batch is void
        if (++sStamp == 0)
        {
            sStamp = 1;
        }
    }
}

bool LLAvatarAnimationJobs::idleUpdateObjects(LLViewerObject* const* objects, U32 count, LLAgent& agent, const F64& frame_time)
{
    static LLCachedControl<bool> jobs_on(gSavedSettings, "AvatarAnimationJobs", true);
    bool compare = false;
#if VKS_FRAMEJOBS_COMPARE
    static LLCachedControl<S32> compare_mode(gSavedSettings, "AvatarAnimationJobsCompare", 0);
    sCompare = compare_mode;
    compare = sCompare != 0;
#endif
    // the job path only where helpers run (or the compare mode asks): alone, the walk would only add its bookkeeping
    if (!jobs_on || ((LL::FrameJobs::helperCount() == 0 || LL::FrameJobs::isSerial()) && !compare))
    {
        return false;
    }

    static bool prepared = false;
    if (!prepared)
    {
        // the settings a core reads, first used here on the main thread
        prepared = true;
        LLPhysicsMotionController::prepareForJobs();
    }

    // nothing is left from a walk that did not finish (an exception on its way out), marks included
    if (!sHeld.empty())
    {
        sHeld.clear();
        sBatch.clear();
        if (++sStamp == 0)
        {
            sStamp = 1;
        }
    }

    for (U32 i = 0; i < count; ++i)
    {
        LLViewerObject* object = objects[i];
        llassert(object->isActive());

        LLVOAvatar* avatar = object->isAvatar() ? object->asAvatar() : nullptr;
        if (!avatar)
        {
            if (!sHeld.empty())
            {
                // an object of a pending avatar's tree keeps its place behind that avatar's End
                LLViewerObject* root = static_cast<LLViewerObject*>(object->getRoot());
                if (marks(root) != 0)
                {
                    mark(root, MARK_HELD);
                    sHeld.push_back({ object, nullptr, false });
                    AAJ_STAT(++sStats.held_objects);
                    continue;
                }
            }
            object->idleUpdate(agent, frame_time);
            continue;
        }

        if (!avatar->idleUpdateSplits() || avatar->idleUpdatePath() != LLVOAvatar::IDLE_UPDATE_FULL)
        {
            // the agent's avatar, a UI avatar, an early way out: whole, at its place, after everything pending
            AAJ_STAT(sStats.early_flushes += sHeld.empty() ? 0 : 1);
            AAJ_STAT(++sStats.main_thread);
            flush(agent, frame_time);
            object->idleUpdate(agent, frame_time);
            continue;
        }

        // the trees this avatar's Begin reads: what it sits on, what its animated object is (or is worn by)
        LLViewerObject* seat = avatar->getParent() ? static_cast<LLViewerObject*>(avatar->getRoot()) : nullptr;
        LLViewerObject* volume_root = avatar->getAnimationVolumeRoot();
        if (!sHeld.empty()
            && (marks(avatar) != 0
                || (seat && (marks(seat) & (MARK_HELD | MARK_ANIMATED_ROOT)) != 0)
                || (seat && marks(seat) != 0 && begin_moves_seat(avatar, seat))
                || (volume_root && marks(volume_root) != 0)))
        {
            // its Begin must see them finished
            AAJ_STAT(++sStats.early_flushes);
            flush(agent, frame_time);
        }

        if (!avatar->idleUpdateBegin(agent, frame_time))
        {
            continue; // not on the full path after all: its update is complete
        }

        const bool in_batch = avatar->animationCoreJobSafe();
        if (in_batch)
        {
            avatar->beginAnimationJob();
            sBatch.push_back(avatar);
            AAJ_STAT(++sStats.batched);
        }
        AAJ_STAT(++sStats.avatars);
        mark(avatar, MARK_PENDING);
        if (seat)
        {
            mark(seat, MARK_RELATED);
        }
        if (volume_root)
        {
            mark(volume_root, MARK_RELATED | MARK_ANIMATED_ROOT);
        }
        sHeld.push_back({ object, avatar, in_batch });
    }

    flush(agent, frame_time);
    return true;
}

#if VKS_FRAMEJOBS_COMPARE
void LLAvatarAnimationJobs::noteLoop(F64 seconds, U32 objects, bool jobs)
{
    static LLCachedControl<bool> stats_on(gSavedSettings, "AvatarAnimationJobsStats", false);
    static F64 window_start = 0.0;
    Stats& stats = sStats;
    if (!stats_on)
    {
        stats = Stats();
        window_start = 0.0;
        return;
    }
    const F64 now = LLTimer::getTotalSeconds().value();
    if (window_start == 0.0)
    {
        // the first frame after the setting was switched on opens the window
        stats = Stats();
        window_start = now;
        return;
    }
    ++stats.frames;
    stats.job_frames += jobs ? 1 : 0;
    stats.objects += objects;
    stats.loop_s += seconds;
    if (now - window_start < 1.0)
    {
        return;
    }
    const F64 per_frame = 1.0 / (F64)stats.frames;
    LL_INFOS("AvatarAnimJobs") << "AvatarAnimJobs: frames " << stats.frames
        << " job_frames " << stats.job_frames
        << " helpers " << LL::FrameJobs::helperCount()
        << " serial " << (LL::FrameJobs::isSerial() ? 1 : 0)
        << " loop_ms " << llformat("%.4f", stats.loop_s * 1000.0 * per_frame)
        << " cores_ms " << llformat("%.4f", stats.cores_s * 1000.0 * per_frame)
        << " objects " << llformat("%.1f", stats.objects * per_frame)
        << " avatars " << llformat("%.1f", stats.avatars * per_frame)
        << " batched " << llformat("%.1f", stats.batched * per_frame)
        << " main_thread " << llformat("%.1f", stats.main_thread * per_frame)
        << " held_objects " << llformat("%.1f", stats.held_objects * per_frame)
        << " batches " << llformat("%.2f", stats.batches * per_frame)
        << " job_batches " << llformat("%.2f", stats.job_batches * per_frame)
        << " early_flushes " << llformat("%.2f", stats.early_flushes * per_frame)
        << " dead " << stats.dead
        << " joints_compared " << stats.joints_compared
        << " joint_mismatch " << stats.joint_mismatches
        << " isolation_checked " << stats.isolation_checked
        << " isolation_mismatch " << stats.isolation_mismatches
        << " joint_mismatch_total " << sJointMismatchTotal
        << " isolation_mismatch_total " << sIsolationMismatchTotal
        << LL_ENDL;
    stats = Stats();
    window_start = now;
}
#endif
