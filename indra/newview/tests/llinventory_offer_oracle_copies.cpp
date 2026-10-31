/**
 * @file llinventory_offer_oracle_copies.cpp
 * @brief Recorder R4b (inventory rung): frozen copies of the viewer's offer and
 * give decision code, verbatim inside namespace r4b (r4b_pack for the instant
 * message packer), compiled against the fakes in llinventory_offer_oracle_fakes.h.
 * Each region names its source, line range and blob; tests/check_r4b_copies.py
 * confirms the copies still match the live source.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_offer_oracle_copies.h"

namespace r4b
{

// R4B-COPY-BEGIN build_fullname
// source: indra/newview/llagentui.cpp
// function: LLAgentUI::buildFullname
// lines: 53-57
// blob: ea868c1ff365bc3a448164fd8715718b1bdf8895
void LLAgentUI::buildFullname(std::string& name)
{
    if (isAgentAvatarValid())
        name = gAgentAvatarp->getFullname();
}
// R4B-COPY-END build_fullname

// R4B-COPY-BEGIN vitem_get_type
// source: indra/newview/llviewerinventory.cpp
// function: LLViewerInventoryItem::getType
// lines: 2439-2450
// blob: a13210dce6ed2561e2b18768645bcfa37068612c
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
// R4B-COPY-END vitem_get_type

// R4B-COPY-BEGIN vitem_get_name
// source: indra/newview/llviewerinventory.cpp
// function: LLViewerInventoryItem::getName
// lines: 2496-2508
// blob: a13210dce6ed2561e2b18768645bcfa37068612c
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
// R4B-COPY-END vitem_get_name

// R4B-COPY-BEGIN vitem_linked
// source: indra/newview/llviewerinventory.cpp
// function: LLViewerInventoryItem::getLinkedItem, getLinkedCategory
// lines: 2693-2716
// blob: a13210dce6ed2561e2b18768645bcfa37068612c
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

LLViewerInventoryCategory *LLViewerInventoryItem::getLinkedCategory() const
{
    if (mType == LLAssetType::AT_LINK_FOLDER)
    {
        LLViewerInventoryCategory *linked_category = gInventory.getCategory(mAssetUUID);
        return linked_category;
    }
    return NULL;
}
// R4B-COPY-END vitem_linked

// R4B-COPY-BEGIN is_item_worn
// source: indra/newview/llinventoryfunctions.cpp
// function: get_is_item_worn (3 overloads)
// lines: 645-698
// blob: e68f61bd684055fb5a8865df8a0d108868b7daff
bool get_is_item_worn(const LLUUID& id, const LLViewerInventoryItem* item)
{
    if (!item)
        return false;

    if (item->getIsLinkType() && !gInventory.getItem(item->getLinkedUUID()))
    {
        return false;
    }

    // Consider the item as worn if it has links in COF.
// [SL:KB] - The code below causes problems across the board so it really just needs to go
//  if (LLAppearanceMgr::instance().isLinkedInCOF(id))
//  {
//      return true;
//  }

    switch(item->getType())
    {
        case LLAssetType::AT_OBJECT:
        {
            if (isAgentAvatarValid() && gAgentAvatarp->isWearingAttachment(item->getLinkedUUID()))
                return true;
            break;
        }
        case LLAssetType::AT_BODYPART:
        case LLAssetType::AT_CLOTHING:
            if(gAgentWearables.isWearingItem(item->getLinkedUUID()))
                return true;
            break;
        case LLAssetType::AT_GESTURE:
            if (LLGestureMgr::instance().isGestureActive(item->getLinkedUUID()))
                return true;
            break;
        default:
            break;
    }
    return false;
}

bool get_is_item_worn(const LLUUID& id)
{
    const LLViewerInventoryItem* item = gInventory.getItem(id);
    return get_is_item_worn(id, item);
}

bool get_is_item_worn(const LLViewerInventoryItem* item)
{
    if (!item)
    {
        return false;
    }
    return get_is_item_worn(item->getUUID(), item);
}
// R4B-COPY-END is_item_worn

// R4B-COPY-BEGIN transfer_commonly_allowed
// source: indra/newview/llinventoryfunctions.cpp
// function: LLInventoryCollectFunctor::itemTransferCommonlyAllowed
// lines: 3009-3028
// blob: e68f61bd684055fb5a8865df8a0d108868b7daff
// static
bool LLInventoryCollectFunctor::itemTransferCommonlyAllowed(const LLInventoryItem* item)
{
    if (!item)
        return false;

    switch(item->getType())
    {
        case LLAssetType::AT_OBJECT:
        case LLAssetType::AT_BODYPART:
        case LLAssetType::AT_CLOTHING:
            if (!get_is_item_worn(item->getUUID()))
                return true;
            break;
        default:
            return true;
            break;
    }
    return false;
}
// R4B-COPY-END transfer_commonly_allowed

// R4B-COPY-BEGIN collect_descendents_if
// source: indra/newview/llinventorymodel.cpp
// function: LLInventoryModel::collectDescendentsIf
// lines: 1369-1458
// blob: 3edfb92f7e7f33345e08f6aa222a907dd23c9f57
void LLInventoryModel::collectDescendentsIf(const LLUUID& id,
                                            cat_array_t& cats,
                                            item_array_t& items,
                                            bool include_trash,
                                            LLInventoryCollectFunctor& add,
                                            bool follow_folder_links)
// [/RLVa:KB]
{
    // Start with categories
    if(!include_trash)
    {
        const LLUUID trash_id = findCategoryUUIDForType(LLFolderType::FT_TRASH);
        if(trash_id.notNull() && (trash_id == id))
            return;
    }
    cat_array_t* cat_array = get_ptr_in_map(mParentChildCategoryTree, id);
    if(cat_array)
    {
        for (auto& cat : *cat_array)
        {
            if (add.exceedsLimit())
            {
                break;
            }
            if(add(cat,NULL))
            {
                cats.push_back(cat);
            }
// [RLVa:KB] - Checked: 2013-04-15 (RLVa-1.4.8)
            collectDescendentsIf(cat->getUUID(), cats, items, include_trash, add, follow_folder_links);
// [/RLVa:KB]
//          collectDescendentsIf(cat->getUUID(), cats, items, include_trash, add);
        }
    }

    item_array_t* item_array = get_ptr_in_map(mParentChildItemTree, id);

    // Move onto items
    if(item_array)
    {
        for (auto& item : *item_array)
        {
            if (add.exceedsLimit())
            {
                break;
            }
            if(add(NULL, item))
            {
                items.push_back(item);
            }
        }
    }

// [RLVa:KB] - Checked: 2010-09-30 (RLVa-1.2.1d) | Added: RLVa-1.2.1d
    // The problem is that we want some way for the functor to know that it's being asked to decide on a folder link
    // but it won't know that until after it has encountered the folder link item (which doesn't happen until *after*
    // it has already collected all items from it the way the code was originally laid out)
    // This breaks the "finish collecting all folders before collecting items (top to bottom and then bottom to top)"
    // assumption but no functor is (currently) relying on it (and likely never should since it's an implementation detail?)
    // [Only LLAppearanceMgr actually ever passes in 'follow_folder_links == true']
    // Follow folder links recursively.  Currently never goes more
    // than one level deep (for current outfit support)
    // Note: if making it fully recursive, need more checking against infinite loops.
    if (follow_folder_links && item_array)
    {
        for (const auto& item : *item_array)
        {
            if (item && item->getActualType() == LLAssetType::AT_LINK_FOLDER)
            {
                LLViewerInventoryCategory *linked_cat = item->getLinkedCategory();
                if (linked_cat && linked_cat->getPreferredType() != LLFolderType::FT_OUTFIT)
                    // BAP - was
                    // LLAssetType::lookupIsEnsembleCategoryType(linked_cat->getPreferredType()))
                    // Change back once ensemble typing is in place.
                {
                    if(add(linked_cat,NULL))
                    {
                        // BAP should this be added here?  May not
                        // matter if it's only being used in current
                        // outfit traversal.
                        cats.push_back(LLPointer<LLViewerInventoryCategory>(linked_cat));
                    }
                    collectDescendentsIf(linked_cat->getUUID(), cats, items, include_trash, add, false);
                }
            }
        }
    }
// [/RLVa:KB]
}

// R4B-COPY-END collect_descendents_if

// R4B-COPY-BEGIN mute_is_linden
// source: indra/newview/llmutelist.cpp
// function: LLMuteList::isLinden
// lines: 218-233
// blob: 3247ec020ecc1929c21e9372767759ad8bdc4d71
bool LLMuteList::isLinden(const std::string& name)
{
    std::string username = boost::replace_all_copy(name, ".", " ");
    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep(" ");
    tokenizer tokens(username, sep);
    tokenizer::iterator token_iter = tokens.begin();

    if (token_iter == tokens.end()) return false;
    token_iter++;
    if (token_iter == tokens.end()) return false;

    std::string last_name = *token_iter;
    LLStringUtil::toLower(last_name);
    return last_name == "linden";
}
// R4B-COPY-END mute_is_linden

// R4B-COPY-BEGIN mute_auto_remove
// source: indra/newview/llmutelist.cpp
// function: LLMuteList::autoRemove
// lines: 667-692
// blob: 3247ec020ecc1929c21e9372767759ad8bdc4d71
bool LLMuteList::autoRemove(const LLUUID& agent_id, const EAutoReason reason)
{
    bool removed = false;

    if (isMuted(agent_id))
    {
        LLMute automute(agent_id, LLStringUtil::null, LLMute::AGENT);
        removed = true;
        remove(automute);

        LLAvatarName av_name;
        if (LLAvatarNameCache::get(agent_id, &av_name))
        {
            // name in cache, call callback directly
            notify_automute_callback(agent_id, av_name, reason);
        }
        else
        {
            // not in cache, lookup name from cache
            LLAvatarNameCache::get(agent_id,
                boost::bind(&notify_automute_callback, _1, _2, reason));
        }
    }

    return removed;
}
// R4B-COPY-END mute_auto_remove

// R4B-COPY-BEGIN mute_is_muted
// source: indra/newview/llmutelist.cpp
// function: LLMuteList::isMuted(id, name, flags)
// lines: 830-872
// blob: 3247ec020ecc1929c21e9372767759ad8bdc4d71
bool LLMuteList::isMuted(const LLUUID& id, const std::string& name, U32 flags) const
{
    // <FS:ND> In case of an empty mutelist, we can exit right away.
    if( 0 == mMutes.size() && mLegacyMutes.size() == 0)
        return false;
    // </FS:ND>

    // for objects, check for muting on their parent prim
    LLViewerObject* mute_object = get_object_to_mute_from_id(id);
    LLUUID id_to_check  = (mute_object) ? mute_object->getID() : id;

    // <FS:Ansariel> FIRE-8540: Make sure we don't mute ourself if we added
    //               a legacy mute by name with our name.
    if (id_to_check == gAgentID)
    {
        return false;
    }

    // don't need name or type for lookup
    LLMute mute(id_to_check);
    mute_set_t::const_iterator mute_it = mMutes.find(mute);
    if (mute_it != mMutes.end())
    {
        // If any of the flags the caller passed are set, this item isn't considered muted for this caller.
        if(flags & mute_it->mFlags)
        {
            return false;
        }
        return true;
    }

    // empty names can't be legacy-muted
    // <FS:Ansariel> FIRE-8268: Revert STORM-1004 or objects muted by name
    //               won't be muted if worn as attachments
    //bool avatar = mute_object && mute_object->isAvatar();
    //if (name.empty() || avatar) return false;
    if (name.empty()) return false;
    // </FS:Ansariel>

    // Look in legacy pile
    string_set_t::const_iterator legacy_it = mLegacyMutes.find(name);
    return legacy_it != mLegacyMutes.end();
}
// R4B-COPY-END mute_is_muted

// R4B-COPY-BEGIN compute_session_id
// source: indra/newview/llimview.cpp
// function: LLIMMgr::computeSessionID
// lines: 2530-2570
// blob: 63e38650ed7c7acaebf772c0ce192355a35143b7
LLUUID LLIMMgr::computeSessionID(
    EInstantMessage dialog,
    const LLUUID& other_participant_id)
{
    LLUUID session_id;
    if (IM_SESSION_GROUP_START == dialog)
    {
        // slam group session_id to the group_id (other_participant_id)
        session_id = other_participant_id;
    }
    else if (IM_SESSION_CONFERENCE_START == dialog)
    {
        session_id.generate();
    }
    else if (IM_SESSION_INVITE == dialog)
    {
        // use provided session id for invites
        session_id = other_participant_id;
    }
    else
    {
        LLUUID agent_id = gAgent.getID();
        if (other_participant_id == agent_id)
        {
            // if we try to send an IM to ourselves then the XOR would be null
            // so we just make the session_id the same as the agent_id
            session_id = agent_id;
        }
        else
        {
            // peer-to-peer or peer-to-asset session_id is the XOR
            session_id = other_participant_id ^ agent_id;
        }
    }

    if (gAgent.isInGroup(session_id, true) && (session_id != other_participant_id))
    {
        LL_WARNS() << "Group session id different from group id: IM type = " << dialog << ", session id = " << session_id << ", group id = " << other_participant_id << LL_ENDL;
    }
    return session_id;
}
// R4B-COPY-END compute_session_id

// R4B-COPY-BEGIN object_inventory_contents
// source: indra/newview/llviewerobject.cpp
// function: LLViewerObject::getInventoryContents
// lines: 3874-3888
// blob: c9ac3bdc20301e3e43da73183f18595712912e79
void LLViewerObject::getInventoryContents(LLInventoryObject::object_list_t& objects)
{
    if(mInventory)
    {
        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for( ; it != end; ++it)
        {
            if ((*it)->getType() != LLAssetType::AT_CATEGORY)
            {
                objects.push_back(*it);
            }
        }
    }
}
// R4B-COPY-END object_inventory_contents

// R4B-COPY-BEGIN rlv_is_give_to_rlv_offer
// source: indra/newview/rlvinventory.cpp
// function: RlvInventory::isGiveToRLVOffer
// lines: 368-387
// blob: 48307d48b3e581cf6a21117a3c64c4f7dafe9fbc
// Checked: 2012-11-28 (RLVa-1.4.8)
bool RlvInventory::isGiveToRLVOffer(const LLOfferInfo& offerInfo)
{
    if ( (!RlvSettings::getForbidGiveToRLV()) && (RlvInventory::instance().getSharedRoot()) )
    {
        if (offerInfo.mFromObject)
        {
            return
                (IM_TASK_INVENTORY_OFFERED == offerInfo.mIM) &&
                (LLAssetType::AT_CATEGORY == offerInfo.mType) && (offerInfo.mDesc.find(RLV_PUTINV_PREFIX) == 1);
        }
        else
        {
            return
                (IM_INVENTORY_OFFERED == offerInfo.mIM) &&
                (LLAssetType::AT_CATEGORY == offerInfo.mType) && (offerInfo.mDesc.find(RLV_PUTINV_PREFIX) == 0);
        }
    }
    return false;
}
// R4B-COPY-END rlv_is_give_to_rlv_offer

// R4B-COPY-BEGIN notif_response_template
// source: indra/llui/llnotifications.cpp
// function: LLNotification::getResponseTemplate
// lines: 615-642
// blob: bcef4757b4694554bdf80f0e83840224c8f4a19e
LLSD LLNotification::getResponseTemplate(EResponseTemplateType type)
{
    LLSD response = LLSD::emptyMap();

    if (!mForm)
    {
        LL_WARNS("Notifications") << "Null form when getting response template for notification " << getName() << LL_ENDL;
        return response;
    }

    for (S32 element_idx = 0;
        element_idx < mForm->getNumElements();
        ++element_idx)
    {
        LLSD element = mForm->getElement(element_idx);
        if (element.has("name"))
        {
            response[element["name"].asString()] = element["value"];
        }

        if ((type == WITH_DEFAULT_BUTTON)
            && element["default"].asBoolean())
        {
            response[element["name"].asString()] = true;
        }
    }
    return response;
}
// R4B-COPY-END notif_response_template

// R4B-COPY-BEGIN notif_selected_option
// source: indra/llui/llnotifications.cpp
// function: LLNotification::getSelectedOption
// lines: 645-664
// blob: bcef4757b4694554bdf80f0e83840224c8f4a19e
S32 LLNotification::getSelectedOption(const LLSD& notification, const LLSD& response)
{
    LLNotificationForm form(notification["form"]);

    for (S32 element_idx = 0;
        element_idx < form.getNumElements();
        ++element_idx)
    {
        LLSD element = form.getElement(element_idx);

        // only look at buttons
        if (element["type"].asString() == "button"
            && response[element["name"].asString()].asBoolean())
        {
            return element["index"].asInteger();
        }
    }

    return -1;
}
// R4B-COPY-END notif_selected_option

// R4B-COPY-BEGIN notif_force_response
// source: indra/llui/llnotifications.cpp
// function: LLNotifications::forceResponse
// lines: 1496-1517
// blob: bcef4757b4694554bdf80f0e83840224c8f4a19e
void LLNotifications::forceResponse(const LLNotification::Params& params, S32 option)
{
    LLNotificationPtr temp_notify = std::make_shared<LLNotification>(params);

    if (!temp_notify->getForm())
    {
        LL_WARNS("Notifications") << "Cannot force response for notification with null form: " << (std::string)params.name << LL_ENDL;
        return;
    }

    LLSD response = temp_notify->getResponseTemplate();
    LLSD selected_item = temp_notify->getForm()->getElement(option);

    if (selected_item.isUndefined())
    {
        LL_WARNS("Notifications") << "Invalid option" << option << " for notification " << (std::string)params.name << LL_ENDL;
        return;
    }
    response[selected_item["name"].asString()] = true;

    temp_notify->respond(response);
}
// R4B-COPY-END notif_force_response

// R4B-COPY-BEGIN notifutil_selected_option
// source: indra/llui/llnotificationsutil.cpp
// function: LLNotificationsUtil::getSelectedOption
// lines: 82-85
// blob: 6b570d043f10aa30d91104ecfb95a3161dd3c442
S32 LLNotificationsUtil::getSelectedOption(const LLSD& notification, const LLSD& response)
{
    return LLNotification::getSelectedOption(notification, response);
}
// R4B-COPY-END notifutil_selected_option

// R4B-COPY-BEGIN throttle_globals
// source: indra/newview/llviewermessage.cpp
// function: gThrottleTimer, OFFER_THROTTLE_*
// lines: 184-187
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
//inventory offer throttle globals
LLFrameTimer gThrottleTimer;
const U32 OFFER_THROTTLE_MAX_COUNT=5; //number of items per time period
const F32 OFFER_THROTTLE_TIME=10.f; //time period in seconds
// R4B-COPY-END throttle_globals

// R4B-COPY-BEGIN discard_agent_offer
// source: indra/newview/llviewermessage.cpp
// function: class LLDiscardAgentOffer
// lines: 1263-1291
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
class LLDiscardAgentOffer : public LLInventoryFetchItemsObserver
{
    LOG_CLASS(LLDiscardAgentOffer);

public:
    LLDiscardAgentOffer(const LLUUID& folder_id, const LLUUID& object_id) :
        LLInventoryFetchItemsObserver(object_id),
        mFolderID(folder_id),
        mObjectID(object_id) {}

    virtual void done()
    {
        LL_DEBUGS("Messaging") << "LLDiscardAgentOffer::done()" << LL_ENDL;

        // We're invoked from LLInventoryModel::notifyObservers().
        // If we now try to remove the inventory item, it will cause a nested
        // notifyObservers() call, which won't work.
        // So defer moving the item to trash until viewer gets idle (in a moment).
        // Use removeObject() rather than removeItem() because at this level,
        // the object could be either an item or a folder.
        LLAppViewer::instance()->addOnIdleCallback(boost::bind(&LLInventoryModel::removeObject, &gInventory, mObjectID));
        gInventory.removeObserver(this);
        delete this;
    }

protected:
    LLUUID mFolderID;
    LLUUID mObjectID;
};
// R4B-COPY-END discard_agent_offer

// R4B-COPY-BEGIN check_offer_throttle
// source: indra/newview/llviewermessage.cpp
// function: check_offer_throttle
// lines: 1294-1387
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
//Returns true if we are OK, false if we are throttled
//Set check_only true if you want to know the throttle status
//without registering a hit
bool check_offer_throttle(const std::string& from_name, bool check_only)
{
    static U32 throttle_count;
    static bool throttle_logged;
    LLChat chat;
    std::string log_message;

    // <FS:Ansariel> This controls if items should be opened in open_inventory_offer()??? No way!
    //if (!gSavedSettings.getBOOL("ShowNewInventory"))
    //  return false;
    // </FS:Ansariel>

    if (check_only)
    {
        return gThrottleTimer.hasExpired();
    }

    if(gThrottleTimer.checkExpirationAndReset(OFFER_THROTTLE_TIME))
    {
        LL_DEBUGS("Messaging") << "Throttle Expired" << LL_ENDL;
        throttle_count=1;
        throttle_logged=false;
        return true;
    }
    else //has not expired
    {
        LL_DEBUGS("Messaging") << "Throttle Not Expired, Count: " << throttle_count << LL_ENDL;
        // When downloading the initial inventory we get a lot of new items
        // coming in and can't tell that from spam.

        // Ansariel: Customizable throttle!
        static LLCachedControl<U32> fsOfferThrottleMaxCount(gSavedSettings, "FSOfferThrottleMaxCount");
        if (LLStartUp::getStartupState() >= STATE_STARTED
            //&& throttle_count >= OFFER_THROTTLE_MAX_COUNT)
            && throttle_count >= fsOfferThrottleMaxCount)
        {
            if (!throttle_logged)
            {
                // Use the name of the last item giver, who is probably the person
                // spamming you.

                LLStringUtil::format_map_t arg;
                std::string log_msg;
                std::ostringstream time ;
                time<<OFFER_THROTTLE_TIME;

                arg["APP_NAME"] = LLAppViewer::instance()->getSecondLifeTitle();
                arg["TIME"] = time.str();


                if (!from_name.empty())
                {
                    // <FS:PP> gSavedSettings to LLCachedControl
                    static LLCachedControl<bool> fsNotifyIncomingObjectSpamFrom(gSavedSettings, "FSNotifyIncomingObjectSpamFrom");
                    if (fsNotifyIncomingObjectSpamFrom)
                    // </FS:PP>
                    {
                        arg["FROM_NAME"] = from_name;
                        log_msg = LLTrans::getString("ItemsComingInTooFastFrom", arg);
                    }
                }
                else
                {
                    // <FS:PP> gSavedSettings to LLCachedControl
                    static LLCachedControl<bool> fsNotifyIncomingObjectSpam(gSavedSettings, "FSNotifyIncomingObjectSpam");
                    if (fsNotifyIncomingObjectSpam)
                    // </FS:PP>
                    {
                        log_msg = LLTrans::getString("ItemsComingInTooFast", arg);
                    }
                }

                //this is kinda important, so actually put it on screen
                if (log_msg != "")
                {
                    LLSD args;
                    args["MESSAGE"] = log_msg;
                    LLNotificationsUtil::add("SystemMessage", args);
                }

                throttle_logged=true;
            }
            return false;
        }
        else
        {
            throttle_count++;
            return true;
        }
    }
}
// R4B-COPY-END check_offer_throttle

// R4B-COPY-BEGIN offer_mute_callbacks
// source: indra/newview/llviewermessage.cpp
// function: inventory_offer_mute_callback, inventory_offer_mute_avatar_callback
// lines: 1608-1651
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
void inventory_offer_mute_callback(const LLUUID& blocked_id,
                                   const std::string& full_name,
                                   bool is_group)
{
    // *NOTE: blocks owner if the offer came from an object
    LLMute::EType mute_type = is_group ? LLMute::GROUP : LLMute::AGENT;

    LLMute mute(blocked_id, full_name, mute_type);
    if (LLMuteList::getInstance()->add(mute))
    {
        LLPanelBlockedList::showPanelAndSelect(blocked_id);
    }

    // purge the message queue of any previously queued inventory offers from the same source.
    class OfferMatcher : public LLNotificationsUI::LLScreenChannel::Matcher
    {
    public:
        OfferMatcher(const LLUUID& to_block) : blocked_id(to_block) {}
        bool matches(const LLNotificationPtr notification) const
        {
            if(notification->getName() == "ObjectGiveItem"
                || notification->getName() == "OwnObjectGiveItem"
                || notification->getName() == "UserGiveItemLegacy" // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
                || notification->getName() == "UserGiveItem")
            {
                return (notification->getPayload()["from_id"].asUUID() == blocked_id);
            }
            return false;
        }
    private:
        const LLUUID& blocked_id;
    };

    LLNotificationsUI::LLChannelManager::getInstance()->killToastsFromChannel(
        LLNotificationsUI::NOTIFICATION_CHANNEL_UUID,
        OfferMatcher(blocked_id));
}


void inventory_offer_mute_avatar_callback(const LLUUID& blocked_id,
    const LLAvatarName& av_name)
{
    inventory_offer_mute_callback(blocked_id, av_name.getUserName(), false);
}
// R4B-COPY-END offer_mute_callbacks

// R4B-COPY-BEGIN offer_info_basic
// source: indra/newview/llviewermessage.cpp
// function: LLOfferInfo ctors, asLLSD, fromLLSD
// lines: 1654-1720
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
std::string LLOfferInfo::mResponderType = "offer_info";

LLOfferInfo::LLOfferInfo()
 : LLNotificationResponderInterface()
 , mFromGroup(false)
 , mFromObject(false)
 , mIM(IM_NOTHING_SPECIAL)
 , mType(LLAssetType::AT_NONE)
 , mPersist(false)
{
}

LLOfferInfo::LLOfferInfo(const LLSD& sd)
{
    mIM = (EInstantMessage)sd["im_type"].asInteger();
    mFromID = sd["from_id"].asUUID();
    mFromGroup = sd["from_group"].asBoolean();
    mFromObject = sd["from_object"].asBoolean();
    mTransactionID = sd["transaction_id"].asUUID();
    mFolderID = sd["folder_id"].asUUID();
    mObjectID = sd["object_id"].asUUID();
    mType = LLAssetType::lookup(sd["type"].asString().c_str());
    mFromName = sd["from_name"].asString();
    mDesc = sd["description"].asString();
    mHost = LLHost(sd["sender"].asString());
    mPersist = sd["persist"].asBoolean();
}

LLOfferInfo::LLOfferInfo(const LLOfferInfo& info)
{
    mIM = info.mIM;
    mFromID = info.mFromID;
    mFromGroup = info.mFromGroup;
    mFromObject = info.mFromObject;
    mTransactionID = info.mTransactionID;
    mFolderID = info.mFolderID;
    mObjectID = info.mObjectID;
    mType = info.mType;
    mFromName = info.mFromName;
    mDesc = info.mDesc;
    mHost = info.mHost;
    mPersist = info.mPersist;
}

LLSD LLOfferInfo::asLLSD()
{
    LLSD sd;
    sd["responder_type"] = mResponderType;
    sd["im_type"] = mIM;
    sd["from_id"] = mFromID;
    sd["from_group"] = mFromGroup;
    sd["from_object"] = mFromObject;
    sd["transaction_id"] = mTransactionID;
    sd["folder_id"] = mFolderID;
    sd["object_id"] = mObjectID;
    sd["type"] = LLAssetType::lookup(mType);
    sd["from_name"] = mFromName;
    sd["description"] = mDesc;
    sd["sender"] = mHost.getIPandPort();
    sd["persist"] = mPersist;
    return sd;
}

void LLOfferInfo::fromLLSD(const LLSD& params)
{
    *this = params;
}
// R4B-COPY-END offer_info_basic

// R4B-COPY-BEGIN offer_info_responses
// source: indra/newview/llviewermessage.cpp
// function: LLOfferInfo::sendReceiveResponse, send_decline_response
// lines: 1722-1829
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
void LLOfferInfo::sendReceiveResponse(bool accept, const LLUUID &destination_folder_id)
{
    if(IM_INVENTORY_OFFERED == mIM)
    {
        // add buddy to recent people list
//      LLRecentPeople::instance().add(mFromID);
// [RLVa:KB] - Checked: RLVa-2.0.1
        // RELEASE-RLVa: [RLVa-2.0.1] Make sure this stays in sync with the condition in inventory_offer_handler()
        bool fRlvCanShowName = (!RlvActions::isRlvEnabled()) ||
            (RlvActions::canShowName(RlvActions::SNC_DEFAULT, mFromID)) || (!RlvUtil::isNearbyAgent(mFromID)) || (RlvUIEnabler::hasOpenIM(mFromID)) || (RlvUIEnabler::hasOpenProfile(mFromID));
        if (fRlvCanShowName)
            LLRecentPeople::instance().add(mFromID);
// [/RLVa:KB]
    }

    if (mTransactionID.isNull())
    {
        // Not provided, message won't work
        LL_WARNS("Messaging") << "Missing transaction id, response for " << mIM << " won't work" << LL_ENDL;
        return;
    }

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_ImprovedInstantMessage);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_MessageBlock);
    msg->addBOOLFast(_PREHASH_FromGroup, false);
    msg->addUUIDFast(_PREHASH_ToAgentID, mFromID);
    msg->addU8Fast(_PREHASH_Offline, IM_ONLINE);
    msg->addUUIDFast(_PREHASH_ID, mTransactionID);
    msg->addU32Fast(_PREHASH_Timestamp, NO_TIMESTAMP); // no timestamp necessary
    std::string name;
    LLAgentUI::buildFullname(name);
    msg->addStringFast(_PREHASH_FromAgentName, name);
    msg->addStringFast(_PREHASH_Message, "");
    msg->addU32Fast(_PREHASH_ParentEstateID, 0);
    msg->addUUIDFast(_PREHASH_RegionID, LLUUID::null);
    msg->addVector3Fast(_PREHASH_Position, gAgent.getPositionAgent());

    // ACCEPT. The math for the dialog works, because the accept
    // for inventory_offered, task_inventory_offer or
    // group_notice_inventory is 1 greater than the offer integer value.
    // Generates IM_INVENTORY_ACCEPTED, IM_TASK_INVENTORY_ACCEPTED,
    // or IM_GROUP_NOTICE_INVENTORY_ACCEPTED
    // Decline for inventory_offered, task_inventory_offer or
    // group_notice_inventory is 2 greater than the offer integer value.

    EInstantMessage im = mIM;
    if (mIM == IM_GROUP_NOTICE_REQUESTED)
    {
        // Request has no responder dialogs
        im = IM_GROUP_NOTICE;
    }

    if (accept)
    {
        msg->addU8Fast(_PREHASH_Dialog, (U8)(im + 1));
        msg->addBinaryDataFast(_PREHASH_BinaryBucket, &(destination_folder_id.mData),
                                sizeof(destination_folder_id.mData));

        LL_DEBUGS("Messaging") << "Processing" << (U8)(im + 1) << " with transaction id " << mTransactionID << LL_ENDL;
    }
    else
    {
        msg->addU8Fast(_PREHASH_Dialog, (U8)(im + 2));
        msg->addBinaryDataFast(_PREHASH_BinaryBucket, EMPTY_BINARY_BUCKET, EMPTY_BINARY_BUCKET_SIZE);
    }
    // send the message
    msg->sendReliable(mHost);

    // transaction id is usable only once
    // Note: a bit of a hack, clicking group notice attachment will not close notice
    // so we reset no longer usable transaction id to know not to send message again
    // Once capabilities for responses will be implemented LLOfferInfo will have to
    // remember that it already responded in another way and ignore IOR_DECLINE
    mTransactionID.setNull();
}

// <FS:Ansariel> Optional V1-like inventory accept messages
void LLOfferInfo::send_decline_response(void)
{
    LLUUID destination_folder_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_ImprovedInstantMessage);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_MessageBlock);
    msg->addBOOLFast(_PREHASH_FromGroup, false);
    msg->addUUIDFast(_PREHASH_ToAgentID, mFromID);
    msg->addU8Fast(_PREHASH_Offline, IM_ONLINE);
    msg->addUUIDFast(_PREHASH_ID, mTransactionID);
    msg->addU32Fast(_PREHASH_Timestamp, NO_TIMESTAMP); // no timestamp necessary
    std::string name;
    LLAgentUI::buildFullname(name);
    msg->addStringFast(_PREHASH_FromAgentName, name);
    msg->addStringFast(_PREHASH_Message, "");
    msg->addU32Fast(_PREHASH_ParentEstateID, 0);
    msg->addUUIDFast(_PREHASH_RegionID, LLUUID::null);
    msg->addVector3Fast(_PREHASH_Position, gAgent.getPositionAgent());
    msg->addU8Fast(_PREHASH_Dialog, (U8)(mIM + 2));
    msg->addBinaryDataFast(_PREHASH_BinaryBucket, &(destination_folder_id.mData), sizeof(destination_folder_id.mData));
    msg->sendReliable(mHost);
}
// </FS:Ansariel> Optional V1-like inventory accept messages
// R4B-COPY-END offer_info_responses

// R4B-COPY-BEGIN offer_info_callbacks
// source: indra/newview/llviewermessage.cpp
// function: LLOfferInfo::handleRespond .. initRespondFunctionMap
// lines: 1831-2362
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
void LLOfferInfo::handleRespond(const LLSD& notification, const LLSD& response)
{
    initRespondFunctionMap();

    const std::string name = notification["name"].asString();
    if(mRespondFunctions.find(name) == mRespondFunctions.end())
    {
        LL_WARNS() << "Unexpected notification name : " << name << LL_ENDL;
        llassert(!"Unexpected notification name");
        return;
    }

    mRespondFunctions[name](notification, response);
}

void inventory_offer_name_callback(const LLAvatarName& av_name, const std::string& from_name, const std::string& message)
{
    LLSD args;
    args["MESSAGE"] = llformat(from_name.c_str(), av_name.getUserName().c_str()) + message;
    LLNotificationsUtil::add("SystemMessageTip", args);
}

bool LLOfferInfo::inventory_offer_callback(const LLSD& notification, const LLSD& response)
{
    LLChat chat;
    std::string log_message;
    S32 button = LLNotificationsUtil::getSelectedOption(notification, response);

    LLInventoryObserver* opener = NULL;
    LLViewerInventoryCategory* catp = NULL;
    catp = (LLViewerInventoryCategory*)gInventory.getCategory(mObjectID);
    LLViewerInventoryItem* itemp = NULL;
    if(!catp)
    {
        itemp = (LLViewerInventoryItem*)gInventory.getItem(mObjectID);
    }

    LLNotificationPtr notification_ptr = LLNotifications::instance().find(notification["id"].asUUID());

    // For muting, we need to add the mute, then decline the offer.
    // This must be done here because:
    // * callback may be called immediately,
    // * adding the mute sends a message,
    // * we can't build two messages at once.
    if (IOR_MUTE == button) // Block
    {
        if (notification_ptr != NULL)
        {
            if (mFromGroup)
            {
                gCacheName->getGroup(mFromID, boost::bind(&inventory_offer_mute_callback, _1, _2, _3));
            }
            else
            {
                LLAvatarNameCache::get(mFromID, boost::bind(&inventory_offer_mute_avatar_callback, _1, _2));
            }
        }
    }

    std::string from_string; // Used in the pop-up.
    std::string chatHistory_string;  // Used in chat history.

    // TODO: when task inventory offers can also be handled the new way, migrate the code that sets these strings here:
    from_string = chatHistory_string = mFromName;

    // accept goes to proper folder, decline gets accepted to trash, muted gets declined
    bool accept_to_trash = true;

    LLNotificationFormPtr modified_form(notification_ptr ? new LLNotificationForm(*notification_ptr->getForm()) : new LLNotificationForm());

    switch(button)
    {
    case IOR_SHOW:
    case IOR_SHOW_SILENT: // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
        // we will want to open this item when it comes back.
        LL_DEBUGS("Messaging") << "Initializing an opener for tid: " << mTransactionID
                 << LL_ENDL;
        switch (mIM)
        {
        case IM_INVENTORY_OFFERED:
            {
                // This is an offer from an agent. In this case, the back
                // end has already copied the items into your inventory,
                // so we can fetch it out of our inventory.
// [RLVa:KB] - Checked: 2010-04-18 (RLVa-1.2.0)
                if ( (rlv_handler_t::isEnabled()) && (!RlvSettings::getForbidGiveToRLV()) && (LLAssetType::AT_CATEGORY == mType) && (mDesc.find(RLV_PUTINV_PREFIX) == 0) )
                {
                    RlvGiveToRLVAgentOffer* pOfferObserver = new RlvGiveToRLVAgentOffer(mObjectID);
                    pOfferObserver->startFetch();
                    if (pOfferObserver->isFinished())
                        pOfferObserver->done();
                    else
                        gInventory.addObserver(pOfferObserver);
                }
// [/RLVa:KB]

                // <FS:Ansariel> FIRE-3234: Ask if items should be previewed;
                // ShowOfferedInventory is always true anyway - instead there is
                // ShowNewInventory that is actually changable by the user!
                //if (gSavedSettings.getBOOL("ShowOfferedInventory"))
                {
                    // <FS:Ansariel> FIRE-23476: Don't select inventory offer in inventory if AutoAcceptNewInventory && !ShowInInventory && ShowNewInventory
                    //LLOpenAgentOffer* open_agent_offer = new LLOpenAgentOffer(mObjectID, from_string);
                    LLOpenAgentOffer* open_agent_offer = new LLOpenAgentOffer(mObjectID, from_string, true);
                    // </FS:Ansariel>
                    open_agent_offer->startFetch();
                    if(catp || (itemp && itemp->isFinished()))
                    {
                        open_agent_offer->done();
                    }
                    else
                    {
                        opener = open_agent_offer;
                    }
                }

                // <FS:Ansariel> Optional V1-like inventory accept messages
                if (gSavedSettings.getBOOL("FSUseLegacyInventoryAcceptMessages") && button == IOR_SHOW)
                {
                    send_auto_receive_response();
                }
                // </FS:Ansariel> Optional V1-like inventory accept messages
            }
            break;
        case IM_GROUP_NOTICE:
        case IM_GROUP_NOTICE_REQUESTED:
            opener = new LLOpenTaskGroupOffer;
            sendReceiveResponse(true, mFolderID);
            break;
        case IM_TASK_INVENTORY_OFFERED:
            // This is an offer from a task or group.
            // We don't use a new instance of an opener
            // We instead use the singular observer gOpenTaskOffer
            // Since it already exists, we don't need to actually do anything
            break;
        default:
            LL_WARNS("Messaging") << "inventory_offer_callback: unknown offer type" << LL_ENDL;
            break;
        }

        if (modified_form != NULL)
        {
            modified_form->setElementEnabled("Show", false);
        }
        break;
        // end switch (mIM)

    case IOR_ACCEPT:
    case IOR_ACCEPT_SILENT: // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
        //don't spam them if they are getting flooded
        if (check_offer_throttle(mFromName, true))
        {
            // <FS:Ansariel> This breaks object owner name parsing
            //log_message = "<nolink>" + chatHistory_string + "</nolink> " + LLTrans::getString("InvOfferGaveYou") + " " + getSanitizedDescription() + LLTrans::getString(".");
            // <FS:Ansariel> FIRE-29677 / SL-13720 workaround
            //log_message = chatHistory_string + " " + LLTrans::getString("InvOfferGaveYou") + " " + getSanitizedDescription() + LLTrans::getString(".");
            // </FS:Ansariel>

            //LLSD args;
            //args["MESSAGE"] = log_message;
            //LLNotificationsUtil::add("SystemMessageTip", args);
            log_message = " " + LLTrans::getString("InvOfferGaveYou") + " " + getSanitizedDescription() + LLTrans::getString(".");

            LLUUID inv_sender_id;
            size_t separator_idx = mFromName.find('|');
            if (separator_idx != std::string::npos && LLUUID::parseUUID(mFromName.substr(0, separator_idx), &inv_sender_id) && mFromName.size() > (++separator_idx))
            {
                LLAvatarNameCache::instance().get(inv_sender_id, boost::bind(&inventory_offer_name_callback, _2, mFromName.substr(separator_idx), log_message));
            }
            else
            {
                LLSD args;
                args["MESSAGE"] = chatHistory_string + log_message;
                LLNotificationsUtil::add("SystemMessageTip", args);
            }
            // </FS:Ansariel>
        }

        // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
        if (mIM == IM_GROUP_NOTICE || mIM == IM_GROUP_NOTICE_REQUESTED)
        {
            opener = new LLOpenTaskGroupOffer;
            sendReceiveResponse(true, mFolderID);
        }
        else if (mIM == IM_INVENTORY_OFFERED)
        {
// [RLVa:KB] - Checked: 2010-04-18 (RLVa-1.2.0)
            if ( (rlv_handler_t::isEnabled()) && (!RlvSettings::getForbidGiveToRLV()) && (LLAssetType::AT_CATEGORY == mType) && (mDesc.find(RLV_PUTINV_PREFIX) == 0) )
            {
                RlvGiveToRLVAgentOffer* pOfferObserver = new RlvGiveToRLVAgentOffer(mObjectID);
                pOfferObserver->startFetch();
                if (pOfferObserver->isFinished())
                    pOfferObserver->done();
                else
                    gInventory.addObserver(pOfferObserver);
            }
// [/RLVa:KB]

            if (gSavedSettings.getBOOL("FSUseLegacyInventoryAcceptMessages") && button == IOR_ACCEPT)
            {
                send_auto_receive_response();
            }
            if (gSavedSettings.getBOOL("ShowInInventory"))
            {
                LLInventoryPanel::openInventoryPanelAndSetSelection(true, mObjectID);
            }
        }
        // </FS:Ansariel>

        break;

    case IOR_MUTE:
        if (modified_form != NULL)
        {
            modified_form->setElementEnabled("Mute", false);
        }
        accept_to_trash = false; // for notices, but IOR_MUTE normally doesn't happen for notices
        // MUTE falls through to decline
    case IOR_DECLINE:
    case IOR_DECLINE_SILENT: // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
        {
            {
                LLStringUtil::format_map_t log_message_args;
                log_message_args["DESC"] = mDesc;
                log_message_args["NAME"] = mFromName;
                log_message = LLTrans::getString("InvOfferDecline", log_message_args);
            }
            chat.mText = log_message;
            if( LLMuteList::getInstance()->isMuted(mFromID ) && ! LLMuteList::isLinden(mFromName) )  // muting for SL-42269
            {
                chat.mMuted = true;
                accept_to_trash = false; // will send decline message
            }

            // *NOTE dzaporozhan
            // Disabled logging to old chat floater to fix crash in group notices - EXT-4149
            // LLFloaterChat::addChatHistory(chat);

            if (mObjectID.notNull()) //make sure we can discard
            {
                LLDiscardAgentOffer* discard_agent_offer = new LLDiscardAgentOffer(mFolderID, mObjectID);
                discard_agent_offer->startFetch();
                if ((catp && gInventory.isCategoryComplete(mObjectID)) || (itemp && itemp->isFinished()))
                {
                    discard_agent_offer->done();
                }
                else
                {
                    opener = discard_agent_offer;
                }
            }
            // <FS:Ansariel> Make group notice attachment move to trash optional
            //else if (mIM == IM_GROUP_NOTICE)
            else if (mIM == IM_GROUP_NOTICE && gSavedSettings.getBOOL("FSDismissGroupNoticeAttachmentsToTrash"))
            // </FS:Ansariel>
            {
                // group notice needs to request object to trash so that user will see it later
                // Note: muted agent offers go to trash, not sure if we should do same for notices
                LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
                sendReceiveResponse(accept_to_trash, trash);
            }

            // <FS:Ansariel> Optional V1-like inventory accept messages
            if ((gSavedSettings.getBOOL("FSUseLegacyInventoryAcceptMessages") && button == IOR_DECLINE) && mIM == IM_INVENTORY_OFFERED)
            {
                send_decline_response();
            }
            // </FS:Ansariel> Optional V1-like inventory accept messages
            if (modified_form != NULL)
            {
                modified_form->setElementEnabled("Show", false);
                modified_form->setElementEnabled("Discard", false);
            }

            break;
        }
    default:
        // close button probably
        // In case of agent offers item has already been fetched and is in your inventory, we simply won't highlight it
        // OR delete it if the notification gets killed, since we don't want that to be a vector for
        // losing inventory offers.
        // <FS:Ansariel> Make group notice attachment move to trash optional
        //if (mIM == IM_GROUP_NOTICE)
        if (mIM == IM_GROUP_NOTICE && gSavedSettings.getBOOL("FSDismissGroupNoticeAttachmentsToTrash"))
        // </FS:Ansariel>
        {
            LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
            sendReceiveResponse(true, trash);
        }
        break;
    }

    if(opener)
    {
        gInventory.addObserver(opener);
    }

    if(!mPersist)
    {
        delete this;
    }

    return false;
}

bool LLOfferInfo::inventory_task_offer_callback(const LLSD& notification, const LLSD& response)
{
    LLChat chat;
    std::string log_message;
    S32 button = LLNotification::getSelectedOption(notification, response);

    // For muting, we need to add the mute, then decline the offer.
    // This must be done here because:
    // * callback may be called immediately,
    // * adding the mute sends a message,
    // * we can't build two messages at once.
    if (2 == button)
    {
        LLNotificationPtr notification_ptr = LLNotifications::instance().find(notification["id"].asUUID());

        llassert(notification_ptr != NULL);
        if (notification_ptr != NULL)
        {
            if (mFromGroup)
            {
                gCacheName->getGroup(mFromID, boost::bind(&inventory_offer_mute_callback, _1, _2, _3));
            }
            else
            {
                LLAvatarNameCache::get(mFromID, boost::bind(&inventory_offer_mute_avatar_callback, _1, _2));
            }
        }
    }

    std::string from_string; // Used in the pop-up.
    std::string chatHistory_string;  // Used in chat history.

    if (mFromObject)
    {
        std::string quot = LLTrans::getString("'");
        if (mFromGroup)
        {
            std::string group_name;
            if (gCacheName->getGroupName(mFromID, group_name))
            {
                from_string = LLTrans::getString("InvOfferAnObjectNamed") + " " +
                    quot + mFromName + quot + " " +
                    LLTrans::getString("InvOfferOwnedByGroup") + " " +
                    quot + group_name + quot;
                chatHistory_string = mFromName + " " +
                    LLTrans::getString("InvOfferOwnedByGroup") + " " +
                    quot + group_name + quot;
            }
            else
            {
                from_string = LLTrans::getString("InvOfferAnObjectNamed") + " " +
                    quot + mFromName + quot + " " +
                    LLTrans::getString("InvOfferOwnedByUnknownGroup");
                chatHistory_string = mFromName + " " +
                    LLTrans::getString("InvOfferOwnedByUnknownGroup");
            }
        }
        else
        {
/*
            LLAvatarName av_name;
            if (LLAvatarNameCache::get(mFromID, &av_name))
            {
                from_string = LLTrans::getString("InvOfferAnObjectNamed") + " " +
                    quot + mFromName + quot + " " +
                    LLTrans::getString("InvOfferOwnedBy") + " " + av_name.getUserName();
                chatHistory_string = mFromName + " " +
                    LLTrans::getString("InvOfferOwnedBy") + " " + av_name.getUserName();
            }
            else
            {
                from_string = LLTrans::getString("InvOfferAnObjectNamed") + " " +
                    quot + mFromName + quot + " " +
                    LLTrans::getString("InvOfferOwnedByUnknownUser");
                chatHistory_string = mFromName + " " +
                    LLTrans::getString("InvOfferOwnedByUnknownUser");
            }
*/
// [SL:KB] - Checked: 2010-11-02 (RLVa-1.2.2a) | Added: RLVa-1.2.2a
            std::string name_slurl = LLSLURL("agent", mFromID, "about").getSLURLString();

