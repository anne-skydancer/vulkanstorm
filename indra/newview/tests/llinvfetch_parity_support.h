/**
 * @file llinvfetch_parity_support.h
 * @brief The driver of the inventory fetch wire scenarios (recorder R3b).
 *
 * The real llaisapi.cpp, llinventorymodelbackgroundfetch.cpp,
 * llinventorymodel.cpp, llviewerinventory.cpp and llinventoryobserver.cpp
 * run on recorder R2's stubbed viewer environment
 * (llinventorymodel_parity_stubs.h, used read-only) with the real
 * coroutines, coprocedure pool and llcorehttp, against the loopback server
 * in vksinventory_mock_server.py. This file provides:
 *
 *   - the synthetic inventory ("world"): written to the mock, and the login
 *     skeleton the viewer loads;
 *   - the environment: the HTTP worker thread, the application status, the
 *     AIS coprocedure pool of a chosen size, a fake clock;
 *   - frames: one viewer frame as LLAppViewer runs it (idle callbacks, the
 *     model's idle notification, the "mainloop" post, a fiber yield);
 *   - the mock's request log, read incrementally, normalised (port, tid,
 *     the UDP listen port, ids the viewer generated);
 *   - Run: one scenario's recording (wire log, side-effect journal with
 *     scheduler snapshots, public scheduler state, the final model) as a
 *     fixture case;
 *   - frozen copies of the login steps of llstartup.cpp and the COF fetch
 *     helpers of llappearancemgr.cpp (checked by check_r3b_copies.py).
 *
 * The fake clock: LLFrameTimer's frame time (the category fetch timers, the
 * 10 / 30 / 60 / 180 / 360 s guards) only moves when a scenario advances
 * it; LLTimer's total time (the background fetch's 1 s / 6 ms frame budget)
 * is frozen by scaling the clock-to-microsecond factor of llcommon's
 * TimerInfo so far down that a whole run moves it by well under a
 * millisecond (details in the fixture README). LLTimer's own expiry timers
 * (the AIS update's 8 ms / 10 ms yield timers) read the raw clock and stay
 * real-time.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLINVFETCH_PARITY_SUPPORT_H
#define LL_LLINVFETCH_PARITY_SUPPORT_H

#include "llinventorymodel_parity_harness.h"

#include "lluuid.h"
#include "llsd.h"

#include <chrono>
#include <functional>
#include <map>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace vks_fetch
{
    using vks_inv::Case;
    using vks_inv::Cases;

    extern const LLUUID AGENT;
    extern const LLUUID SESSION;
    extern const LLUUID LIBRARY_OWNER;   // ALEXANDRIA_LINDEN_ID

    // ---- the synthetic inventory --------------------------------------------
    struct Folder
    {
        LLUUID id, parent, owner;
        S32 type;
        S32 version;
        std::string name;
        bool server = true;     // false: only in the login skeleton, unknown to the server
    };
    struct Item
    {
        LLUUID id, parent, owner, creator, asset, linked;
        bool link = false;
        S32 type = 0, inv_type = 0;
        U32 flags = 0;
        S32 created = 1700000000;
        U32 base = 0x7fffffff, owner_mask = 0x7fffffff, group_mask = 0, everyone = 0, next = 0x7fffffff;
        S32 sale_type = 0, sale_price = 10;
        std::string name, desc;
    };
    struct World
    {
        LLUUID agent, libowner, root, libroot;
        std::vector<Folder> folders;
        std::vector<Item> items;

        Folder& add_folder(const LLUUID& id, const LLUUID& parent, S32 type, S32 version, const std::string& name, bool library = false);
        Item& add_item(const LLUUID& id, const LLUUID& parent, S32 type, S32 inv_type, const std::string& name, bool library = false);
        Item& add_link(const LLUUID& id, const LLUUID& parent, const LLUUID& linked, bool folder_link, const std::string& name);
        const Folder* folder(const LLUUID& id) const;
        Folder* folder(const LLUUID& id);
        const Item* item(const LLUUID& id) const;
        // The mock's line format (vksinventory_mock_server.py World.load).
        std::string text() const;
        // The login response's inventory-skeleton (library=false: the agent's
        // folders) or inventory-skel-lib.
        LLSD skeleton(bool library) const;
        // The login response the frozen startup steps read.
        LLSD login_response() const;
        void collect_ids(std::set<LLUUID>& ids) const;
    };

    // ---- environment ------------------------------------------------------------
    // False (with a reason) when the mock server is not there: the test
    // fails, it never skips.
    bool init(const std::string& template_file, const std::string& work_dir, std::string& error);
    // Ends the run: the background fetch, the coprocedure pools and their
    // coroutines, the model's HTTP requests, the HTTP service thread.
    void shutdown();
    std::string base_url();                 // http://127.0.0.1:<port>
    std::string cap_url(const std::string& cap);  // base_url() + "/cap/" + cap
    // (Re)creates the coprocedure manager with an AIS pool of n coroutines
    // and sets PoolSizeAIS = n (the scheduler reads it on every frame).
    void set_ais_pool(U32 n, bool force = false);
    // The scripted frame-budget clock of llinvfetch_parity_bgf.cpp: every
    // LLTimer::getTotalSeconds() read of the background fetch returns it and
    // advances it by step (0: frozen).
    void bgf_clock(F64 now, F64 step);
    S64 bgf_clock_reads();
    // llinvfetch_parity_ais.cpp: LLCoros::checkStop() calls of llaisapi.cpp
    // since the last arm; abort_at > 0 throws LLCoros::Stopping at that call.
    void ais_stop_arm(S64 abort_at);
    // The next n hasExpired() calls of llaisapi.cpp's timers answer true
    // (llinvfetch_parity_ais.cpp); 0 between scenarios.
    void ais_timer_force_expired(S64 n);
    S64 ais_stop_calls();
    S64 ais_stop_thrown();
    // Resets everything a scenario touches: gInventory, the background
    // fetch singleton, R2's configuration and journal, the callback ids,
    // the settings, the fake clock; installs the recorder.
    void reset_scenario(const std::string& cache_dir);
    // The region: AIS and the legacy capabilities pointing at the mock.
    enum CapSet { CAPS_NONE = 0, CAPS_AIS = 1, CAPS_LEGACY = 2, CAPS_ALL = 3 };
    void set_region(bool present, int caps, bool received = true);

    // ---- the fake clock ---------------------------------------------------------
    void clock_set(F64 frame_seconds);
    void clock_advance(F64 seconds);
    F64 clock_now();

    // ---- the background fetch's state (private members, read only) --------
    struct SchedState
    {
        bool bg_active, folder_active, inv_started, lib_started, mkt_started, all_fetched;
        S32 fetch_count, folder_count;
        std::vector<std::pair<LLUUID, S32> > folder_queue, item_queue;
        std::vector<LLUUID> expected;
        std::vector<LLUUID> forced;
    };
    SchedState sched_state();
    std::string sched_text(const SchedState& s);
    // The public predicates of LLInventoryModelBackgroundFetch.
    std::string public_state();

    // ---- the mock's log -----------------------------------------------------------
    struct Wire
    {
        int seq = 0;
        int conn = 0;
        double t_in = 0, t_out = 0;
        std::string method, target, head, body;
        int status = 0;
        std::string rbody;
        bool have_rbody = false;
        size_t rlen = 0;
        std::string rtype;
        bool answered = false;
        bool held = false;
        int round = 0;
    };
    void mock_world(const World& w);
    void mock_mode(const std::string& lines);

    // ---- normalisation ----------------------------------------------------------------
    class Norm
    {
    public:
        void known(const std::set<LLUUID>& ids) { mKnown.insert(ids.begin(), ids.end()); }
        std::string apply(const std::string& s);
        size_t tids() const { return mTids.size(); }
        size_t tid_uses() const { return mTidUses; }
        size_t generated() const { return mGen.size(); }
    private:
        std::set<LLUUID> mKnown;
        std::map<std::string, std::string> mGen;
        std::set<std::string> mTids;
        size_t mTidUses = 0;
    };

    // ---- one scenario --------------------------------------------------------------
    class Run
    {
    public:
        // LOCKSTEP: production concurrency with the mock holding every
        // reply; each round dispatches (one frame), then the replies are
        // released one at a time in request order and each is processed
        // with the idle callbacks held (round()/drain()): the request log
        // sorted within each round, the journal and the model in full.
        // SEQ: one request in flight at a time, everything in total order.
        // PHASED: the legacy capabilities keep several requests in flight on
        // their own (up to 12 + 4); the request log and the journal are
        // compared sorted within each phase (phases end at step()), the
        // scheduler only at the steps. CONC: the request log and the
        // order-free journal lines as multisets, the model with sorted child
        // lists, the concurrency bounds. large: the journal and the model
        // as digests with a sample.
        enum Mode { SEQ, PHASED, CONC, LOCKSTEP };
        Run(const std::string& id, const World* world, Mode mode = SEQ, bool large = false);
        ~Run();

        // One frame. idle=false holds the idle callbacks (the scheduler) back.
        void frame(bool idle = true);
        // Frames until nothing is in flight anywhere (AIS pool idle, no
        // background fetch request outstanding, every mock request answered,
        // every legacy item handler done) and the scheduler state has been
        // stable for a few frames. queues_empty additionally waits for the
        // fetch queues to drain. Fails the scenario on timeout.
        // fetch_idle=false: the background fetch's request count may stay
        // above zero (a legacy handler nobody pumps any more).
        bool settle(bool idle = true, bool queues_empty = false, F64 timeout = 120.0, bool fetch_idle = true);
        // Frames until pred() holds after a frame (timeout: the scenario fails).
        bool until(const std::function<bool()>& pred, bool idle = true, F64 timeout = 120.0);
        void frames(int n, bool idle = true);
        // One frame with no pause, journaling the background fetch's request
        // count before and after the idle callbacks ("frame <label> ...").
        // Deterministic while no reply can arrive (the mock holds replies).
        void frame_note(const std::string& label);
        // A journal marker with the number of requests seen so far.
        void step(const std::string& name);
        // A line compared in every mode (a deterministic measurement or an
        // assertion the scenario computed).
        void fact(const std::string& line) { mFacts.push_back(line); }
        // Held replies (mock mode hold=1). release() answers the given
        // arrivals; held() lists the unanswered ones, sorted by request.
        void release(const std::vector<int>& seqs);
        void release_all();
        std::vector<int> held();
        // LOCKSTEP: one dispatch frame, wait until every active AIS
        // coroutine's request has arrived, then release them one at a time
        // (sorted by method, target, body), each processed with the idle
        // callbacks held. Returns false when nothing was in flight.
        bool round(const std::string& label);
        // LOCKSTEP: wait until every active AIS coroutine's request has
        // arrived at the mock and nothing more arrives for 100 ms.
        void await_arrivals(const std::string& label);
        // round() until nothing is dispatched and nothing is in flight.
        void drain(const std::string& label, int max_rounds = 500);
        // Releases one held request and processes it (idle held).
        void release_one(int seq);
        // The request lines as finish() writes them (normalised), in arrival order.
        std::vector<std::string> wire_lines();
        void note(const std::string& line);
        // Requests the mock has received so far in this scenario.
        size_t requests();
        // Snapshot of the scheduler into the journal if it changed.
        void snapshot(const std::string& why = "");
        void fail(const std::string& why) { mFailures.push_back(why); }
        const std::vector<std::string>& failures() const { return mFailures; }

        // Ends the scenario: builds the fixture case (and, in dump mode, the
        // response bodies).
        Case finish(std::string& responses);

        Norm& norm() { return mNorm; }
        std::vector<Wire>& wire() { return mWire; }

    private:
        void read_log();
        bool idle_now(bool fetch_idle);
        void check_budget();
        std::string format_wire(const Wire& w);

        std::string mId;
        const World* mWorld;
        Mode mMode;
        bool mConcurrent, mLarge;
        std::vector<size_t> mPhaseStarts;   // arrival counts at the steps (PHASED)
        Norm mNorm;
        std::vector<Wire> mWire;
        std::map<int, size_t> mBySeq;
        size_t mLogOffset = 0;
        size_t mArrivals = 0;
        std::vector<std::string> mFailures;
        std::string mLastSnap;
        S32 mSnapCount = 0;
        // concurrency measures
        S32 mMaxInFlight = 0, mMaxDispatch = 0, mMaxPool = 0;
        S32 mLastBefore = 0, mLastAfter = 0;
        S32 mFrames = 0;
        S32 mItemHandlersDone = 0;
        std::vector<std::string> mFacts;
        S32 mRound = 0;
        std::chrono::steady_clock::time_point mStart;
        F64 mBudget = 300.0;
    public:
        // The scenario's wall-clock budget: exceeding it, or any settle /
        // until timeout, aborts the scenario and the run (Abort).
        void budget(F64 seconds) { mBudget = seconds; }
    };

    // Thrown on a timeout or an exhausted budget: run_scenarios() records
    // the scenario as failed and skips the rest of the run, so a stalled
    // port fails in minutes.
    struct Abort : public std::runtime_error
    {
        explicit Abort(const std::string& w) : std::runtime_error(w) {}
    };

    // The window's busy count is set high at the start of a scenario; every
    // legacy item handler decrements it once (FetchItemHttpHandler), so the
    // difference counts the item handlers that completed.
    S32 item_handlers_done();

    // ---- recording hooks ------------------------------------------------------------
    // An LLInventoryCallback (via doInventoryCb) and an AISAPI completion
    // that journal "callback <tag> <id>".
    LLPointer<LLInventoryCallback> cb(const std::string& tag);
    std::function<void(const LLUUID&)> ais_cb(const std::string& tag);
    std::function<void()> nullary(const std::string& tag);
    // How many of the callbacks above have fired (part of settle's
    // stability test: a coroutine outside the AIS pool, such as the
    // CreateInventoryCategory one, ends in a callback).
    int callbacks_fired();

    // ---- frozen copies (llstartup.cpp, llappearancemgr.cpp) -----------------------------
    namespace startup
    {
        void inventory_send(const LLSD& response);      // STATE_INVENTORY_SEND (library root and owner)
        void inventory_skel(const LLSD& response);      // STATE_INVENTORY_SKEL
        void inventory_send2();                          // STATE_INVENTORY_SEND2 (inventory part)
        bool inventory_callbacks();                      // STATE_INVENTORY_CALLBACKS (false: waiting)
        void find_lost_items();                          // STATE_STARTED: findLostItems
    }
    namespace appearance
    {
        // A type of its own (not llappearancemgr.h's std::function typedef),
        // so the frozen copies' unqualified calls pick these functions over
        // llappearancemgr.h's by overload resolution, without editing them.
        struct nullary_func_t : public std::function<void()>
        {
            nullary_func_t() {}
            nullary_func_t(const std::function<void()>& f) : std::function<void()>(f) {}
        };
        void callAfterCOFFetch(nullary_func_t cb);
        void callAfterCategoryFetch(const LLUUID& cat_id, nullary_func_t cb);
        void callAfterCategoryLinksFetch(const LLUUID& cat_id, nullary_func_t cb);
    }

    // ---- utilities ---------------------------------------------------------------------
    std::string escape_text(const std::string& s);   // CR, LF, tab, \\ and non-printables escaped
    std::string short_sha(const std::string& bytes); // first 16 hex digits of SHA-256
}

#endif
