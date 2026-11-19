// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Multi-core stage C6: the avatars' animation cores as frame jobs (reports/multicore-5a-2026-11-18, section 3 row 1;
// doc/perf/multicore-simd-study.md, "C6 as built").
//
// LLViewerObjectList::update calls every active object's idleUpdate in list order. An avatar's idleUpdate is three
// steps (llvoavatar.h): Begin (everything up to the motions' update), the animation core (the motions, the blend onto
// the joints, the joints' world matrices) and End (head offset, footsteps, attachments, name tag and the rest). The core
// touches its own avatar only, so the cores of many avatars can run at once on LL::FrameJobs. idleUpdateObjects() walks
// the list in order and
//   - runs an avatar's Begin at its place, and holds its core and its End back: the avatar is pending;
//   - holds back, behind them, every object of a tree a pending avatar belongs to: its attachments, the linkset it sits
//     on, the linkset an animated object's avatar follows. Those are the objects whose idleUpdate an End reads or
//     writes around (an attachment's turn, the seat's move), so they keep their place relative to it;
//   - runs every other object at its place;
//   - at the end of the list, and before anything that must see the pending avatars finished, runs the pending cores
//     as one batch and then, in list order, the Ends and the held objects ("flush").
// A flush comes before: the agent's own avatar (always animated on the main thread, at its place), a UI avatar, an
// avatar whose idleUpdate takes an early way out, an animated object's avatar whose linkset or wearer is pending or
// held, the wearer of a pending attached animated object, and an avatar that sits on a linkset with a held object, on
// an animated object's, or on one a pending avatar sits on whose root drawable this avatar's Begin would still move
// (the pending core reads it: a sitting avatar's root joint hangs on the seat's drawable). An avatar whose core is not fit for a job this frame (LLVOAvatar::animationCoreJobSafe: the
// poser's motion, a pending change of sex) stays pending and has its core run on the main thread at its End's turn.
//
// What is equal to the serial loop: every avatar's Begin, core and End see the avatar, its attachments, its seat and
// the agent as the serial loop showed them, and every non-avatar idleUpdate sees its own tree as before (none of them
// reads an avatar's joints: LLViewerObject, grass, tree; the others are empty). The order of Begin among avatars and
// objects, and the order of End among avatars and their held objects, are the list's. What differs: the Begin of a
// later avatar and the idleUpdate of an unrelated later object now come before the End of an earlier pending avatar.
// They share no value; they do share the pipeline's queues (moved list, rebuild queues), where unrelated drawables can
// stand in another order.
//
// Job rules (llframejobs.h): the core copies no LLPointer, calls no GL, deletes nothing (the motion controller defers
// its deletions and stop requests to the End) and draws its random numbers from the avatar's own stream.
//
// Debug settings: AvatarAnimationJobs (on; off is the former loop), and in development builds (VKS_FRAMEJOBS_COMPARE)
// AvatarAnimationJobsCompare and AvatarAnimationJobsStats.
#ifndef LL_LLAVATARANIMATIONJOBS_H
#define LL_LLAVATARANIMATIONJOBS_H

#include "stdtypes.h"

class LLAgent;
class LLViewerObject;

namespace LLAvatarAnimationJobs
{
    // Runs the idle updates of the active objects (`objects`, in list order) as described above. Returns false, having
    // done nothing, when the job path is off or has no helpers to run on: the caller then runs its own loop.
    bool idleUpdateObjects(LLViewerObject* const* objects, U32 count, LLAgent& agent, const F64& frame_time);

#if VKS_FRAMEJOBS_COMPARE
    // Development builds: the time the active objects' loop took this frame, whichever path ran, for the AvatarAnimJobs
    // log line of AvatarAnimationJobsStats (one a second).
    void noteLoop(F64 seconds, U32 objects, bool jobs);
#endif
}

#endif // LL_LLAVATARANIMATIONJOBS_H