// [RLVa:KB] - Checked: RLVa-2.0.1
            // RELEASE-RLVa: [RLVa-2.0.1] Make sure this stays in sync with the condition in inventory_offer_handler()
            bool fRlvCanShowName = (!RlvActions::isRlvEnabled()) ||
                (RlvActions::canShowName(RlvActions::SNC_DEFAULT, mFromID)) || (!RlvUtil::isNearbyAgent(mFromID)) || (RlvUIEnabler::hasOpenIM(mFromID)) || (RlvUIEnabler::hasOpenProfile(mFromID));
            if (!fRlvCanShowName)
                name_slurl = LLSLURL("agent", mFromID, "rlvanonym").getSLURLString();
// [/RLVa:KB]

            from_string = LLTrans::getString("InvOfferAnObjectNamed") + " "+ LLTrans::getString("'") + mFromName
                + LLTrans::getString("'")+" " + LLTrans::getString("InvOfferOwnedBy") + name_slurl;
            chatHistory_string = mFromName + " " + LLTrans::getString("InvOfferOwnedBy") + " " + name_slurl;
// [/SL:KB]
        }
    }
    else
    {
        from_string = chatHistory_string = mFromName;
    }

    LLUUID destination;
    bool accept = true;

    // If user accepted, accept to proper folder, if user discarded, accept to trash.
    switch(button)
    {
        case IOR_ACCEPT:
// [RLVa:KB] - Checked: 2010-09-23 (RLVa-1.2.1)
            // Only treat the offer as 'Give to #RLV' if:
            //   - the user has enabled the feature
            //   - the inventory offer came from a script (and specifies a folder)
            //   - the name starts with the prefix - mDesc format: '[OBJECTNAME]'  ( http://slurl.com/... )
            if ( (rlv_handler_t::isEnabled()) && (IM_TASK_INVENTORY_OFFERED == mIM) && (LLAssetType::AT_CATEGORY == mType) && (mDesc.find(RLV_PUTINV_PREFIX) == 1) )
            {
                if (!RlvSettings::getForbidGiveToRLV())
                {
                    const LLUUID& idRlvRoot = RlvInventory::instance().getSharedRootID();
                    if (idRlvRoot.notNull())
                        mFolderID = idRlvRoot;

                    // "accepted_in_rlv" is sent from RlvGiveToRLVTaskOffer *after* we have the folder
                    RlvGiveToRLVTaskOffer* pOfferObserver = new RlvGiveToRLVTaskOffer(mTransactionID);
                    gInventory.addObserver(pOfferObserver);
                }
                else
                {
                    std::string::size_type idxToken = mDesc.find("'  ( http://");
                    if (std::string::npos != idxToken)
                    {
                        RlvBehaviourNotifyHandler::sendNotification("accepted_in_inv inv_offer " + mDesc.substr(1, idxToken - 1));
                    }
                }
            }
// [/RLVa:KB]

            destination = mFolderID;
            //don't spam user if flooded
            if (check_offer_throttle(mFromName, true))
            {
                // <FS:Ansariel> This breaks object owner name parsing
                //log_message = "<nolink>" + chatHistory_string + "</nolink> " + LLTrans::getString("InvOfferGaveYou") + " " + getSanitizedDescription() + LLTrans::getString(".");
                log_message = chatHistory_string + " " + LLTrans::getString("InvOfferGaveYou") + " " + getSanitizedDescription() + LLTrans::getString(".");
                // </FS:Ansariel>
                LLSD args;
                args["MESSAGE"] = log_message;
                LLNotificationsUtil::add("SystemMessageTip", args);
            }
            break;
        case IOR_MUTE:
            // MUTE falls through to decline
            accept = false;
        case IOR_DECLINE:
        default:
            // close button probably (or any of the fall-throughs from above)
            destination = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
// [RLVa:KB] - Checked: 2010-09-23 (RLVa-1.2.1e) | Added: RLVa-1.2.1e
            if ( (rlv_handler_t::isEnabled()) &&
                 (IM_TASK_INVENTORY_OFFERED == mIM) && (LLAssetType::AT_CATEGORY == mType) && (mDesc.find(RLV_PUTINV_PREFIX) == 1) )
            {
                std::string::size_type idxToken = mDesc.find("'  ( http://");
                if (std::string::npos != idxToken)
                    RlvBehaviourNotifyHandler::sendNotification("declined inv_offer " + mDesc.substr(1, idxToken - 1));
            }
// [/RLVa:KB]
            // <FS:Ansariel> Leave this notification in
            if (gSavedSettings.getBOOL("LogInventoryDecline"))
            {
                LLStringUtil::format_map_t log_message_args;
                log_message_args["DESC"] = mDesc;
                log_message_args["NAME"] = mFromName;
                log_message = LLTrans::getString("InvOfferDecline", log_message_args);


                LLSD args;
                args["MESSAGE"] = log_message;
                LLNotificationsUtil::add("SystemMessageTip", args);
            }
            // </FS:Ansariel>
            if (accept && LLMuteList::getInstance()->isMuted(mFromID, mFromName))
            {
                // Note: muted offers are usually declined automatically,
                // but user can mute object after receiving message
                accept = false;
            }
            break;
    }

    sendReceiveResponse(accept, destination);

    if(!mPersist)
    {
        delete this;
    }
    return false;
}

