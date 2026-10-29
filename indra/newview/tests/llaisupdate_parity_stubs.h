/**
 * @file llaisupdate_parity_stubs.h
 * @brief The recording environment of AISUpdate for the parity test.
 *
 * llaisapi.cpp is compiled as it is. Everything it calls outside itself is
 * supplied by llaisupdate_parity_stubs.cpp: the inventory model (gInventory)
 * as a recording stub over a scripted skeleton, the viewer inventory classes
 * (frozen copies of their parsing members, recording stubs for the members
 * with side effects), the agent, the message system's outgoing side, the
 * notifications, an injected clock behind AISUpdate's timers and a stub of
 * the per-frame yield. See tests/fixtures/inventory/ais/update/README.md.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLAISUPDATE_PARITY_STUBS_H
#define LL_LLAISUPDATE_PARITY_STUBS_H

#include "llsd.h"
#include "lluuid.h"

#include <functional>
#include <string>
#include <vector>

class LLViewerInventoryItem;
class LLViewerInventoryCategory;

namespace vks_ais
{
    // How AISUpdate's two timers (the per-task 8 ms timer and the shared
    // per-frame 10 ms batch timer) see time.
    //  NEVER: the timers never report expiry, so the op list carries no
    //         yields at all (the canonical order of model calls).
    //  SCHEDULED: the timers keep LLTimer's arithmetic exactly, over an
    //         injected clock that advances by call_us at every recorded
    //         model call and by frame_us at every yield. The schedule may
    //         also say that, after a yield, another AIS update resumed
    //         first in the new frame and ran for others_us[k] (cycled over
    //         the yields): it re-armed the shared batch timer and took the
    //         frame, as a concurrent InvokeAISCommandCoro would.
    enum TimerMode { TIMERS_NEVER = 0, TIMERS_SCHEDULED = 1 };

    const char* timer_mode_name(TimerMode mode);

    // ---- a run: one case in one timer mode ---------------------------------
    // Starts a run: clears the op list, sets the mode, starts a new frame
    // (so AISUpdate's static batch frame never matches a frame left over by
    // an earlier run) and sets the relative clock and frame to zero.
    // yield_extra_us[k] is added to the clock after yield k (timer
    // boundaries). The first run also reads AIS_BATCH_EXPIRY_SECONDS from
    // the class.
    void begin_run(TimerMode mode, U64 call_us, U64 frame_us, const std::vector<U64>& others_us,
                   const std::vector<U64>& yield_extra_us);
    // Advances to the next frame, as the viewer's main loop does between
    // two AIS replies that are not processed in the same frame.
    void next_frame();
    // Appends one event to the op list. In SCHEDULED mode every event but a
    // step marker costs call_us on the clock.
    void record(const LLSD& op, bool costs_time = true);
    // Takes the op list of the run.
    LLSD take_ops();

    // ---- the skeleton ------------------------------------------------------
    // Loads a scripted skeleton: {"agent_id", "root_id", "categories":
    // [category maps], "items": [AIS item maps]} (see the README).
    void skeleton_load(const LLSD& case_def);
    // The final state of the skeleton: every category and item, ordered by
    // id, and the changed-id set the model holds.
    LLSD skeleton_dump();

    // Field dumps used in the op list: every member of the object, raw
    // (no link indirection), with enum values as integers.
    LLSD item_fields(const LLViewerInventoryItem* item);
    LLSD category_fields(const LLViewerInventoryCategory* cat);

    // ---- the outgoing message side -----------------------------------------
    // Creates the real message system on the message template, so the
    // MoveInventoryItem built by updateParentOnServer() is encoded by the
    // real template builder. Returns false if the template cannot be read.
    bool message_system_init(const std::string& template_file);

    // ---- driving the code under test ---------------------------------------
    // The global the coroutine reads before anything else.
    void set_disconnected(bool disconnected);
    // Runs one step (see the README, "Steps"):
    //  - {type, target_id, url, body, callback}: the real
    //    AISAPI::InvokeAISCommandCoro with an invoke function that returns
    //    'result' (the LLSD the HTTP adapter would have produced): the
    //    error branch, AISUpdate(result, type, body), doUpdate(), the
    //    completion callback contract;
    //  - {call, args, caps, no_region, pending, idle, idle_pending,
    //    callback}: the real AISAPI command function, through the
    //    recording coprocedure manager and HTTP adapter ('result', if
    //    defined, is what the adapter returns).
    // "hooks" [{apply_checkpoint|parse_checkpoint|yield: k, actions: [...]}]
    // run at the step's k-th checkTimeout() in doUpdate() (both runs), its
    // k-th checkTimeout() before doUpdate() (sched run only) or after its
    // k-th yield (sched run only); a run_step action carries its own
    // "result". A scripted shutdown (LLCoros::Stopping) ends the step as the
    // coroutine's top level would: recorded as "abort", no callback.
    void run_step(const LLSD& step, const LLSD& result);
    // The case and run being recorded (named in the tid messages).
    void set_case(const std::string& id, TimerMode mode);
    // One message per write tid that was malformed, null or reused (case,
    // run, op index); must stay empty.
    std::vector<std::string> tid_failures();

    // Command type names (the enum's names) and back; -1 if unknown.
    const char* command_name(int type);
    int command_from_name(const std::string& name);
}

#endif // LL_LLAISUPDATE_PARITY_STUBS_H
