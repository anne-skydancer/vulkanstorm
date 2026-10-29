/**
 * @file llaisupdate_parity_stubs.cpp
 * @brief The recording environment of AISUpdate for the parity test.
 *
 * llaisapi.cpp is compiled unchanged into the test. This file supplies what
 * it calls outside itself:
 *
 *  - gInventory: a recording stub of LLInventoryModel over a scripted
 *    skeleton. Every call llaisapi.cpp makes is appended to the op list
 *    with its full arguments, queries included, and its effect on the
 *    skeleton is emulated so that later queries of the same update see what
 *    the real model would show them (see "The skeleton model" in the
 *    fixture README for what is emulated and what is not).
 *  - LLViewerInventoryItem / LLViewerInventoryCategory: frozen copies of the
 *    members the parse runs (constructors, copies, unpackMessage, the link
 *    getters) and of updateParentOnServer(); recording stubs for getVersion,
 *    setVersion and fetch. Each frozen copy is fenced by VKS-COPY markers
 *    and checked against the live source by tests/check_r3a_copies.py.
 *  - The agent, the region, settings, notifications, the grid manager.
 *  - The message system's outgoing side: the real LLMessageSystem on the
 *    real template builds MoveInventoryItem; gAgent.sendReliableMessage()
 *    records the encoded message instead of sending it.
 *  - AISUpdate's timers and its yield: this file includes every header
 *    llaisapi.cpp uses, then renames LLTimer to vks_r3a_LLTimer and
 *    llcoro::suspendUntilNextFrame to vks_r3a_suspendUntilNextFrame and
 *    includes llaisapi.cpp verbatim, so the renames reach only the text of
 *    llaisapi.h and llaisapi.cpp (every other header is already in, behind
 *    its guard). vks_r3a_LLTimer is a class of this file with LLTimer's
 *    arithmetic over an injected clock; the yield is defined below over an
 *    injected frame counter. No other translation unit sees AISUpdate.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

// Every header llaisapi.cpp and llaisapi.h include, before the renames.
#include "lluuid.h"
#include <map>
#include <set>
#include <string>
#include "../llviewerinventory.h"
#include "llcorehttputil.h"
#include "llcoproceduremanager.h"
#include "../llagent.h"
#include "../llappviewer.h"
#include "llcallbacklist.h"
#include "../llinventorymodel.h"
#include "../llinventoryobserver.h"
#include "llnotificationsutil.h"
#include "llsdutil.h"
#include "../llviewerregion.h"
#include "../llvoavatar.h"
#include "../llvoavatarself.h"
#include "../llviewercontrol.h"
#include "../llviewernetwork.h"
#include "lleventcoro.h"
#include "llframetimer.h"
#include "lltimer.h"

// ---- What llaisapi.cpp calls outside itself, renamed for its text only ----
//
// The renames below reach only the text of llaisapi.h and llaisapi.cpp:
// every header they include is already in, behind its guard. Each renamed
// name is a class or function of this file; the rest of the executable
// keeps the real one.

// AISUpdate's timers: LLTimer's arithmetic (lltimer.cpp) over the injected
// clock (microsecond ticks): start() and reset() leave mExpirationTicks at 0,
// setTimerExpirySec() adds (U64)(F32)(seconds * frequency) to now, and
// hasExpired() is now >= mExpirationTicks. In TIMERS_NEVER mode hasExpired()
// is always false. hasExpired() on a task timer is also the harness's
// checkpoint: AISUpdate::checkTimeout() asks the task timer first, every
// time, so its k-th call is the k-th checkTimeout() of the update. Only the
// members llaisapi.cpp uses.
class vks_r3a_LLTimer
{
public:
    vks_r3a_LLTimer() { start(); }
    void start() { reset(); mStarted = true; }
    void reset();
    void setTimerExpirySec(F32SecondsImplicit expiration);
    bool hasExpired() const;
    F32SecondsImplicit getElapsedTimeF32() const;

private:
    U64 mLastClockCount = 0;
    U64 mExpirationTicks = 0;
    bool mStarted = false;
};

namespace llcoro
{
    // The yield AISUpdate::checkTimeout() makes (defined below).
    void vks_r3a_suspendUntilNextFrame();
}

// LLCoros::checkStop(), the only LLCoros member llaisapi.cpp calls: throws
// LLCoros::Stopping when the case scripts a shutdown at that yield.
struct vks_r3a_LLCoros
{
    static void checkStop();
};

// LLCoreHttpUtil::HttpCoroutineAdapter as the AIS commands use it: the
// methods they bind record the request (method, URL, body, Destination,
// headers, options) and return the step's scripted result.
namespace vks_r3a_http
{
    class HttpCoroutineAdapter
    {
    public:
        typedef std::shared_ptr<HttpCoroutineAdapter> ptr_t;
        static const std::string HTTP_RESULTS;
        static LLCore::HttpStatus getStatusFromLLSD(const LLSD& results)
        {
            return LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(results);
        }
        LLSD postAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body,
                            LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers);
        LLSD putAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body,
                           LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers);
        LLSD patchAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body,
                             LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers);
        LLSD getAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url,
                           LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers);
        LLSD deleteAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url,
                              LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers);
        LLSD copyAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const std::string destination,
                            LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers);
    };
    const std::string HttpCoroutineAdapter::HTTP_RESULTS = LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS;
}

// LLCoprocedureManager as EnqueueAISCommand() and onIdle() use it: the
// pending count is scripted, an enqueue is recorded and the procedure runs
// at once on a recording adapter.
class vks_r3a_LLCoprocedureManager
{
public:
    typedef std::function<void(vks_r3a_http::HttpCoroutineAdapter::ptr_t&, const LLUUID& id)> CoProcedure_t;
    static vks_r3a_LLCoprocedureManager& instance();
    size_t countPending(const std::string& pool) const;
    LLUUID enqueueCoprocedure(const std::string& pool, const std::string& name, CoProcedure_t proc);
};

// gAgent as llaisapi.cpp uses it (getRegion() for the capabilities, getID()
// for link permissions); the region answers the case's capability URLs.
struct vks_r3a_Region
{
    bool isCapabilityAvailable(const std::string& name) const;
    std::string getCapability(const std::string& name) const;
};

struct vks_r3a_Agent
{
    vks_r3a_Region* getRegion() const;
    const LLUUID& getID() const { return gAgentID; }
};
vks_r3a_Agent vks_r3a_gAgent;

// The start of AISUpdate::doUpdate(), the apply phase. doUpdate() is
// declared, defined and called with an extra unnamed parameter of type
// "function returning vks_r3a_apply_tag" (an abstract declarator, so the
// declaration and the definition stay valid); at the one call site the
// argument vks_r3a_apply_tag() converts to that pointer, and the conversion
// tells the harness the apply phase has begun. Nothing else changes.
struct vks_r3a_apply_tag
{
    typedef vks_r3a_apply_tag (*fn_t)();
    operator fn_t() const;
};

// The code under test, unchanged.
#define doUpdate() doUpdate(vks_r3a_apply_tag())
#define LLTimer vks_r3a_LLTimer
#define suspendUntilNextFrame vks_r3a_suspendUntilNextFrame
#define LLCoros vks_r3a_LLCoros
#define LLCoreHttpUtil vks_r3a_http
#define LLCoprocedureManager vks_r3a_LLCoprocedureManager
#define gAgent vks_r3a_gAgent
#include "../llaisapi.cpp"
#undef doUpdate
#undef LLTimer
#undef suspendUntilNextFrame
#undef LLCoros
#undef LLCoreHttpUtil
#undef LLCoprocedureManager
#undef gAgent

#include "../llagent.h"
#include "../llagentdata.h"
#include "../llappviewer.h"
#include "../llinventorymodel.h"
#include "../llinventoryobserver.h"
#include "../llviewerinventory.h"
#include "../llviewerregion.h"
#include "../llviewercontrol.h"
#include "../llviewernetwork.h"
#include "../llvoavatarself.h"

#include "llnotificationsutil.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llapr.h"
#include "message.h"
#include "llmessagebuilder.h"
#include "lleventcoro.h"
#include "llframetimer.h"
#include "lltimer.h"

#include "llaisupdate_parity_stubs.h"

#include <stdexcept>

static const char * const LOG_INV("Inventory");
static const char * const LOG_LOCAL("InventoryLocalize");

// Access to private and protected members: an explicit template
// instantiation may name them, and the friend it defines hands the pointer
// out (the pattern of lltexturefetch_parity_stubs.cpp).
namespace
{
    template<typename Tag, typename Tag::type M>
    struct Steal
    {
        friend typename Tag::type steal(Tag) { return M; }
    };

    typedef std::function<LLSD(vks_r3a_http::HttpCoroutineAdapter::ptr_t, LLCore::HttpRequest::ptr_t,
        const std::string, LLSD, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)> invoke_fn_t;

    struct InvokeTag
    {
        typedef void (*type)(vks_r3a_http::HttpCoroutineAdapter::ptr_t, invoke_fn_t, std::string,
                             LLUUID, LLSD, AISAPI::completion_t, AISAPI::COMMAND_TYPE);
        friend type steal(InvokeTag);
    };
    struct BatchConstTag { typedef const F32 AISUpdate::*type; friend type steal(BatchConstTag); };
    struct PostponedTag
    {
        typedef std::list<std::pair<std::string, vks_r3a_LLCoprocedureManager::CoProcedure_t> >* type;
        friend type steal(PostponedTag);
    };
    struct OnIdleTag { typedef void (*type)(void*); friend type steal(OnIdleTag); };
    struct BuilderTag { typedef LLMessageBuilder* LLMessageSystem::*type; friend type steal(BuilderTag); };
    struct CatVersionTag { typedef S32 LLViewerInventoryCategory::*type; friend type steal(CatVersionTag); };
    struct BatchTimerTag { typedef vks_r3a_LLTimer* type; friend type steal(BatchTimerTag); };
    struct BatchFrameTag { typedef U32* type; friend type steal(BatchFrameTag); };
    struct CatFetchingTag
    {
        typedef LLViewerInventoryCategory::EFetchType LLViewerInventoryCategory::*type;
        friend type steal(CatFetchingTag);
    };
}
template struct Steal<InvokeTag, &AISAPI::InvokeAISCommandCoro>;
template struct Steal<BuilderTag, &LLMessageSystem::mMessageBuilder>;
template struct Steal<CatVersionTag, &LLViewerInventoryCategory::mVersion>;
template struct Steal<CatFetchingTag, &LLViewerInventoryCategory::mFetching>;
template struct Steal<BatchTimerTag, &AISUpdate::sBatchTimer>;
template struct Steal<BatchFrameTag, &AISUpdate::sBatchFrameCount>;
template struct Steal<BatchConstTag, &AISUpdate::AIS_BATCH_EXPIRY_SECONDS>;
template struct Steal<PostponedTag, &AISAPI::sPostponedQuery>;
template struct Steal<OnIdleTag, &AISAPI::onIdle>;

// The frame counter AISUpdate reads (LLFrameTimer::getFrameCount()).
namespace
{
    struct FrameCount : public LLFrameTimer
    {
        static S32 get() { return sFrameCount; }
        static void set(S32 v) { sFrameCount = v; }
    };
}

//============================================================================
// The recorder and the skeleton
//============================================================================
namespace vks_ais
{
    namespace
    {
        struct State
        {
            typedef std::map<LLUUID, LLPointer<LLViewerInventoryCategory> > cat_map_t;
            typedef std::map<LLUUID, LLPointer<LLViewerInventoryItem> > item_map_t;
            cat_map_t cats;
            item_map_t items;
            LLUUID root;
            int internal = 0;
            TimerMode mode = TIMERS_NEVER;
            U64 clock = 1000000;    // microseconds; absolute, never reset
            U64 clock_base = 0;
            S32 frame_base = 0;
            U64 call_us = 100;
            U64 frame_us = 16667;
            std::vector<U64> others_us;
            std::vector<U64> yield_extra_us;
            size_t yields = 0;
            LLSD ops = LLSD::emptyArray();
            // the step being run: its hooks, its checkpoint and yield counts
            LLSD hooks = LLSD::emptyArray();
            bool in_apply = false;          // doUpdate() has begun
            S32 parse_checkpoints = 0;      // checkTimeout() calls before it
            S32 apply_checkpoints = 0;      // checkTimeout() calls in it
            bool recheck_pending = false;   // the next task-timer test is the while loop's recheck
            bool force_expiry = false;      // this checkTimeout() yields (a scripted stop)
            S32 step_yields = 0;
            int nest = 0;
            bool stop_armed = false;
            // the shutdown contract: the model and the op count at the throw
            std::string stop_dump;
            size_t stop_op_index = 0;
            // the case being run, for the tid messages
            std::string case_id;
            std::string run_mode;
            // the command functions' environment
            bool has_region = true;
            std::string inv_cap;
            std::string lib_cap;
            S32 pending = 0;
            LLSD next_result;
            std::set<std::string> tids;
            std::vector<std::string> tid_failures;
            F32 batch_expiry = -1.f;
        };

        State& st()
        {
            static State s;
            return s;
        }

        LLTransactionID to_txn(const LLUUID& id)
        {
            LLTransactionID t;
            static_cast<LLUUID&>(t) = id;
            return t;
        }

        // Calls made by the emulation itself (and by the harness) are not
        // the code under test's: they are neither recorded nor timed.
        struct Internal
        {
            Internal() { ++st().internal; }
            ~Internal() { --st().internal; }
        };

        bool recording()
        {
            return st().internal == 0;
        }

        LLViewerInventoryCategory* store_cat(const LLUUID& id)
        {
            State::cat_map_t::iterator it = st().cats.find(id);
            return it == st().cats.end() ? NULL : it->second.get();
        }

        LLViewerInventoryItem* store_item(const LLUUID& id)
        {
            State::item_map_t::iterator it = st().items.find(id);
            return it == st().items.end() ? NULL : it->second.get();
        }

        bool is_model_cat(const LLViewerInventoryCategory* cat)
        {
            return cat && store_cat(cat->getUUID()) == cat;
        }

        bool is_model_item(const LLViewerInventoryItem* item)
        {
            return item && store_item(item->getUUID()) == item;
        }

        // Children of a folder in the skeleton, as the real model's
        // parent-child arrays would hold them (a folder the skeleton does
        // not know has no arrays: 0).
        S32 child_count(const LLUUID& id)
        {
            if (!store_cat(id))
            {
                return 0;
            }
            S32 n = 0;
            for (const auto& c : st().cats)
            {
                if (c.second->getParentUUID() == id)
                {
                    ++n;
                }
            }
            for (const auto& i : st().items)
            {
                if (i.second->getParentUUID() == id)
                {
                    ++n;
                }
            }
            return n;
        }

        // Removes a folder's descendents from the skeleton, deepest first.
        void remove_descendents(const LLUUID& id)
        {
            std::vector<LLUUID> sub;
            for (const auto& c : st().cats)
            {
                if (c.second->getParentUUID() == id)
                {
                    sub.push_back(c.first);
                }
            }
            for (const LLUUID& s : sub)
            {
                remove_descendents(s);
                st().cats.erase(s);
            }
            std::vector<LLUUID> items;
            for (const auto& i : st().items)
            {
                if (i.second->getParentUUID() == id)
                {
                    items.push_back(i.first);
                }
            }
            for (const LLUUID& s : items)
            {
                st().items.erase(s);
            }
        }

        U64 rel_clock()
        {
            return st().clock - st().clock_base;
        }

        S32 rel_frame()
        {
            return FrameCount::get() - st().frame_base;
        }
    }

    const char* timer_mode_name(TimerMode mode)
    {
        return mode == TIMERS_SCHEDULED ? "sched" : "never";
    }

    void begin_run(TimerMode mode, U64 call_us, U64 frame_us, const std::vector<U64>& others_us,
                   const std::vector<U64>& yield_extra_us)
    {
        State& s = st();
        s.ops = LLSD::emptyArray();
        s.mode = mode;
        s.call_us = call_us;
        s.frame_us = frame_us;
        s.others_us = others_us;
        s.yield_extra_us = yield_extra_us;
        s.yields = 0;
        if (s.batch_expiry < 0.f)
        {
            // AISUpdate::AIS_BATCH_EXPIRY_SECONDS is a const member: read it
            // from a throwaway update (an empty result: no model call),
            // keeping the statics its constructor touches.
            Internal guard;
            vks_r3a_LLTimer saved_timer = *steal(BatchTimerTag());
            U32 saved_frame = *steal(BatchFrameTag());
            {
                AISUpdate probe(LLSD::emptyMap(), AISAPI::UPDATEITEM, LLSD());
                s.batch_expiry = probe.*steal(BatchConstTag());
            }
            *steal(BatchTimerTag()) = saved_timer;
            *steal(BatchFrameTag()) = saved_frame;
        }
        s.hooks = LLSD::emptyArray();
        s.stop_armed = false;
        // A frame no earlier run has used: AISUpdate's static batch frame
        // can never match it, so the first update restarts the batch timer.
        FrameCount::set(FrameCount::get() + 1);
        s.frame_base = FrameCount::get();
        s.clock += 1000000;
        s.clock_base = s.clock;
    }

    void next_frame()
    {
        FrameCount::set(FrameCount::get() + 1);
        if (st().mode == TIMERS_SCHEDULED)
        {
            st().clock += st().frame_us;
        }
    }

    void record(const LLSD& op, bool costs_time)
    {
        static const bool trace = getenv("VKS_AISUPDATE_TRACE") != NULL;
        if (trace)
        {
            std::cerr << "[aisupdate]   " << op["op"].asString() << std::endl;
        }
        st().ops.append(op);
        if (costs_time && st().mode == TIMERS_SCHEDULED)
        {
            st().clock += st().call_us;
        }
    }

    LLSD take_ops()
    {
        LLSD out = st().ops;
        st().ops = LLSD::emptyArray();
        return out;
    }

    LLSD item_fields(const LLViewerInventoryItem* item)
    {
        LLSD m = LLSD::emptyMap();
        if (!item)
        {
            return m;
        }
        Internal guard;
        // Qualified calls: the base members, never the viewer overrides
        // that follow links.
        m["item_id"] = item->LLInventoryObject::getUUID();
        m["parent_id"] = item->LLInventoryObject::getParentUUID();
        m["asset_id"] = item->LLInventoryItem::getAssetUUID();
        m["type"] = (LLSD::Integer)item->LLInventoryObject::getType();
        m["inv_type"] = (LLSD::Integer)item->LLInventoryItem::getInventoryType();
        m["flags"] = (LLSD::Integer)(S32)item->LLInventoryItem::getFlags();
        m["name"] = item->LLInventoryObject::getName();
        m["desc"] = item->LLInventoryItem::getDescription();
        m["created_at"] = (LLSD::Integer)(S32)item->LLInventoryItem::getCreationDate();
        m["thumbnail_id"] = item->LLInventoryObject::getThumbnailUUID();
        m["favorite"] = item->LLInventoryObject::getIsFavorite();
        m["runtime"] = item->LLInventoryObject::getRuntime();
        const LLPermissions& p = item->LLInventoryItem::getPermissions();
        LLSD perm = LLSD::emptyMap();
        perm["creator_id"] = p.getCreator();
        perm["owner_id"] = p.getOwner();
        perm["last_owner_id"] = p.getLastOwner();
        perm["group_id"] = p.getGroup();
        perm["is_owner_group"] = p.isGroupOwned();
        perm["base_mask"] = (LLSD::Integer)(S32)p.getMaskBase();
        perm["owner_mask"] = (LLSD::Integer)(S32)p.getMaskOwner();
        perm["group_mask"] = (LLSD::Integer)(S32)p.getMaskGroup();
        perm["everyone_mask"] = (LLSD::Integer)(S32)p.getMaskEveryone();
        perm["next_owner_mask"] = (LLSD::Integer)(S32)p.getMaskNextOwner();
        m["permissions"] = perm;
        const LLSaleInfo& sale = item->LLInventoryItem::getSaleInfo();
        LLSD si = LLSD::emptyMap();
        si["sale_type"] = (LLSD::Integer)sale.getSaleType();
        si["sale_price"] = (LLSD::Integer)sale.getSalePrice();
        m["sale_info"] = si;
        m["complete"] = item->isFinished();
        m["transaction_id"] = LLUUID(item->getTransactionID());
        m["running"] = item->getIsRunning();
        m["faulted"] = item->getIsFaulted();
        return m;
    }

    LLSD category_fields(const LLViewerInventoryCategory* cat)
    {
        LLSD m = LLSD::emptyMap();
        if (!cat)
        {
            return m;
        }
        Internal guard;
        m["category_id"] = cat->LLInventoryObject::getUUID();
        m["parent_id"] = cat->LLInventoryObject::getParentUUID();
        m["name"] = cat->LLInventoryObject::getName();
        m["type_default"] = (LLSD::Integer)cat->getPreferredType();
        m["type"] = (LLSD::Integer)cat->LLInventoryObject::getType();
        m["thumbnail_id"] = cat->LLInventoryObject::getThumbnailUUID();
        m["favorite"] = cat->LLInventoryObject::getIsFavorite();
        m["owner_id"] = cat->getOwnerID();
        m["version"] = (LLSD::Integer)(cat->*steal(CatVersionTag()));
        m["descendents"] = (LLSD::Integer)cat->getDescendentCount();
        m["fetching"] = (LLSD::Integer)(cat->*steal(CatFetchingTag()));
        return m;
    }

    void skeleton_load(const LLSD& c)
    {
        Internal guard;
        State& s = st();
        s.cats.clear();
        s.items.clear();
        gAgentID = c["agent_id"].asUUID();
        gAgentSessionID = c["session_id"].asUUID();
        s.root = c["root_id"].asUUID();
        const LLSD& skel = c["skeleton"];
        for (LLSD::array_const_iterator it = skel["categories"].beginArray(); it != skel["categories"].endArray(); ++it)
        {
            const LLSD& m = *it;
            LLPointer<LLViewerInventoryCategory> cat = new LLViewerInventoryCategory(
                m["category_id"].asUUID(), m["parent_id"].asUUID(),
                (LLFolderType::EType)m["type_default"].asInteger(), m["name"].asString(),
                m.has("owner_id") ? m["owner_id"].asUUID() : gAgentID);
            cat->setVersion(m.has("version") ? m["version"].asInteger() : (S32)LLViewerInventoryCategory::VERSION_UNKNOWN);
            cat->setDescendentCount(m.has("descendents") ? m["descendents"].asInteger()
                                                         : (S32)LLViewerInventoryCategory::DESCENDENT_COUNT_UNKNOWN);
            s.cats[cat->getUUID()] = cat;
        }
        for (LLSD::array_const_iterator it = skel["items"].beginArray(); it != skel["items"].endArray(); ++it)
        {
            const LLSD& m = *it;
            LLPointer<LLViewerInventoryItem> item = new LLViewerInventoryItem();
            item->LLInventoryItem::fromLLSD(m);
            item->setComplete(m.has("_complete") ? m["_complete"].asBoolean() : true);
            if (m.has("_transaction_id"))
            {
                item->setTransactionID(to_txn(m["_transaction_id"].asUUID()));
            }
            s.items[item->getUUID()] = item;
        }
        // Clears the changed-id set and the masks.
        gInventory.notifyObservers();
    }

    LLSD skeleton_dump()
    {
        Internal guard;
        LLSD out = LLSD::emptyMap();
        LLSD cats = LLSD::emptyArray();
        for (const auto& c : st().cats)
        {
            cats.append(category_fields(c.second.get()));
        }
        LLSD items = LLSD::emptyArray();
        for (const auto& i : st().items)
        {
            items.append(item_fields(i.second.get()));
        }
        LLSD changed = LLSD::emptyArray();
        for (const LLUUID& id : gInventory.getChangedIDs())
        {
            changed.append(id);
        }
        out["categories"] = cats;
        out["items"] = items;
        out["changed_ids"] = changed;
        return out;
    }

    void set_disconnected(bool disconnected)
    {
        gDisconnected = disconnected;
    }

    namespace
    {
        AISAPI::completion_t recording_callback(bool with_callback)
        {
            AISAPI::completion_t callback;
            if (with_callback)
            {
                callback = [](const LLUUID& id)
                {
                    LLSD op = LLSD::emptyMap();
                    op["op"] = "callback";
                    op["id"] = id;
                    record(op);
                };
            }
            return callback;
        }

        // A command result through the real coroutine, with an invoke
        // function that returns it.
        void invoke_result(int type, const std::string& url, const LLUUID& target_id, const LLSD& body,
                           const LLSD& result, bool with_callback)
        {
            invoke_fn_t invoke = [result](vks_r3a_http::HttpCoroutineAdapter::ptr_t, LLCore::HttpRequest::ptr_t,
                                          const std::string, LLSD, LLCore::HttpOptions::ptr_t,
                                          LLCore::HttpHeaders::ptr_t) -> LLSD
            {
                return result;
            };
            steal(InvokeTag())(vks_r3a_http::HttpCoroutineAdapter::ptr_t(), invoke, url, target_id, body,
                               recording_callback(with_callback), (AISAPI::COMMAND_TYPE)type);
        }

        AISAPI::ITEM_TYPE item_type(const LLSD& args)
        {
            return args["item_type"].asString() == "LIBRARY" ? AISAPI::LIBRARY : AISAPI::INVENTORY;
        }

        void record_postponed()
        {
            size_t n = steal(PostponedTag())->size();
            LLSD op = LLSD::emptyMap();
            op["op"] = "postponed";
            op["count"] = (LLSD::Integer)n;
            op["idle_registered"] = gIdleCallbacks.containsFunction(steal(OnIdleTag()), NULL);
            record(op, false);
        }

        // One call of a real AISAPI command function (or of isAvailable /
        // getCapNames), in the capability and pool state the step scripts.
        void call_command(const LLSD& step, const LLSD& result)
        {
            State& s = st();
            const LLSD& caps = step["caps"];
            s.has_region = !step["no_region"].asBoolean();
            s.inv_cap = caps.has("inv") ? caps["inv"].asString() : std::string();
            s.lib_cap = caps.has("lib") ? caps["lib"].asString() : std::string();
            s.pending = step["pending"].asInteger();
            s.next_result = result;
            const LLSD& args = step["args"];
            const std::string name = step["call"].asString();
            AISAPI::completion_t cb = recording_callback(step.has("callback") ? step["callback"].asBoolean() : true);
            if (name == "CreateInventory")
                AISAPI::CreateInventory(args["parent_id"].asUUID(), args["new_inventory"], cb);
            else if (name == "SlamFolder")
                AISAPI::SlamFolder(args["folder_id"].asUUID(), args["new_inventory"], cb);
            else if (name == "RemoveCategory")
                AISAPI::RemoveCategory(args["category_id"].asUUID(), cb);
            else if (name == "RemoveItem")
                AISAPI::RemoveItem(args["item_id"].asUUID(), cb);
            else if (name == "PurgeDescendents")
                AISAPI::PurgeDescendents(args["category_id"].asUUID(), cb);
            else if (name == "UpdateCategory")
                AISAPI::UpdateCategory(args["category_id"].asUUID(), args["updates"], cb);
            else if (name == "UpdateItem")
                AISAPI::UpdateItem(args["item_id"].asUUID(), args["updates"], cb);
            else if (name == "FetchItem")
                AISAPI::FetchItem(args["item_id"].asUUID(), item_type(args), cb);
            else if (name == "FetchCategoryChildren")
                AISAPI::FetchCategoryChildren(args["category_id"].asUUID(), item_type(args), args["recursive"].asBoolean(),
                                              cb, args["depth"].asInteger());
            else if (name == "FetchCategoryChildrenByName")
                AISAPI::FetchCategoryChildren(args["identifier"].asString(), args["recursive"].asBoolean(), cb,
                                              args["depth"].asInteger());
            else if (name == "FetchCategoryCategories")
                AISAPI::FetchCategoryCategories(args["category_id"].asUUID(), item_type(args),
                                                args["recursive"].asBoolean(), cb, args["depth"].asInteger());
            else if (name == "FetchCategorySubset")
            {
                uuid_vec_t children;
                for (LLSD::array_const_iterator it = args["children"].beginArray(); it != args["children"].endArray(); ++it)
                {
                    children.push_back(it->asUUID());
                }
                AISAPI::FetchCategorySubset(args["category_id"].asUUID(), children, item_type(args),
                                            args["recursive"].asBoolean(), cb, args["depth"].asInteger());
            }
            else if (name == "FetchCOF")
                AISAPI::FetchCOF(cb);
            else if (name == "FetchCategoryLinks")
                AISAPI::FetchCategoryLinks(args["category_id"].asUUID(), cb);
            else if (name == "FetchOrphans")
                AISAPI::FetchOrphans(cb);
            else if (name == "CopyLibraryCategory")
                AISAPI::CopyLibraryCategory(args["source_id"].asUUID(), args["dest_id"].asUUID(),
                                            args["copy_subfolders"].asBoolean(), cb);
            else if (name == "isAvailable")
            {
                LLSD op = LLSD::emptyMap();
                op["op"] = "isAvailable";
                op["result"] = AISAPI::isAvailable();
                record(op);
            }
            else if (name == "getCapNames")
            {
                LLSD names = LLSD::emptyArray();
                AISAPI::getCapNames(names);
                LLSD op = LLSD::emptyMap();
                op["op"] = "getCapNames";
                op["result"] = names;
                record(op);
            }
            else
            {
                throw std::logic_error("unknown AISAPI call " + name);
            }
            record_postponed();
            if (step["idle"].asBoolean())
            {
                // The main loop's idle callbacks run (AISAPI::onIdle drains
                // the postponed commands while the pool has room).
                s.pending = step["idle_pending"].asInteger();
                LLSD op = LLSD::emptyMap();
                op["op"] = "idle";
                record(op, false);
                gIdleCallbacks.callFunctions();
                record_postponed();
            }
        }

        void run_actions(const LLSD& hook, const char* where, S32 index);
    }

    void run_step(const LLSD& step, const LLSD& result)
    {
        State& s = st();
        if (s.nest == 0)
        {
            s.hooks = step.has("hooks") ? step["hooks"] : LLSD::emptyArray();
            s.in_apply = false;
            s.parse_checkpoints = 0;
            s.apply_checkpoints = 0;
            s.recheck_pending = false;
            s.force_expiry = false;
            s.step_yields = 0;
            s.stop_armed = false;
        }
        try
        {
            if (step.has("call"))
            {
                call_command(step, result);
            }
            else
            {
                invoke_result(command_from_name(step["type"].asString()), step["url"].asString(),
                              step["target_id"].asUUID(), step["body"], result,
                              step.has("callback") ? step["callback"].asBoolean() : true);
            }
        }
        catch (const LLCoros::Stop& e)
        {
            // What the coroutine's top level does with it: the update ends
            // where it is. The contract (checked by the mandatory check): no
            // callback and no model call after the throw, the model as it
            // was at the throw.
            LLSD op = LLSD::emptyMap();
            op["op"] = "abort";
            op["what"] = std::string(e.what());
            op["ops_after_stop"] = (LLSD::Integer)(s.ops.size() - s.stop_op_index);
            std::ostringstream now;
            LLSDSerialize::toBinary(skeleton_dump(), now);
            op["model_unchanged"] = now.str() == s.stop_dump;
            record(op, false);
        }
        if (s.nest == 0)
        {
            s.hooks = LLSD::emptyArray();
            s.in_apply = false;
            s.stop_armed = false;
            s.force_expiry = false;
            s.recheck_pending = false;
        }
    }

    void set_case(const std::string& id, TimerMode mode)
    {
        st().case_id = id;
        st().run_mode = timer_mode_name(mode);
    }

    std::vector<std::string> tid_failures()
    {
        return st().tid_failures;
    }

    namespace
    {
        void fire_hooks(const char* where, S32 index)
        {
            State& s = st();
            if (s.nest != 0)
            {
                return;
            }
            for (LLSD::array_const_iterator it = s.hooks.beginArray(); it != s.hooks.endArray(); ++it)
            {
                if (it->has(where) && (*it)[where].asInteger() == index)
                {
                    run_actions(*it, where, index);
                }
            }
        }

        // Another piece of main-thread code changes the model, or another
        // AIS reply is processed, while this update is suspended.
        void run_actions(const LLSD& hook, const char* where, S32 index)
        {
            State& s = st();
            LLSD marker = LLSD::emptyMap();
            marker["op"] = "hook";
            marker["at"] = where;
            marker["index"] = index;
            record(marker, false);
            const LLSD& actions = hook["actions"];
            for (LLSD::array_const_iterator it = actions.beginArray(); it != actions.endArray(); ++it)
            {
                const LLSD& a = *it;
                if (a.has("set_category"))
                {
                    Internal guard;
                    const LLSD& m = a["set_category"];
                    LLViewerInventoryCategory* cat = store_cat(m["category_id"].asUUID());
                    if (cat)
                    {
                        if (m.has("version")) cat->setVersion(m["version"].asInteger());
                        if (m.has("descendents")) cat->setDescendentCount(m["descendents"].asInteger());
                        if (m.has("name")) cat->rename(m["name"].asString());
                        if (m.has("parent_id")) cat->setParent(m["parent_id"].asUUID());
                    }
                }
                else if (a.has("set_item"))
                {
                    Internal guard;
                    const LLSD& m = a["set_item"];
                    LLViewerInventoryItem* item = store_item(m["item_id"].asUUID());
                    if (item)
                    {
                        if (m.has("name")) item->rename(m["name"].asString());
                        if (m.has("desc")) item->setDescription(m["desc"].asString());
                        if (m.has("parent_id")) item->setParent(m["parent_id"].asUUID());
                        if (m.has("flags")) item->setFlags((U32)m["flags"].asInteger());
                        if (m.has("transaction_id")) item->setTransactionID(to_txn(m["transaction_id"].asUUID()));
                    }
                }
                else if (a.has("delete"))
                {
                    Internal guard;
                    const LLUUID id = a["delete"].asUUID();
                    if (store_cat(id))
                    {
                        remove_descendents(id);
                    }
                    s.cats.erase(id);
                    s.items.erase(id);
                }
                else if (a.has("run_step"))
                {
                    ++s.nest;
                    run_step(a["run_step"], a["result"]);
                    --s.nest;
                }
                else if (a.has("construct_update"))
                {
                    // Another update constructed now (its constructor's
                    // batch-timer logic runs), processed later.
                    AISUpdate other(LLSD::emptyMap(), (AISAPI::COMMAND_TYPE)command_from_name(a["construct_update"].asString()),
                                    LLSD());
                }
                else if (a.has("stop"))
                {
                    // At a checkpoint: this checkTimeout() yields (in both
                    // runs) and LLCoros::checkStop() after the yield throws.
                    // After a yield: the checkStop() that follows throws.
                    s.stop_armed = true;
                    if (std::string(where) != "yield")
                    {
                        s.force_expiry = true;
                    }
                }
                else if (a.has("advance_us"))
                {
                    // the other code ran this long (the sched run's clock)
                    if (s.mode == TIMERS_SCHEDULED)
                    {
                        s.clock += (U64)a["advance_us"].asInteger();
                    }
                }
            }
            LLSD end = LLSD::emptyMap();
            end["op"] = "hook_end";
            record(end, false);
        }
    }

    namespace
    {
        const char* const COMMAND_NAMES[] = {
            "COPYINVENTORY", "SLAMFOLDER", "REMOVECATEGORY", "REMOVEITEM", "PURGEDESCENDENTS",
            "UPDATECATEGORY", "UPDATEITEM", "COPYLIBRARYCATEGORY", "CREATEINVENTORY", "FETCHITEM",
            "FETCHCATEGORYCHILDREN", "FETCHCATEGORYCATEGORIES", "FETCHCATEGORYSUBSET", "FETCHCOF",
            "FETCHORPHANS", "FETCHCATEGORYLINKS"};
    }

    const char* command_name(int type)
    {
        int i = (int)type;
        return (i >= 0 && i < (int)LL_ARRAY_SIZE(COMMAND_NAMES)) ? COMMAND_NAMES[i] : "?";
    }

    int command_from_name(const std::string& name)
    {
        for (int i = 0; i < (int)LL_ARRAY_SIZE(COMMAND_NAMES); ++i)
        {
            if (name == COMMAND_NAMES[i])
            {
                return i;
            }
        }
        return -1;
    }

    bool message_system_init(const std::string& template_file)
    {
        if (gMessageSystem)
        {
            return true;
        }
        ll_init_apr();
        gMessageSystem = new LLMessageSystem(template_file, NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, true, 5.f, 100.f);
        return gMessageSystem->isOK();
    }

    [[noreturn]] void unreached(const char* what)
    {
        throw std::logic_error(std::string("not reached by the AISUpdate harness: ") + what);
    }
}

using vks_ais::record;

//============================================================================
// Globals llaisapi.cpp and the copies refer to
//============================================================================
LLUUID gAgentID;
LLUUID gAgentSessionID;
// gDisconnected is defined in llcommon (llapp.cpp) since upstream #5241 (147e864246).
LLControlGroup gSavedSettings("Global");
LLPointer<LLVOAvatarSelf> gAgentAvatarp = NULL;

void dump_sequential_xml(const std::string outprefix, const LLSD& content)
{
}

// The agent: only the members below are called; the object itself is raw
// storage that is never constructed (as in lltexturefetch_parity_stubs.cpp).
#if LL_WINDOWS
extern "C" unsigned char vks_r3a_gAgentStorage[1 << 16];
extern "C" unsigned char vks_r3a_gAgentStorage[1 << 16] = { 0 };
#pragma comment(linker, "/alternatename:?gAgent@@3VLLAgent@@A=vks_r3a_gAgentStorage")
#else
alignas(64) unsigned char vks_r3a_gAgentStorage[1 << 16] __asm__("gAgent") = { 0 };
#endif

LLViewerRegion* LLAgent::getRegion() const
{
    return NULL;
}

// Records the message the frozen updateParentOnServer() built, encoded by
// the real template builder (message number and blocks, before the packet
// header and zero-coding the circuit adds), then discards it.
void LLAgent::sendReliableMessage()
{
    LLMessageBuilder* builder = gMessageSystem->*steal(BuilderTag());
    std::vector<U8> buf(MAX_BUFFER_SIZE);
    U32 size = builder->buildMessage(&buf[0], MAX_BUFFER_SIZE, 0);
    buf.resize(size);
    LLSD op = LLSD::emptyMap();
    op["op"] = "msg.send";
    op["reliable"] = true;
    op["name"] = std::string(builder->getMessageName() ? builder->getMessageName() : "");
    op["body"] = LLSD::Binary(buf.begin(), buf.end());
    record(op);
    gMessageSystem->clearMessage();
}

bool LLViewerRegion::isCapabilityAvailable(std::string_view name) const
{
    return false;
}

LLNotificationPtr LLNotificationsUtil::add(const std::string& name)
{
    LLSD op = LLSD::emptyMap();
    op["op"] = "notify";
    op["name"] = name;
    record(op);
    return LLNotificationPtr();
}

LLGridManager::LLGridManager()
{
}

LLGridManager::~LLGridManager()
{
}

// Second Life main grid (only AISAPI::isAvailable() asks; the harness never
// calls it).
bool LLGridManager::isInProductionGrid() const
{
    return true;
}

bool LLGridManager::isSystemGrid(const std::string& grid) const
{
    return true;
}

//============================================================================
// What llaisapi.cpp calls under the renames
//============================================================================
namespace
{
    U64 fake_clock()
    {
        return vks_ais::st().clock;
    }

    vks_r3a_Region g_region;
}

void vks_r3a_LLTimer::reset()
{
    mLastClockCount = fake_clock();
    mExpirationTicks = 0;
}

void vks_r3a_LLTimer::setTimerExpirySec(F32SecondsImplicit expiration)
{
    mExpirationTicks = fake_clock() + (U64)((F32)((F64)expiration.value() * 1000000.0));
}

bool vks_r3a_LLTimer::hasExpired() const
{
    vks_ais::State& s = vks_ais::st();
    if (this != steal(BatchTimerTag()) && s.nest == 0)
    {
        // checkTimeout() tests the task timer first. A test right after a
        // yield is its while loop's recheck (bug 51), not a new checkpoint.
        if (s.recheck_pending)
        {
            s.recheck_pending = false;
        }
        else if (s.in_apply)
        {
            ++s.apply_checkpoints;
            vks_ais::fire_hooks("apply_checkpoint", s.apply_checkpoints);
            if (s.force_expiry)
            {
                s.force_expiry = false;
                return true;
            }
        }
        else
        {
            ++s.parse_checkpoints;
            if (s.mode == vks_ais::TIMERS_SCHEDULED)
            {
                // Parse-phase hooks belong to the relaxable sched check.
                vks_ais::fire_hooks("parse_checkpoint", s.parse_checkpoints);
                if (s.force_expiry)
                {
                    s.force_expiry = false;
                    return true;
                }
            }
        }
    }
    if (s.mode == vks_ais::TIMERS_NEVER)
    {
        return false;
    }
    return fake_clock() >= mExpirationTicks;
}

F32SecondsImplicit vks_r3a_LLTimer::getElapsedTimeF32() const
{
    return (F32)((F64)(fake_clock() - mLastClockCount) / 1000000.0);
}

// llcoro::suspendUntilNextFrame() as llaisapi.cpp calls it: the AIS
// coroutine yields to the next frame. Recorded as a "yield" event; the frame
// counter and the clock move on, then what the schedule says happened in
// the new frame before this update resumes.
void llcoro::vks_r3a_suspendUntilNextFrame()
{
    LLSD op = LLSD::emptyMap();
    op["op"] = "yield";
    op["frame"] = (LLSD::Integer)vks_ais::rel_frame();
    op["clock_us"] = (LLSD::Integer)(S32)vks_ais::rel_clock();
    vks_ais::State& s = vks_ais::st();
    size_t k = s.yields;
    U64 other = s.others_us.empty() ? 0 : s.others_us[k % s.others_us.size()];
    U64 extra = k < s.yield_extra_us.size() ? s.yield_extra_us[k] : 0;
    ++s.yields;
    op["others_us"] = (LLSD::Integer)(S32)other;
    if (extra)
    {
        op["extra_us"] = (LLSD::Integer)(S32)extra;
    }
    record(op, false);
    FrameCount::set(FrameCount::get() + 1);
    s.clock += s.frame_us;
    if (other > 0)
    {
        // Another AIS update resumed first in this frame: as its
        // checkTimeout() does, it re-armed the shared batch timer with
        // AISUpdate::AIS_BATCH_EXPIRY_SECONDS (read from the class, see
        // begin_run) and took the frame, then ran for 'other'.
        steal(BatchTimerTag())->setTimerExpirySec(s.batch_expiry);
        *steal(BatchFrameTag()) = (U32)FrameCount::get();
        s.clock += other;
    }
    s.clock += extra;
    if (s.nest == 0)
    {
        s.recheck_pending = true;
        ++s.step_yields;
        if (s.mode == vks_ais::TIMERS_SCHEDULED)
        {
            vks_ais::fire_hooks("yield", s.step_yields);
        }
    }
}

vks_r3a_apply_tag::operator fn_t() const
{
    if (vks_ais::st().nest == 0)
    {
        vks_ais::st().in_apply = true;
    }
    return NULL;
}

void vks_r3a_LLCoros::checkStop()
{
    vks_ais::State& s = vks_ais::st();
    if (s.stop_armed && s.nest == 0)
    {
        s.stop_armed = false;
        LLSD op = LLSD::emptyMap();
        op["op"] = "checkStop";
        op["throws"] = "LLCoros::Stopping";
        record(op, false);
        std::ostringstream dump;
        LLSDSerialize::toBinary(vks_ais::skeleton_dump(), dump);
        s.stop_dump = dump.str();
        s.stop_op_index = s.ops.size();
        throw LLCoros::Stopping("viewer is stopping (scripted)");
    }
}

namespace
{
    // One request the adapter would send. The tid of a write is checked
    // (a well-formed, non-null UUID never seen before in the process) and
    // replaced by <tid>.
    LLSD http_call(const char* method, const std::string& url, const LLSD* body, const std::string* destination,
                   LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
    {
        vks_ais::State& s = vks_ais::st();
        LLSD op = LLSD::emptyMap();
        op["op"] = "http";
        op["method"] = method;
        std::string u = url;
        size_t p = u.find("?tid=");
        if (p != std::string::npos)
        {
            std::string tid = u.substr(p + 5, 36);
            bool ok = tid.size() == 36 && LLUUID::validate(tid) && LLUUID(tid).notNull() && s.tids.insert(tid).second;
            op["tid_ok"] = ok;
            if (!ok)
            {
                s.tid_failures.push_back("case " + s.case_id + " " + s.run_mode + " op " + std::to_string(s.ops.size())
                                         + " (http " + method + "): tid '" + tid + "' is malformed, null or reused");
                op["tid"] = tid;
            }
            u.replace(p + 5, tid.size(), "<tid>");
        }
        op["url"] = u;
        if (body)
        {
            op["body"] = *body;
        }
        if (destination)
        {
            op["destination"] = *destination;
        }
        op["headers"] = headers ? "set" : "none";
        op["timeout"] = options ? (LLSD::Integer)options->getTimeout() : (LLSD::Integer)-1;
        record(op);
        LLSD r = s.next_result;
        if (!r.isMap() || !r.has("http_result"))
        {
            r = LLSD::emptyMap();
            LLSD h = LLSD::emptyMap();
            h["success"] = true;
            h["type"] = 200;
            h["status"] = 0;
            h["message"] = "";
            h["url"] = u;
            h["headers"] = LLSD::emptyMap();
            r["http_result"] = h;
        }
        return r;
    }
}

LLSD vks_r3a_http::HttpCoroutineAdapter::postAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body,
                                                       LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
{
    return http_call("POST", url, &body, NULL, options, headers);
}

LLSD vks_r3a_http::HttpCoroutineAdapter::putAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body,
                                                      LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
{
    return http_call("PUT", url, &body, NULL, options, headers);
}

LLSD vks_r3a_http::HttpCoroutineAdapter::patchAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body,
                                                        LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
{
    return http_call("PATCH", url, &body, NULL, options, headers);
}

LLSD vks_r3a_http::HttpCoroutineAdapter::getAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url,
                                                      LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
{
    return http_call("GET", url, NULL, NULL, options, headers);
}

LLSD vks_r3a_http::HttpCoroutineAdapter::deleteAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url,
                                                         LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
{
    return http_call("DELETE", url, NULL, NULL, options, headers);
}

LLSD vks_r3a_http::HttpCoroutineAdapter::copyAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const std::string destination,
                                                       LLCore::HttpOptions::ptr_t options, LLCore::HttpHeaders::ptr_t headers)
{
    return http_call("COPY", url, NULL, &destination, options, headers);
}

vks_r3a_LLCoprocedureManager& vks_r3a_LLCoprocedureManager::instance()
{
    static vks_r3a_LLCoprocedureManager manager;
    return manager;
}

size_t vks_r3a_LLCoprocedureManager::countPending(const std::string& pool) const
{
    LLSD op = LLSD::emptyMap();
    op["op"] = "q.countPending";
    op["pool"] = pool;
    op["result"] = (LLSD::Integer)vks_ais::st().pending;
    record(op);
    return (size_t)vks_ais::st().pending;
}

// The real manager queues the procedure for a coroutine of the pool; here it
// runs at once, on a recording adapter.
LLUUID vks_r3a_LLCoprocedureManager::enqueueCoprocedure(const std::string& pool, const std::string& name, CoProcedure_t proc)
{
    LLSD op = LLSD::emptyMap();
    op["op"] = "enqueue";
    op["pool"] = pool;
    op["name"] = name;
    record(op);
    vks_r3a_http::HttpCoroutineAdapter::ptr_t adapter = std::make_shared<vks_r3a_http::HttpCoroutineAdapter>();
    proc(adapter, LLUUID::null);
    return LLUUID::null;
}

vks_r3a_Region* vks_r3a_Agent::getRegion() const
{
    return vks_ais::st().has_region ? &g_region : NULL;
}

bool vks_r3a_Region::isCapabilityAvailable(const std::string& name) const
{
    return !getCapability(name).empty();
}

std::string vks_r3a_Region::getCapability(const std::string& name) const
{
    const vks_ais::State& s = vks_ais::st();
    std::string url = name == "InventoryAPIv3" ? s.inv_cap : (name == "LibraryAPIv3" ? s.lib_cap : std::string());
    LLSD op = LLSD::emptyMap();
    op["op"] = "q.getCapability";
    op["name"] = name;
    op["result"] = url;
    record(op);
    return url;
}

//============================================================================
// LLInventoryModel: recording stub over the skeleton
//============================================================================
LLInventoryModel gInventory;

LLInventoryModel::LLInventoryModel()
:   mBacklinkMMap(),
    mIsAgentInvUsable(true),
    mRootFolderID(),
    mLibraryRootFolderID(),
    mLibraryOwnerID(),
    mCategoryMap(),
    mItemMap(),
    mParentChildCategoryTree(),
    mParentChildItemTree(),
    mLastItem(NULL),
    mIsNotifyObservers(false),
    mModifyMask(LLInventoryObserver::ALL),
    mChangedItemIDs(),
    mModifyMaskBacklog(LLInventoryObserver::NONE),
    mObservers(),
    mHttpRequestFG(NULL),
    mHttpRequestBG(NULL),
    mHttpOptions(),
    mHttpHeaders(),
    mHttpPolicyClass(LLCore::HttpRequest::DEFAULT_POLICY_ID),
    mCategoryLock(),
    mItemLock()
{
}

LLInventoryModel::~LLInventoryModel()
{
}

const LLUUID& LLInventoryModel::getRootFolderID() const
{
    return vks_ais::st().root;
}

LLViewerInventoryItem* LLInventoryModel::getItem(const LLUUID& id) const
{
    LLViewerInventoryItem* item = vks_ais::store_item(id);
    if (vks_ais::recording())
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "q.getItem";
        op["id"] = id;
        op["found"] = item != NULL;
        if (item)
        {
            op["obj"] = vks_ais::item_fields(item);
        }
        record(op);
    }
    return item;
}

LLViewerInventoryCategory* LLInventoryModel::getCategory(const LLUUID& id) const
{
    LLViewerInventoryCategory* cat = vks_ais::store_cat(id);
    if (vks_ais::recording())
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "q.getCategory";
        op["id"] = id;
        op["found"] = cat != NULL;
        if (cat)
        {
            op["obj"] = vks_ais::category_fields(cat);
        }
        record(op);
    }
    return cat;
}

// The real lookup (findCategoryUUIDForTypeInRoot): the root itself for
// FT_ROOT_INVENTORY, else the smallest id among the root's direct children
// of that preferred type, else null.
const LLUUID LLInventoryModel::findCategoryUUIDForType(LLFolderType::EType preferred_type) const
{
    LLUUID rv;
    const LLUUID& root = vks_ais::st().root;
    if (preferred_type == LLFolderType::FT_ROOT_INVENTORY)
    {
        rv = root;
    }
    else if (root.notNull())
    {
        for (const auto& c : vks_ais::st().cats)
        {
            if (c.second->getParentUUID() == root && c.second->getPreferredType() == preferred_type)
            {
                if (rv.isNull() || c.first < rv)
                {
                    rv = c.first;
                }
            }
        }
    }
    if (vks_ais::recording())
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "q.findCategoryUUIDForType";
        op["type"] = (LLSD::Integer)preferred_type;
        op["result"] = rv;
        record(op);
    }
    return rv;
}

// The changed-id bookkeeping of the real addChangedMask (outside
// notifyObservers): the referent joins the changed set; with LABEL, the
// links that point at it join too (addChangedMaskForLinks). The
// marketplace hook is not emulated.
void LLInventoryModel::addChangedMask(U32 mask, const LLUUID& referent)
{
    mModifyMask |= mask;
    if (referent.isNull() || mChangedItemIDs.find(referent) != mChangedItemIDs.end())
    {
        return;
    }
    mChangedItemIDs.insert(referent);
    if (mask & LLInventoryObserver::ADD)
    {
        mAddedItemIDs.insert(referent);
    }
    if (mask & LLInventoryObserver::LABEL)
    {
        LLViewerInventoryItem* target_item = vks_ais::store_item(referent);
        bool exists = target_item || vks_ais::store_cat(referent);
        if (exists && !(target_item && target_item->getIsLinkType()))
        {
            for (const auto& i : vks_ais::st().items)
            {
                if (i.second->getIsLinkType() && i.second->LLInventoryItem::getAssetUUID() == referent)
                {
                    addChangedMask(LLInventoryObserver::LABEL, i.first);
                }
            }
        }
    }
}

void LLInventoryModel::notifyObservers(const LLUUID& transaction_id)
{
    if (vks_ais::recording())
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "notifyObservers";
        op["changed"] = (LLSD::Integer)mChangedItemIDs.size();
        record(op);
    }
    mModifyMask = mModifyMaskBacklog;
    mChangedItemIDs.clear();
    mAddedItemIDs.clear();
    mModifyMaskBacklog = LLInventoryObserver::NONE;
}

void LLInventoryModel::updateCategory(const LLViewerInventoryCategory* cat, U32 mask)
{
    LLSD op = LLSD::emptyMap();
    bool record_it = vks_ais::recording();
    if (record_it)
    {
        op["op"] = "updateCategory";
        op["mask"] = (LLSD::Integer)mask;
        op["obj"] = vks_ais::category_fields(cat);
        op["exists"] = cat && vks_ais::store_cat(cat->getUUID()) != NULL;
        op["parent_known"] = cat && vks_ais::store_cat(cat->getParentUUID()) != NULL;
        record(op);
    }
    vks_ais::Internal guard;
    if (!cat || cat->getUUID().isNull())
    {
        return;
    }
    LLViewerInventoryCategory* old_cat = vks_ais::store_cat(cat->getUUID());
    if (old_cat)
    {
        if (old_cat->getParentUUID() != cat->getParentUUID())
        {
            mask |= LLInventoryObserver::STRUCTURE;
            mask |= LLInventoryObserver::INTERNAL;
        }
        if (old_cat->LLInventoryObject::getName() != cat->LLInventoryObject::getName())
        {
            mask |= LLInventoryObserver::LABEL;
        }
        old_cat->copyViewerCategory(cat);
        addChangedMask(mask, cat->getUUID());
    }
    else
    {
        LLPointer<LLViewerInventoryCategory> new_cat = new LLViewerInventoryCategory(cat->getOwnerID());
        new_cat->copyViewerCategory(cat);
        vks_ais::st().cats[new_cat->getUUID()] = new_cat;
        mask |= LLInventoryObserver::ADD;
        addChangedMask(mask, cat->getUUID());
    }
}

U32 LLInventoryModel::updateItem(const LLViewerInventoryItem* item, U32 mask)
{
    bool record_it = vks_ais::recording();
    size_t op_index = vks_ais::st().ops.size();
    if (record_it)
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "updateItem";
        op["mask"] = (LLSD::Integer)mask;
        op["obj"] = vks_ais::item_fields(item);
        op["exists"] = item && vks_ais::store_item(item->getUUID()) != NULL;
        op["parent_known"] = item && vks_ais::store_cat(item->getParentUUID()) != NULL;
        record(op);
    }
    vks_ais::Internal guard;
    // What the real updateItem() would do with it, for the reader: the
    // emulation below keeps the skeleton in step; the real model's own
    // server moves are noted here, not performed.
    std::string outcome;
    if (item->getUUID().isNull())
    {
        outcome = "ignored_null_id";
    }
    else
    {
        LLAssetType::EType type = item->getType();
        if (type == LLAssetType::AT_MESH || type == LLAssetType::AT_GLTF || type == LLAssetType::AT_GLTF_BIN)
        {
            outcome = "skipped_type";
        }
        else
        {
            LLViewerInventoryItem* old_item = vks_ais::store_item(item->getUUID());
            if (old_item)
            {
                LLUUID old_parent_id = old_item->getParentUUID();
                LLUUID new_parent_id = item->getParentUUID();
                bool update_parent_on_server = false;
                outcome = "updated";
                if (new_parent_id.isNull())
                {
                    if (old_parent_id.isNull())
                    {
                        new_parent_id = findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);
                        outcome = "updated_null_parent_to_lost_and_found_server_move";
                    }
                    else
                    {
                        new_parent_id = old_parent_id;
                        outcome = "updated_null_parent_kept_old_parent_server_move";
                    }
                    update_parent_on_server = true;
                }
                if (update_parent_on_server && old_parent_id != new_parent_id && vks_ais::store_cat(new_parent_id))
                {
                    // the real model accounts the destination (+1, version)
                    LLInventoryModel::LLCategoryUpdate update(new_parent_id, 1);
                    gInventory.accountForUpdate(update);
                }
                if (old_parent_id != new_parent_id)
                {
                    mask |= LLInventoryObserver::STRUCTURE;
                }
                if (old_item->LLInventoryObject::getName() != item->LLInventoryObject::getName())
                {
                    mask |= LLInventoryObserver::LABEL;
                }
                if (old_item->LLInventoryItem::getPermissions() != item->LLInventoryItem::getPermissions())
                {
                    mask |= LLInventoryObserver::INTERNAL;
                }
                old_item->copyViewerItem(item);
                if (update_parent_on_server)
                {
                    old_item->setParent(new_parent_id);
                }
                mask |= LLInventoryObserver::INTERNAL;
            }
            else if (type <= LLAssetType::AT_NONE)
            {
                // addItem() refuses an item of no type and updateItem()
                // returns the mask as it came: nothing listed, accounted
                // or marked changed (bug 23).
                outcome = "refused_bad_type";
            }
            else
            {
                LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
                outcome = "added";
                if (item->getParentUUID().isNull())
                {
                    const LLUUID category_id = findCategoryUUIDForType(LLFolderType::assetTypeToFolderType(new_item->getType()));
                    new_item->setParent(category_id);
                    outcome = vks_ais::store_cat(category_id) ? "added_null_parent_to_default_folder_server_move"
                                                              : "added_null_parent_no_default_folder";
                    if (vks_ais::store_cat(category_id))
                    {
                        LLInventoryModel::LLCategoryUpdate update(category_id, 1);
                        gInventory.accountForUpdate(update);
                    }
                }
                else if (item->getParentUUID() == CATEGORIZE_LOST_AND_FOUND_ID)
                {
                    LLUUID parent_id = findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);
                    new_item->setParent(parent_id);
                    LLInventoryModel::LLCategoryUpdate update(parent_id, 1);
                    gInventory.accountForUpdate(update);
                    outcome = "added_categorize_lost_and_found";
                    if (!vks_ais::store_cat(parent_id))
                    {
                        outcome = "added_categorize_lost_and_found_no_lost_and_found";
                    }
                }
                else if (!vks_ais::store_cat(item->getParentUUID()))
                {
                    LLUUID parent_id = findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);
                    new_item->setParent(parent_id);
                    outcome = vks_ais::store_cat(parent_id) ? "added_unknown_parent_to_lost_and_found_server_move"
                                                            : "added_unknown_parent_no_lost_and_found";
                    if (vks_ais::store_cat(parent_id))
                    {
                        LLInventoryModel::LLCategoryUpdate update(parent_id, 1);
                        gInventory.accountForUpdate(update);
                    }
                }
                vks_ais::st().items[new_item->getUUID()] = new_item;
                mask |= LLInventoryObserver::ADD;
            }
            if (outcome != "refused_bad_type")
            {
                if (type == LLAssetType::AT_CALLINGCARD)
                {
                    mask |= LLInventoryObserver::CALLING_CARD;
                }
                else if (type == LLAssetType::AT_GESTURE)
                {
                    mask |= LLInventoryObserver::GESTURE;
                }
                addChangedMask(mask, item->getUUID());
            }
        }
    }
    if (record_it)
    {
        vks_ais::st().ops[(LLSD::Integer)op_index]["model"] = outcome;
    }
    return mask;
}

void LLInventoryModel::onObjectDeletedFromServer(const LLUUID& object_id, bool fix_broken_links,
                                                 bool update_parent_version, bool do_notify_observers)
{
    bool record_it = vks_ais::recording();
    LLViewerInventoryCategory* cat = vks_ais::store_cat(object_id);
    LLViewerInventoryItem* item = vks_ais::store_item(object_id);
    if (record_it)
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "onObjectDeletedFromServer";
        op["id"] = object_id;
        op["fix_broken_links"] = fix_broken_links;
        op["update_parent_version"] = update_parent_version;
        op["do_notify_observers"] = do_notify_observers;
        op["exists"] = cat ? "category" : (item ? "item" : "none");
        record(op);
    }
    vks_ais::Internal guard;
    if (!cat && !item)
    {
        return;
    }
    LLUUID parent_id = cat ? cat->getParentUUID() : item->getParentUUID();
    if (cat)
    {
        vks_ais::remove_descendents(object_id);
    }
    if (update_parent_version)
    {
        LLInventoryModel::LLCategoryUpdate up(parent_id, -1);
        accountForUpdate(up);
    }
    // deleteObject(): changed mask, notify, erase, changed mask again,
    // notify when asked.
    addChangedMask(LLInventoryObserver::REMOVE, object_id);
    notifyObservers();
    vks_ais::st().cats.erase(object_id);
    vks_ais::st().items.erase(object_id);
    addChangedMask(LLInventoryObserver::REMOVE, object_id);
    if (do_notify_observers)
    {
        notifyObservers();
    }
}

bool LLInventoryModel::fetchDescendentsOf(const LLUUID& folder_id) const
{
    bool record_it = vks_ais::recording();
    size_t op_index = vks_ais::st().ops.size();
    if (record_it)
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "fetchDescendentsOf";
        op["id"] = folder_id;
        record(op);
    }
    vks_ais::Internal guard;
    bool rv = false;
    if (folder_id.notNull())
    {
        LLViewerInventoryCategory* cat = vks_ais::store_cat(folder_id);
        if (cat)
        {
            rv = cat->fetch();
        }
    }
    if (record_it)
    {
        vks_ais::st().ops[(LLSD::Integer)op_index]["would_start"] = rv;
    }
    return rv;
}

// ---- VKS-COPY-BEGIN llinventorymodel.cpp 2780-2822 blob 1e810ec346b8d68ad57c7d82192ea0e92701a550 : LLInventoryModel::accountForUpdate(const LLCategoryUpdate&)
void LLInventoryModel::accountForUpdate(const LLCategoryUpdate& update) const
{
    bool vks_record_it = vks_ais::recording(); size_t vks_op_index = vks_ais::st().ops.size(); // VKS-HARNESS
    if (vks_record_it) { LLSD op = LLSD::emptyMap(); op["op"] = "accountForUpdate"; op["category"] = update.mCategoryID; op["delta"] = (LLSD::Integer)update.mDescendentDelta; op["change_version"] = update.mChangeVersion; record(op); } // VKS-HARNESS
    vks_ais::Internal vks_guard; // VKS-HARNESS
    LLViewerInventoryCategory* cat = getCategory(update.mCategoryID);
    if(cat)
    {
        S32 version = cat->getVersion();
        if(version != LLViewerInventoryCategory::VERSION_UNKNOWN)
        {
            S32 descendents_server = cat->getDescendentCount();
            S32 descendents_actual = cat->getViewerDescendentCount();
            if(descendents_server == descendents_actual)
            {
                descendents_actual += update.mDescendentDelta;
                cat->setDescendentCount(descendents_actual);
                if (update.mChangeVersion)
                {
                cat->setVersion(++version);
                }
                LL_DEBUGS(LOG_INV) << "accounted: '" << cat->getName() << "' "
                                   << version << " with " << descendents_actual
                                   << " descendents." << LL_ENDL;
            }
            else
            {
                // Error condition, this means that the category did not register that
                // it got new descendents (perhaps because it is still being loaded)
                // which means its descendent count will be wrong.
                LL_WARNS(LOG_INV) << "Accounting failed for '" << cat->getName() << "' version:"
                                  << version << " due to mismatched descendent count:  server == "
                                  << descendents_server << ", viewer == " << descendents_actual << LL_ENDL;
            }
        }
        else
        {
            LL_WARNS(LOG_INV) << "Accounting failed for '" << cat->getName() << "' version: unknown ("
                              << version << ")" << LL_ENDL;
        }
    }
    else
    {
        LL_WARNS(LOG_INV) << "No category found for update " << update.mCategoryID << LL_ENDL;
    }
    if (vks_record_it) { LLSD m = LLSD::emptyMap(); m["found"] = cat != NULL; if (cat) { m["version"] = (LLSD::Integer)(cat->*steal(CatVersionTag())); m["descendents"] = (LLSD::Integer)cat->getDescendentCount(); } vks_ais::st().ops[(LLSD::Integer)vks_op_index]["model"] = m; } // VKS-HARNESS
}
// ---- VKS-COPY-END

//============================================================================
// LLViewerInventoryItem
//============================================================================

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 107-133 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : class LLLocalizedInventoryItemsDictionary
class LLLocalizedInventoryItemsDictionary : public LLSingleton<LLLocalizedInventoryItemsDictionary>
{
    LLSINGLETON(LLLocalizedInventoryItemsDictionary);
public:
    std::map<std::string, std::string> mInventoryItemsDict;

    /**
     * Finds passed name in dictionary and replaces it with found localized value.
     *
     * @param object_name - string to be localized.
     * @return true if passed name was found and localized, false otherwise.
     */
    bool localizeInventoryObjectName(std::string& object_name)
    {
        LL_DEBUGS(LOG_LOCAL) << "Searching for localization: " << object_name << LL_ENDL;

        std::map<std::string, std::string>::const_iterator dictionary_iter = mInventoryItemsDict.find(object_name);

        bool found = dictionary_iter != mInventoryItemsDict.end();
        if(found)
        {
            object_name = dictionary_iter->second;
            LL_DEBUGS(LOG_LOCAL) << "Found, new name is: " << object_name << LL_ENDL;
        }
        return found;
    }
};
// ---- VKS-COPY-END