std::string LLOfferInfo::getSanitizedDescription()
{
    // currently we get description from server as: 'Object' ( Location )
    // object name shouldn't be shown as a hyperlink
    std::string description = mDesc;

    std::size_t start = mDesc.find_first_of("'");
    std::size_t end = mDesc.find_last_of("'");
    // <FS:Ansariel> FIRE-29194: <nolink> tags showing in offline inventory offer log
    //if ((start != std::string::npos) && (end != std::string::npos))
    if ((start != std::string::npos) && (end != std::string::npos) && start != end)
    // </FS:Ansariel>
    {
        description.insert(start, "<nolink>");
        description.insert(end + 8, "</nolink>");
    }
    return description;
}


void LLOfferInfo::initRespondFunctionMap()
{
    if(mRespondFunctions.empty())
    {
        mRespondFunctions["ObjectGiveItem"] = boost::bind(&LLOfferInfo::inventory_task_offer_callback, this, _1, _2);
        mRespondFunctions["OwnObjectGiveItem"] = boost::bind(&LLOfferInfo::inventory_task_offer_callback, this, _1, _2);
        mRespondFunctions["UserGiveItem"] = boost::bind(&LLOfferInfo::inventory_offer_callback, this, _1, _2);
        // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
        mRespondFunctions["UserGiveItemLegacy"] = boost::bind(&LLOfferInfo::inventory_offer_callback, this, _1, _2);
    }
}
// R4B-COPY-END offer_info_callbacks

