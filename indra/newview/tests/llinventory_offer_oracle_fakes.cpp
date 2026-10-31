/**
 * @file llinventory_offer_oracle_fakes.cpp
 * @brief Recorder R4b (inventory rung): the fakes behind the frozen offer and
 * give copies. See llinventory_offer_oracle_fakes.h.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_offer_oracle_fakes.h"
#include "llinventory_offer_oracle_copies.h"
#include "llinventory_offer_oracle_boundary.h"

#include "llsdserialize.h"
#include "vkstorm_ffi.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// LLMessageSystem::wrapBuiltTemplateData() is private; the test reads the
// built packet through it with the explicit-instantiation access rule
// ([temp.explicit]: access is not checked in an explicit instantiation's
// template arguments). Test-only; no production change.
namespace
{
    template<typename Tag, typename Tag::type M>
    struct R4bAccess
    {
        friend typename Tag::type r4b_access(Tag) { return M; }
    };
    struct WrapBuilt
    {
        typedef LLSD (LLMessageSystem::*type)() const;
        friend type r4b_access(WrapBuilt);
    };
    template struct R4bAccess<WrapBuilt, &LLMessageSystem::wrapBuiltTemplateData>;

    LLSD built_template_data(const LLMessageSystem* msg)
    {
        return (msg->*r4b_access(WrapBuilt()))();
    }
}

namespace r4b
{
    // ------------------------------------------------------------ helpers
    EventLog& ev()
    {
        static EventLog log;
        return log;
    }

    std::string hex(const void* data, size_t size)
    {
        static const char* digits = "0123456789abcdef";
        const U8* p = (const U8*)data;
        std::string out;
        out.reserve(size * 2);
        for (size_t i = 0; i < size; ++i)
        {
            out += digits[p[i] >> 4];
            out += digits[p[i] & 15];
        }
        return out;
    }

    std::string sd_bin_hex(const LLSD& sd)
    {
        std::ostringstream out;
        LLSDSerialize::toBinary(sd, out);
        std::string s = out.str();
        return hex(s.data(), s.size());
    }

    std::string sd_notation(const LLSD& sd)
    {
        std::ostringstream out;
        LLSDSerialize::toNotation(sd, out);
        return esc(out.str());
    }

    std::string fmt(const char* format, ...)
    {
        va_list va;
        va_start(va, format);
        va_list copy;
        va_copy(copy, va);
        int n = vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        std::string out(n > 0 ? (size_t)n : 0, ' ');
        if (n > 0) vsnprintf(&out[0], (size_t)n + 1, format, va);
        va_end(va);
        return out;
    }

    std::string esc(const std::string& s)
    {
        std::string out;
        for (unsigned char c : s)
        {
            if (c == '\\') out += "\\\\";
            else if (c >= 0x20 && c < 0x7f) out += (char)c;
            else
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            }
        }
        return out;
    }

    static U32 s_generate_counter = 0;
    void r4b_generate(LLUUID& id)
    {
        ++s_generate_counter;
        id.set(fmt("7a000000-0000-4000-8000-%012x", s_generate_counter));
        ev().add(fmt("uuid.generate %s", id.asString().c_str()));
    }

    LLDate r4b_now()
    {
        return LLDate(1700000000.0);
    }

    // ----------------------------------------------------------- settings
    FakeSettings gSavedSettings;
    FakeSettings gSavedPerAccountSettings;

    static void unpinned(const FakeSettings& s, const std::string& name)
    {
        fprintf(stderr, "r4b: setting %s.%s is not pinned by the harness\n", s.label, name.c_str());
        std::abort();
    }

    bool FakeSettings::getBOOL(const std::string& name) const
    {
        auto it = b.find(name);
        if (it == b.end()) unpinned(*this, name);
        return it->second;
    }

    U32 FakeSettings::getU32(const std::string& name) const
    {
        auto it = u.find(name);
        if (it == u.end()) unpinned(*this, name);
        return it->second;
    }

    std::string FakeSettings::getString(const std::string& name) const
    {
        auto it = s.find(name);
        if (it == s.end()) unpinned(*this, name);
        return it->second;
    }

    // ----------------------------------------------------- message system
    static LLMessageSystem s_fake_msg;
    LLMessageSystem* gMessageSystem = &s_fake_msg;

    void LLMessageSystem::newMessageFast(const char* name)
    {
        mFields.clear();
        mFields.push_back(std::string("message=") + name);
        ::gMessageSystem->newMessageFast(name);
    }
    void LLMessageSystem::nextBlockFast(const char* block)
    {
        mFields.push_back(std::string("block=") + block);
        ::gMessageSystem->nextBlockFast(block);
    }
    void LLMessageSystem::addUUIDFast(const char* var, const LLUUID& v)
    {
        mFields.push_back(std::string(var) + "=" + v.asString());
        ::gMessageSystem->addUUIDFast(var, v);
    }
    void LLMessageSystem::addBOOLFast(const char* var, bool v)
    {
        mFields.push_back(std::string(var) + "=" + (v ? "1" : "0"));
        ::gMessageSystem->addBOOLFast(var, v);
    }
    void LLMessageSystem::addU8Fast(const char* var, U8 v)
    {
        mFields.push_back(fmt("%s=%u", var, (U32)v));
        ::gMessageSystem->addU8Fast(var, v);
    }
    void LLMessageSystem::addU32Fast(const char* var, U32 v)
    {
        mFields.push_back(fmt("%s=%u", var, v));
        ::gMessageSystem->addU32Fast(var, v);
    }
    void LLMessageSystem::addStringFast(const char* var, const char* v)
    {
        mFields.push_back(std::string(var) + "=" + (v ? "\"" + esc(v) + "\"" : std::string("NULL")));
        ::gMessageSystem->addStringFast(var, v);
    }
    void LLMessageSystem::addStringFast(const char* var, const std::string& v)
    {
        mFields.push_back(std::string(var) + "=\"" + esc(v) + "\"");
        ::gMessageSystem->addStringFast(var, v);
    }
    void LLMessageSystem::addVector3Fast(const char* var, const LLVector3& v)
    {
        mFields.push_back(fmt("%s=<%g,%g,%g>", var, v.mV[0], v.mV[1], v.mV[2]));
        ::gMessageSystem->addVector3Fast(var, v);
    }
    void LLMessageSystem::addBinaryDataFast(const char* var, const void* data, S32 size)
    {
        mFields.push_back(std::string(var) + "=" + hex(data, size));
        ::gMessageSystem->addBinaryDataFast(var, data, size);
    }

    void LLMessageSystem::capture(const std::string& route)
    {
        LLSD wrapped = built_template_data(::gMessageSystem);
        std::vector<U8> bytes = wrapped["binary-template-data"].asBinary();
        // buildMessage leaves the flags and packet id (bytes 0-4) as they
        // were; sendMessage zeroes them and then stamps the circuit's packet
        // id and flags, which this record leaves at zero
        for (size_t i = 0; i < 5 && i < bytes.size(); ++i) bytes[i] = 0;
        std::vector<U8> coded(bytes.size() * 2 + 16);
        int32_t n = vks_msg_zero_code(bytes.data(), bytes.size(), coded.data(), coded.size());
        std::string zc = n > 0 ? hex(coded.data(), n) : std::string("-");
        ev().add("send " + route + " bytes=" + hex(bytes.data(), bytes.size()) + " zc=" + zc);
        std::string fields;
        for (const std::string& f : mFields)
        {
            if (!fields.empty()) fields += " ";
            fields += f;
        }
        ev().add("note:fields " + fields);
        mFields.clear();
        ::gMessageSystem->clearMessage();
    }

    S32 LLMessageSystem::sendReliable(const LLHost& host)
    {
        capture("host=" + host.getIPandPort());
        return 1;
    }

    void pack_instant_message(
        LLMessageSystem* msg,
        const LLUUID& from_id,
        bool from_group,
        const LLUUID& session_id,
        const LLUUID& to_id,
        const std::string& name,
        const std::string& message,
        U8 offline,
        EInstantMessage dialog,
        const LLUUID& id,
        U32 parent_estate_id,
        const LLUUID& region_id,
        const LLVector3& position,
        U32 timestamp,
        const U8* binary_bucket,
        S32 binary_bucket_size)
    {
        msg->mFields.clear();
        msg->mFields.push_back(fmt("pack_instant_message dialog=%d name=\"%s\" message_len=%d bucket_size=%d",
                                        (S32)dialog, esc(name).c_str(), (S32)message.size(), binary_bucket_size));
        // what the caller asked to pack (before pack_instant_message's
        // truncation, which the packet shows)
        ev().add(fmt("pack dialog=%d to=%s message=\"%s\" bucket=", (S32)dialog, to_id.asString().c_str(), esc(message).c_str()) +
                 (binary_bucket ? hex(binary_bucket, binary_bucket_size) : std::string("EMPTY")));
        ::pack_instant_message(::gMessageSystem, from_id, from_group, session_id, to_id, name, message, offline,
                               dialog, id, parent_estate_id, region_id, position, timestamp, binary_bucket,
                               binary_bucket_size);
    }

    // ------------------------------------------------------------- objects
    FakeObjectList gObjectList;

    LLViewerObject* FakeObjectList::findObject(const LLUUID& id) const
    {
        auto it = objects.find(id);
        return it == objects.end() ? nullptr : it->second;
    }

    LLViewerObject* get_object_to_mute_from_id(LLUUID id)
    {
        return nullptr;
    }

    // --------------------------------------------------------------- agent
    FakeAgent gAgent;
    LLUUID gAgentID;
    LLUUID gAgentSessionID;
    static FakeAvatar s_avatar;
    FakeAvatar* gAgentAvatarp = &s_avatar;
    FakeWearables gAgentWearables;

    bool isAgentAvatarValid()
    {
        return gAgentAvatarp && gAgentAvatarp->mValid;
    }

    void FakeAgent::sendReliableMessage()
    {
        s_fake_msg.capture("agent_region=" + mRegionHost.getIPandPort());
    }

    LLGestureMgr& LLGestureMgr::instance()
    {
        static LLGestureMgr mgr;
        return mgr;
    }

    // --------------------------------------------------------------- model
    LLInventoryModel gInventory;

    void LLInventoryModel::clear()
    {
        for (auto& p : mParentChildCategoryTree) delete p.second;
        for (auto& p : mParentChildItemTree) delete p.second;
        mParentChildCategoryTree.clear();
        mParentChildItemTree.clear();
        mCategoryMap.clear();
        mItemMap.clear();
        mIncomplete.clear();
        mSystemFolders.clear();
        for (LLInventoryObserver* o : mObservers) delete o;
        mObservers.clear();
    }

    void LLInventoryModel::addCategory(LLViewerInventoryCategory* cat)
    {
        mCategoryMap[cat->getUUID()] = cat;
        cat_array_t*& arr = mParentChildCategoryTree[cat->getParentUUID()];
        if (!arr) arr = new cat_array_t;
        arr->push_back(cat);
    }

    void LLInventoryModel::addItem(LLViewerInventoryItem* item)
    {
        mItemMap[item->getUUID()] = item;
        item_array_t*& arr = mParentChildItemTree[item->getParentUUID()];
        if (!arr) arr = new item_array_t;
        arr->push_back(item);
    }

    LLViewerInventoryItem* LLInventoryModel::getItem(const LLUUID& id) const
    {
        auto it = mItemMap.find(id);
        return it == mItemMap.end() ? nullptr : it->second.get();
    }

    LLViewerInventoryCategory* LLInventoryModel::getCategory(const LLUUID& id) const
    {
        auto it = mCategoryMap.find(id);
        return it == mCategoryMap.end() ? nullptr : it->second.get();
    }

    bool LLInventoryModel::isCategoryComplete(const LLUUID& id) const
    {
        return mIncomplete.count(id) == 0;
    }

    const LLUUID LLInventoryModel::findCategoryUUIDForType(LLFolderType::EType preferred_type)
    {
        // every folder type has a patterned id (f0000000-...-<type as U16>)
        // unless the case overrides it
        auto it = mSystemFolders.find((S32)preferred_type);
        LLUUID id;
        if (it != mSystemFolders.end()) id = it->second;
        else id.set(fmt("000000f0-0000-4000-8000-%012x", (U32)(U16)preferred_type));
        ev().add(fmt("model.findCategoryUUIDForType %d -> %s", (S32)preferred_type, id.asString().c_str()));
        return id;
    }

    void LLInventoryModel::deleteObject(const LLUUID& id)
    {
        ev().add("model.deleteObject " + id.asString());
        auto it = mItemMap.find(id);
        if (it != mItemMap.end())
        {
            LLPointer<LLViewerInventoryItem> item = it->second;
            mItemMap.erase(it);
            item_array_t* arr = get_ptr_in_map(mParentChildItemTree, item->getParentUUID());
            if (arr)
            {
                arr->erase(std::remove(arr->begin(), arr->end(), item), arr->end());
            }
        }
    }

    void LLInventoryModel::removeObject(const LLUUID& id)
    {
        ev().add("model.removeObject " + id.asString());
    }

    void LLInventoryModel::notifyObservers()
    {
        ev().add("model.notifyObservers");
    }

    void LLInventoryModel::addObserver(LLInventoryObserver* observer)
    {
        ev().add("model.addObserver " + observer_kind(observer));
        mObservers.push_back(observer);
    }

    void LLInventoryModel::removeObserver(LLInventoryObserver* observer)
    {
        ev().add("model.removeObserver " + observer_kind(observer));
        mObservers.erase(std::remove(mObservers.begin(), mObservers.end(), observer), mObservers.end());
    }

    static std::string ids(const uuid_vec_t& v)
    {
        std::string out = "[";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i) out += ",";
            out += v[i].asString();
        }
        return out + "]";
    }

    LLInventoryFetchItemsObserver::LLInventoryFetchItemsObserver(const LLUUID& item_id)
    {
        mIDs.clear();
        mIDs.push_back(item_id);
    }

    LLInventoryFetchItemsObserver::LLInventoryFetchItemsObserver(const uuid_vec_t& item_ids)
    {
        mIDs = item_ids;
    }

    void LLInventoryFetchItemsObserver::startFetch()
    {
        for (const LLUUID& id : mIDs)
        {
            LLViewerInventoryItem* item = gInventory.getItem(id);
            if (item && item->isFinished())
            {
                mComplete.push_back(id);
                continue;
            }
            if (gInventory.getCategory(id)) continue;
            if (id.isNull()) continue;
            mIncomplete.push_back(id);
        }
        ev().add("fetch.start " + observer_kind(this) + " ids=" + ids(mIDs) + " complete=" + ids(mComplete) +
                 " incomplete=" + ids(mIncomplete));
    }

    void LLInventoryFetchItemsObserver::done()
    {
        ev().add("fetch.done " + observer_kind(this));
    }

    void open_inventory_offer(const uuid_vec_t& items, const std::string& from_name, bool from_agent_manual)
    {
        ev().add("open_inventory_offer items=" + ids(items) + " from=\"" + esc(from_name) + "\" manual=" + (from_agent_manual ? "1" : "0"));
    }

    LLOpenTaskGroupOffer::LLOpenTaskGroupOffer()
    {
        ev().add("new LLOpenTaskGroupOffer");
    }

    void LLInventoryPanel::openInventoryPanelAndSetSelection(bool auto_open, const LLUUID& obj_id, bool, bool, bool)
    {
        ev().add(fmt("inventory_panel.select auto_open=%d id=%s", (S32)auto_open, obj_id.asString().c_str()));
    }

    static std::string move_list(const std::shared_ptr<LLMoveInv>& m)
    {
        std::string out = "object=" + m->mObjectID.asString() + " category=" + m->mCategoryID.asString() + " list=[";
        bool first = true;
        for (const two_uuids_t& p : m->mMoveList)
        {
            if (!first) out += ",";
            first = false;
            out += p.first.asString() + ">" + p.second.asString();
        }
        return out + "]";
    }

    void warn_move_inventory(LLViewerObject* object, std::shared_ptr<LLMoveInv> move_inv)
    {
        ev().add("warn_move_inventory " + move_list(move_inv));
    }

    bool move_task_inventory_callback(const LLSD& notification, const LLSD& response, std::shared_ptr<LLMoveInv> move_inv)
    {
        S32 option = LLNotification::getSelectedOption(notification, response);
        ev().add(fmt("move_task_inventory_callback option=%d ", option) + move_list(move_inv));
        return false;
    }

    bool LLInventoryFilter::check(const LLInventoryItem* item)
    {
        return item && mPasses.count(item->getUUID()) != 0;
    }

    // --------------------------------------------------------------- names
    static void name_answer(const std::string& what, const std::function<void()>& cb)
    {
        if (world().defer_name_lookups)
        {
            ev().add("name.deferred " + what);
            LLAppViewer::instance()->mIdle.push_back(cb);
        }
        else
        {
            cb();
        }
    }

    LLAvatarNameCache& LLAvatarNameCache::instance()
    {
        static LLAvatarNameCache cache;
        return cache;
    }

    bool LLAvatarNameCache::get(const LLUUID& id, LLAvatarName* name)
    {
        auto& n = instance().names;
        auto it = n.find(id);
        if (it == n.end()) return false;
        name->mUserName = it->second;
        return true;
    }

    LLAvatarNameCache::connection LLAvatarNameCache::get(const LLUUID& id, callback_slot_t slot)
    {
        LLAvatarName av;
        if (!get(id, &av))
        {
            av.mUserName = "resolved." + id.asString().substr(0, 8);
        }
        ev().add("avatarname.lookup " + id.asString() + " -> \"" + esc(av.mUserName) + "\"");
        name_answer("avatar " + id.asString(), [slot, id, av]() { slot(id, av); });
        return connection();
    }

    LLUUID LLAvatarNameCache::findIdByName(const std::string& name)
    {
        auto it = ids_by_legacy_name.find(name);
        return it == ids_by_legacy_name.end() ? LLUUID::null : it->second;
    }

    static LLCacheName s_cache_name;
    LLCacheName* gCacheName = &s_cache_name;

    void LLCacheName::getGroup(const LLUUID& group_id, const callback_t& callback)
    {
        std::string name;
        if (!getGroupName(group_id, name)) name = "Group " + group_id.asString().substr(0, 8);
        ev().add("cachename.getGroup " + group_id.asString() + " -> \"" + esc(name) + "\"");
        name_answer("group " + group_id.asString(), [callback, group_id, name]() { callback(group_id, name, true); });
    }

    bool LLCacheName::getGroupName(const LLUUID& id, std::string& group)
    {
        auto it = group_names.find(id);
        if (it == group_names.end()) return false;
        group = it->second;
        return true;
    }

    void LLCacheName::get(const LLUUID& id, bool is_group, const callback_t& callback)
    {
        std::string name;
        if (!getGroupName(id, name)) name = "Group " + id.asString().substr(0, 8);
        ev().add(fmt("cachename.get %s group=%d -> \"%s\"", id.asString().c_str(), (S32)is_group, esc(name).c_str()));
        name_answer("cachename " + id.asString(), [callback, id, name, is_group]() { callback(id, name, is_group); });
    }

    // --------------------------------------------------------------- mutes
    LLMuteList* LLMuteList::getInstance()
    {
        static LLMuteList list;
        return &list;
    }

    bool LLMuteList::add(const LLMute& mute, U32 flags)
    {
        ev().add(fmt("mute.add id=%s name=\"%s\" type=%d flags=%u", mute.mID.asString().c_str(), esc(mute.mName).c_str(),
                          (S32)mute.mType, flags));
        mMutes.insert(mute);
        return true;
    }

    bool LLMuteList::remove(const LLMute& mute, U32 flags)
    {
        ev().add(fmt("mute.remove id=%s type=%d", mute.mID.asString().c_str(), (S32)mute.mType));
        mMutes.erase(mute);
        return true;
    }

    void notify_automute_callback(const LLUUID& agent_id, const LLAvatarName& av_name, LLMuteList::EAutoReason reason)
    {
        ev().add(fmt("mute.auto_unmute_notice id=%s name=\"%s\" reason=%d", agent_id.asString().c_str(),
                          esc(av_name.getUserName()).c_str(), (S32)reason));
    }

    void LLPanelBlockedList::showPanelAndSelect(const LLUUID& idToSelect)
    {
        ev().add("blocked_list.show " + idToSelect.asString());
    }

    // ------------------------------------------------------- notifications
    std::set<LLNotificationResponderInterface*>& live_responders()
    {
        static std::set<LLNotificationResponderInterface*> s;
        return s;
    }

    std::function<void(const char*)>& offer_snapshot_hook()
    {
        static std::function<void(const char*)> hook;
        return hook;
    }

    void snapshot_offers(const char* where)
    {
        if (offer_snapshot_hook()) offer_snapshot_hook()(where);
    }

    LLNotificationResponderInterface::LLNotificationResponderInterface()
    {
        live_responders().insert(this);
    }

    LLNotificationResponderInterface::~LLNotificationResponderInterface()
    {
        live_responders().erase(this);
        ev().add("responder.deleted");
    }

    static U32 s_notification_counter = 0;

    LLNotification::LLNotification(const Params& p)
    {
        mName = p.name.value;
        ++s_notification_counter;
        mID.set(fmt("6e000000-0000-4000-8000-%012x", s_notification_counter));
        mPayload = p.payload.value;
        mSubstitutions = p.substitutions.value;
        LLSD form = notification_form(mName);
        if (form.isArray())
        {
            mForm = std::make_shared<LLNotificationForm>(form);
        }
        mFunctor = p.functor.function.value;
        mResponder = p.functor.responder.value;
    }

    void LLNotification::respond(const LLSD& response)
    {
        std::string selected;
        for (LLSD::map_const_iterator it = response.beginMap(); it != response.endMap(); ++it)
        {
            if (it->second.isBoolean() && it->second.asBoolean())
            {
                selected += (selected.empty() ? "" : ",") + it->first;
            }
        }
        bool live = LLNotifications::instance().find(mID) != nullptr;
        ev().add("notification.respond " + mName + " live=" + (live ? "1" : "0") + " selected=" + (selected.empty() ? std::string("-") : selected));
        snapshot_offers("respond");
        if (mResponder)
        {
            // offer notifications answer through the W6/W7 seam
            if (LLOfferInfo* offer = dynamic_cast<LLOfferInfo*>(mResponder.get()))
            {
                r4b_boundary::decisions().respond(offer, asLLSD(), response);
            }
            else
            {
                mResponder->handleRespond(asLLSD(), response);
            }
        }
        else if (mFunctor)
        {
            mFunctor(asLLSD(), response);
        }
    }

    LLSD LLNotification::asLLSD() const
    {
        LLSD sd;
        sd["name"] = mName;
        sd["id"] = mID;
        sd["form"] = mForm ? mForm->asLLSD() : LLSD::emptyArray();
        sd["payload"] = mPayload;
        sd["substitutions"] = mSubstitutions;
        return sd;
    }

    LLNotifications& LLNotifications::instance()
    {
        static LLNotifications n;
        return n;
    }

    LLNotificationPtr LLNotifications::find(LLUUID uuid)
    {
        auto it = mLive.find(uuid);
        return it == mLive.end() ? LLNotificationPtr() : it->second;
    }

    static std::string notify_line(const char* kind, const std::string& name, const LLSD& subs, const LLSD& payload)
    {
        return std::string(kind) + " " + name + " args=" + sd_bin_hex(subs) + " payload=" + sd_bin_hex(payload);
    }
    static std::string notify_note(const LLSD& subs, const LLSD& payload)
    {
        return "note:args " + sd_notation(subs) + " payload " + sd_notation(payload);
    }

    LLNotificationPtr LLNotificationsUtil::add(const std::string& name)
    {
        return add(name, LLSD(), LLSD());
    }

    LLNotificationPtr LLNotificationsUtil::add(const std::string& name, const LLSD& substitutions)
    {
        return add(name, substitutions, LLSD());
    }

    LLNotificationPtr LLNotificationsUtil::add(const std::string& name, const LLSD& substitutions, const LLSD& payload)
    {
        ev().add(notify_line("notify", name, substitutions, payload));
        ev().add(notify_note(substitutions, payload));
        LLNotification::Params p(name);
        p.substitutions = substitutions;
        p.payload = payload;
        return std::make_shared<LLNotification>(p);
    }

    LLNotificationPtr LLNotificationsUtil::add(const std::string& name, const LLSD& substitutions, const LLSD& payload,
                                               const ResponseFunctor& functor)
    {
        ev().add(notify_line("notify", name, substitutions, payload) + " functor=1");
        ev().add(notify_note(substitutions, payload));
        LLNotification::Params p(name);
        p.substitutions = substitutions;
        p.payload = payload;
        p.functor.function = functor;
        LLNotificationPtr n = std::make_shared<LLNotification>(p);
        LLNotifications::instance().mLive[n->id()] = n;
        world().pending.push_back({ name, payload, functor });
        return n;
    }

    void LLPostponedNotification::lookupName(const LLUUID& id, bool is_group)
    {
        if (world().defer_name_lookups)
        {
            ev().add("name.deferred postponed " + id.asString());
            LLAppViewer::instance()->mIdle.push_back([this, id, is_group]() { finishLookup(id, is_group); });
            return;
        }
        finishLookup(id, is_group);
    }

    void LLPostponedNotification::finishLookup(const LLUUID& id, bool is_group)
    {
        if (is_group)
        {
            std::string name;
            if (!gCacheName->getGroupName(id, name)) name = "Group " + id.asString().substr(0, 8);
            mName = name;
        }
        else
        {
            LLAvatarName av;
            if (!LLAvatarNameCache::get(id, &av)) av.mUserName = "resolved." + id.asString().substr(0, 8);
            mName = av.getUserName();
        }
        modifyNotificationParams();
        LLNotificationPtr n = std::make_shared<LLNotification>(mParams);
        LLNotifications::instance().mLive[n->id()] = n;
        ev().add(notify_line("notify.postponed", n->getName(), n->mSubstitutions, n->getPayload()) +
                 fmt(" from=%s group=%d offer_from_agent=%d responder=%d", id.asString().c_str(), (S32)is_group,
                          (S32)mParams.offer_from_agent.value, (S32)(bool)mParams.functor.responder.value));
        ev().add(notify_note(n->mSubstitutions, n->getPayload()));
        world().posted.push_back({ mParams, mName, n->id() });
        snapshot_offers("postponed");
        delete this;
    }

    namespace LLNotificationsUI
    {
        const LLUUID NOTIFICATION_CHANNEL_UUID("AEED1EF0-1111-4fbc-8E36-AB00B4B3B1A3");

        LLChannelManager* LLChannelManager::getInstance()
        {
            static LLChannelManager m;
            return &m;
        }

        void LLChannelManager::killToastsFromChannel(const LLUUID& channel_id, const LLScreenChannel::Matcher& matcher)
        {
            std::string killed;
            for (auto& p : LLNotifications::instance().mLive)
            {
                if (matcher.matches(p.second))
                {
                    killed += (killed.empty() ? "" : ",") + p.second->getName();
                }
            }
            ev().add("toasts.kill channel=" + channel_id.asString() + " matching=[" + killed + "]");
        }
    }

    // ------------------------------------------------------- template forms
    // The button elements of the notification forms the copies respond
    // through, in form order, frozen from skins/default/xui/en/notifications.xml
    // (usetemplate forms expanded). check_r4b_copies.py confirms the table.
    // R4B-FORMS-BEGIN
    // kind: the form element's tag; only buttons are runtime elements here
    struct FormButton { const char* notification; const char* kind; const char* name; S32 index; bool is_default; };
    static const FormButton FORMS[] = {
        { "ObjectGiveItem", "button", "Keep", 0, false },
        { "ObjectGiveItem", "button", "Discard", 1, false },
        { "ObjectGiveItem", "button", "Mute", 2, false },
        { "OwnObjectGiveItem", "button", "Keep", 0, false },
        { "OwnObjectGiveItem", "button", "Discard", 1, false },
        { "UserGiveItem", "button", "Show", 3, false },
        { "UserGiveItem", "button", "Keep", 0, false },
        { "UserGiveItem", "button", "Discard", 1, false },
        { "UserGiveItem", "button", "Mute", 2, false },
        { "UserGiveItemLegacy", "button", "Show", 3, false },
        { "UserGiveItemLegacy", "button", "Accept", 0, false },
        { "UserGiveItemLegacy", "button", "Discard", 1, false },
        { "UserGiveItemLegacy", "button", "ShowSilent", 6, false },
        { "UserGiveItemLegacy", "button", "AcceptSilent", 4, false },
        { "UserGiveItemLegacy", "button", "DiscardSilent", 5, false },
        { "UserGiveItemLegacy", "button", "Mute", 2, false },
        { "CannotCopyWarning", "button", "OK_okcancelbuttons", 0, true },
        { "CannotCopyWarning", "button", "Cancel_okcancelbuttons", 1, false },
        { "CannotCopyCountItems", "button", "OK_okcancelbuttons", 0, true },
        { "CannotCopyCountItems", "button", "Cancel_okcancelbuttons", 1, false },
        { "MoveInventoryFromObject", "button", "OK_okcancelignore", 0, true },
        { "MoveInventoryFromObject", "button", "Cancel_okcancelignore", 1, false },
        { "MoveInventoryFromObject", "ignore", "", -1, false },
        { "ShareItemsConfirmation", "button", "OK_okcancelignore", 0, true },
        { "ShareItemsConfirmation", "button", "Cancel_okcancelignore", 1, false },
        { "ShareItemsConfirmation", "ignore", "", -1, false },
    };
    // R4B-FORMS-END

    LLSD notification_form(const std::string& name)
    {
        LLSD form;
        for (const FormButton& b : FORMS)
        {
            if (name != b.notification || std::string(b.kind) != "button") continue;
            LLSD e;
            e["type"] = "button";
            e["name"] = b.name;
            e["index"] = b.index;
            e["text"] = b.name;
            if (b.is_default) e["default"] = true;
            form.append(e);
        }
        return form;
    }

    // ---------------------------------------------------- strings and SLURLs
    std::string LLTrans::getString(const std::string& key)
    {
        return "{" + key + "}";
    }

    std::string LLTrans::getString(const std::string& key, const LLStringUtil::format_map_t& args)
    {
        std::string out = "{" + key;
        for (const auto& a : args)
        {
            out += "|" + std::string(a.first) + "=" + std::string(a.second);
        }
        return out + "}";
    }

    void LLUIString::setArgs(const LLSD& args)
    {
        mString += " " + sd_notation(args);
    }

    LLSLURL::LLSLURL(const std::string& command, const LLUUID& id, const std::string& verb)
    {
        mString = "secondlife:///app/" + command + "/" + id.asString() + "/" + verb;
    }

    // ---------------------------------------------------------- viewer misc
    F64& fake_clock()
    {
        static F64 now = 1000.0;
        return now;
    }

    bool LLFrameTimer::hasExpired() const
    {
        return fake_clock() >= mExpiry;
    }

    bool LLFrameTimer::checkExpirationAndReset(F32 expiration)
    {
        if (hasExpired())
        {
            mExpiry = fake_clock() + expiration;
            return true;
        }
        return false;
    }

    EStartupState LLStartUp::getStartupState()
    {
        return world().startup_state;
    }

    LLAppViewer* LLAppViewer::instance()
    {
        static LLAppViewer app;
        return &app;
    }

    void LLAppViewer::addOnIdleCallback(const std::function<void()>& cb)
    {
        ev().add("idle.queue");
        mIdle.push_back(cb);
    }

    void LLAppViewer::postToMainCoro(const std::function<void()>& cb)
    {
        ev().add("main_coro.post");
        mIdle.push_back(cb);
    }

    void LLIMProcessing::processNewMessage(LLUUID from_id, bool from_group, LLUUID to_id, U8 offline, EInstantMessage dialog,
                                           LLUUID session_id, U32 timestamp, std::string agentName, std::string message,
                                           U32 parent_estate_id, LLUUID region_id, LLVector3 position, U8* binary_bucket,
                                           S32 binary_bucket_size, LLHost& sender, LLSD metadata, LLUUID aux_id)
    {
        ev().add(fmt("processNewMessage offline=%u dialog=%d from=%s group=%d session=%s aux=%s sender=%s size=%d bucket=",
                     (U32)offline, (S32)dialog, from_id.asString().c_str(), (S32)from_group, session_id.asString().c_str(),
                     aux_id.asString().c_str(), sender.getIPandPort().c_str(), binary_bucket_size) +
                 hex(binary_bucket, binary_bucket_size));
        // through the W6/W7 seam (the frozen implementation runs process_offer_im)
        r4b_boundary::decisions().receive_im(from_id, from_group, dialog, session_id, timestamp, agentName, message,
                                             binary_bucket, binary_bucket_size, sender, aux_id);
    }

    LLRecentPeople& LLRecentPeople::instance()
    {
        static LLRecentPeople r;
        return r;
    }

    bool LLRecentPeople::add(const LLUUID& id, const LLSD&)
    {
        ev().add("recent_people.add " + id.asString());
        return true;
    }

    void LLFirstUse::newInventory(bool)
    {
        ev().add("first_use.newInventory");
    }

    void LLFloaterReg::hideInstance(const std::string& name, const LLSD&)
    {
        ev().add("floater.hide " + name);
    }

    namespace FSCommon
    {
        void report_to_nearby_chat(const std::string& message)
        {
            ev().add("nearby_chat \"" + esc(message) + "\"");
        }
    }

    void make_ui_sound(const char* name)
    {
        ev().add(std::string("ui_sound ") + name);
    }

    std::string LLIMProcessing::getAutoresponseTextForAvatar(const LLUUID& from_id, bool is_do_not_disturb, bool is_autorespond,
                                                             bool is_autorespond_nonfriends, bool is_afk, bool send_away_response,
                                                             bool is_friend)
    {
        return gSavedPerAccountSettings.getString("DoNotDisturbModeResponse");
    }

    static LLIMMgr s_im_mgr;
    LLIMMgr* gIMMgr = &s_im_mgr;

    void LLIMMgr::addSystemMessage(const LLUUID& session_id, const std::string& message_name, const LLSD& args)
    {
        ev().add("im.system_message session=" + session_id.asString() + " " + message_name + " args=" + sd_bin_hex(args));
    }

    LLIMModel* LLIMModel::getInstance()
    {
        static LLIMModel m;
        return &m;
    }

    LLIMModel::LLIMSession* LLIMModel::findIMSession(const LLUUID& session_id) const
    {
        if (!open_sessions.count(session_id)) return nullptr;
        mSession.mName = "session " + session_id.asString().substr(0, 8);
        return &mSession;
    }

    bool LLIMModel::logToFile(const std::string& file_name, const std::string& from, const LLUUID& from_id, const std::string& line)
    {
        ev().add("im.log_to_file file=\"" + esc(file_name) + "\" from=\"" + esc(from) + "\" id=" + from_id.asString() +
                 " line=\"" + esc(line) + "\"");
        return true;
    }

    void LLPanelGroup::showNotice(const std::string& subject, const std::string& message, const LLUUID& group_id,
                                  bool has_inventory, const std::string& inventory_name, LLOfferInfo* inventory_offer)
    {
        ev().add(fmt("group_panel.showNotice group=%s has_inventory=%d inventory_name=\"%s\" offer=%d subject=\"%s\" message=\"%s\"",
                          group_id.asString().c_str(), (S32)has_inventory, esc(inventory_name).c_str(), (S32)(inventory_offer != nullptr),
                          esc(subject).c_str(), esc(message).c_str()));
        world().group_panel_offers[group_id] = inventory_offer;
        snapshot_offers("group_panel");
    }

    void notification_group_name_cb(const std::string& group_name, const std::string& sender, const std::string& subject,
                                    const std::string& message, const LLSD& payload, U32 timestamp)
    {
        ev().add(fmt("notify GroupNotice group_name=\"%s\" sender=\"%s\" subject=\"%s\" message=\"%s\" timestamp=%u payload=",
                          esc(group_name).c_str(), esc(sender).c_str(), esc(subject).c_str(), esc(message).c_str(), timestamp) +
                 sd_bin_hex(payload));
        ev().add("note:payload " + sd_notation(payload));
        world().group_notice_payload = payload;
    }

    // ----------------------------------------------------------------- HUD
    void LLHUDEffect::setSourceObject(FakeAvatar* avatar) { ev().add("hud.beam source=self"); }
    void LLHUDEffect::setTargetObject(LLViewerObject* object)
    {
        ev().add("hud.beam target=" + (object ? object->getID().asString() : std::string("null")));
    }
    void LLHUDEffect::setDuration(F32 duration) { ev().add(fmt("hud.beam duration=%g", duration)); }
    void LLHUDEffect::setColor(const LLColor4U& color) { ev().add("hud.beam color"); }

    LLHUDManager* LLHUDManager::getInstance()
    {
        static LLHUDManager m;
        return &m;
    }

    LLHUDEffect* LLHUDManager::createViewerEffect(S32 type, bool send_to_sim, bool originated_here)
    {
        ev().add(fmt("hud.createViewerEffect type=%d send_to_sim=%d", type, (S32)send_to_sim));
        static LLHUDEffectSpiral effect;
        return &effect;
    }

    void FakeFloaterTools::dirty() { ev().add("floater_tools.dirty"); }
    static FakeFloaterTools s_tools;
    FakeFloaterTools* gFloaterTools = &s_tools;

    // ---------------------------------------------------------------- RLVa
    FakeRlvHandler gRlvHandler;
    FakeRlvAttachmentLocks gRlvAttachmentLocks;

    bool FakeRlvHandler::isEnabled() { return world().rlv_enabled; }
    bool FakeRlvHandler::hasBehaviour(ERlvBehaviour b) const
    {
        switch (b)
        {
        case RLV_BHVR_UNSIT: return world().rlv_unsit;
        case RLV_BHVR_SITTP: return world().rlv_sittp;
        default: return false;
        }
    }
    bool FakeRlvAttachmentLocks::isLockedAttachment(const LLViewerObject* obj) const { return world().rlv_locked_attachment; }

    bool RlvActions::isRlvEnabled() { return world().rlv_enabled; }
    bool RlvActions::canShowName(EShowNamesContext, const LLUUID&) { return world().rlv_can_show_names; }
    bool RlvActions::canGiveInventory(const LLUUID& id) { return world().rlv_share_blocked.count(id) == 0; }
    bool RlvSettings::getForbidGiveToRLV() { return world().rlv_forbid_give_to_rlv; }

    RlvInventory& RlvInventory::instance()
    {
        static RlvInventory inv;
        return inv;
    }
    const LLUUID& RlvInventory::getSharedRootID() const { return world().rlv_shared_root; }
    LLViewerInventoryCategory* RlvInventory::getSharedRoot() const
    {
        static LLPointer<LLViewerInventoryCategory> root = new LLViewerInventoryCategory(LLUUID::null, LLUUID::null, LLFolderType::FT_NONE, "#RLV");
        return world().rlv_shared_root.notNull() ? root.get() : nullptr;
    }

    bool RlvUtil::isNearbyAgent(const LLUUID& id) { return world().rlv_nearby.count(id) != 0; }
    void RlvUtil::notifyBlocked(const std::string& strMsg, const LLSD& sdArgs, bool)
    {
        ev().add("rlv.notifyBlocked " + strMsg + " args=" + sd_bin_hex(sdArgs));
    }
    bool RlvUIEnabler::hasOpenProfile(const LLUUID&) { return false; }
    bool RlvUIEnabler::hasOpenIM(const LLUUID&) { return false; }
    std::string RlvStrings::getAnonym(const LLAvatarName& av_name) { return "anon(" + av_name.getUserName() + ")"; }
    std::string RlvStrings::getAnonym(const std::string& name) { return "anon(" + name + ")"; }

    namespace RlvStringKeys
    {
        namespace Blocked
        {
            const std::string Share = "blocked_share";
        }
    }

    void RlvBehaviourNotifyHandler::sendNotification(const std::string& strText, const std::string& strSuffix)
    {
        ev().add("rlv.sendNotification \"" + esc(strText) + "\"");
    }

    RlvGiveToRLVAgentOffer::RlvGiveToRLVAgentOffer(const LLUUID& idFolder) : LLInventoryFetchItemsObserver(idFolder)
    {
        ev().add("new RlvGiveToRLVAgentOffer " + idFolder.asString());
    }
    void RlvGiveToRLVAgentOffer::done() { ev().add("RlvGiveToRLVAgentOffer.done"); }

    RlvGiveToRLVTaskOffer::RlvGiveToRLVTaskOffer(const LLUUID& idTransaction)
    {
        ev().add("new RlvGiveToRLVTaskOffer " + idTransaction.asString());
    }

    LLWorld* LLWorld::getInstance()
    {
        static LLWorld w;
        return &w;
    }

    // ---------------------------------------------------------------- world
    World& world()
    {
        static World w;
        return w;
    }

    void World::reset()
    {
        *this = World();
    }

    void reset_fakes()
    {
        // release the previous case's notifications and offers silently
        ev().on = false;
        world().reset();
        gInventory.clear();
        LLMuteList::getInstance()->mMutes.clear();
        LLMuteList::getInstance()->mLegacyMutes.clear();
        LLNotifications::instance().mLive.clear();
        // offers nothing owns any more (handed to the group panel and never
        // answered, or abandoned by a case)
        while (!live_responders().empty())
        {
            delete *live_responders().begin();
        }
        LLAppViewer::instance()->mIdle.clear();
        LLAvatarNameCache::instance().names.clear();
        LLAvatarNameCache::instance().ids_by_legacy_name.clear();
        gCacheName->group_names.clear();
        LLIMModel::getInstance()->open_sessions.clear();
        gObjectList.objects.clear();
        gAgentWearables.worn.clear();
        LLGestureMgr::instance().active.clear();
        *gAgentAvatarp = FakeAvatar();
        gAgent = FakeAgent();
        gAgentID.setNull();
        gAgentSessionID.setNull();
        gSavedSettings = FakeSettings();
        gSavedPerAccountSettings = FakeSettings();
        gSavedPerAccountSettings.label = "gSavedPerAccountSettings";
        LLWorld::getInstance()->mMaxInventoryItemsTransfer = 42;
        gThrottleTimer.mExpiry = 0.0;
        fake_clock() = 1000.0;
        s_generate_counter = 0;
        s_notification_counter = 0;
        s_fake_msg.mFields.clear();
        ev().lines.clear();
        ev().on = true;
    }
}