// Not a copy: the real constructor fills the dictionary from LLTrans
// strings the harness does not load (strings.xml). It is left empty, so
// every name passes through unchanged (see the README, "Name localisation").
LLLocalizedInventoryItemsDictionary::LLLocalizedInventoryItemsDictionary()
{
}

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 365-369 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::LLViewerInventoryItem()
LLViewerInventoryItem::LLViewerInventoryItem() :
    LLInventoryItem(),
    mIsComplete(false)
{
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 371-380 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::LLViewerInventoryItem(const LLViewerInventoryItem*)
LLViewerInventoryItem::LLViewerInventoryItem(const LLViewerInventoryItem* other) :
    LLInventoryItem()
{
    copyViewerItem(other);
    if (!mIsComplete)
    {
        LL_WARNS(LOG_INV) << "LLViewerInventoryItem copy constructor for incomplete item"
                          << mUUID << LL_ENDL;
    }
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 389-391 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::~LLViewerInventoryItem()
LLViewerInventoryItem::~LLViewerInventoryItem()
{
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 393-400 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::copyViewerItem
void LLViewerInventoryItem::copyViewerItem(const LLViewerInventoryItem* other)
{
    LLInventoryItem::copyItem(other);
    mIsComplete = other->mIsComplete;
    mTransactionID = other->mTransactionID;
    mIsRunning = other->mIsRunning;
    mIsFaulted = other->mIsFaulted;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 403-408 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::copyItem
void LLViewerInventoryItem::copyItem(const LLInventoryItem *other)
{
    LLInventoryItem::copyItem(other);
    mIsComplete = true;
    mTransactionID.setNull();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 551-561 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::unpackMessage(const LLSD&)
bool LLViewerInventoryItem::unpackMessage(const LLSD& item)
{
    bool rv = LLInventoryItem::fromLLSD(item);
    mIsRunning = item["running"].asBoolean();
    mIsFaulted = item["faulted"].asBoolean();

    LLLocalizedInventoryItemsDictionary::getInstance()->localizeInventoryObjectName(mName);

    mIsComplete = true;
    return rv;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 574-577 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::setTransactionID
void LLViewerInventoryItem::setTransactionID(const LLTransactionID& transaction_id)
{
    mTransactionID = transaction_id;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 606-619 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::updateParentOnServer
void LLViewerInventoryItem::updateParentOnServer(bool restamp) const
{
    if (vks_ais::recording()) { LLSD op = LLSD::emptyMap(); op["op"] = "updateParentOnServer"; op["id"] = mUUID; op["parent_id"] = mParentUUID; op["restamp"] = restamp; op["is_model_object"] = vks_ais::is_model_item(this); record(op); } // VKS-HARNESS
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MoveInventoryItem);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addBOOLFast(_PREHASH_Stamp, restamp);
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addUUIDFast(_PREHASH_ItemID, mUUID);
    msg->addUUIDFast(_PREHASH_FolderID, mParentUUID);
    msg->addString("NewName", NULL);
    gAgent.sendReliableMessage();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2449-2460 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::getType
LLAssetType::EType LLViewerInventoryItem::getType() const
{
    if (const LLViewerInventoryItem *linked_item = getLinkedItem())
    {
        return linked_item->getType();
    }
    if (const LLViewerInventoryCategory *linked_category = getLinkedCategory())
    {
        return linked_category->getType();
    }
    return LLInventoryItem::getType();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2506-2518 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::getName
const std::string& LLViewerInventoryItem::getName() const
{
    if (const LLViewerInventoryItem *linked_item = getLinkedItem())
    {
        return linked_item->getName();
    }
    if (const LLViewerInventoryCategory *linked_category = getLinkedCategory())
    {
        return linked_category->getName();
    }

    return  LLInventoryItem::getName();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2703-2716 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::getLinkedItem
LLViewerInventoryItem *LLViewerInventoryItem::getLinkedItem() const
{
    if (mType == LLAssetType::AT_LINK)
    {
        LLViewerInventoryItem *linked_item = gInventory.getItem(mAssetUUID);
        if (linked_item && linked_item->getIsLinkType())
        {
            LL_WARNS(LOG_INV) << "Warning: Accessing link to link" << LL_ENDL;
            return NULL;
        }
        return linked_item;
    }
    return NULL;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2718-2726 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryItem::getLinkedCategory
LLViewerInventoryCategory *LLViewerInventoryItem::getLinkedCategory() const
{
    if (mType == LLAssetType::AT_LINK_FOLDER)
    {
        LLViewerInventoryCategory *linked_category = gInventory.getCategory(mAssetUUID);
        return linked_category;
    }
    return NULL;
}
// ---- VKS-COPY-END

// The other virtual members: never reached by AISUpdate (the harness's own
// field dumps read the base members); a call fails the test loudly.
const LLUUID& LLViewerInventoryItem::getAssetUUID() const { vks_ais::unreached("LLViewerInventoryItem::getAssetUUID"); }
const LLUUID& LLViewerInventoryItem::getProtectedAssetUUID() const { vks_ais::unreached("LLViewerInventoryItem::getProtectedAssetUUID"); }
S32 LLViewerInventoryItem::getSortField() const { vks_ais::unreached("LLViewerInventoryItem::getSortField"); }
void LLViewerInventoryItem::getSLURL() { vks_ais::unreached("LLViewerInventoryItem::getSLURL"); }
const LLPermissions& LLViewerInventoryItem::getPermissions() const { vks_ais::unreached("LLViewerInventoryItem::getPermissions"); }
const bool LLViewerInventoryItem::getIsFullPerm() const { vks_ais::unreached("LLViewerInventoryItem::getIsFullPerm"); }
const LLUUID& LLViewerInventoryItem::getCreatorUUID() const { vks_ais::unreached("LLViewerInventoryItem::getCreatorUUID"); }
const std::string& LLViewerInventoryItem::getDescription() const { vks_ais::unreached("LLViewerInventoryItem::getDescription"); }
const LLSaleInfo& LLViewerInventoryItem::getSaleInfo() const { vks_ais::unreached("LLViewerInventoryItem::getSaleInfo"); }
const LLUUID& LLViewerInventoryItem::getThumbnailUUID() const { vks_ais::unreached("LLViewerInventoryItem::getThumbnailUUID"); }
LLInventoryType::EType LLViewerInventoryItem::getInventoryType() const { vks_ais::unreached("LLViewerInventoryItem::getInventoryType"); }
bool LLViewerInventoryItem::isWearableType() const { vks_ais::unreached("LLViewerInventoryItem::isWearableType"); }
LLWearableType::EType LLViewerInventoryItem::getWearableType() const { vks_ais::unreached("LLViewerInventoryItem::getWearableType"); }
bool LLViewerInventoryItem::isSettingsType() const { vks_ais::unreached("LLViewerInventoryItem::isSettingsType"); }
LLSettingsType::type_e LLViewerInventoryItem::getSettingsType() const { vks_ais::unreached("LLViewerInventoryItem::getSettingsType"); }
U32 LLViewerInventoryItem::getFlags() const { vks_ais::unreached("LLViewerInventoryItem::getFlags"); }
time_t LLViewerInventoryItem::getCreationDate() const { vks_ais::unreached("LLViewerInventoryItem::getCreationDate"); }
U32 LLViewerInventoryItem::getCRC32() const { vks_ais::unreached("LLViewerInventoryItem::getCRC32"); }
void LLViewerInventoryItem::updateServer(bool is_new) const { vks_ais::unreached("LLViewerInventoryItem::updateServer"); }
void LLViewerInventoryItem::packMessage(LLMessageSystem* msg) const { vks_ais::unreached("LLViewerInventoryItem::packMessage"); }
bool LLViewerInventoryItem::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num) { vks_ais::unreached("LLViewerInventoryItem::unpackMessage(msg)"); }
bool LLViewerInventoryItem::importLegacyStream(std::istream& input_stream) { vks_ais::unreached("LLViewerInventoryItem::importLegacyStream"); }

//============================================================================
// LLViewerInventoryCategory
//============================================================================

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 649-656 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& owner_id)
LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& owner_id) :
    mOwnerID(owner_id),
    mVersion(LLViewerInventoryCategory::VERSION_UNKNOWN),
    mDescendentCount(LLViewerInventoryCategory::DESCENDENT_COUNT_UNKNOWN),
    mFetching(FETCH_NONE)
{
    mDescendentsRequested.reset();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 635-647 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& uuid, ...)
LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& uuid,
                                                     const LLUUID& parent_uuid,
                                                     LLFolderType::EType pref,
                                                     const std::string& name,
                                                     const LLUUID& owner_id) :
    LLInventoryCategory(uuid, parent_uuid, pref, name),
    mOwnerID(owner_id),
    mVersion(LLViewerInventoryCategory::VERSION_UNKNOWN),
    mDescendentCount(LLViewerInventoryCategory::DESCENDENT_COUNT_UNKNOWN),
    mFetching(FETCH_NONE)
{
    mDescendentsRequested.reset();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 658-662 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::LLViewerInventoryCategory(const LLViewerInventoryCategory*)
LLViewerInventoryCategory::LLViewerInventoryCategory(const LLViewerInventoryCategory* other)
{
    copyViewerCategory(other);
    mFetching = FETCH_NONE;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 664-666 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::~LLViewerInventoryCategory()
LLViewerInventoryCategory::~LLViewerInventoryCategory()
{
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 668-675 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::copyViewerCategory
void LLViewerInventoryCategory::copyViewerCategory(const LLViewerInventoryCategory* other)
{
    copyCategory(other);
    mOwnerID = other->mOwnerID;
    setVersion(other->getVersion());
    mDescendentCount = other->mDescendentCount;
    mDescendentsRequested = other->mDescendentsRequested;
}
// ---- VKS-COPY-END

// Recording stubs (not copies; the real members are one-line accessors,
// llviewerinventory.cpp:735-743). Calls on the model's own objects are
// recorded; calls on the parse's detached copies are not.
S32 LLViewerInventoryCategory::getVersion() const
{
    if (vks_ais::recording() && vks_ais::is_model_cat(this))
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "q.getVersion";
        op["id"] = mUUID;
        op["result"] = (LLSD::Integer)mVersion;
        record(op);
    }
    return mVersion;
}

void LLViewerInventoryCategory::setVersion(S32 version)
{
    if (vks_ais::recording() && vks_ais::is_model_cat(this))
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "setVersion";
        op["id"] = mUUID;
        op["version"] = (LLSD::Integer)version;
        op["before"] = (LLSD::Integer)mVersion;
        record(op);
    }
    mVersion = version;
}

// Recording stub of fetch() (llviewerinventory.cpp:745-795). The real
// member starts a background fetch when the version is unknown and the
// request timer has expired, then arms the timer for expiry_seconds; the
// stub records the call and keeps that gate and timer (LLFrameTimer, whose
// frame time never advances in the test), without fetching.
bool LLViewerInventoryCategory::fetch(S32 expiry_seconds)
{
    bool would_start = (VERSION_UNKNOWN == mVersion) && mDescendentsRequested.hasExpired();
    if (would_start)
    {
        mDescendentsRequested.reset();
        mDescendentsRequested.setTimerExpirySec((F32)expiry_seconds);
    }
    if (vks_ais::recording())
    {
        LLSD op = LLSD::emptyMap();
        op["op"] = "fetch";
        op["id"] = mUUID;
        op["expiry"] = (LLSD::Integer)expiry_seconds;
        op["version"] = (LLSD::Integer)mVersion;
        op["would_start"] = would_start;
        op["is_model_object"] = vks_ais::is_model_cat(this);
        record(op);
    }
    return would_start;
}

// Emulation (not a copy): the real member counts the model's parent-child
// arrays for this folder; the skeleton's children are those arrays.
S32 LLViewerInventoryCategory::getViewerDescendentCount() const
{
    return vks_ais::child_count(mUUID);
}

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 1005-1008 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::localizeName
void LLViewerInventoryCategory::localizeName()
{
    LLLocalizedInventoryItemsDictionary::getInstance()->localizeInventoryObjectName(mName);
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 1011-1016 blob 41e2a9b23982f076e14a5de056d443438f8980b3 : LLViewerInventoryCategory::unpackMessage(const LLSD&)
bool LLViewerInventoryCategory::unpackMessage(const LLSD& category)
{
    bool rv = LLInventoryCategory::fromLLSD(category);
    localizeName();
    return rv;
}
// ---- VKS-COPY-END

void LLViewerInventoryCategory::updateParentOnServer(bool restamp) const { vks_ais::unreached("LLViewerInventoryCategory::updateParentOnServer"); }
void LLViewerInventoryCategory::updateServer(bool is_new) const { vks_ais::unreached("LLViewerInventoryCategory::updateServer"); }
void LLViewerInventoryCategory::packMessage(LLMessageSystem* msg) const { vks_ais::unreached("LLViewerInventoryCategory::packMessage"); }
void LLViewerInventoryCategory::exportLLSD(LLSD& sd) const { vks_ais::unreached("LLViewerInventoryCategory::exportLLSD"); }
bool LLViewerInventoryCategory::importLLSD(const std::string& label, const LLSD& value) { vks_ais::unreached("LLViewerInventoryCategory::importLLSD"); }
void LLViewerInventoryCategory::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num) { vks_ais::unreached("LLViewerInventoryCategory::unpackMessage(msg)"); }