// R4B-COPY-BEGIN send_dnd_message
// source: indra/newview/llviewermessage.cpp
// function: send_do_not_disturb_message
// lines: 2570-2593
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
void send_do_not_disturb_message (LLMessageSystem* msg, const LLUUID& from_id, const LLUUID& session_id)
{
    if (gAgent.isDoNotDisturb())
    {
        std::string my_name;
        LLAgentUI::buildFullname(my_name);
        // <FS> FS autoresponse feature
        // std::string response = gSavedPerAccountSettings.getString("DoNotDisturbModeResponse");
        std::string response = LLIMProcessing::getAutoresponseTextForAvatar(from_id, true, false, false, false, false, false);
        // </FS>
        pack_instant_message(
            msg,
            gAgent.getID(),
            false,
            gAgent.getSessionID(),
            from_id,
            my_name,
            response,
            IM_ONLINE,
            IM_DO_NOT_DISTURB_AUTO_RESPONSE,
            session_id);
        gAgent.sendReliableMessage();
    }
}
// R4B-COPY-END send_dnd_message

// R4B-COPY-BEGIN offer_force_response
// source: indra/newview/llviewermessage.cpp
// function: LLOfferInfo::forceResponse
// lines: 8860-8889
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
void LLOfferInfo::forceResponse(InventoryOfferResponse response)
{
    // <FS:Ansariel> Now this is a hell of piece of... forceResponse() will look for the
    //               ELEMENT index, and NOT the button index. So if we want to force a
    //               response of IOR_ACCEPT, we need to pass the correct element
    //               index of the button.
    //LLNotification::Params params("UserGiveItem");
    //params.functor.function(boost::bind(&LLOfferInfo::inventory_offer_callback, this, _1, _2));
    //LLNotifications::instance().forceResponse(params, response);
    S32 element_index;
    switch (response)
    {
        case IOR_ACCEPT:
            element_index = 1;
            break;
        case IOR_DECLINE:
            element_index = 2;
            break;
        case IOR_MUTE:
            element_index = 3;
            break;
        default:
            element_index = -1;
            break;
    }
    LLNotification::Params params("UserGiveItem");
    params.functor.function(boost::bind(&LLOfferInfo::inventory_offer_callback, this, _1, _2));
    LLNotifications::instance().forceResponse(params, element_index);
    // </FS:Ansariel>
}
// R4B-COPY-END offer_force_response

// R4B-COPY-BEGIN clean_name_from_im
// source: indra/newview/llimprocessing.cpp
// function: clean_name_from_im
// lines: 91-139
// blob: 8fa885d340173cce97bd512894040370f970b57a
static std::string clean_name_from_im(const std::string& name, EInstantMessage type)
{
    switch (type)
    {
        case IM_NOTHING_SPECIAL:
        case IM_MESSAGEBOX:
        case IM_GROUP_INVITATION:
        case IM_INVENTORY_OFFERED:
        case IM_INVENTORY_ACCEPTED:
        case IM_INVENTORY_DECLINED:
        case IM_GROUP_VOTE:
        case IM_GROUP_MESSAGE_DEPRECATED:
            //IM_TASK_INVENTORY_OFFERED
            //IM_TASK_INVENTORY_ACCEPTED
            //IM_TASK_INVENTORY_DECLINED
        case IM_NEW_USER_DEFAULT:
        case IM_SESSION_INVITE:
        case IM_SESSION_P2P_INVITE:
        case IM_SESSION_GROUP_START:
        case IM_SESSION_CONFERENCE_START:
        case IM_SESSION_SEND:
        case IM_SESSION_LEAVE:
            //IM_FROM_TASK
        case IM_DO_NOT_DISTURB_AUTO_RESPONSE:
        case IM_CONSOLE_AND_CHAT_HISTORY:
        case IM_LURE_USER:
        case IM_LURE_ACCEPTED:
        case IM_LURE_DECLINED:
        case IM_GODLIKE_LURE_USER:
        case IM_TELEPORT_REQUEST:
        case IM_GROUP_ELECTION_DEPRECATED:
            //IM_GOTO_URL
            //IM_FROM_TASK_AS_ALERT
        case IM_GROUP_NOTICE:
        case IM_GROUP_NOTICE_INVENTORY_ACCEPTED:
        case IM_GROUP_NOTICE_INVENTORY_DECLINED:
        case IM_GROUP_INVITATION_ACCEPT:
        case IM_GROUP_INVITATION_DECLINE:
        case IM_GROUP_NOTICE_REQUESTED:
        case IM_FRIENDSHIP_OFFERED:
        case IM_FRIENDSHIP_ACCEPTED:
        case IM_FRIENDSHIP_DECLINED_DEPRECATED:
            //IM_TYPING_START
            //IM_TYPING_STOP
            return LLCacheName::cleanFullName(name);
        default:
            return name;
    }
}
// R4B-COPY-END clean_name_from_im

// R4B-COPY-BEGIN inventory_offer_handler
// source: indra/newview/llimprocessing.cpp
// function: LLPostponedOfferNotification, inventory_offer_handler
// lines: 255-547
// blob: 8fa885d340173cce97bd512894040370f970b57a

class LLPostponedOfferNotification : public LLPostponedNotification
{
protected:
    /* virtual */
    void modifyNotificationParams()
    {
        LLSD substitutions = mParams.substitutions;
        substitutions["NAME"] = mName;
        mParams.substitutions = substitutions;
    }
};

void inventory_offer_handler(LLOfferInfo* info)
{
    // If muted, don't even go through the messaging stuff.  Just curtail the offer here.
    // Passing in a null UUID handles the case of where you have muted one of your own objects by_name.
    // The solution for STORM-1297 seems to handle the cases where the object is owned by someone else.
    if (LLMuteList::getInstance()->isMuted(info->mFromID, info->mFromName) ||
        LLMuteList::getInstance()->isMuted(LLUUID::null, info->mFromName))
    {
        info->forceResponse(IOR_MUTE);
        return;
    }


    bool bAutoAccept(false);
    // Avoid the Accept/Discard dialog if the user so desires. JC
    // <FS:Ansariel> Auto-accept any kind of inventory (FIRE-4128)
    //if (gSavedSettings.getBOOL("AutoAcceptNewInventory")
    //  && (info->mType == LLAssetType::AT_NOTECARD
    //      || info->mType == LLAssetType::AT_LANDMARK
    //      || info->mType == LLAssetType::AT_TEXTURE))
//  if (gSavedSettings.getBOOL("AutoAcceptNewInventory"))
    // </FS:Ansariel> Auto-accept any kind of inventory (FIRE-4128)
// [RLVa:KB]
    // Don't auto-accept give-to-RLV inventory offers
    if ( (gSavedSettings.getBOOL("AutoAcceptNewInventory")) &&
         ( (!rlv_handler_t::isEnabled()) || (!RlvInventory::instance().isGiveToRLVOffer(*info)) ) )
// [/RLVa:KB]
    {
        // For certain types, just accept the items into the inventory,
        // and possibly open them on receipt depending upon "ShowNewInventory".
        bAutoAccept = true;
    }

    // Strip any SLURL from the message display. (DEV-2754)
    std::string msg = info->mDesc;
    auto indx = msg.find(" ( http://slurl.com/secondlife/");
    if (indx == std::string::npos)
    {
        // https
        indx = msg.find(" ( https://slurl.com/secondlife/");
    }
    if (indx == std::string::npos)
    {
        // try to find new slurl http host
        indx = msg.find(" ( http://maps.secondlife.com/secondlife/");
    }
    if (indx == std::string::npos)
    {
        // try to find new slurl https host
        indx = msg.find(" ( https://maps.secondlife.com/secondlife/");
    }
    if (indx >= 0)
    {
        LLStringUtil::truncate(msg, indx);
    }

    LLSD args;
    args["[OBJECTNAME]"] = msg;

    LLSD payload;

    // must protect against a NULL return from lookupHumanReadable()
    std::string typestr = ll_safe_string(LLAssetType::lookupHumanReadable(info->mType));
    if (!typestr.empty())
    {
        // human readable matches string name from strings.xml
        // lets get asset type localized name
        args["OBJECTTYPE"] = LLTrans::getString(typestr);
    }
    else
    {
        LL_WARNS("Messaging") << "LLAssetType::lookupHumanReadable() returned NULL - probably bad asset type: " << info->mType << LL_ENDL;
        args["OBJECTTYPE"] = "";

        // This seems safest, rather than propagating bogosity
        LL_WARNS("Messaging") << "Forcing an inventory-decline for probably-bad asset type." << LL_ENDL;
        info->forceResponse(IOR_DECLINE);
        return;
    }

    // If mObjectID is null then generate the object_id based on msg to prevent
    // multiple creation of chiclets for same object.
    LLUUID object_id = info->mObjectID;
    if (object_id.isNull())
        object_id.generate(msg);

    payload["from_id"] = info->mFromID;
    // Needed by LLScriptFloaterManager to bind original notification with
    // faked for toast one.
    payload["object_id"] = object_id;
    // Flag indicating that this notification is faked for toast.
    payload["give_inventory_notification"] = false;
    args["OBJECTFROMNAME"] = info->mFromName;
    args["NAME"] = info->mFromName;
    if (info->mFromGroup)
    {
        args["NAME_SLURL"] = LLSLURL("group", info->mFromID, "about").getSLURLString();
    }
    else
    {
// [SL:KB] - Patch: UI-Notifications | Checked: 2011-04-11 (Catznip-2.5.0a) | Added: Catznip-2.5.0a
        args["NAME_LABEL"] = LLSLURL("agent", info->mFromID, "completename").getSLURLString();
// [/SL:KB]
        args["NAME_SLURL"] = LLSLURL("agent", info->mFromID, "about").getSLURLString();
    }
    std::string verb = "select?name=" + LLURI::escape(msg);
    args["ITEM_SLURL"] = LLSLURL("inventory", info->mObjectID, verb.c_str()).getSLURLString();

    LLNotification::Params p;

    // Object -> Agent Inventory Offer
    if (info->mFromObject && !bAutoAccept)
    {
// [RLVa:KB] - Checked: RLVa-1.2.2
        // Only filter if the object owner is a nearby agent
        if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, info->mFromID)) && (RlvUtil::isNearbyAgent(info->mFromID)) )
        {
            payload["rlv_shownames"] = true;
            args["NAME_SLURL"] = LLSLURL("agent", info->mFromID, "rlvanonym").getSLURLString();
        }
