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
 *  - AISUpdate's timers and its yield: this file compiles llaisapi.cpp
 *    (included verbatim, first) with LLTimer renamed to vks_r3a_LLTimer
 *    and llcoro::suspendUntilNextFrame renamed to
 *    vks_r3a_suspendUntilNextFrame; both are defined below over an
 *    injected clock and an injected frame counter. The test source applies
 *    the same renames before its includes, so both see one AISUpdate.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

// The renames must precede every include.
#define LLTimer vks_r3a_LLTimer
#define suspendUntilNextFrame vks_r3a_suspendUntilNextFrame

// The code under test, unchanged.
#include "../llaisapi.cpp"

#include "../llaisapi.h"
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

    typedef std::function<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t, LLCore::HttpRequest::ptr_t,
        const std::string, LLSD, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)> invoke_fn_t;

    struct InvokeTag
    {
        typedef void (*type)(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t, invoke_fn_t, std::string,
                             LLUUID, LLSD, AISAPI::completion_t, AISAPI::COMMAND_TYPE);
        friend type steal(InvokeTag);
    };
    struct BuilderTag { typedef LLMessageBuilder* LLMessageSystem::*type; friend type steal(BuilderTag); };
    struct CatVersionTag { typedef S32 LLViewerInventoryCategory::*type; friend type steal(CatVersionTag); };
    struct BatchTimerTag { typedef LLTimer* type; friend type steal(BatchTimerTag); };
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
            size_t yields = 0;
            LLSD ops = LLSD::emptyArray();
        };

        State& st()
        {
            static State s;
            return s;
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

    void begin_run(TimerMode mode, U64 call_us, U64 frame_us, const std::vector<U64>& others_us)
    {
        State& s = st();
        s.ops = LLSD::emptyArray();
        s.mode = mode;
        s.call_us = call_us;
        s.frame_us = frame_us;
        s.others_us = others_us;
        s.yields = 0;
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

    void invoke_command(AISAPI::COMMAND_TYPE type, const std::string& url,
                        const LLUUID& target_id, const LLSD& body,
                        const LLSD& result, bool with_callback)
    {
        invoke_fn_t invoke = [result](LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t, LLCore::HttpRequest::ptr_t,
                                      const std::string, LLSD, LLCore::HttpOptions::ptr_t,
                                      LLCore::HttpHeaders::ptr_t) -> LLSD
        {
            return result;
        };
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
        steal(InvokeTag())(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t(), invoke, url, target_id, body, callback, type);
    }

    namespace
    {
        const char* const COMMAND_NAMES[] = {
            "COPYINVENTORY", "SLAMFOLDER", "REMOVECATEGORY", "REMOVEITEM", "PURGEDESCENDENTS",
            "UPDATECATEGORY", "UPDATEITEM", "COPYLIBRARYCATEGORY", "CREATEINVENTORY", "FETCHITEM",
            "FETCHCATEGORYCHILDREN", "FETCHCATEGORYCATEGORIES", "FETCHCATEGORYSUBSET", "FETCHCOF",
            "FETCHORPHANS", "FETCHCATEGORYLINKS"};
    }

    const char* command_name(AISAPI::COMMAND_TYPE type)
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
bool gDisconnected = false;
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

bool LLViewerRegion::isCapabilityAvailable(const std::string& name) const
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
// AISUpdate's timers and yield (renamed by r3-update.cmake)
//============================================================================
// LLTimer here is vks_r3a_LLTimer: LLTimer's declaration from lltimer.h,
// defined over the injected clock (microsecond ticks). The arithmetic is
// LLTimer's (lltimer.cpp): start() and reset() leave mExpirationTicks at 0,
// setTimerExpirySec() adds (U64)(F32)(seconds * frequency) to now, and
// hasExpired() is now >= mExpirationTicks. In TIMERS_NEVER mode hasExpired()
// is always false.
LLTimer* LLTimer::sTimer = NULL;

namespace
{
    U64 fake_clock()
    {
        return vks_ais::st().clock;
    }
}

LLTimer::LLTimer()
{
    mStarted = true;
    reset();
}

LLTimer::~LLTimer()
{
}

void LLTimer::reset()
{
    mLastClockCount = fake_clock();
    mExpirationTicks = 0;
}

void LLTimer::setLastClockCount(U64 current_count)
{
    mLastClockCount = current_count;
}

void LLTimer::setTimerExpirySec(F32SecondsImplicit expiration)
{
    mExpirationTicks = fake_clock() + (U64)((F32)((F64)expiration.value() * 1000000.0));
}

bool LLTimer::hasExpired() const
{
    if (vks_ais::st().mode == vks_ais::TIMERS_NEVER)
    {
        return false;
    }
    return fake_clock() >= mExpirationTicks;
}

F64SecondsImplicit LLTimer::getElapsedTimeF64() const
{
    return (F64)(fake_clock() - mLastClockCount) / 1000000.0;
}

F32SecondsImplicit LLTimer::getElapsedTimeF32() const
{
    return (F32)getElapsedTimeF64();
}

U64MicrosecondsImplicit LLTimer::getTotalTime()
{
    return totalTime();
}

F64SecondsImplicit LLTimer::getTotalSeconds()
{
    return F64Microseconds(U64_to_F64(getTotalTime()));
}

// llcoro::suspendUntilNextFrame() (renamed): the AIS coroutine yields to the
// next frame. Recorded as a "yield" event; the frame counter and the clock
// move on.
void llcoro::vks_r3a_suspendUntilNextFrame()
{
    LLSD op = LLSD::emptyMap();
    op["op"] = "yield";
    op["frame"] = (LLSD::Integer)vks_ais::rel_frame();
    op["clock_us"] = (LLSD::Integer)(S32)vks_ais::rel_clock();
    vks_ais::State& s = vks_ais::st();
    U64 other = s.others_us.empty() ? 0 : s.others_us[s.yields % s.others_us.size()];
    ++s.yields;
    op["others_us"] = (LLSD::Integer)(S32)other;
    record(op, false);
    FrameCount::set(FrameCount::get() + 1);
    s.clock += s.frame_us;
    if (other > 0)
    {
        // Another AIS update resumed first in this frame: as its
        // checkTimeout() does, it re-armed the shared batch timer
        // (AIS_BATCH_EXPIRY_SECONDS, 10 ms) and took the frame, then ran.
        steal(BatchTimerTag())->setTimerExpirySec(0.010f);
        *steal(BatchFrameTag()) = (U32)FrameCount::get();
        s.clock += other;
    }
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
                }
                else if (!vks_ais::store_cat(item->getParentUUID()))
                {
                    LLUUID parent_id = findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);
                    new_item->setParent(parent_id);
                    outcome = vks_ais::store_cat(parent_id) ? "added_unknown_parent_to_lost_and_found_server_move"
                                                            : "added_unknown_parent_no_lost_and_found";
                }
                vks_ais::st().items[new_item->getUUID()] = new_item;
                mask |= LLInventoryObserver::ADD;
            }
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 107-133 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : class LLLocalizedInventoryItemsDictionary
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 365-369 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::LLViewerInventoryItem()
LLViewerInventoryItem::LLViewerInventoryItem() :
    LLInventoryItem(),
    mIsComplete(false)
{
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 371-380 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::LLViewerInventoryItem(const LLViewerInventoryItem*)
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 389-391 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::~LLViewerInventoryItem()
LLViewerInventoryItem::~LLViewerInventoryItem()
{
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 393-400 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::copyViewerItem
void LLViewerInventoryItem::copyViewerItem(const LLViewerInventoryItem* other)
{
    LLInventoryItem::copyItem(other);
    mIsComplete = other->mIsComplete;
    mTransactionID = other->mTransactionID;
    mIsRunning = other->mIsRunning;
    mIsFaulted = other->mIsFaulted;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 403-408 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::copyItem
void LLViewerInventoryItem::copyItem(const LLInventoryItem *other)
{
    LLInventoryItem::copyItem(other);
    mIsComplete = true;
    mTransactionID.setNull();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 548-558 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::unpackMessage(const LLSD&)
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 571-574 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::setTransactionID
void LLViewerInventoryItem::setTransactionID(const LLTransactionID& transaction_id)
{
    mTransactionID = transaction_id;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 603-616 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::updateParentOnServer
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2446-2457 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::getType
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2503-2515 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::getName
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2700-2713 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::getLinkedItem
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 2715-2723 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryItem::getLinkedCategory
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 646-653 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& owner_id)
LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& owner_id) :
    mOwnerID(owner_id),
    mVersion(LLViewerInventoryCategory::VERSION_UNKNOWN),
    mDescendentCount(LLViewerInventoryCategory::DESCENDENT_COUNT_UNKNOWN),
    mFetching(FETCH_NONE)
{
    mDescendentsRequested.reset();
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 632-644 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::LLViewerInventoryCategory(const LLUUID& uuid, ...)
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 655-659 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::LLViewerInventoryCategory(const LLViewerInventoryCategory*)
LLViewerInventoryCategory::LLViewerInventoryCategory(const LLViewerInventoryCategory* other)
{
    copyViewerCategory(other);
    mFetching = FETCH_NONE;
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 661-663 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::~LLViewerInventoryCategory()
LLViewerInventoryCategory::~LLViewerInventoryCategory()
{
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 665-672 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::copyViewerCategory
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

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 1002-1005 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::localizeName
void LLViewerInventoryCategory::localizeName()
{
    LLLocalizedInventoryItemsDictionary::getInstance()->localizeInventoryObjectName(mName);
}
// ---- VKS-COPY-END

// ---- VKS-COPY-BEGIN llviewerinventory.cpp 1008-1013 blob 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335 : LLViewerInventoryCategory::unpackMessage(const LLSD&)
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
