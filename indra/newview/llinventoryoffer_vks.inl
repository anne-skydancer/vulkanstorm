/**
 * @file llinventoryoffer_vks.inl
 * @brief The host and executor of the Rust inventory offer and give
 * decisions (vkstorm-ffi vks_inv_r4b_*, rung seven sub-rung R4b).
 *
 * Rust decides; this file answers its queries about the viewer's state and
 * carries out the actions it returns, in order, through the viewer's own
 * classes (the message system, notifications, the model, the mute list, the
 * name caches, the fetch observers, RLVa). It also holds the wrappers the
 * viewer calls in place of the replaced C++ (vks_r4b_*).
 *
 * One source, two builds. llinventoryoffer_vks.cpp includes it in the viewer;
 * the R4b oracle (tests/llinventory_offer_oracle_rust.cpp) includes it inside
 * namespace r4b, where the same unqualified names resolve to the oracle's
 * fakes, so the oracle replays its fixtures through exactly this executor.
 * The including file provides the declarations it uses and the few hooks
 * whose viewer and fake forms differ (below). The wrappers (vks_r4b_*, after
 * "the wrappers") have external linkage; llinventoryoffer_vks.h declares
 * them for the viewer.
 *
 * Hooks (static functions the including file defines before this file):
 *   F64 vks_r4b_frame_time();              the frame timer's clock
 *   F64 vks_r4b_now();                     LLDate::now() in seconds
 *   LLUUID vks_r4b_generate();             LLUUID::generate()
 *   bool vks_r4b_notification_form(const std::string& name, LLSD& form);
 *   void vks_r4b_children(const LLUUID& parent,
 *            std::vector<LLViewerInventoryCategory*>& cats,
 *            std::vector<LLViewerInventoryItem*>& items);   model order
 *   LLViewerObject* vks_r4b_avatar_root();
 *   VksInvR4bThrottle* vks_r4b_throttle(); the throttle's state
 *   void vks_r4b_throttle_sync(bool before); before and after every call
 *   LLInventoryObserver* vks_r4b_new_discard_agent_offer(const LLUUID& folder,
 *            const LLUUID& object);         new LLDiscardAgentOffer
 *   LLInventoryObserver* vks_r4b_new_open_task_group_offer();
 *   void vks_r4b_post_offer_notification(const LLNotification::Params& p,
 *            const LLUUID& from, bool is_group);
 *            LLPostponedNotification::add<LLPostponedOfferNotification>
 * and the declarations of the offer callbacks it binds
 * (inventory_offer_mute_callback, inventory_offer_mute_avatar_callback,
 * inventory_offer_name_callback, notification_group_name_cb,
 * notify_automute_callback).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

// ------------------------------------------------------------- conversions

static inline VksInvBytes vks_r4b_bytes(const std::string& s)
{
    VksInvBytes b;
    b.ptr = reinterpret_cast<const uint8_t*>(s.data());
    b.len = s.size();
    return b;
}

static inline std::string vks_r4b_string(const VksInvBytes& b)
{
    return b.len ? std::string(reinterpret_cast<const char*>(b.ptr), b.len) : std::string();
}

static inline LLUUID vks_r4b_uuid(const uint8_t* p)
{
    LLUUID id;
    if (p)
    {
        memcpy(id.mData, p, UUID_BYTES);
    }
    return id;
}

// Binary LLSD without a header; undefined is empty.
static std::string vks_r4b_sd_bytes(const LLSD& sd)
{
    if (sd.isUndefined())
    {
        return std::string();
    }
    std::ostringstream out;
    LLSDSerialize::toBinary(sd, out);
    return out.str();
}

static LLSD vks_r4b_sd(const VksInvBytes& b)
{
    LLSD sd;
    if (b.len)
    {
        std::istringstream in(std::string(reinterpret_cast<const char*>(b.ptr), b.len));
        LLSDSerialize::fromBinary(sd, in, (llssize)b.len);
    }
    return sd;
}

static VksInvNetHost vks_r4b_net_host(const LLHost& host)
{
    VksInvNetHost h;
    U32 ip = host.getAddress();
    memcpy(h.ip, &ip, 4);
    h.port = host.getPort();
    return h;
}

static LLHost vks_r4b_llhost(const VksInvNetHost& h)
{
    U32 ip;
    memcpy(&ip, h.ip, 4);
    return LLHost(ip, h.port);
}

// LLOfferInfo's fields (the strings borrow the offer's).
static VksInvOffer vks_r4b_offer(const LLOfferInfo& o)
{
    VksInvOffer v;
    v.im = (int32_t)o.mIM;
    memcpy(v.from_id, o.mFromID.mData, UUID_BYTES);
    v.from_group = o.mFromGroup;
    v.from_object = o.mFromObject;
    memcpy(v.transaction_id, o.mTransactionID.mData, UUID_BYTES);
    memcpy(v.folder_id, o.mFolderID.mData, UUID_BYTES);
    memcpy(v.object_id, o.mObjectID.mData, UUID_BYTES);
    v.type = (int32_t)o.mType;
    v.from_name = vks_r4b_bytes(o.mFromName);
    v.desc = vks_r4b_bytes(o.mDesc);
    v.host = vks_r4b_net_host(o.mHost);
    v.persist = o.mPersist;
    return v;
}

static void vks_r4b_set_offer(LLOfferInfo& o, const VksInvOffer& v)
{
    o.mIM = (EInstantMessage)v.im;
    o.mFromID = vks_r4b_uuid(v.from_id);
    o.mFromGroup = v.from_group;
    o.mFromObject = v.from_object;
    o.mTransactionID = vks_r4b_uuid(v.transaction_id);
    o.mFolderID = vks_r4b_uuid(v.folder_id);
    o.mObjectID = vks_r4b_uuid(v.object_id);
    o.mType = (LLAssetType::EType)v.type;
    o.mFromName = vks_r4b_string(v.from_name);
    o.mDesc = vks_r4b_string(v.desc);
    o.mHost = vks_r4b_llhost(v.host);
    o.mPersist = v.persist;
}

// An item record: the stored type, asset id, name and the item's own
// permissions (not the link-following viewer getters).
static VksInvR4bItem vks_r4b_item(const LLInventoryItem* item)
{
    VksInvR4bItem r;
    memcpy(r.id, item->getUUID().mData, UUID_BYTES);
    memcpy(r.parent, item->getParentUUID().mData, UUID_BYTES);
    r.type = (int32_t)item->getActualType();
    memcpy(r.asset, item->LLInventoryItem::getAssetUUID().mData, UUID_BYTES);
    r.name = vks_r4b_bytes(item->LLInventoryItem::getName());
    const LLPermissions& perm = item->LLInventoryItem::getPermissions();
    memcpy(&r.perms, &perm, sizeof(VksInvPerms));
    const LLViewerInventoryItem* vitem = dynamic_cast<const LLViewerInventoryItem*>(item);
    r.finished = vitem ? vitem->isFinished() : true;
    return r;
}

static VksInvR4bCat vks_r4b_cat(const LLInventoryCategory* cat)
{
    VksInvR4bCat r;
    memcpy(r.id, cat->getUUID().mData, UUID_BYTES);
    memcpy(r.parent, cat->getParentUUID().mData, UUID_BYTES);
    r.preferred = (int32_t)cat->getPreferredType();
    r.name = vks_r4b_bytes(cat->getName());
    return r;
}

// A category record for an inventory object that is not an item (drag cargo).
static VksInvR4bCat vks_r4b_object_as_cat(const LLInventoryObject* obj)
{
    if (const LLInventoryCategory* cat = dynamic_cast<const LLInventoryCategory*>(obj))
    {
        return vks_r4b_cat(cat);
    }
    VksInvR4bCat r;
    memcpy(r.id, obj->getUUID().mData, UUID_BYTES);
    memcpy(r.parent, obj->getParentUUID().mData, UUID_BYTES);
    r.preferred = (int32_t)LLFolderType::FT_NONE;
    r.name = vks_r4b_bytes(obj->getName());
    return r;
}

static VksInvR4bViewerObject vks_r4b_viewer_object(LLViewerObject* obj)
{
    VksInvR4bViewerObject r;
    memcpy(r.id, obj->getID().mData, UUID_BYTES);
    r.perm_modify = obj->permModify();
    r.flag_allow_inventory_add = obj->flagAllowInventoryAdd();
    r.perm_you_owner = obj->permYouOwner();
    r.pcode = (uint8_t)obj->getPCode();
    r.is_attachment = obj->isAttachment();
    LLViewerObject* root = obj->getRootEdit();
    memcpy(r.root, (root ? root->getID() : obj->getID()).mData, UUID_BYTES);
    return r;
}

// ------------------------------------------------------------- the call

// The state of one call into Rust: the current offer, the observers it made
// (handles), and what a give or drop call needs to carry out its actions.
struct VksR4bCall
{
    VksInvR4bHost host;
    LLOfferInfo* cur = nullptr;
    std::vector<std::pair<S32, LLInventoryObserver*> > observers;
    // move_inv_category_world_to_agent
    std::function<void(S32, void*, const LLMoveInv*)> move_callback;
    void* move_user_data = nullptr;
    LLInventoryFilter* filter = nullptr;
    LLInventoryObject::object_list_t object_inventory;

    explicit VksR4bCall(LLOfferInfo* offer = nullptr);
    ~VksR4bCall();
};

static void vks_r4b_out_uuid(VksInvR4bOut* out, const LLUUID& id)
{
    vks_inv_r4b_out_bytes(out, id.mData, UUID_BYTES);
}

static void vks_r4b_out_string(VksInvR4bOut* out, const std::string& s)
{
    vks_inv_r4b_out_bytes(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

static void vks_r4b_out_item(VksInvR4bOut* out, const LLInventoryItem* item)
{
    VksInvR4bItem r = vks_r4b_item(item);
    vks_inv_r4b_out_item(out, &r);
}

static void vks_r4b_out_cat(VksInvR4bOut* out, const LLInventoryCategory* cat)
{
    VksInvR4bCat r = vks_r4b_cat(cat);
    vks_inv_r4b_out_cat(out, &r);
}

static int64_t vks_r4b_query(void* ctx, const VksInvR4bQuery* q, VksInvR4bOut* out)
{
    VksR4bCall* call = static_cast<VksR4bCall*>(ctx);
    const LLUUID id = vks_r4b_uuid(q->id);
    switch (q->code)
    {
    case VKS_INV_R4B_Q_AGENT_ID:
        vks_r4b_out_uuid(out, gAgent.getID());
        return 1;
    case VKS_INV_R4B_Q_AGENT_SESSION_ID:
        vks_r4b_out_uuid(out, gAgent.getSessionID());
        return 1;
    case VKS_INV_R4B_Q_AGENT_GROUP_ID:
        vks_r4b_out_uuid(out, gAgent.getGroupID());
        return 1;
    case VKS_INV_R4B_Q_AGENT_POSITION:
    {
        LLVector3 p = gAgent.getPositionAgent();
        float v[3] = { p.mV[0], p.mV[1], p.mV[2] };
        vks_inv_r4b_out_bytes(out, reinterpret_cast<const uint8_t*>(v), sizeof(v));
        return 1;
    }
    case VKS_INV_R4B_Q_AGENT_REGION_HOST:
    {
        VksInvNetHost h = vks_r4b_net_host(gAgent.getRegionHost());
        vks_inv_r4b_out_bytes(out, reinterpret_cast<const uint8_t*>(&h), sizeof(h));
        return 1;
    }
    case VKS_INV_R4B_Q_IS_DO_NOT_DISTURB:
        return gAgent.isDoNotDisturb();
    case VKS_INV_R4B_Q_HAVE_AVATAR:
        return gAgentAvatarp != nullptr;
    case VKS_INV_R4B_Q_AVATAR_VALID:
        return isAgentAvatarValid();
    case VKS_INV_R4B_Q_AVATAR_FULLNAME:
        if (gAgentAvatarp)
        {
            vks_r4b_out_string(out, gAgentAvatarp->getFullname());
        }
        return 1;
    case VKS_INV_R4B_Q_IS_WEARING_ATTACHMENT:
        return gAgentAvatarp && gAgentAvatarp->isWearingAttachment(id);
    case VKS_INV_R4B_Q_IS_WEARING_ITEM:
        return gAgentWearables.isWearingItem(id);
    case VKS_INV_R4B_Q_IS_GESTURE_ACTIVE:
        return LLGestureMgr::instance().isGestureActive(id);
    case VKS_INV_R4B_Q_AVATAR_SITTING:
        return gAgentAvatarp && gAgentAvatarp->isSitting();
    case VKS_INV_R4B_Q_AVATAR_ROOT:
    {
        LLViewerObject* root = vks_r4b_avatar_root();
        if (!root)
        {
            return 0;
        }
        vks_r4b_out_uuid(out, root->getID());
        return 1;
    }
    case VKS_INV_R4B_Q_SETTING_BOOL:
        return gSavedSettings.getBOOL(vks_r4b_string(q->text));
    case VKS_INV_R4B_Q_SETTING_U32:
        return (int64_t)gSavedSettings.getU32(vks_r4b_string(q->text));
    case VKS_INV_R4B_Q_AUTORESPONSE_TEXT:
        vks_r4b_out_string(out, LLIMProcessing::getAutoresponseTextForAvatar(id, true, false, false, false, false, false));
        return 1;
    case VKS_INV_R4B_Q_TRANS:
    {
        const std::string key = vks_r4b_string(q->text);
        if (!q->list_len)
        {
            vks_r4b_out_string(out, LLTrans::getString(key));
            return 1;
        }
        LLStringUtil::format_map_t args;
        for (size_t i = 0; i + 1 < q->list_len; i += 2)
        {
            args[vks_r4b_string(q->list[i])] = vks_r4b_string(q->list[i + 1]);
        }
        vks_r4b_out_string(out, LLTrans::getString(key, args));
        return 1;
    }
    case VKS_INV_R4B_Q_SLURL_LOCATION:
        vks_r4b_out_string(out, LLSLURL(vks_r4b_string(q->text)).getLocationString());
        return 1;
    case VKS_INV_R4B_Q_RLV_ANONYM:
        vks_r4b_out_string(out, RlvStrings::getAnonym(vks_r4b_string(q->text)));
        return 1;
    case VKS_INV_R4B_Q_RLV_ANONYM_AVATAR:
    {
        LLAvatarName av_name;
        if (!LLAvatarNameCache::get(id, &av_name))
        {
            return 0;
        }
        vks_r4b_out_string(out, RlvStrings::getAnonym(av_name));
        return 1;
    }
    case VKS_INV_R4B_Q_SLURL:
        vks_r4b_out_string(out, LLSLURL(vks_r4b_string(q->text), id, vks_r4b_string(q->text2)).getSLURLString());
        return 1;
    case VKS_INV_R4B_Q_APP_TITLE:
        vks_r4b_out_string(out, LLAppViewer::instance()->getSecondLifeTitle());
        return 1;
    case VKS_INV_R4B_Q_TRIM_RESIDENT_SURNAME:
        return LLAvatarName::trimResidentSurname();
    case VKS_INV_R4B_Q_RLV_ENABLED:
        return RlvActions::isRlvEnabled();
    case VKS_INV_R4B_Q_RLV_CAN_SHOW_NAME:
        return RlvActions::canShowName(RlvActions::SNC_DEFAULT, id);
    case VKS_INV_R4B_Q_RLV_CAN_GIVE_INVENTORY:
        return RlvActions::canGiveInventory(id);
    case VKS_INV_R4B_Q_RLV_IS_NEARBY:
        return RlvUtil::isNearbyAgent(id);
    case VKS_INV_R4B_Q_RLV_HAS_OPEN_IM:
        return RlvUIEnabler::hasOpenIM(id);
    case VKS_INV_R4B_Q_RLV_HAS_OPEN_PROFILE:
        return RlvUIEnabler::hasOpenProfile(id);
    case VKS_INV_R4B_Q_RLV_FORBID_GIVE_TO_RLV:
        return RlvSettings::getForbidGiveToRLV();
    case VKS_INV_R4B_Q_RLV_HAS_SHARED_ROOT:
        return RlvInventory::instance().getSharedRoot() != nullptr;
    case VKS_INV_R4B_Q_RLV_SHARED_ROOT_ID:
        vks_r4b_out_uuid(out, RlvInventory::instance().getSharedRootID());
        return 1;
    case VKS_INV_R4B_Q_RLV_LOCKED_ATTACHMENT:
        return gRlvAttachmentLocks.isLockedAttachment(gObjectList.findObject(id));
    case VKS_INV_R4B_Q_RLV_HAS_BEHAVIOUR:
        return gRlvHandler.hasBehaviour(q->number == 0 ? RLV_BHVR_UNSIT : RLV_BHVR_SITTP);
    case VKS_INV_R4B_Q_IS_MUTED:
        return LLMuteList::getInstance()->isMuted(id, vks_r4b_string(q->text), (U32)q->number);
    case VKS_INV_R4B_Q_AVATAR_USER_NAME:
    case VKS_INV_R4B_Q_AVATAR_COMPLETE_NAME:
    {
        LLAvatarName av_name;
        if (!LLAvatarNameCache::get(id, &av_name))
        {
            return 0;
        }
        vks_r4b_out_string(out, q->code == VKS_INV_R4B_Q_AVATAR_USER_NAME ? av_name.getUserName() : av_name.getCompleteName());
        return 1;
    }
    case VKS_INV_R4B_Q_GROUP_NAME:
    {
        std::string name;
        if (!gCacheName->getGroupName(id, name))
        {
            return 0;
        }
        vks_r4b_out_string(out, name);
        return 1;
    }
    case VKS_INV_R4B_Q_FIND_ID_BY_NAME:
        vks_r4b_out_uuid(out, LLAvatarNameCache::getInstance()->findIdByName(vks_r4b_string(q->text)));
        return 1;
    case VKS_INV_R4B_Q_NOTIFICATION_LIVE:
        return LLNotifications::instance().find(id) != nullptr;
    case VKS_INV_R4B_Q_NOTIFICATION_FORM:
    {
        LLSD form;
        if (!vks_r4b_notification_form(vks_r4b_string(q->text), form))
        {
            return 0;
        }
        vks_r4b_out_string(out, vks_r4b_sd_bytes(form));
        return 1;
    }
    case VKS_INV_R4B_Q_ITEM:
    {
        LLViewerInventoryItem* item = gInventory.getItem(id);
        if (!item)
        {
            return 0;
        }
        vks_r4b_out_item(out, item);
        return 1;
    }
    case VKS_INV_R4B_Q_CATEGORY:
    {
        LLViewerInventoryCategory* cat = gInventory.getCategory(id);
        if (!cat)
        {
            return 0;
        }
        vks_r4b_out_cat(out, cat);
        return 1;
    }
    case VKS_INV_R4B_Q_CHILDREN:
    {
        std::vector<LLViewerInventoryCategory*> cats;
        std::vector<LLViewerInventoryItem*> items;
        vks_r4b_children(id, cats, items);
        for (LLViewerInventoryCategory* cat : cats)
        {
            vks_r4b_out_cat(out, cat);
        }
        for (LLViewerInventoryItem* item : items)
        {
            vks_r4b_out_item(out, item);
        }
        return 1;
    }
    case VKS_INV_R4B_Q_IS_CATEGORY_COMPLETE:
        return gInventory.isCategoryComplete(id);
    case VKS_INV_R4B_Q_FIND_CATEGORY_FOR_TYPE:
        vks_r4b_out_uuid(out, gInventory.findCategoryUUIDForType((LLFolderType::EType)q->number));
        return 1;
    case VKS_INV_R4B_Q_GENERATE_UUID:
        vks_r4b_out_uuid(out, vks_r4b_generate());
        return 1;
    case VKS_INV_R4B_Q_NOW:
    case VKS_INV_R4B_Q_FRAME_TIME:
    {
        double t = q->code == VKS_INV_R4B_Q_NOW ? vks_r4b_now() : vks_r4b_frame_time();
        vks_inv_r4b_out_bytes(out, reinterpret_cast<const uint8_t*>(&t), sizeof(t));
        return 1;
    }
    case VKS_INV_R4B_Q_STARTUP_STARTED:
        return LLStartUp::getStartupState() >= STATE_STARTED;
    case VKS_INV_R4B_Q_IM_SESSION_NAME:
    {
        LLIMModel::LLIMSession* session = LLIMModel::instance().findIMSession(id);
        if (!session)
        {
            return 0;
        }
        vks_r4b_out_string(out, session->mName);
        return 1;
    }
    case VKS_INV_R4B_Q_MAX_ITEMS_TRANSFER:
        return LLWorld::getInstance()->getMaxInventoryItemsTransfer();
    case VKS_INV_R4B_Q_OBJECT:
    {
        LLViewerObject* obj = gObjectList.findObject(id);
        if (!obj)
        {
            return 0;
        }
        VksInvR4bViewerObject r = vks_r4b_viewer_object(obj);
        vks_inv_r4b_out_object(out, &r);
        return 1;
    }
    case VKS_INV_R4B_Q_OBJECT_INVENTORY:
    {
        call->object_inventory.clear();
        if (LLViewerObject* obj = gObjectList.findObject(id))
        {
            obj->getInventoryContents(call->object_inventory);
        }
        for (const LLPointer<LLInventoryObject>& o : call->object_inventory)
        {
            VksInvR4bTaskObject r;
            memcpy(r.id, o->getUUID().mData, UUID_BYTES);
            r.type = (int32_t)o->getType();
            const LLInventoryItem* item = dynamic_cast<const LLInventoryItem*>(o.get());
            r.has_perms = item != nullptr;
            if (item)
            {
                const LLPermissions& perm = item->getPermissions();
                memcpy(&r.perms, &perm, sizeof(VksInvPerms));
            }
            else
            {
                memset(&r.perms, 0, sizeof(r.perms));
            }
            vks_inv_r4b_out_task_object(out, &r);
        }
        return 1;
    }
    case VKS_INV_R4B_Q_HAVE_FLOATER_TOOLS:
        return gFloaterTools != nullptr;
    case VKS_INV_R4B_Q_OBSERVER_NEW:
    {
        const LLUUID id2 = vks_r4b_uuid(q->id2);
        LLInventoryObserver* o = nullptr;
        switch (q->number)
        {
        case VKS_INV_R4B_OBS_FETCH_ITEMS:
            o = new LLInventoryFetchItemsObserver(id);
            break;
        case VKS_INV_R4B_OBS_OPEN_AGENT_OFFER:
            o = new LLOpenAgentOffer(id, vks_r4b_string(q->text), q->flag);
            break;
        case VKS_INV_R4B_OBS_DISCARD_AGENT_OFFER:
            o = vks_r4b_new_discard_agent_offer(id, id2);
            break;
        case VKS_INV_R4B_OBS_RLV_AGENT_OFFER:
            o = new RlvGiveToRLVAgentOffer(id);
            break;
        case VKS_INV_R4B_OBS_RLV_TASK_OFFER:
            o = new RlvGiveToRLVTaskOffer(id);
            break;
        default:
            o = vks_r4b_new_open_task_group_offer();
            break;
        }
        call->observers.push_back(std::make_pair(q->number, o));
        return (int64_t)call->observers.size() - 1;
    }
    case VKS_INV_R4B_Q_OBSERVER_START:
    {
        const std::pair<S32, LLInventoryObserver*>& o = call->observers.at(q->number);
        switch (o.first)
        {
        case VKS_INV_R4B_OBS_FETCH_ITEMS:
        case VKS_INV_R4B_OBS_OPEN_AGENT_OFFER:
        case VKS_INV_R4B_OBS_DISCARD_AGENT_OFFER:
        case VKS_INV_R4B_OBS_RLV_AGENT_OFFER:
        {
            LLInventoryFetchItemsObserver* f = static_cast<LLInventoryFetchItemsObserver*>(o.second);
            f->startFetch();
            return f->isFinished();
        }
        default:
            return 1;
        }
    }
    case VKS_INV_R4B_Q_FILTER_CHECK:
    {
        if (!call->filter || q->number < 0 || (size_t)q->number >= call->object_inventory.size())
        {
            return 0;
        }
        LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(std::next(call->object_inventory.begin(), q->number)->get());
        return call->filter->check(item);
    }
    default:
        return 0;
    }
}

// The name lookups' callbacks (VKS_INV_R4B_THEN_*).
void vks_r4b_share_confirmation(const std::string& name, const LLUUID& item_id, const std::string& item_name,
                                const std::string& dest, const LLUUID& dest_agent);

static void vks_r4b_name_lookup(const VksInvR4bAction* a)
{
    const LLUUID id = vks_r4b_uuid(a->id[0]);
    switch (a->number[1])
    {
    case VKS_INV_R4B_THEN_OFFER_MUTE_AVATAR:
        LLAvatarNameCache::get(id, boost::bind(&inventory_offer_mute_avatar_callback, _1, _2));
        break;
    case VKS_INV_R4B_THEN_OFFER_MUTE_GROUP:
        gCacheName->getGroup(id, boost::bind(&inventory_offer_mute_callback, _1, _2, _3));
        break;
    case VKS_INV_R4B_THEN_SYSTEM_MESSAGE_TIP:
        LLAvatarNameCache::instance().get(id, boost::bind(&inventory_offer_name_callback, _2,
                                                          vks_r4b_string(a->text[1]), vks_r4b_string(a->text[2])));
        break;
    case VKS_INV_R4B_THEN_GROUP_NOTICE:
        gCacheName->get(id, a->kind == VKS_INV_R4B_A_CACHE_NAME_GET ? a->flag[0] : true,
                        boost::bind(&notification_group_name_cb, _2, vks_r4b_string(a->text[1]), vks_r4b_string(a->text[2]),
                                    vks_r4b_string(a->text[3]), vks_r4b_sd(a->sd[1]), (U32)a->number[2]));
        break;
    case VKS_INV_R4B_THEN_SHARE_CONFIRMATION:
    {
        const LLUUID item_id = vks_r4b_uuid(a->id[1]);
        const std::string item_name = vks_r4b_string(a->text[1]);
        const std::string dest = vks_r4b_string(a->text[2]);
        LLAvatarNameCache::get(id, [item_id, item_name, dest](const LLUUID& agent, const LLAvatarName& av_name)
            { vks_r4b_share_confirmation(av_name.getUserName(), item_id, item_name, dest, agent); });
        break;
    }
    case VKS_INV_R4B_THEN_AUTO_UNMUTE_NOTICE:
        LLAvatarNameCache::get(id, boost::bind(&notify_automute_callback, _1, _2, (LLMuteList::EAutoReason)a->number[2]));
        break;
    default:
        break;
    }
}

bool vks_r4b_handler_answer(S32 handler, const LLSD& notification, const LLSD& response);

static std::shared_ptr<LLMoveInv> vks_r4b_move(const VksR4bCall* call, const VksInvR4bMove* m)
{
    std::shared_ptr<LLMoveInv> move_inv = std::make_shared<LLMoveInv>();
    move_inv->mObjectID = vks_r4b_uuid(m->object_id);
    move_inv->mCategoryID = vks_r4b_uuid(m->category_id);
    move_inv->mCallback = call->move_callback;
    move_inv->mUserData = call->move_user_data;
    for (size_t i = 0; i < m->count; ++i)
    {
        two_uuids_t two(vks_r4b_uuid(m->pairs + 32 * i), vks_r4b_uuid(m->pairs + 32 * i + 16));
        move_inv->mMoveList.push_back(two);
    }
    return move_inv;
}

// Adds a string field with the exact data Rust built (see VksInvR4bIm).
static void vks_r4b_add_string_field(LLMessageSystem* msg, const char* var, const VksInvBytes& data)
{
    if (!data.len)
    {
        msg->addStringFast(var, std::string());
    }
    else if (data.len == 1)
    {
        msg->addStringFast(var, "");
    }
    else
    {
        msg->addStringFast(var, std::string(reinterpret_cast<const char*>(data.ptr), data.len - 1));
    }
}

// The size of the bucket buffer processNewMessage is called with when it is
// not process_improved_im's U8[MTUBYTES] (0): the offline loop's vector, set
// around the call it posts (main coroutine only).
static size_t s_vks_r4b_bucket_capacity = 0;

static void vks_r4b_act(void* ctx, const VksInvR4bAction* a)
{
    VksR4bCall* call = static_cast<VksR4bCall*>(ctx);
    switch (a->kind)
    {
    case VKS_INV_R4B_A_OFFER_NEW:
        call->cur = new LLOfferInfo();
        vks_r4b_set_offer(*call->cur, *a->offer);
        break;
    case VKS_INV_R4B_A_OFFER_UPDATE:
        if (call->cur)
        {
            vks_r4b_set_offer(*call->cur, *a->offer);
        }
        break;
    case VKS_INV_R4B_A_OFFER_DELETE:
        delete call->cur;
        call->cur = nullptr;
        break;
    case VKS_INV_R4B_A_SEND_RELIABLE:
    {
        const VksInvR4bIm& im = *a->im;
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_ImprovedInstantMessage);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, vks_r4b_uuid(im.agent_id));
        msg->addUUIDFast(_PREHASH_SessionID, vks_r4b_uuid(im.session_id));
        msg->nextBlockFast(_PREHASH_MessageBlock);
        msg->addBOOLFast(_PREHASH_FromGroup, im.from_group);
        msg->addUUIDFast(_PREHASH_ToAgentID, vks_r4b_uuid(im.to_agent_id));
        msg->addU8Fast(_PREHASH_Offline, im.offline);
        msg->addUUIDFast(_PREHASH_ID, vks_r4b_uuid(im.id));
        msg->addU32Fast(_PREHASH_Timestamp, im.timestamp);
        vks_r4b_add_string_field(msg, _PREHASH_FromAgentName, im.from_agent_name);
        vks_r4b_add_string_field(msg, _PREHASH_Message, im.message);
        msg->addU32Fast(_PREHASH_ParentEstateID, im.parent_estate_id);
        msg->addUUIDFast(_PREHASH_RegionID, vks_r4b_uuid(im.region_id));
        msg->addVector3Fast(_PREHASH_Position, LLVector3(im.position[0], im.position[1], im.position[2]));
        msg->addU8Fast(_PREHASH_Dialog, (U8)im.dialog);
        msg->addBinaryDataFast(_PREHASH_BinaryBucket, im.bucket.ptr, (S32)im.bucket.len);
        if (a->number[0] == 0)
        {
            msg->sendReliable(vks_r4b_llhost(a->host));
        }
        else
        {
            gAgent.sendReliableMessage();
        }
        break;
    }
    case VKS_INV_R4B_A_PACK_IM:
    {
        const VksInvR4bIm& im = *a->im;
        pack_instant_message(gMessageSystem, vks_r4b_uuid(im.agent_id), im.from_group, vks_r4b_uuid(im.session_id),
                             vks_r4b_uuid(im.to_agent_id), vks_r4b_string(im.from_agent_name), vks_r4b_string(im.message),
                             im.offline, (EInstantMessage)im.dialog, vks_r4b_uuid(im.id), im.parent_estate_id,
                             vks_r4b_uuid(im.region_id), LLVector3(im.position[0], im.position[1], im.position[2]),
                             im.timestamp, im.has_bucket ? im.bucket.ptr : nullptr, im.has_bucket ? (S32)im.bucket.len : 0);
        break;
    }
    case VKS_INV_R4B_A_SEND_PACKED_TO_REGION:
        gAgent.sendReliableMessage();
        break;
    case VKS_INV_R4B_A_NOTIFY:
    {
        const std::string name = vks_r4b_string(a->text[0]);
        const LLSD subs = vks_r4b_sd(a->sd[0]);
        const LLSD payload = vks_r4b_sd(a->sd[1]);
        const S32 handler = a->number[0];
        if (handler == VKS_INV_R4B_H_NONE)
        {
            LLNotificationsUtil::add(name, subs, payload);
        }
        else
        {
            LLNotificationsUtil::add(name, subs, payload, [handler](const LLSD& notification, const LLSD& response)
                { vks_r4b_handler_answer(handler, notification, response); });
        }
        break;
    }
    case VKS_INV_R4B_A_NOTIFY_POSTPONED:
    {
        LLNotification::Params p;
        if (a->flag[0])
        {
            p.responder = call->cur;
        }
        p.substitutions(vks_r4b_sd(a->sd[0])).payload(vks_r4b_sd(a->sd[1])).functor.responder(LLNotificationResponderPtr(call->cur));
        p.name = vks_r4b_string(a->text[0]);
        if (a->flag[0])
        {
            p.offer_from_agent = true;
        }
        vks_r4b_post_offer_notification(p, vks_r4b_uuid(a->id[0]), a->flag[1]);
        break;
    }
    case VKS_INV_R4B_A_FORCE_RESPOND:
    {
        // LLNotifications::forceResponse's temporary notification, answered
        // without a functor: the decision runs the callback itself.
        LLNotificationPtr temp = std::make_shared<LLNotification>(LLNotification::Params(vks_r4b_string(a->text[0])));
        temp->respond(vks_r4b_sd(a->sd[0]));
        break;
    }
    case VKS_INV_R4B_A_AVATAR_NAME_LOOKUP:
    case VKS_INV_R4B_A_GROUP_NAME_LOOKUP:
    case VKS_INV_R4B_A_CACHE_NAME_GET:
        vks_r4b_name_lookup(a);
        break;
    case VKS_INV_R4B_A_OBSERVER_DONE:
    {
        const std::pair<S32, LLInventoryObserver*>& o = call->observers.at(a->number[0]);
        static_cast<LLInventoryFetchItemsObserver*>(o.second)->done();
        break;
    }
    case VKS_INV_R4B_A_OBSERVER_ADD:
        gInventory.addObserver(call->observers.at(a->number[0]).second);
        break;
    case VKS_INV_R4B_A_OBSERVER_DROP:
        delete call->observers.at(a->number[0]).second;
        break;
    case VKS_INV_R4B_A_RECENT_PEOPLE_ADD:
        LLRecentPeople::instance().add(vks_r4b_uuid(a->id[0]));
        break;
    case VKS_INV_R4B_A_FIRST_USE_NEW_INVENTORY:
        LLFirstUse::newInventory();
        break;
    case VKS_INV_R4B_A_NEARBY_CHAT:
        FSCommon::report_to_nearby_chat(vks_r4b_string(a->text[0]));
        break;
    case VKS_INV_R4B_A_UI_SOUND:
        make_ui_sound(vks_r4b_string(a->text[0]).c_str());
        break;
    case VKS_INV_R4B_A_INVENTORY_PANEL_SELECT:
        LLInventoryPanel::openInventoryPanelAndSetSelection(a->flag[0], vks_r4b_uuid(a->id[0]));
        break;
    case VKS_INV_R4B_A_GROUP_PANEL_SHOW_NOTICE:
        LLPanelGroup::showNotice(vks_r4b_string(a->text[0]), vks_r4b_string(a->text[1]), vks_r4b_uuid(a->id[0]), a->flag[0],
                                 vks_r4b_string(a->text[2]), a->flag[1] ? call->cur : nullptr);
        break;
    case VKS_INV_R4B_A_RLV_SEND_NOTIFICATION:
        RlvBehaviourNotifyHandler::sendNotification(vks_r4b_string(a->text[0]));
        break;
    case VKS_INV_R4B_A_RLV_NOTIFY_BLOCKED_SHARE:
        RlvUtil::notifyBlocked(RlvStringKeys::Blocked::Share, vks_r4b_sd(a->sd[0]));
        break;
    case VKS_INV_R4B_A_POST_TO_MAIN_CORO:
    {
        const VksInvR4bIm& im = *a->im;
        const LLUUID from_id = vks_r4b_uuid(im.agent_id);
        const bool from_group = im.from_group;
        const LLUUID to_id = vks_r4b_uuid(im.to_agent_id);
        const U8 offline = im.offline;
        const EInstantMessage dialog = (EInstantMessage)im.dialog;
        const LLUUID session_id = vks_r4b_uuid(im.session_id);
        const U32 timestamp = im.timestamp;
        const std::string name = vks_r4b_string(im.from_agent_name);
        const std::string message = vks_r4b_string(im.message);
        const U32 parent_estate_id = im.parent_estate_id;
        const LLUUID region_id = vks_r4b_uuid(im.region_id);
        const LLVector3 position(im.position[0], im.position[1], im.position[2]);
        // processNewMessage reads at least MTUBYTES bytes of the bucket's
        // buffer (see vks_r4b_receive_im): the vector holds the bucket and
        // zeros up to that size; the size passed is the bucket's.
        std::vector<U8> bin_bucket(im.bucket.ptr, im.bucket.ptr + im.bucket.len);
        const S32 bucket_size = (S32)bin_bucket.size();
        if (bin_bucket.size() < (size_t)MTUBYTES)
        {
            bin_bucket.resize(MTUBYTES, 0);
        }
        const LLHost sender = vks_r4b_llhost(a->host);
        const LLSD metadata = vks_r4b_sd(a->sd[0]);
        const LLUUID aux_id = vks_r4b_uuid(a->id[0]);
        LLAppViewer::instance()->postToMainCoro([=]()
            {
                std::vector<U8> local_bin_bucket = bin_bucket;
                LLHost local_sender = sender;
                const size_t outer = s_vks_r4b_bucket_capacity;
                s_vks_r4b_bucket_capacity = local_bin_bucket.size();
                LLIMProcessing::processNewMessage(from_id, from_group, to_id, offline, dialog, session_id, timestamp, name,
                                                  message, parent_estate_id, region_id, position, local_bin_bucket.data(),
                                                  bucket_size, local_sender, metadata, aux_id);
                s_vks_r4b_bucket_capacity = outer;
            });
        break;
    }
    case VKS_INV_R4B_A_HUD_BEAM:
    {
        LLHUDEffectSpiral* effectp = (LLHUDEffectSpiral*)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_BEAM, true);
        effectp->setSourceObject(gAgentAvatarp);
        effectp->setTargetObject(gObjectList.findObject(vks_r4b_uuid(a->id[0])));
        effectp->setDuration(LL_HUD_DUR_SHORT);
        effectp->setColor(gAgent.getEffectColor());
        break;
    }
    case VKS_INV_R4B_A_FLOATER_TOOLS_DIRTY:
        gFloaterTools->dirty();
        break;
    case VKS_INV_R4B_A_MUTE_AUTO_REMOVE:
        LLMuteList::getInstance()->autoRemove(vks_r4b_uuid(a->id[0]), (LLMuteList::EAutoReason)a->number[0]);
        break;
    case VKS_INV_R4B_A_IM_SYSTEM_MESSAGE:
        gIMMgr->addSystemMessage(vks_r4b_uuid(a->id[0]), vks_r4b_string(a->text[0]), vks_r4b_sd(a->sd[0]));
        break;
    case VKS_INV_R4B_A_IM_LOG_TO_FILE:
    {
        LLUIString message(vks_r4b_string(a->text[2]));
        message.setArgs(vks_r4b_sd(a->sd[0]));
        LLIMModel::instance().logToFile(vks_r4b_string(a->text[0]), vks_r4b_string(a->text[1]), vks_r4b_uuid(a->id[0]),
                                        message.getString());
        break;
    }
    case VKS_INV_R4B_A_DELETE_OBJECT:
        gInventory.deleteObject(vks_r4b_uuid(a->id[0]));
        break;
    case VKS_INV_R4B_A_NOTIFY_OBSERVERS:
        gInventory.notifyObservers();
        break;
    case VKS_INV_R4B_A_FLOATER_HIDE:
        LLFloaterReg::hideInstance(vks_r4b_string(a->text[0]));
        break;
    case VKS_INV_R4B_A_WARN_MOVE_INVENTORY:
    {
        std::shared_ptr<LLMoveInv> move_inv = vks_r4b_move(call, a->move);
        warn_move_inventory(gObjectList.findObject(move_inv->mObjectID), move_inv);
        break;
    }
    case VKS_INV_R4B_A_MOVE_TASK_INVENTORY:
        move_task_inventory_callback(vks_r4b_sd(a->sd[0]), vks_r4b_sd(a->sd[1]), vks_r4b_move(call, a->move));
        break;
    case VKS_INV_R4B_A_LOG:
    {
        // the C++ log lines, level and tag as they were ("Messaging" or none)
        const std::string message = vks_r4b_string(a->text[1]);
        const bool messaging = vks_r4b_string(a->text[0]) == "Messaging";
        switch (a->number[0])
        {
        case 0:
            if (messaging)
                LL_DEBUGS("Messaging") << message << LL_ENDL;
            else
                LL_DEBUGS() << message << LL_ENDL;
            break;
        case 1:
            if (messaging)
                LL_INFOS("Messaging") << message << LL_ENDL;
            else
                LL_INFOS() << message << LL_ENDL;
            break;
        default:
            if (messaging)
                LL_WARNS("Messaging") << message << LL_ENDL;
            else
                LL_WARNS() << message << LL_ENDL;
            break;
        }
        break;
    }
    default:
        break;
    }
}

VksR4bCall::VksR4bCall(LLOfferInfo* offer) : cur(offer)
{
    host.ctx = this;
    host.query = &vks_r4b_query;
    host.act = &vks_r4b_act;
    host.throttle = vks_r4b_throttle();
    vks_r4b_throttle_sync(true);
}

VksR4bCall::~VksR4bCall()
{
    vks_r4b_throttle_sync(false);
}

// ------------------------------------------------------------ the wrappers

// processNewMessage's case for IM_INVENTORY_OFFERED, IM_TASK_INVENTORY_OFFERED,
// IM_GROUP_NOTICE and IM_GROUP_NOTICE_REQUESTED, from its raw arguments.
// `bucket` points at process_improved_im's U8[MTUBYTES], or at the offline
// loop's vector, which the executor pads with zeros to at least MTUBYTES and
// whose size it notes for the call (VKS_INV_R4B_A_POST_TO_MAIN_CORO). Rust
// reads the bucket within that buffer, as the C++ did (the legacy task bucket
// is a C string, read past the bucket's size up to a NUL). Returns false
// where the C++ case returned from processNewMessage instead of breaking.
bool vks_r4b_receive_im(const LLUUID& from_id, bool from_group, EInstantMessage dialog, const LLUUID& session_id,
                        U32 timestamp, const std::string& name, const std::string& message, const U8* bucket,
                        S32 bucket_size, const LLHost& sender, const LLUUID& aux_id)
{
    VksR4bCall call;
    const size_t buffer_len = !bucket ? 0 : s_vks_r4b_bucket_capacity ? s_vks_r4b_bucket_capacity : (size_t)MTUBYTES;
    const VksInvNetHost host = vks_r4b_net_host(sender);
    return vks_inv_r4b_receive_im(&call.host, from_id.mData, from_group, (int32_t)dialog, session_id.mData, timestamp,
                                  reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                                  reinterpret_cast<const uint8_t*>(message.data()), message.size(), bucket, buffer_len,
                                  bucket_size, &host, aux_id.mData);
}

// The ReadOfflineMsgs loop over its validated messages array.
void vks_r4b_receive_offline(const LLSD& messages)
{
    VksR4bCall call;
    const std::string sd = vks_r4b_sd_bytes(messages);
    vks_inv_r4b_receive_offline(&call.host, reinterpret_cast<const uint8_t*>(sd.data()), sd.size());
}

void vks_r4b_respond(LLOfferInfo* offer, const LLSD& notification, const LLSD& response)
{
    VksR4bCall call(offer);
    const VksInvOffer o = vks_r4b_offer(*offer);
    const std::string n = vks_r4b_sd_bytes(notification);
    const std::string r = vks_r4b_sd_bytes(response);
    vks_inv_r4b_respond(&call.host, &o, reinterpret_cast<const uint8_t*>(n.data()), n.size(),
                        reinterpret_cast<const uint8_t*>(r.data()), r.size());
}

void vks_r4b_force_response(LLOfferInfo* offer, S32 response)
{
    VksR4bCall call(offer);
    const VksInvOffer o = vks_r4b_offer(*offer);
    vks_inv_r4b_force_response(&call.host, &o, response);
}

void vks_r4b_send_receive_response(LLOfferInfo* offer, bool accept, const LLUUID& folder)
{
    VksR4bCall call(offer);
    const VksInvOffer o = vks_r4b_offer(*offer);
    vks_inv_r4b_send_receive_response(&call.host, &o, accept, folder.mData);
}

void vks_r4b_send_auto_receive_response(LLOfferInfo* offer)
{
    VksR4bCall call(offer);
    const VksInvOffer o = vks_r4b_offer(*offer);
    vks_inv_r4b_send_auto_receive_response(&call.host, &o);
}

void vks_r4b_send_decline_response(LLOfferInfo* offer)
{
    VksR4bCall call(offer);
    const VksInvOffer o = vks_r4b_offer(*offer);
    vks_inv_r4b_send_decline_response(&call.host, &o);
}

bool vks_r4b_check_offer_throttle(const std::string& from_name, bool check_only)
{
    VksR4bCall call;
    return vks_inv_r4b_check_offer_throttle(&call.host, reinterpret_cast<const uint8_t*>(from_name.data()),
                                            from_name.size(), check_only);
}

bool vks_r4b_give_item(const LLUUID& to, const LLInventoryItem* item, const LLUUID& im_session)
{
    VksR4bCall call;
    if (!item)
    {
        return vks_inv_r4b_give_item(&call.host, to.mData, nullptr, im_session.mData);
    }
    const VksInvR4bItem r = vks_r4b_item(item);
    return vks_inv_r4b_give_item(&call.host, to.mData, &r, im_session.mData);
}

bool vks_r4b_give_category(const LLUUID& to, const LLInventoryCategory* cat, const LLUUID& im_session,
                           const std::string& success_notification)
{
    VksR4bCall call;
    const uint8_t* n = reinterpret_cast<const uint8_t*>(success_notification.data());
    if (!cat)
    {
        return vks_inv_r4b_give_category(&call.host, to.mData, nullptr, im_session.mData, n, success_notification.size());
    }
    const VksInvR4bCat r = vks_r4b_cat(cat);
    return vks_inv_r4b_give_category(&call.host, to.mData, &r, im_session.mData, n, success_notification.size());
}

// The answer to a notification posted with a handler (VKS_INV_R4B_H_*).
bool vks_r4b_handler_answer(S32 handler, const LLSD& notification, const LLSD& response)
{
    VksR4bCall call;
    const std::string n = vks_r4b_sd_bytes(notification);
    const std::string r = vks_r4b_sd_bytes(response);
    return vks_inv_r4b_answer(&call.host, handler, reinterpret_cast<const uint8_t*>(n.data()), n.size(),
                              reinterpret_cast<const uint8_t*>(r.data()), r.size());
}

// The share dialog's callback for `avatars`.
void vks_r4b_share(const uuid_vec_t& avatars, const LLSD& notification, const LLSD& response,
                   const std::set<LLUUID>& selected)
{
    VksR4bCall call;
    std::vector<uint8_t> av(avatars.size() * UUID_BYTES);
    for (size_t i = 0; i < avatars.size(); ++i)
    {
        memcpy(&av[i * UUID_BYTES], avatars[i].mData, UUID_BYTES);
    }
    std::vector<uint8_t> sel;
    sel.reserve(selected.size() * UUID_BYTES);
    for (const LLUUID& id : selected)
    {
        sel.insert(sel.end(), id.mData, id.mData + UUID_BYTES);
    }
    const std::string n = vks_r4b_sd_bytes(notification);
    const std::string r = vks_r4b_sd_bytes(response);
    vks_inv_r4b_share(&call.host, av.data(), avatars.size(), reinterpret_cast<const uint8_t*>(n.data()), n.size(),
                      reinterpret_cast<const uint8_t*>(r.data()), r.size(), sel.data(), selected.size());
}

// isInventoryGiveAcceptable (0), isInventoryGroupGiveAcceptable (1),
// itemTransferCommonlyAllowed (2).
bool vks_r4b_item_predicate(int which, const LLInventoryItem* item)
{
    VksR4bCall call;
    VksInvR4bItem r;
    const VksInvR4bItem* p = nullptr;
    if (item)
    {
        r = vks_r4b_item(item);
        p = &r;
    }
    switch (which)
    {
    case 0:
        return vks_inv_r4b_is_give_acceptable(&call.host, p);
    case 1:
        return vks_inv_r4b_is_group_give_acceptable(&call.host, p);
    default:
        return vks_inv_r4b_transfer_commonly_allowed(&call.host, p);
    }
}

// A collect functor (VKS_INV_R4B_F_*) on one category or item; *count gets
// countNoCopy() for the counting functors.
bool vks_r4b_collect_functor(S32 functor, const LLInventoryCategory* cat, const LLInventoryItem* item, S32* count)
{
    VksR4bCall call;
    VksInvR4bCat c;
    VksInvR4bItem i;
    if (cat)
    {
        c = vks_r4b_cat(cat);
    }
    if (item)
    {
        i = vks_r4b_item(item);
    }
    int32_t n = -1;
    bool r = vks_inv_r4b_collect_functor(&call.host, functor, cat ? &c : nullptr, item ? &i : nullptr, &n);
    if (count)
    {
        *count = n;
    }
    return r;
}

EAcceptance vks_r4b_will_object_accept(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type)
{
    VksR4bCall call;
    VksInvR4bViewerObject o;
    VksInvR4bItem i;
    if (obj)
    {
        o = vks_r4b_viewer_object(obj);
    }
    if (item)
    {
        i = vks_r4b_item(item);
    }
    return (EAcceptance)vks_inv_r4b_will_object_accept(&call.host, obj ? &o : nullptr, item ? &i : nullptr, (int32_t)type);
}

bool vks_r4b_world_to_agent(const LLUUID& object_id, const LLUUID& category_id, bool drop,
                            std::function<void(S32, void*, const LLMoveInv*)> callback, void* user_data,
                            LLInventoryFilter* filter)
{
    VksR4bCall call;
    call.move_callback = callback;
    call.move_user_data = user_data;
    call.filter = filter;
    const int64_t filter_types = filter ? (int64_t)filter->getFilterTypes() : -1;
    return vks_inv_r4b_world_to_agent(&call.host, object_id.mData, category_id.mData, drop, filter_types);
}

bool vks_r4b_give_drag_and_drop(const LLUUID& dest_agent, const LLUUID& session_id, bool drop,
                                EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept,
                                const LLSD& dest)
{
    VksR4bCall call;
    LLInventoryObject* inv_obj = (LLInventoryObject*)cargo_data;
    VksInvR4bItem i;
    VksInvR4bCat c;
    const VksInvR4bItem* ip = nullptr;
    const VksInvR4bCat* cp = nullptr;
    if (inv_obj)
    {
        if (LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(inv_obj))
        {
            i = vks_r4b_item(item);
            ip = &i;
        }
        else
        {
            c = vks_r4b_object_as_cat(inv_obj);
            cp = &c;
        }
    }
    const std::string d = vks_r4b_sd_bytes(dest);
    int32_t acc = (int32_t)ACCEPT_NO;
    bool r = vks_inv_r4b_give_drag_and_drop(&call.host, dest_agent.mData, session_id.mData, drop, (int32_t)cargo_type, ip,
                                            cp, reinterpret_cast<const uint8_t*>(d.data()), d.size(), &acc);
    if (accept)
    {
        *accept = (EAcceptance)acc;
    }
    return r;
}

// dad3dGiveInventoryObject (0), dad3dGiveInventory (1),
// dad3dGiveInventoryCategory (2).
EAcceptance vks_r4b_dad3d_give(int kind, S32 source, LLViewerInventoryItem* item, LLViewerInventoryCategory* cat,
                               LLViewerObject* obj, bool drop)
{
    VksR4bCall call;
    VksInvR4bItem i;
    VksInvR4bCat c;
    if (item)
    {
        i = vks_r4b_item(item);
    }
    if (cat)
    {
        c = vks_r4b_cat(cat);
    }
    return (EAcceptance)vks_inv_r4b_dad3d_give(&call.host, kind, source, item ? &i : nullptr, cat ? &c : nullptr,
                                               obj ? obj->getID().mData : nullptr, drop);
}

void vks_r4b_share_confirmation(const std::string& name, const LLUUID& item_id, const std::string& item_name,
                                const std::string& dest, const LLUUID& dest_agent)
{
    VksR4bCall call;
    vks_inv_r4b_share_confirmation(&call.host, reinterpret_cast<const uint8_t*>(name.data()), name.size(), item_id.mData,
                                   reinterpret_cast<const uint8_t*>(item_name.data()), item_name.size(),
                                   reinterpret_cast<const uint8_t*>(dest.data()), dest.size(), dest_agent.mData,
                                   LLUUID::null.mData);
}