// [/RLVa:KB]

        // Inventory Slurls don't currently work for non agent transfers, so only display the object name.
        args["ITEM_SLURL"] = msg;
        // Note: sets inventory_task_offer_callback as the callback
        p.substitutions(args).payload(payload).functor.responder(LLNotificationResponderPtr(info));
        info->mPersist = true;

        // Offers from your own objects need a special notification template.
        p.name = info->mFromID == gAgentID ? "OwnObjectGiveItem" : "ObjectGiveItem";

        // Pop up inv offer chiclet and let the user accept (keep), or reject (and silently delete) the inventory.
        LLPostponedNotification::add<LLPostponedOfferNotification>(p, info->mFromID, info->mFromGroup);
    }
    else // Agent -> Agent Inventory Offer
    {
// [RLVa:KB] - Checked: RLVa-2.0.1
        // Only filter if the offer is from a nearby agent and if there's no open IM session (doesn't necessarily have to be focused)
        bool fRlvCanShowName = (!RlvActions::isRlvEnabled()) ||
            (RlvActions::canShowName(RlvActions::SNC_DEFAULT, info->mFromID)) || (!RlvUtil::isNearbyAgent(info->mFromID)) || (RlvUIEnabler::hasOpenIM(info->mFromID)) || (RlvUIEnabler::hasOpenProfile(info->mFromID));
        if (!fRlvCanShowName)
        {
            payload["rlv_shownames"] = true;
            LLAvatarName av_name;
            if (LLAvatarNameCache::get(info->mFromID, &av_name))
            {
                args["NAME"] = RlvStrings::getAnonym(av_name);
            }
            else
            {
                args["NAME"] = RlvStrings::getAnonym(info->mFromName);
            }
            args["NAME_SLURL"] = LLSLURL("agent", info->mFromID, "rlvanonym").getSLURLString();
        }
// [/RLVa:KB]

        p.responder = info;
        // Note: sets inventory_offer_callback as the callback
        // *TODO fix memory leak
        // inventory_offer_callback() is not invoked if user received notification and
        // closes viewer(without responding the notification)
        p.substitutions(args).payload(payload).functor.responder(LLNotificationResponderPtr(info));
        info->mPersist = true;
        // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
        //p.name = "UserGiveItem";
        p.name = (gSavedSettings.getBOOL("FSUseLegacyInventoryAcceptMessages") ? "UserGiveItemLegacy" : "UserGiveItem");
        // </FS:Ansariel>
        p.offer_from_agent = true;

        // Prefetch the item into your local inventory.
        LLInventoryFetchItemsObserver* fetch_item = new LLInventoryFetchItemsObserver(info->mObjectID);
        fetch_item->startFetch();
        if(fetch_item->isFinished())
        {
            fetch_item->done();
        }
        else
        {
            gInventory.addObserver(fetch_item);
        }

        // In viewer 2 we're now auto receiving inventory offers and messaging as such (not sending reject messages).
        // <FS:Ansariel> Optional V1-like inventory accept messages
        //info->send_auto_receive_response();
        // Also needs to be send on auto-accept so the item gets into the inventory!
        if (bAutoAccept || !gSavedSettings.getBOOL("FSUseLegacyInventoryAcceptMessages"))
        {
            info->send_auto_receive_response();
        }
        // </FS:Ansariel> Optional V1-like inventory accept messages

        if (gAgent.isDoNotDisturb())
        {
            send_do_not_disturb_message(gMessageSystem, info->mFromID);
        }

        if (!bAutoAccept) // if we auto accept, do not pester the user
        {
            // Inform user that there is a script floater via toast system
            payload["give_inventory_notification"] = true;
            p.payload = payload;
            LLPostponedNotification::add<LLPostponedOfferNotification>(p, info->mFromID, false);
        }
        // <FS:Ansariel> FIRE-19540: Log auto-accepted inventory to nearby chat
        else if (gSavedSettings.getBOOL("FSLogAutoAcceptInventoryToChat") || gSavedSettings.getBOOL("FSShowAutoAcceptInventoryInNotifications"))
        {
            std::string message_type;
            LLStringUtil::format_map_t chat_args;

            chat_args["OBJECT_TYPE"] = (!typestr.empty() ? LLTrans::getString(typestr) : "");
            chat_args["DESC"] = msg;

            if (info->mFromObject)
            {
                std::string str_pos;
                std::string::size_type idx_start = info->mDesc.rfind(" ( http://");
                std::string::size_type idx_end = info->mDesc.find(" )", idx_start);
                if (idx_start != std::string::npos && idx_end != std::string::npos)
                {
                    LLSLURL url_pos(info->mDesc.substr(idx_start + 3, idx_end - (idx_start + 3)));
                    str_pos = "&slurl=" + LLURI::escape(url_pos.getLocationString());
                }

                chat_args["OBJECT_NAME"] = "[" + LLSLURL("objectim", info->mObjectID, "").getSLURLString() + "?name=" + LLURI::escape(info->mFromName) + "&owner=" + info->mFromID.asString() + (info->mFromGroup ? "&groupowned=true" : "") + str_pos + " " + info->mFromName + "]";
                message_type = "InvOfferAutoAcceptObject";
            }
            else
            {
                bool fRlvCanShowName = (!RlvActions::isRlvEnabled()) ||
                    (RlvActions::canShowName(RlvActions::SNC_DEFAULT, info->mFromID)) || (!RlvUtil::isNearbyAgent(info->mFromID)) || (RlvUIEnabler::hasOpenIM(info->mFromID)) || (RlvUIEnabler::hasOpenProfile(info->mFromID));

                std::string name_slurl = LLSLURL("agent", info->mFromID, (fRlvCanShowName ? "inspect" : "rlvanonym")).getSLURLString();

                chat_args["USER_NAME"] = name_slurl;
                message_type = "InvOfferAutoAcceptUser";
            }

            std::string inv_offer_message = LLTrans::getString(message_type, chat_args);
            if (gSavedSettings.getBOOL("FSLogAutoAcceptInventoryToChat"))
            {
                FSCommon::report_to_nearby_chat(inv_offer_message);
            }
            if (gSavedSettings.getBOOL("FSShowAutoAcceptInventoryInNotifications"))
            {
                LLSD args;
                args["MESSAGE"] = inv_offer_message;
                LLNotificationsUtil::add("IMSystemMessageTip", args);
            }

            make_ui_sound("UISndInventoryOffer");
        }
        // </FS:Ansariel>

        // <FS:Ansariel> Show offered inventory also if auto-accept is enabled (FIRE-5101)
        if (bAutoAccept && gSavedSettings.getBOOL("ShowNewInventory"))
        {
            LLViewerInventoryCategory* catp = NULL;
            catp = (LLViewerInventoryCategory*)gInventory.getCategory(info->mObjectID);
            LLViewerInventoryItem* itemp = NULL;
            if(!catp)
            {
                itemp = (LLViewerInventoryItem*)gInventory.getItem(info->mObjectID);
            }

            LLOpenAgentOffer* open_agent_offer = new LLOpenAgentOffer(info->mObjectID, info->mFromName, false);
            open_agent_offer->startFetch();
            if(catp || (itemp && itemp->isFinished()))
            {
                open_agent_offer->done();
            }
            else
            {
                gInventory.addObserver(open_agent_offer);
            }
        }
        // </FS:Ansariel> Show offered inventory also if auto-accept is enabled (FIRE-5101)
    }

    LLFirstUse::newInventory();
}
// R4B-COPY-END inventory_offer_handler

// R4B-COPY-BEGIN give_inventory
// source: indra/newview/llgiveinventory.cpp
// function: LLGiveable .. LLGiveInventory::commitGiveInventoryCategory
// lines: 59-693
// blob: bab5f1cff6a6f70484ea449991848618f517fc07
// substitutions: 'transaction_id.generate();' -> 'r4b_generate(transaction_id);'
// MAX ITEMS is based on (sizeof(uuid)+2) * count must be < MTUBYTES
// or 18 * count < 1200 => count < 1200/18 => 66. I've cut it down a
// bit from there to give some pad.
const S32 MAX_ITEMS = 42;

class LLGiveable : public LLInventoryCollectFunctor
{
public:
    LLGiveable() : mCountLosing(0) {}
    virtual ~LLGiveable() {}
    virtual bool operator()(LLInventoryCategory* cat, LLInventoryItem* item);

    S32 countNoCopy() const { return mCountLosing; }
protected:
    S32 mCountLosing;
};

bool LLGiveable::operator()(LLInventoryCategory* cat, LLInventoryItem* item)
{
    // All categories can be given.
    if (cat)
        return true;

    bool allowed = false;
    if (item)
    {
        allowed = itemTransferCommonlyAllowed(item);
        if (allowed &&
           !item->getPermissions().allowOperationBy(PERM_TRANSFER,
                                gAgent.getID()))
        {
            allowed = false;
        }
        if (allowed &&
           !item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            ++mCountLosing;
        }
    }
    return allowed;
}

class LLUncopyableItems : public LLInventoryCollectFunctor
{
public:
    LLUncopyableItems() {}
    virtual ~LLUncopyableItems() {}
    virtual bool operator()(LLInventoryCategory* cat, LLInventoryItem* item);
};

bool LLUncopyableItems::operator()(LLInventoryCategory* cat,
                                   LLInventoryItem* item)
{
    bool uncopyable = false;
    if (item)
    {
        if (itemTransferCommonlyAllowed(item) &&
            !item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            uncopyable = true;
        }
    }
    return uncopyable;
}

// static
bool LLGiveInventory::isInventoryGiveAcceptable(const LLInventoryItem* item)
{
    if (!item) return false;

    if (!isAgentAvatarValid()) return false;

    if (!item->getPermissions().allowOperationBy(PERM_TRANSFER, gAgentID))
    {
        return false;
    }

    bool acceptable = true;
    switch(item->getType())
    {
    case LLAssetType::AT_OBJECT:
    case LLAssetType::AT_BODYPART:
    case LLAssetType::AT_CLOTHING:
        {
            if (get_is_item_worn(item->getUUID()))
            {
                acceptable = false;
            }
            break;
        }
        break;
    default:
        break;
    }
    return acceptable;
}

// static
bool LLGiveInventory::isInventoryGroupGiveAcceptable(const LLInventoryItem* item)
{
    if (!item) return false;

    if (!isAgentAvatarValid()) return false;

    // These permissions are double checked in the simulator in
    // LLGroupNoticeInventoryItemFetch::result().
    if (!item->getPermissions().allowOperationBy(PERM_TRANSFER, gAgentID))
    {
        return false;
    }
    if (!item->getPermissions().allowCopyBy(gAgent.getID()))
    {
        return false;
    }


    bool acceptable = true;
    switch(item->getType())
    {
    case LLAssetType::AT_OBJECT:
        if (gAgentAvatarp->isWearingAttachment(item->getUUID()))
        {
            acceptable = false;
        }
        break;
    default:
        break;
    }
    return acceptable;
}

// static
bool LLGiveInventory::doGiveInventoryItem(const LLUUID& to_agent,
                                      const LLInventoryItem* item,
                                      const LLUUID& im_session_id/* = LLUUID::null*/)

{
    bool res = true;
    LL_INFOS() << "LLGiveInventory::giveInventory()" << LL_ENDL;
    if (!isInventoryGiveAcceptable(item))
    {
        return false;
    }
    if (item->getPermissions().allowCopyBy(gAgentID))
    {
        // just give it away.
// [RLVa:KB] - @share
        res = LLGiveInventory::commitGiveInventoryItem(to_agent, item, im_session_id);
// [/RLVa:KB]
//      LLGiveInventory::commitGiveInventoryItem(to_agent, item, im_session_id);
    }
    else
    {
        // ask if the agent is sure.
        LLSD substitutions;
        substitutions["ITEMS"] = item->getName();
        LLSD payload;
        payload["agent_id"] = to_agent;
        LLSD items = LLSD::emptyArray();
        items.append(item->getUUID());
        payload["items"] = items;
        LLNotificationsUtil::add("CannotCopyWarning", substitutions, payload,
            &LLGiveInventory::handleCopyProtectedItem);
        res = false;
    }

    return res;
}

bool LLGiveInventory::doGiveInventoryCategory(const LLUUID& to_agent,
                                              const LLInventoryCategory* cat,
                                              const LLUUID& im_session_id,
                                              const std::string& notification_name)

{
    if (!cat)
    {
        return false;
    }
    LL_INFOS() << "LLGiveInventory::giveInventoryCategory() - "
        << cat->getUUID() << LL_ENDL;

    if (!isAgentAvatarValid())
    {
        return false;
    }

    bool give_successful = true;
    // Test out how many items are being given.
    LLViewerInventoryCategory::cat_array_t cats;
    LLViewerInventoryItem::item_array_t items;
    LLGiveable giveable;
    gInventory.collectDescendentsIf (cat->getUUID(),
        cats,
        items,
        LLInventoryModel::EXCLUDE_TRASH,
        giveable);
    auto count = cats.size();
    bool complete = true;
    for(size_t i = 0; i < count; ++i)
    {
        if (!gInventory.isCategoryComplete(cats.at(i)->getUUID()))
        {
            complete = false;
            break;
        }
    }
    if (!complete)
    {
        LLNotificationsUtil::add("IncompleteInventory");
        give_successful = false;
    }
    count = items.size() + cats.size();
// <FS:CR> Aurora Sim
    //if (count > MAX_ITEMS)
    if (count > LLWorld::getInstance()->getMaxInventoryItemsTransfer())
// </FS:CR> Aurora Sim
    {
        LLNotificationsUtil::add("TooManyItems");
        give_successful = false;
    }
    else if (count == 0)
    {
        LLNotificationsUtil::add("NoItems");
        give_successful = false;
    }
    else if (give_successful)
    {
        if (0 == giveable.countNoCopy())
        {
            give_successful = LLGiveInventory::commitGiveInventoryCategory(to_agent, cat, im_session_id);
        }
        else
        {
            LLSD args;
            args["COUNT"] = llformat("%d",giveable.countNoCopy());
            LLSD payload;
            payload["agent_id"] = to_agent;
            payload["folder_id"] = cat->getUUID();
            if (!notification_name.empty())
            {
                payload["success_notification"] = notification_name;
            }
            LLNotificationsUtil::add("CannotCopyCountItems", args, payload, &LLGiveInventory::handleCopyProtectedCategory);
            give_successful = false;
        }
    }

    return give_successful;
}

//////////////////////////////////////////////////////////////////////////
//     PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//static
void LLGiveInventory::logInventoryOffer(const LLUUID& to_agent, const LLUUID &im_session_id, const std::string& item_name, bool is_folder)
{
    // compute id of possible IM session with agent that has "to_agent" id
    LLUUID session_id = LLIMMgr::computeSessionID(IM_NOTHING_SPECIAL, to_agent);
    // If this item was given by drag-and-drop into an IM panel, log this action in the IM panel chat.
    LLSD args;
    args["user_id"] = to_agent;
    args["ITEM_NAME"] = item_name;
    std::string message_name = is_folder ? "inventory_folder_offered" : "inventory_item_offered";
    if (im_session_id.notNull())
    {
        gIMMgr->addSystemMessage(im_session_id, message_name, args);
    }
// [RLVa:KB] - Checked: RLVa-1.2.0
    else if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, to_agent)) && (RlvUtil::isNearbyAgent(to_agent)) && (!RlvUIEnabler::hasOpenProfile(to_agent)) )
    {
        // Log to chat history if the user didn't drop on an IM session or a profile to avoid revealing the name of the recipient
        std::string strMsgName = "inventory_item_offered-im"; LLSD args; LLAvatarName avName;
        if (LLAvatarNameCache::get(to_agent, &avName))
        {
            args["NAME"] = RlvStrings::getAnonym(avName);
            strMsgName = "inventory_item_offered_rlv";
        }
        gIMMgr->addSystemMessage(LLUUID::null, strMsgName, args);
    }
// [/RLVa:KB]
    // If this item was given by drag-and-drop on avatar while IM panel was open, log this action in the IM panel chat.
    else if (LLIMModel::getInstance()->findIMSession(session_id))
    {
        gIMMgr->addSystemMessage(session_id, message_name, args);
    }
    // If this item was given by drag-and-drop on avatar while IM panel wasn't open, log this action to IM history.
    else
    {
        LLAvatarName av_name;
        if (LLAvatarNameCache::get(to_agent, &av_name))
        {
            // Build a new format username or firstname_lastname for legacy names
            // to use it for a history log filename.
            // <FS:Ansariel> [Legacy IM logfile names]
            //std::string full_name = LLCacheName::buildUsername(av_name.getUserName());
            std::string full_name = av_name.getUserName();
            if (gSavedSettings.getBOOL("UseLegacyIMLogNames"))
            {
                full_name = full_name.substr(0, full_name.find(" Resident"));;
            }
            else
            {
                full_name = LLCacheName::buildUsername(full_name);
            }
            // </FS:Ansariel> [Legacy IM logfile names]
            LLUIString message = LLTrans::getString(message_name + "-im");
            message.setArgs(args);
            LLIMModel::instance().logToFile(full_name, LLTrans::getString("SECOND_LIFE"), im_session_id, message.getString());
        }
    }
}

// static
bool LLGiveInventory::handleCopyProtectedItem(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    LLSD itmes = notification["payload"]["items"];
    LLInventoryItem* item = NULL;
    bool give_successful = true;
    switch(option)
    {
    case 0:  // "Yes"
// [RLVa:KB] - @share
        if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(notification["payload"]["agent_id"].asUUID())) )
        {
            RlvUtil::notifyBlocked(RlvStringKeys::Blocked::Share, LLSD().with("RECIPIENT", LLSLURL("agent", notification["payload"]["agent_id"], "completename").getSLURLString()));
            return false;
        }
// [/RLVa:KB]

        for (LLSD::array_iterator it = itmes.beginArray(); it != itmes.endArray(); it++)
        {
            item = gInventory.getItem((*it).asUUID());
            if (item)
            {
                LLGiveInventory::commitGiveInventoryItem(notification["payload"]["agent_id"].asUUID(),
                    item);
                // delete it for now - it will be deleted on the server
                // quickly enough.
                gInventory.deleteObject(item->getUUID());
                gInventory.notifyObservers();
            }
            else
            {
                LLNotificationsUtil::add("CannotGiveItem");
                give_successful = false;
            }
        }
        if (give_successful && notification["payload"]["success_notification"].isDefined())
        {
            LLNotificationsUtil::add(notification["payload"]["success_notification"].asString());
        }
        break;

    default: // no, cancel, whatever, who cares, not yes.
        LLNotificationsUtil::add("TransactionCancelled");
        give_successful = false;
        break;
    }
    return give_successful;
}

// static
//void LLGiveInventory::commitGiveInventoryItem(const LLUUID& to_agent,
//                                              const LLInventoryItem* item,
//                                              const LLUUID& im_session_id)
// [RLVa:KB] - @share
bool LLGiveInventory::commitGiveInventoryItem(const LLUUID& to_agent,
                                                const LLInventoryItem* item,
                                                const LLUUID& im_session_id)
// [/RLVa:KB]
{
//  if (!item) return;
// [RLVa:KB] - @share
    if (!item) return false;
    if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(to_agent)) )
    {
        return false;
    }
// [/RLVa:KB]

    std::string name;
    std::string item_name = item->getName();
    LLAgentUI::buildFullname(name);
    LLUUID transaction_id;
    r4b_generate(transaction_id);
    const S32 BUCKET_SIZE = sizeof(U8) + UUID_BYTES;
    U8 bucket[BUCKET_SIZE];
    bucket[0] = (U8)item->getType();
    memcpy(&bucket[1], &(item->getUUID().mData), UUID_BYTES);       /* Flawfinder: ignore */
    pack_instant_message(
        gMessageSystem,
        gAgentID,
        false,
        gAgentSessionID,
        to_agent,
        name,
        item_name,
        IM_ONLINE,
        IM_INVENTORY_OFFERED,
        transaction_id,
        0,
        LLUUID::null,
        gAgent.getPositionAgent(),
        NO_TIMESTAMP,
        bucket,
        BUCKET_SIZE);
    gAgent.sendReliableMessage();

    // VEFFECT: giveInventory
    // <FS:Ansariel> Make the particle effect optional
    if (gSavedSettings.getBOOL("FSCreateGiveInventoryParticleEffect"))
    {
        LLHUDEffectSpiral *effectp = (LLHUDEffectSpiral *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_BEAM, true);
        effectp->setSourceObject(gAgentAvatarp);
        effectp->setTargetObject(gObjectList.findObject(to_agent));
        effectp->setDuration(LL_HUD_DUR_SHORT);
        effectp->setColor(LLColor4U(gAgent.getEffectColor()));
    }

    if (gFloaterTools)
    {
        gFloaterTools->dirty();
    }

    LLMuteList::getInstance()->autoRemove(to_agent, LLMuteList::AR_INVENTORY);

    logInventoryOffer(to_agent, im_session_id, item_name);

    // add buddy to recent people list
//  LLRecentPeople::instance().add(to_agent);
// [RLVa:KB] - Checked: RLVa-2.0.0
    // Block the recent activity update if this was an in-world drop on an avatar (as opposed to a drop on an IM session or on a profile)
    if ( (!RlvActions::isRlvEnabled()) ||
         (RlvActions::canShowName(RlvActions::SNC_DEFAULT, to_agent)) || (im_session_id.notNull()) || (!RlvUtil::isNearbyAgent(to_agent)) || (RlvUIEnabler::hasOpenProfile(to_agent)) )
    {
        LLRecentPeople::instance().add(to_agent);
    }
    return true;
// [/RLVa:KB]
}

// static
bool LLGiveInventory::handleCopyProtectedCategory(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    LLInventoryCategory* cat = NULL;
    bool give_successful = true;
    switch(option)
    {
    case 0:  // "Yes"
        cat = gInventory.getCategory(notification["payload"]["folder_id"].asUUID());
        if (cat)
        {
// [RLVa:KB] - @share
            if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(notification["payload"]["agent_id"].asUUID())) )
            {
                RlvUtil::notifyBlocked(RlvStringKeys::Blocked::Share, LLSD().with("RECIPIENT", LLSLURL("agent", notification["payload"]["agent_id"], "completename").getSLURLString()));
                return false;
            }
// [/RLVa:KB]

            give_successful = LLGiveInventory::commitGiveInventoryCategory(notification["payload"]["agent_id"].asUUID(),
                cat);
            LLViewerInventoryCategory::cat_array_t cats;
            LLViewerInventoryItem::item_array_t items;
            LLUncopyableItems remove;
            gInventory.collectDescendentsIf (cat->getUUID(),
                cats,
                items,
                LLInventoryModel::EXCLUDE_TRASH,
                remove);
            auto count = items.size();
            for(size_t i = 0; i < count; ++i)
            {
                gInventory.deleteObject(items.at(i)->getUUID());
            }
            gInventory.notifyObservers();

            if (give_successful && notification["payload"]["success_notification"].isDefined())
            {
                LLNotificationsUtil::add(notification["payload"]["success_notification"].asString());
            }
        }
        else
        {
            LLNotificationsUtil::add("CannotGiveCategory");
            give_successful = false;
        }
        break;

    default: // no, cancel, whatever, who cares, not yes.
        LLNotificationsUtil::add("TransactionCancelled");
        give_successful = false;
        break;
    }
    return give_successful;
}

// static
bool LLGiveInventory::commitGiveInventoryCategory(const LLUUID& to_agent,
                                                    const LLInventoryCategory* cat,
                                                    const LLUUID& im_session_id)

{
    if (!cat)
    {
        return false;
    }
// [RLVa:KB] - @share
    if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(to_agent)) )
    {
        return false;
    }
// [/RLVa:KB]

    LL_INFOS() << "LLGiveInventory::commitGiveInventoryCategory() - "
        << cat->getUUID() << LL_ENDL;

    // add buddy to recent people list
//  LLRecentPeople::instance().add(to_agent);
// [RLVa:KB] - Checked: RLVa-2.0.0
    // Block the recent activity update if this was an in-world drop on an avatar (as opposed to a drop on an IM session or on a profile)
    if ( (!RlvActions::isRlvEnabled()) ||
         (RlvActions::canShowName(RlvActions::SNC_DEFAULT, to_agent)) || (im_session_id.notNull()) || (!RlvUtil::isNearbyAgent(to_agent)) || (RlvUIEnabler::hasOpenProfile(to_agent)) )
    {
        LLRecentPeople::instance().add(to_agent);
    }
// [/RLVa:KB]

    // Test out how many items are being given.
    LLViewerInventoryCategory::cat_array_t cats;
    LLViewerInventoryItem::item_array_t items;
    LLGiveable giveable;
    gInventory.collectDescendentsIf (cat->getUUID(),
        cats,
        items,
        LLInventoryModel::EXCLUDE_TRASH,
        giveable);
    std::string cat_name = cat->getName();
    bool give_successful = true;
    // MAX ITEMS is based on (sizeof(uuid)+2) * count must be <
    // MTUBYTES or 18 * count < 1200 => count < 1200/18 =>
    // 66. I've cut it down a bit from there to give some pad.
    auto count = items.size() + cats.size();
// <FS:CR> Aurora Sim>
    //if (count > MAX_ITEMS)
    if (count > LLWorld::getInstance()->getMaxInventoryItemsTransfer())
// <FS:CR> Aurora Sim
    {
        LLNotificationsUtil::add("TooManyItems");
        give_successful = false;
    }
    else if (count == 0)
    {
        LLNotificationsUtil::add("NoItems");
        give_successful = false;
    }
    else
    {
        std::string name;
        LLAgentUI::buildFullname(name);
        LLUUID transaction_id;
        r4b_generate(transaction_id);
        S32 bucket_size = (sizeof(U8) + UUID_BYTES) * (static_cast<S32>(count) + 1);
        U8* bucket = new U8[bucket_size];
        U8* pos = bucket;
        U8 type = (U8)cat->getType();
        memcpy(pos, &type, sizeof(U8));     /* Flawfinder: ignore */
        pos += sizeof(U8);
        memcpy(pos, &(cat->getUUID()), UUID_BYTES);     /* Flawfinder: ignore */
        pos += UUID_BYTES;
        S32 i;
        count = cats.size();
        for(i = 0; i < count; ++i)
        {
            memcpy(pos, &type, sizeof(U8));     /* Flawfinder: ignore */
            pos += sizeof(U8);
            memcpy(pos, &(cats.at(i)->getUUID()), UUID_BYTES);      /* Flawfinder: ignore */
            pos += UUID_BYTES;
        }
        count = items.size();
        for(i = 0; i < count; ++i)
        {
            type = (U8)items.at(i)->getType();
            memcpy(pos, &type, sizeof(U8));     /* Flawfinder: ignore */
            pos += sizeof(U8);
            memcpy(pos, &(items.at(i)->getUUID()), UUID_BYTES);     /* Flawfinder: ignore */
            pos += UUID_BYTES;
        }
        pack_instant_message(
            gMessageSystem,
            gAgent.getID(),
            false,
            gAgent.getSessionID(),
            to_agent,
            name,
            cat_name,
            IM_ONLINE,
            IM_INVENTORY_OFFERED,
            transaction_id,
            0,
            LLUUID::null,
            gAgent.getPositionAgent(),
            NO_TIMESTAMP,
            bucket,
            bucket_size);
        gAgent.sendReliableMessage();
        delete[] bucket;

        // VEFFECT: giveInventoryCategory
        // <FS:Ansariel> Make the particle effect optional
        if (gSavedSettings.getBOOL("FSCreateGiveInventoryParticleEffect"))
        {
            LLHUDEffectSpiral *effectp = (LLHUDEffectSpiral *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_BEAM, true);
            effectp->setSourceObject(gAgentAvatarp);
            effectp->setTargetObject(gObjectList.findObject(to_agent));
            effectp->setDuration(LL_HUD_DUR_SHORT);
            effectp->setColor(LLColor4U(gAgent.getEffectColor()));
        }

        if (gFloaterTools)
        {
            gFloaterTools->dirty();
        }

        LLMuteList::getInstance()->autoRemove(to_agent, LLMuteList::AR_INVENTORY);

        logInventoryOffer(to_agent, im_session_id, cat_name, true);
    }

    return give_successful;
}
// R4B-COPY-END give_inventory

// R4B-COPY-BEGIN drop_functors
// source: indra/newview/lltooldraganddrop.cpp
// function: LLDroppableItem, LLDropCopyableItems
// lines: 119-182
// blob: c2bb754bcbc9e7241b3e6f49f4fb646b402ea7fc
class LLDroppableItem : public LLInventoryCollectFunctor
{
public:
    LLDroppableItem(bool is_transfer) :
        mCountLosing(0), mIsTransfer(is_transfer) {}
    virtual ~LLDroppableItem() {}
    virtual bool operator()(LLInventoryCategory* cat,
                            LLInventoryItem* item);
    S32 countNoCopy() const { return mCountLosing; }

protected:
    S32 mCountLosing;
    bool mIsTransfer;
};

bool LLDroppableItem::operator()(LLInventoryCategory* cat,
                 LLInventoryItem* item)
{
    bool allowed = false;
    if (item)
    {
        allowed = itemTransferCommonlyAllowed(item);

        if (allowed
           && mIsTransfer
           && !item->getPermissions().allowOperationBy(PERM_TRANSFER,
                                   gAgent.getID()))
        {
            allowed = false;
        }
        if (allowed && !item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            ++mCountLosing;
        }
    }
    return allowed;
}

class LLDropCopyableItems : public LLInventoryCollectFunctor
{
public:
    LLDropCopyableItems() {}
    virtual ~LLDropCopyableItems() {}
    virtual bool operator()(LLInventoryCategory* cat, LLInventoryItem* item);
};


bool LLDropCopyableItems::operator()(
    LLInventoryCategory* cat,
    LLInventoryItem* item)
{
    bool allowed = false;
    if (item)
    {
        allowed = itemTransferCommonlyAllowed(item);
        if (allowed &&
           !item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            // whoops, can't copy it - don't allow it.
            allowed = false;
        }
    }
    return allowed;
}
// R4B-COPY-END drop_functors

// R4B-COPY-BEGIN will_object_accept
// source: indra/newview/lltooldraganddrop.cpp
// function: LLToolDragAndDrop::willObjectAcceptInventory
// lines: 2131-2230
// blob: c2bb754bcbc9e7241b3e6f49f4fb646b402ea7fc
EAcceptance LLToolDragAndDrop::willObjectAcceptInventory(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type)
{
    // check the basics
    if (!item || !obj)
        return ACCEPT_NO;

    // HACK: downcast
    LLViewerInventoryItem* vitem = (LLViewerInventoryItem*)item;
    if (!vitem->isFinished() && (type != DAD_CATEGORY))
    {
        // Note: for DAD_CATEGORY we assume that folder version check passed and folder
        // is complete, meaning that items inside are up to date.
        // (isFinished() == false) at the moment shows that item was loaded from cache.
        // Library or agent inventory only.
        return ACCEPT_NO;
    }
    if (vitem->getIsLinkType())
        return ACCEPT_NO; // No giving away links

    // deny attempts to drop from an object onto itself. This is to
    // help make sure that drops that are from an object to an object
    // don't have to worry about order of evaluation. Think of this
    // like check for self in assignment.
    if (obj->getID() == item->getParentUUID())
    {
        return ACCEPT_NO;
    }

    //bool copy = (perm.allowCopyBy(gAgent.getID(),
    //                            gAgent.getGroupID())
    //           && (obj->mPermModify || obj->mFlagAllowInventoryAdd));
    bool worn = false;
    LLVOAvatarSelf* my_avatar = NULL;
    switch (item->getType())
    {
    case LLAssetType::AT_OBJECT:
        my_avatar = gAgentAvatarp;
        if(my_avatar && my_avatar->isWearingAttachment(item->getUUID()))
        {
                worn = true;
        }
        break;
    case LLAssetType::AT_BODYPART:
    case LLAssetType::AT_CLOTHING:
        if (gAgentWearables.isWearingItem(item->getUUID()))
        {
            worn = true;
        }
        break;
    case LLAssetType::AT_CALLINGCARD:
        // Calling Cards in object are disabled for now
        // because of incomplete LSL support. See STORM-1117.
        return ACCEPT_NO;
    default:
        break;
    }
    const LLPermissions& perm = item->getPermissions();
    bool modify = (obj->permModify() || obj->flagAllowInventoryAdd());
    bool transfer = false;
    if ((obj->permYouOwner() && (perm.getOwner() == gAgent.getID()))
       || perm.allowOperationBy(PERM_TRANSFER, gAgent.getID()))
    {
        transfer = true;
    }
    bool volume = (LL_PCODE_VOLUME == obj->getPCode());
    bool attached = obj->isAttachment();
    bool unrestricted = (perm.getMaskBase() & PERM_ITEM_UNRESTRICTED) == PERM_ITEM_UNRESTRICTED;

// [RLVa:KB] - Checked: 2010-03-31 (RLVa-1.2.0c) | Modified: RLVa-1.0.0c
    if (rlv_handler_t::isEnabled())
    {
        const LLViewerObject* pObjRoot = obj->getRootEdit();
        if (gRlvAttachmentLocks.isLockedAttachment(pObjRoot))
        {
            return ACCEPT_NO_LOCKED;        // Disallow inventory drops on a locked attachment
        }
        else if ( (gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP)) )
        {
            if ( (isAgentAvatarValid()) && (gAgentAvatarp->isSitting()) && (gAgentAvatarp->getRoot() == pObjRoot) )
                return ACCEPT_NO_LOCKED;    // ... or on a linkset the avie is sitting on under @unsit=n/@sittp=n
        }
    }
// [/RLVa:KB]

    if(attached && !unrestricted)
    {
        // Attachments are in world and in inventory simultaneously,
        // at the moment server doesn't support such a situation.
        return ACCEPT_NO_LOCKED;
    }
    else if(modify && transfer && volume && !worn)
    {
        return ACCEPT_YES_MULTI;
    }
    else if(!modify)
    {
        return ACCEPT_NO_LOCKED;
    }
    return ACCEPT_NO;
}
// R4B-COPY-END will_object_accept

// R4B-COPY-BEGIN world_to_agent
// source: indra/newview/llinventorybridge.cpp
// function: move_inv_category_world_to_agent
// lines: 3554-3659
// blob: 7f4a4413538fed5a50b1b51a5fb03df856a74cd4
bool move_inv_category_world_to_agent(const LLUUID& object_id,
                                      const LLUUID& category_id,
                                      bool drop,
                                      std::function<void(S32, void*, const LLMoveInv*)> callback,
                                      void* user_data,
                                      LLInventoryFilter* filter)
{
    // Make sure the object exists. If we allowed dragging from
    // anonymous objects, it would be possible to bypass
    // permissions.
    // content category has same ID as object itself
    LLViewerObject* object = gObjectList.findObject(object_id);
    if (!object)
    {
        LL_INFOS() << "Object not found for drop." << LL_ENDL;
        return false;
    }

    // this folder is coming from an object, as there is only one folder in an object, the root,
    // we need to collect the entire contents and handle them as a group
    LLInventoryObject::object_list_t inventory_objects;
    object->getInventoryContents(inventory_objects);

    if (inventory_objects.empty())
    {
        LL_INFOS() << "Object contents not found for drop." << LL_ENDL;
        return false;
    }

    bool accept = false;
    bool is_move = false;
    bool use_filter = false;
    if (filter)
    {
        U64 filter_types = filter->getFilterTypes();
        use_filter = filter_types && (filter_types&LLInventoryFilter::FILTERTYPE_DATE || (filter_types&LLInventoryFilter::FILTERTYPE_OBJECT)==0);
    }

    // coming from a task. Need to figure out if the person can
    // move/copy this item.
    for (LLPointer<LLInventoryObject> obj : inventory_objects)
    {
        LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(obj.get());
        if (!item)
        {
            LL_WARNS() << "Invalid inventory item for drop" << LL_ENDL;
            continue;
        }

        // coming from a task. Need to figure out if the person can
        // move/copy this item.
        LLPermissions perm(item->getPermissions());
        if ((perm.allowCopyBy(gAgent.getID(), gAgent.getGroupID())
            && perm.allowTransferTo(gAgent.getID())))
//          || gAgent.isGodlike())
        {
            accept = true;
        }
        else if (object->permYouOwner())
        {
            // If the object cannot be copied, but the object the
            // inventory is owned by the agent, then the item can be
            // moved from the task to agent inventory.
            is_move = true;
            accept = true;
        }

        if (accept && use_filter)
        {
            accept = filter->check(item);
        }

        if (!accept)
        {
            break;
        }
    }

    if (drop && accept)
    {
        std::shared_ptr<LLMoveInv> move_inv = std::make_shared<LLMoveInv>();
        move_inv->mObjectID = object_id;
        move_inv->mCategoryID = category_id;
        move_inv->mCallback = callback;
        move_inv->mUserData = user_data;

        for (LLPointer<LLInventoryObject> obj : inventory_objects)
        {
            two_uuids_t two(category_id, obj->getUUID());
            move_inv->mMoveList.push_back(two);
        }

        if (is_move)
        {
            // Callback called from within here.
            warn_move_inventory(object, move_inv);
        }
        else
        {
            LLNotification::Params params("MoveInventoryFromObject");
            params.functor.function(boost::bind(move_task_inventory_callback, _1, _2, move_inv));
            LLNotifications::instance().forceResponse(params, 0);
        }
    }
    return accept;
}
// R4B-COPY-END world_to_agent

// R4B-COPY-BEGIN dad_share_confirm
// source: indra/newview/lltooldraganddrop.cpp
// function: give_inventory_cb, show_object_sharing_confirmation, get_name_cb, LLToolDragAndDrop::handleGiveDragAndDrop
// lines: 2231-2395
// blob: c2bb754bcbc9e7241b3e6f49f4fb646b402ea7fc


static void give_inventory_cb(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    // if Cancel pressed
    if (option == 1)
    {
        return;
    }

    LLSD payload = notification["payload"];
    const LLUUID& session_id = payload["session_id"];
    const LLUUID& agent_id = payload["agent_id"];
    LLViewerInventoryItem * inv_item =  gInventory.getItem(payload["item_id"]);
    LLViewerInventoryCategory * inv_cat =  gInventory.getCategory(payload["item_id"]);
    if (NULL == inv_item && NULL == inv_cat)
    {
        llassert( false );
        return;
    }
    bool successfully_shared;
    if (inv_item)
    {
        successfully_shared = LLGiveInventory::doGiveInventoryItem(agent_id, inv_item, session_id);
    }
    else
    {
        successfully_shared = LLGiveInventory::doGiveInventoryCategory(agent_id, inv_cat, session_id);
    }
    if (successfully_shared)
    {
        if ("avatarpicker" == payload["d&d_dest"].asString())
        {
            LLFloaterReg::hideInstance("avatar_picker");
        }
        LLNotificationsUtil::add("ItemsShared");
    }
}

static void show_object_sharing_confirmation(const std::string name,
                       LLInventoryObject* inv_item,
                       const LLSD& dest,
                       const LLUUID& dest_agent,
                       const LLUUID& session_id = LLUUID::null)
{
    if (!inv_item)
    {
        llassert(NULL != inv_item);
        return;
    }
    LLSD substitutions;
    substitutions["RESIDENTS"] = name;
    substitutions["ITEMS"] = inv_item->getName();
    LLSD payload;
    payload["agent_id"] = dest_agent;
    payload["item_id"] = inv_item->getUUID();
    payload["session_id"] = session_id;
    payload["d&d_dest"] = dest.asString();
    LLNotificationsUtil::add("ShareItemsConfirmation", substitutions, payload, &give_inventory_cb);
}

static void get_name_cb(const LLUUID& id,
                        const LLAvatarName& av_name,
                        LLInventoryObject* inv_obj,
                        const LLSD& dest,
                        const LLUUID& dest_agent)
{
    show_object_sharing_confirmation(av_name.getUserName(),
                                     inv_obj,
                                     dest,
                                     id,
                                     LLUUID::null);
}

// function used as drag-and-drop handler for simple agent give inventory requests
//static
bool LLToolDragAndDrop::handleGiveDragAndDrop(LLUUID dest_agent, LLUUID session_id, bool drop,
                                              EDragAndDropType cargo_type,
                                              void* cargo_data,
                                              EAcceptance* accept,
                                              const LLSD& dest)
{
// [RLVa:KB] - @share
    if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(dest_agent)) )
    {
        *accept = ACCEPT_NO_LOCKED;
        return true;
    }
// [/RLVa:KB]

    // check the type
    switch(cargo_type)
    {
    case DAD_TEXTURE:
    case DAD_SOUND:
    case DAD_LANDMARK:
    case DAD_SCRIPT:
    case DAD_OBJECT:
    case DAD_NOTECARD:
    case DAD_CLOTHING:
    case DAD_BODYPART:
    case DAD_ANIMATION:
    case DAD_GESTURE:
    case DAD_CALLINGCARD:
    case DAD_MESH:
    case DAD_CATEGORY:
    case DAD_SETTINGS:
    case DAD_MATERIAL:
    {
        LLInventoryObject* inv_obj = (LLInventoryObject*)cargo_data;
        if(gInventory.getCategory(inv_obj->getUUID()) || (gInventory.getItem(inv_obj->getUUID())
            && LLGiveInventory::isInventoryGiveAcceptable(dynamic_cast<LLInventoryItem*>(inv_obj))))
        {
            // *TODO: get multiple object transfers working
            *accept = ACCEPT_YES_COPY_SINGLE;
            if(drop)
            {
                LLIMModel::LLIMSession * session = LLIMModel::instance().findIMSession(session_id);

                // If no IM session found get the destination agent's name by id.
                if (NULL == session)
                {
                    LLAvatarName av_name;

                    // If destination agent's name is found in cash proceed to showing the confirmation dialog.
                    // Otherwise set up a callback to show the dialog when the name arrives.
                    if (LLAvatarNameCache::get(dest_agent, &av_name))
                    {
                        show_object_sharing_confirmation(av_name.getUserName(), inv_obj, dest, dest_agent, LLUUID::null);
                    }
                    else
                    {
                        LLAvatarNameCache::get(dest_agent, boost::bind(&get_name_cb, _1, _2, inv_obj, dest, dest_agent));
                    }

                    return true;
                }
                std::string dest_name = session->mName;
                LLAvatarName av_name;
                if(LLAvatarNameCache::get(dest_agent, &av_name))
                {
                    dest_name = av_name.getCompleteName();
                }
                // If an IM session with destination agent is found item offer will be logged in this session.
                show_object_sharing_confirmation(dest_name, inv_obj, dest, dest_agent, session_id);
            }
        }
        else
        {
            // It's not in the user's inventory (it's probably
            // in an object's contents), so disallow dragging
            // it here.  You can't give something you don't
            // yet have.
            *accept = ACCEPT_NO;
        }
        break;
    }
    default:
        *accept = ACCEPT_NO;
        break;
    }

    return true;
}
// R4B-COPY-END dad_share_confirm

// R4B-COPY-BEGIN dad_give_3d
// source: indra/newview/lltooldraganddrop.cpp
// function: LLToolDragAndDrop::dad3dGiveInventoryObject, dad3dGiveInventory, dad3dGiveInventoryCategory
// lines: 3221-3315
// blob: c2bb754bcbc9e7241b3e6f49f4fb646b402ea7fc
EAcceptance LLToolDragAndDrop::dad3dGiveInventoryObject(
    LLViewerObject* obj, S32 face, MASK mask, bool drop)
{
    LL_DEBUGS() << "LLToolDragAndDrop::dad3dGiveInventoryObject()" << LL_ENDL;

    // item has to be in agent inventory.
    if(mSource != SOURCE_AGENT) return ACCEPT_NO;

    // find the item now.
    LLViewerInventoryItem* item;
    LLViewerInventoryCategory* cat;
    locateInventory(item, cat);
    if (!item || !item->isFinished()) return ACCEPT_NO;
    if(!item->getPermissions().allowOperationBy(PERM_TRANSFER, gAgent.getID()))
    {
        // cannot give away no-transfer objects
        return ACCEPT_NO;
    }
    LLVOAvatarSelf* avatar = gAgentAvatarp;
    if(avatar && avatar->isWearingAttachment( item->getUUID() ) )
    {
        // You can't give objects that are attached to you
        return ACCEPT_NO;
    }
    if( obj && avatar )
    {
// [RLVa:KB] - @share
        if ( (obj) && (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(obj->getID())) )
        {
            return ACCEPT_NO_LOCKED;
        }
// [/RLVa:KB]
        if(drop)
        {
            LLGiveInventory::doGiveInventoryItem(obj->getID(), item );
        }
        // *TODO: deal with all the issues surrounding multi-object
        // inventory transfers.
        return ACCEPT_YES_SINGLE;
    }
    return ACCEPT_NO;
}


EAcceptance LLToolDragAndDrop::dad3dGiveInventory(
    LLViewerObject* obj, S32 face, MASK mask, bool drop)
{
    LL_DEBUGS() << "LLToolDragAndDrop::dad3dGiveInventory()" << LL_ENDL;
    // item has to be in agent inventory.
    if(mSource != SOURCE_AGENT) return ACCEPT_NO;
    LLViewerInventoryItem* item;
    LLViewerInventoryCategory* cat;
    locateInventory(item, cat);
    if (!item || !item->isFinished()) return ACCEPT_NO;
    if (!LLGiveInventory::isInventoryGiveAcceptable(item))
    {
        return ACCEPT_NO;
    }
// [RLVa:KB] - @share
    if ( (obj) && (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(obj->getID())) )
    {
        return ACCEPT_NO_LOCKED;
    }
// [/RLVa:KB]
    if (drop && obj)
    {
        LLGiveInventory::doGiveInventoryItem(obj->getID(), item);
    }
    // *TODO: deal with all the issues surrounding multi-object
    // inventory transfers.
    return ACCEPT_YES_SINGLE;
}

EAcceptance LLToolDragAndDrop::dad3dGiveInventoryCategory(
    LLViewerObject* obj, S32 face, MASK mask, bool drop)
{
    LL_DEBUGS() << "LLToolDragAndDrop::dad3dGiveInventoryCategory()" << LL_ENDL;
// [RLVa:KB] - @share
    if ( (obj) && (RlvActions::isRlvEnabled()) && (!RlvActions::canGiveInventory(obj->getID())) )
    {
        return ACCEPT_NO_LOCKED;
    }
// [/RLVa:KB]
    if(drop && obj)
    {
        LLViewerInventoryItem* item;
        LLViewerInventoryCategory* cat;
        locateInventory(item, cat);
        if(!cat) return ACCEPT_NO;
        LLGiveInventory::doGiveInventoryCategory(obj->getID(), cat);
    }
    // *TODO: deal with all the issues surrounding multi-object
    // inventory transfers.
    return ACCEPT_YES_SINGLE;
}
// R4B-COPY-END dad_give_3d

// llavataractions.cpp declares these in namespace action_give_inventory.
namespace action_give_inventory
{
// R4B-COPY-BEGIN share_give_cb
// source: indra/newview/llavataractions.cpp
// function: action_give_inventory::LLShareInfo, give_inventory_cb
// lines: 1016-1104
// blob: c29ff7575b2b691ff33bb99f47b7df0c25712757
    struct LLShareInfo : public LLSingleton<LLShareInfo>
    {
        LLSINGLETON_EMPTY_CTOR(LLShareInfo);
    public:
        std::vector<LLAvatarName> mAvatarNames;
        uuid_vec_t mAvatarUuids;
    };

    static void give_inventory_cb(const LLSD& notification, const LLSD& response, std::set<LLUUID> inventory_selected_uuids)
    {
        S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
        // if Cancel pressed
        if (option == 1)
        {
            return;
        }

        if (inventory_selected_uuids.empty())
        {
            return;
        }

        auto count = LLShareInfo::instance().mAvatarNames.size();
        bool shared = count && !inventory_selected_uuids.empty();

        // iterate through avatars
        for(size_t i = 0; i < count; ++i)
        {
            const LLUUID& avatar_uuid = LLShareInfo::instance().mAvatarUuids[i];

            // We souldn't open IM session, just calculate session ID for logging purpose. See EXT-6710
            const LLUUID session_id = gIMMgr->computeSessionID(IM_NOTHING_SPECIAL, avatar_uuid);

            std::set<LLUUID>::const_iterator it = inventory_selected_uuids.begin();
            const std::set<LLUUID>::const_iterator it_end = inventory_selected_uuids.end();

            const std::string& separator = LLTrans::getString("words_separator");
            std::string noncopy_item_names;
            LLSD noncopy_items = LLSD::emptyArray();
            // iterate through selected inventory objects
            for (; it != it_end; ++it)
            {
                LLViewerInventoryCategory* inv_cat = gInventory.getCategory(*it);
                if (inv_cat)
                {
                    if (!LLGiveInventory::doGiveInventoryCategory(avatar_uuid, inv_cat, session_id, "ItemsShared"))
                    {
                        shared = false;
                    }
                    break;
                }
                LLViewerInventoryItem* inv_item = gInventory.getItem(*it);
                if (!inv_item->getPermissions().allowCopyBy(gAgentID))
                {
                    if (!noncopy_item_names.empty())
                    {
                        noncopy_item_names.append(separator);
                    }
                    noncopy_item_names.append(inv_item->getName());
                    noncopy_items.append(*it);
                }
                else
                {
                    if (!LLGiveInventory::doGiveInventoryItem(avatar_uuid, inv_item, session_id))
                    {
                        shared = false;
                    }
                }
            }
            if (noncopy_items.beginArray() != noncopy_items.endArray())
            {
                LLSD substitutions;
                substitutions["ITEMS"] = noncopy_item_names;
                LLSD payload;
                payload["agent_id"] = avatar_uuid;
                payload["items"] = noncopy_items;
                payload["success_notification"] = "ItemsShared";
                LLNotificationsUtil::add("CannotCopyWarning", substitutions, payload,
                    &LLGiveInventory::handleCopyProtectedItem);
                shared = false;
                break;
            }
        }
        if (shared)
        {
            LLFloaterReg::hideInstance("avatar_picker");
            LLNotificationsUtil::add("ItemsShared");
        }
    }
// R4B-COPY-END share_give_cb

} // namespace action_give_inventory

// The ReadOfflineMsgs capability's message loop: the harness supplies the
// array of messages the coroutine has validated.
void process_offline_messages(LLSD messages)
{
// R4B-COPY-BEGIN offline_loop
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::requestOfflineMessagesCoro (the message loop)
// lines: 2502-2581
// blob: 8fa885d340173cce97bd512894040370f970b57a
    LLHost sender = gAgent.getRegionHost();

    LLSD::array_iterator i = messages.beginArray();
    LLSD::array_iterator iEnd = messages.endArray();
    for (; i != iEnd; ++i)
    {
        const LLSD &message_data(*i);

        /* RIDER: Many fields in this message are using a '_' rather than the standard '-'.  This
         * should be changed but would require tight coordination with the simulator.
         */
        LLVector3 position;
        if (message_data.has("position"))
        {
            position.setValue(message_data["position"]);
        }
        else
        {
            position.set((F32)message_data["local_x"].asReal(), (F32)message_data["local_y"].asReal(), (F32)message_data["local_z"].asReal());
        }

        std::vector<U8> bin_bucket;
        if (message_data.has("binary_bucket"))
        {
            bin_bucket = message_data["binary_bucket"].asBinary();
        }
        else
        {
            bin_bucket.push_back(0);
        }

        // Todo: once drtsim-451 releases, remove the string option
        bool from_group;
        if (message_data["from_group"].isInteger())
        {
            from_group = message_data["from_group"].asInteger();
        }
        else
        {
            from_group = message_data["from_group"].asString() == "Y";
        }

        LLSD metadata;
        if (message_data.has("metadata"))
        {
            metadata = message_data["metadata"];
        }

        EInstantMessage dialog = static_cast<EInstantMessage>(message_data["dialog"].asInteger());
        LLUUID session_id = message_data["transaction-id"].asUUID();
        if (session_id.isNull() && dialog == IM_FROM_TASK)
        {
            session_id = message_data["asset_id"].asUUID();
        }

        LLAppViewer::instance()->postToMainCoro([=]()
            {
                std::vector<U8> local_bin_bucket = bin_bucket;
                LLHost local_sender = sender;
                LLIMProcessing::processNewMessage(
                    message_data["from_agent_id"].asUUID(),
                    from_group,
                    message_data["to_agent_id"].asUUID(),
                    message_data.has("offline") ? static_cast<U8>(message_data["offline"].asInteger()) : IM_OFFLINE,
                    dialog,
                    session_id,
                    static_cast<U32>(message_data["timestamp"].asInteger()),
                    message_data["from_agent_name"].asString(),
                    message_data["message"].asString(),
                    static_cast<U32>((message_data.has("parent_estate_id")) ? message_data["parent_estate_id"].asInteger() : 1), // 1 - IMMainland
                    message_data["region_id"].asUUID(),
                    position,
                    local_bin_bucket.data(),
                    S32(local_bin_bucket.size()),
                    local_sender,
                    metadata,
                    message_data["asset_id"].asUUID());
            });

    }
// R4B-COPY-END offline_loop

}

// The instant message handler, reduced to the inventory offer and group
// notice dialogs: the harness supplies the parameters and locals that
// LLIMProcessing::processNewMessage declares around the copied regions.
void process_offer_im(LLUUID from_id, bool from_group, EInstantMessage dialog, LLUUID session_id,
                      U32 timestamp, std::string agentName, std::string message,
                      U8* binary_bucket, S32 binary_bucket_size, LLHost& sender, LLUUID aux_id)
{
    std::string name = agentName;

// R4B-COPY-BEGIN im_name_clean
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::processNewMessage (name trim and clean)
// lines: 739-749
// blob: 8fa885d340173cce97bd512894040370f970b57a
    LLStringUtil::trim(name);
    if (name.empty())
    {
        name = LLTrans::getString("Unnamed");
    }

    // Preserve the unaltered name for use in group notice mute checking.
    std::string original_name = name;

    // IDEVO convert new-style "Resident" names for display
    name = clean_name_from_im(name, dialog);
// R4B-COPY-END im_name_clean

// R4B-COPY-BEGIN im_dnd
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::processNewMessage (do not disturb)
// lines: 751-751
// blob: 8fa885d340173cce97bd512894040370f970b57a
    bool is_do_not_disturb = gAgent.isDoNotDisturb();
// R4B-COPY-END im_dnd

// R4B-COPY-BEGIN im_is_muted
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::processNewMessage (is_muted)
// lines: 761-763
// blob: 8fa885d340173cce97bd512894040370f970b57a
    bool is_muted = LLMuteList::getInstance()->isMuted(from_id, name, LLMute::flagTextChat)
        // object IMs contain sender object id in session_id (STORM-1209)
        || (dialog == IM_FROM_TASK && LLMuteList::getInstance()->isMuted(session_id));
// R4B-COPY-END im_is_muted

    LLSD args;
    LLSD payload;
    switch (dialog)
    {
// R4B-COPY-BEGIN im_group_notice_case
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::processNewMessage case IM_GROUP_NOTICE
// lines: 1278-1463
// blob: 8fa885d340173cce97bd512894040370f970b57a
// substitutions: 'LLDate::now()' -> 'r4b_now()'
        case IM_GROUP_NOTICE:
        case IM_GROUP_NOTICE_REQUESTED:
        {
            LL_INFOS("Messaging") << "Received IM_GROUP_NOTICE message." << LL_ENDL;

            LLUUID agent_id;
            U8 has_inventory;
            U8 asset_type = 0;
            LLUUID group_id;
            std::string item_name;

            if (aux_id.notNull())
            {
                // aux_id contains group id, binary bucket contains name and asset type
                group_id = aux_id;
                has_inventory = binary_bucket_size > 1;
                from_group = true; // inaccurate value correction
                if (has_inventory)
                {
                    std::string str_bucket = ll_safe_string((char*)binary_bucket, binary_bucket_size);

                    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
                    boost::char_separator<char> sep("|", "", boost::keep_empty_tokens);
                    tokenizer tokens(str_bucket, sep);
                    tokenizer::iterator iter = tokens.begin();

                    asset_type = (LLAssetType::EType)(atoi((*(iter++)).c_str()));
                    iter++; // wearable type if applicable, otherwise asset type
                    item_name = std::string((*(iter++)).c_str());
                }
            }
            else
            {
                // All info is in binary bucket, read it for more information.
                struct notice_bucket_header_t
                {
                    U8 has_inventory;
                    U8 asset_type;
                    LLUUID group_id;
                };
                struct notice_bucket_full_t
                {
                    struct notice_bucket_header_t header;
                    U8 item_name[DB_INV_ITEM_NAME_BUF_SIZE];
                }*notice_bin_bucket;

                // Make sure the binary bucket is big enough to hold the header
                // and a null terminated item name.
                if ((binary_bucket_size < (S32)((sizeof(notice_bucket_header_t) + sizeof(U8))))
                    || (binary_bucket[binary_bucket_size - 1] != '\0'))
                {
                    LL_WARNS("Messaging") << "Malformed group notice binary bucket" << LL_ENDL;
                    // <FS:Ansariel> Don't flash task icon
                    //break;
                    return;
                }

                notice_bin_bucket = (struct notice_bucket_full_t*) &binary_bucket[0];
                has_inventory = notice_bin_bucket->header.has_inventory;
                asset_type = notice_bin_bucket->header.asset_type;
                group_id = notice_bin_bucket->header.group_id;
                item_name = ll_safe_string((const char*)notice_bin_bucket->item_name);
            }

            if (group_id != from_id)
            {
                agent_id = from_id;
            }
            else
            {
                auto index = original_name.find(" Resident");
                if (index != std::string::npos)
                {
                    original_name = original_name.substr(0, index);
                }

                // The group notice packet does not have an AgentID.  Obtain one from the name cache.
                // If last name is "Resident" strip it out so the cache name lookup works.
                std::string legacy_name = gCacheName->buildLegacyName(original_name);
                agent_id = LLAvatarNameCache::getInstance()->findIdByName(legacy_name);

                if (agent_id.isNull())
                {
                    LL_WARNS("Messaging") << "buildLegacyName returned null while processing " << original_name << LL_ENDL;
                }
            }

            if (agent_id.notNull() && LLMuteList::getInstance()->isMuted(agent_id))
            {
                // <FS:Ansariel> Don't flash task icon
                //break;
                return;
            }

            // If there is inventory, give the user the inventory offer.
            LLOfferInfo* info = NULL;

            if (has_inventory)
            {
                info = new LLOfferInfo();

                info->mIM = dialog;
                info->mFromID = from_id;
                info->mFromGroup = from_group;
                info->mTransactionID = session_id;
                info->mType = (LLAssetType::EType) asset_type;
                info->mFolderID = gInventory.findCategoryUUIDForType(LLFolderType::assetTypeToFolderType(info->mType));
                std::string from_name;

                // <FS:Ansariel> FIRE-17714: Make group notice attachment confirmation localizable
                //from_name += "A group member named ";
                //from_name += name;
                LLStringUtil::format_map_t args;
                args["NAME"] = name;
                from_name += LLTrans::getString("InvOfferGroupNoticeName", args);

                // <FS:Ansariel> FIRE-29677 / SL-13720 workaround
                if (LLUUID::validate(name))
                {
                    LLStringUtil::format_map_t args;
                    args["NAME"] = "%s";
                    std::string placeholder_from_name = LLTrans::getString("InvOfferGroupNoticeName", args);
                    from_name = name + "|" + placeholder_from_name;
                }
                // </FS:Ansariel>

                info->mFromName = from_name;
                info->mDesc = item_name;
                info->mHost = sender;
            }

            std::string str(message);

            // Tokenize the string.
            // TODO: Support escaped tokens ("||" -> "|")
            typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
            boost::char_separator<char> sep("|", "", boost::keep_empty_tokens);
            tokenizer tokens(str, sep);
            tokenizer::iterator iter = tokens.begin();

            std::string subj(*iter++);
            std::string mes(*iter++);

            // Send the notification down the new path.
            // For requested notices, we don't want to send the popups.
            if (dialog != IM_GROUP_NOTICE_REQUESTED)
            {
                payload["subject"] = subj;
                payload["message"] = mes;
                payload["sender_name"] = name;
                payload["sender_id"] = agent_id;
                payload["group_id"] = group_id;
                payload["inventory_name"] = item_name;
                payload["received_time"] = r4b_now();
                if (info && info->asLLSD())
                {
                    payload["inventory_offer"] = info->asLLSD();
                }

                // <FS:Ansariel> FIRE-505: Group name not shown in notification well
                //LLSD args;
                //args["SUBJECT"] = subj;
                //args["MESSAGE"] = mes;
                //LLDate notice_date = LLDate(timestamp).notNull() ? LLDate(timestamp) : r4b_now();
                //LLNotifications::instance().add(LLNotification::Params("GroupNotice").substitutions(args).payload(payload).time_stamp(notice_date));
                //make_ui_sound("UISndGroupNotice"); // <FS:PP> Group notice sound
                if (group_id.isNull())
                {
                    LL_WARNS() << "Received group notice with null id!" << LL_ENDL;
                }
                gCacheName->get(group_id, true, boost::bind(&notification_group_name_cb, _2, name, subj, mes, payload, timestamp));
                // </FS:Ansariel>
            }

            // Also send down the old path for now.
            if (IM_GROUP_NOTICE_REQUESTED == dialog)
            {

                LLPanelGroup::showNotice(subj, mes, group_id, has_inventory, item_name, info);
            }
            else
            {
                delete info;
            }
        }
        break;
// R4B-COPY-END im_group_notice_case

// R4B-COPY-BEGIN im_offer_case
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::processNewMessage case IM_INVENTORY_OFFERED
// lines: 1555-1651
// blob: 8fa885d340173cce97bd512894040370f970b57a
        case IM_INVENTORY_OFFERED:
        case IM_TASK_INVENTORY_OFFERED:
            // Someone has offered us some inventory.
        {
            LLOfferInfo* info = new LLOfferInfo;
            if (IM_INVENTORY_OFFERED == dialog)
            {
                struct offer_agent_bucket_t
                {
                    S8      asset_type;
                    LLUUID  object_id;
                }*bucketp;

                if (sizeof(offer_agent_bucket_t) != binary_bucket_size)
                {
                    LL_WARNS("Messaging") << "Malformed inventory offer from agent" << LL_ENDL;
                    delete info;
                    // <FS:Ansariel> Don't flash task icon
                    //break;
                    return;
                }
                bucketp = (struct offer_agent_bucket_t*) &binary_bucket[0];
                info->mType = (LLAssetType::EType) bucketp->asset_type;
                info->mObjectID = bucketp->object_id;
                info->mFromObject = false;
            }
            else // IM_TASK_INVENTORY_OFFERED
            {
                if (sizeof(S8) == binary_bucket_size)
                {
                    info->mType = (LLAssetType::EType) binary_bucket[0];
                }
                else
                {
                    /*RIDER*/ // The previous version of the protocol returned the wrong binary bucket... we
                    // still might be able to figure out the type... even though the offer is not retrievable.

                    // Should be safe to remove once DRTSIM-451 fully deploys
                    std::string str_bucket(reinterpret_cast<char *>(binary_bucket));
                    std::string str_type(str_bucket.substr(0, str_bucket.find('|')));

                    std::stringstream type_convert(str_type);

                    S32 type;
                    type_convert >> type;

                    // We could try AT_UNKNOWN which would be more accurate, but that causes an auto decline
                    info->mType = static_cast<LLAssetType::EType>(type);
                    // Don't break in the case of a bad binary bucket.  Go ahead and show the
                    // accept/decline popup even though it will not do anything.
                    LL_WARNS("Messaging") << "Malformed inventory offer from object, type might be " << info->mType << LL_ENDL;
                }
                info->mObjectID = LLUUID::null;
                info->mFromObject = true;
            }

            info->mIM = dialog;
            info->mFromID = from_id;
            info->mFromGroup = from_group;
            info->mFolderID = gInventory.findCategoryUUIDForType(LLFolderType::assetTypeToFolderType(info->mType));

            info->mTransactionID = session_id.notNull() ? session_id : aux_id;

            info->mFromName = name;
            info->mDesc = message;
            info->mHost = sender;
            //if (((is_do_not_disturb && !is_owned_by_me) || is_muted))
            if (is_muted)
            {
                // Prefetch the offered item so that it can be discarded by the appropriate observer. (EXT-4331)
                if (IM_INVENTORY_OFFERED == dialog)
                {
                    LLInventoryFetchItemsObserver* fetch_item = new LLInventoryFetchItemsObserver(info->mObjectID);
                    fetch_item->startFetch();
                    delete fetch_item;
                    // Same as closing window
                    info->forceResponse(IOR_DECLINE);
                }
                else
                {
                    info->forceResponse(IOR_MUTE);
                }
            }
            // old logic: busy mode must not affect interaction with objects (STORM-565)
            // new logic: inventory offers from in-world objects should be auto-declined (CHUI-519)
            else if (is_do_not_disturb && dialog == IM_TASK_INVENTORY_OFFERED)
            {
                // Until throttling is implemented, do not disturb mode should reject inventory instead of silently
                // accepting it.  SEE SL-39554
                info->forceResponse(IOR_DECLINE);
            }
            else
            {
                inventory_offer_handler(info);
            }
        }
        break;
// R4B-COPY-END im_offer_case

        default:
            break;
    }
}

// Harness wrappers (not copies) around the file-local copied classes.
bool eval_functor(const std::string& functor, LLInventoryCategory* cat, LLInventoryItem* item, S32* count)
{
    if (functor == "giveable") { LLGiveable f; bool r = f(cat, item); if (count) *count = f.countNoCopy(); return r; }
    if (functor == "uncopyable") { LLUncopyableItems f; return f(cat, item); }
    if (functor == "droppable_transfer") { LLDroppableItem f(true); bool r = f(cat, item); if (count) *count = f.countNoCopy(); return r; }
    if (functor == "droppable_no_transfer") { LLDroppableItem f(false); bool r = f(cat, item); if (count) *count = f.countNoCopy(); return r; }
    if (functor == "drop_copyable") { LLDropCopyableItems f; return f(cat, item); }
    llassert_always(false);
    return false;
}

void collect_with(const std::string& functor, const LLUUID& root, LLInventoryModel::cat_array_t& cats,
                  LLInventoryModel::item_array_t& items, S32* count)
{
    if (functor == "giveable") { LLGiveable f; gInventory.collectDescendentsIf(root, cats, items, LLInventoryModel::EXCLUDE_TRASH, f); if (count) *count = f.countNoCopy(); return; }
    if (functor == "uncopyable") { LLUncopyableItems f; gInventory.collectDescendentsIf(root, cats, items, LLInventoryModel::EXCLUDE_TRASH, f); return; }
    if (functor == "droppable_transfer") { LLDroppableItem f(true); gInventory.collectDescendentsIf(root, cats, items, LLInventoryModel::EXCLUDE_TRASH, f); if (count) *count = f.countNoCopy(); return; }
    llassert_always(false);
}

void share_set_avatars(const uuid_vec_t& ids)
{
    action_give_inventory::LLShareInfo::instance().mAvatarUuids = ids;
    action_give_inventory::LLShareInfo::instance().mAvatarNames.assign(ids.size(), LLAvatarName());
}

void share_give_inventory_cb(const LLSD& notification, const LLSD& response, std::set<LLUUID> selected)
{
    action_give_inventory::give_inventory_cb(notification, response, selected);
}

// Factories for RustDecisions's executor (llinventory_offer_oracle_rust.cpp):
// the classes above are local to this file, as in the viewer's sources.
LLInventoryObserver* harness_new_discard_agent_offer(const LLUUID& folder_id, const LLUUID& object_id)
{
    return new LLDiscardAgentOffer(folder_id, object_id);
}

void harness_post_offer_notification(const LLNotification::Params& p, const LLUUID& from_id, bool is_group)
{
    LLPostponedNotification::add<LLPostponedOfferNotification>(p, from_id, is_group);
}

// The class name of an observer, for the event log.
std::string observer_kind(const LLInventoryObserver* o)
{
    if (dynamic_cast<const LLDiscardAgentOffer*>(o)) return "LLDiscardAgentOffer";
    if (dynamic_cast<const LLOpenAgentOffer*>(o)) return "LLOpenAgentOffer";
    if (dynamic_cast<const RlvGiveToRLVAgentOffer*>(o)) return "RlvGiveToRLVAgentOffer";
    if (dynamic_cast<const RlvGiveToRLVTaskOffer*>(o)) return "RlvGiveToRLVTaskOffer";
    if (dynamic_cast<const LLOpenTaskGroupOffer*>(o)) return "LLOpenTaskGroupOffer";
    if (dynamic_cast<const LLInventoryFetchItemsObserver*>(o)) return "LLInventoryFetchItemsObserver";
    return "LLInventoryObserver";
}

} // namespace r4b

namespace r4b_pack
{
using LLMessageSystem = r4b::LLMessageSystem;

// R4B-COPY-BEGIN pack_instant_message_block
// source: indra/llmessage/llinstantmessage.cpp
// function: pack_instant_message_block
// lines: 96-165
// blob: 885b20c0dcf831842495ca64d6573811f2bdaf69
void pack_instant_message_block(
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
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, from_id);
    msg->addUUIDFast(_PREHASH_SessionID, session_id);
    msg->nextBlockFast(_PREHASH_MessageBlock);
    msg->addBOOLFast(_PREHASH_FromGroup, from_group);
    msg->addUUIDFast(_PREHASH_ToAgentID, to_id);
    msg->addU32Fast(_PREHASH_ParentEstateID, parent_estate_id);
    msg->addUUIDFast(_PREHASH_RegionID, region_id);
    msg->addVector3Fast(_PREHASH_Position, position);
    msg->addU8Fast(_PREHASH_Offline, offline);
    msg->addU8Fast(_PREHASH_Dialog, (U8) dialog);
    msg->addUUIDFast(_PREHASH_ID, id);
    msg->addU32Fast(_PREHASH_Timestamp, timestamp);
    msg->addStringFast(_PREHASH_FromAgentName, name);
    S32 bytes_left = MTUBYTES;
    if(!message.empty())
    {
        char buffer[MTUBYTES];
        int num_written = snprintf(buffer, MTUBYTES, "%s", message.c_str());    /* Flawfinder: ignore */
        // snprintf returns number of bytes that would have been written
        // had the output not being truncated. In that case, it will
        // return either -1 or value >= passed in size value . So a check needs to be added
        // to detect truncation, and if there is any, only account for the
        // actual number of bytes written..and not what could have been
        // written.
        if (num_written < 0 || num_written >= MTUBYTES)
        {
            num_written = MTUBYTES - 1;
            LL_WARNS() << "pack_instant_message_block: message truncated: " << message << LL_ENDL;
        }

        bytes_left -= num_written;
        bytes_left = llmax(0, bytes_left);
        msg->addStringFast(_PREHASH_Message, buffer);
    }
    else
    {
        msg->addStringFast(_PREHASH_Message, NULL);
    }
    const U8* bb;
    if(binary_bucket)
    {
        bb = binary_bucket;
        binary_bucket_size = llmin(bytes_left, binary_bucket_size);
    }
    else
    {
        bb = (const U8*)EMPTY_BINARY_BUCKET;
        binary_bucket_size = EMPTY_BINARY_BUCKET_SIZE;
    }
    msg->addBinaryDataFast(_PREHASH_BinaryBucket, bb, binary_bucket_size);
}
// R4B-COPY-END pack_instant_message_block

} // namespace r4b_pack
