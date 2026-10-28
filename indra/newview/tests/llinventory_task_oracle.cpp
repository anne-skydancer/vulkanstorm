/**
 * @file llinventory_task_oracle.cpp
 * @brief Frozen copies of the task-inventory code of llviewerobject.cpp and
 * of the LLViewerInventoryItem members it uses (llviewerinventory.cpp), as
 * they were before vkstorm-rust rung seven, in the test namespace
 * vks_task_oracle (see llinventory_task_oracle.h). Derived mechanically: each
 * region between a ">>> VKS-R4A-COPY" and a "<<< VKS-R4A-COPY" line is the
 * source text of the named lines, unedited; tests/check_r4a_copies.py checks
 * every region against the live source. Not used by the viewer.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_task_oracle.h"

#include "llfile.h"
#include "llsdutil.h"
#include "llstl.h"
#include "message_prehash.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

namespace vks_task_oracle
{
// >>> VKS-R4A-COPY V0 indra/newview/llviewerinventory.cpp lines 87-88 blob 41e2a9b23982f076e14a5de056d443438f8980b3
// LOG_INV, LOG_LOCAL
// conditionals:
static const char * const LOG_INV("Inventory");
static const char * const LOG_LOCAL("InventoryLocalize");
// <<< VKS-R4A-COPY V0

// >>> VKS-R4A-COPY V1 indra/newview/llviewerinventory.cpp lines 104-233 blob 41e2a9b23982f076e14a5de056d443438f8980b3
// class LLLocalizedInventoryItemsDictionary and its constructor
// conditionals:
///----------------------------------------------------------------------------
/// Helper class to store special inventory item names and their localized values.
///----------------------------------------------------------------------------
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

LLLocalizedInventoryItemsDictionary::LLLocalizedInventoryItemsDictionary()
{
    mInventoryItemsDict["New Shape"]        = LLTrans::getString("New Shape");
    mInventoryItemsDict["New Skin"]         = LLTrans::getString("New Skin");
    mInventoryItemsDict["New Hair"]         = LLTrans::getString("New Hair");
    mInventoryItemsDict["New Eyes"]         = LLTrans::getString("New Eyes");
    mInventoryItemsDict["New Shirt"]        = LLTrans::getString("New Shirt");
    mInventoryItemsDict["New Pants"]        = LLTrans::getString("New Pants");
    mInventoryItemsDict["New Shoes"]        = LLTrans::getString("New Shoes");
    mInventoryItemsDict["New Socks"]        = LLTrans::getString("New Socks");
    mInventoryItemsDict["New Jacket"]       = LLTrans::getString("New Jacket");
    mInventoryItemsDict["New Gloves"]       = LLTrans::getString("New Gloves");
    mInventoryItemsDict["New Undershirt"]   = LLTrans::getString("New Undershirt");
    mInventoryItemsDict["New Underpants"]   = LLTrans::getString("New Underpants");
    mInventoryItemsDict["New Skirt"]        = LLTrans::getString("New Skirt");
    mInventoryItemsDict["New Alpha"]        = LLTrans::getString("New Alpha");
    mInventoryItemsDict["New Tattoo"]       = LLTrans::getString("New Tattoo");
    mInventoryItemsDict["New Universal"]    = LLTrans::getString("New Universal");
    mInventoryItemsDict["New Physics"]      = LLTrans::getString("New Physics");
    mInventoryItemsDict["Invalid Wearable"] = LLTrans::getString("Invalid Wearable");

    mInventoryItemsDict["New Gesture"]      = LLTrans::getString("New Gesture");
    mInventoryItemsDict["New Material"] = LLTrans::getString("New Material");
    mInventoryItemsDict["New Script"]       = LLTrans::getString("New Script");
    mInventoryItemsDict["New Folder"]       = LLTrans::getString("New Folder");
    mInventoryItemsDict["New Note"]         = LLTrans::getString("New Note");
    mInventoryItemsDict["Contents"]         = LLTrans::getString("Contents");

    mInventoryItemsDict["Gesture"]          = LLTrans::getString("Gesture");
    mInventoryItemsDict["Male Gestures"]    = LLTrans::getString("Male Gestures");
    mInventoryItemsDict["Female Gestures"]  = LLTrans::getString("Female Gestures");
    mInventoryItemsDict["Other Gestures"]   = LLTrans::getString("Other Gestures");
    mInventoryItemsDict["Speech Gestures"]  = LLTrans::getString("Speech Gestures");
    mInventoryItemsDict["Common Gestures"]  = LLTrans::getString("Common Gestures");

    //predefined gestures

    //male
    mInventoryItemsDict["Male - Excuse me"]         = LLTrans::getString("Male - Excuse me");
    mInventoryItemsDict["Male  - Get lost"]         = LLTrans::getString("Male - Get lost"); // double space after Male. EXT-8319
    mInventoryItemsDict["Male - Blow kiss"]         = LLTrans::getString("Male - Blow kiss");
    mInventoryItemsDict["Male - Boo"]               = LLTrans::getString("Male - Boo");
    mInventoryItemsDict["Male - Bored"]             = LLTrans::getString("Male - Bored");
    mInventoryItemsDict["Male - Hey"]               = LLTrans::getString("Male - Hey");
    mInventoryItemsDict["Male - Laugh"]             = LLTrans::getString("Male - Laugh");
    mInventoryItemsDict["Male - Repulsed"]          = LLTrans::getString("Male - Repulsed");
    mInventoryItemsDict["Male - Shrug"]             = LLTrans::getString("Male - Shrug");
    mInventoryItemsDict["Male - Stick tougue out"]  = LLTrans::getString("Male - Stick tougue out");
    mInventoryItemsDict["Male - Wow"]               = LLTrans::getString("Male - Wow");

    //female
    mInventoryItemsDict["Female - Chuckle"]         = LLTrans::getString("Female - Chuckle");
    mInventoryItemsDict["Female - Cry"]             = LLTrans::getString("Female - Cry");
    mInventoryItemsDict["Female - Embarrassed"]     = LLTrans::getString("Female - Embarrassed");
    mInventoryItemsDict["Female - Excuse me"]       = LLTrans::getString("Female - Excuse me");
    mInventoryItemsDict["Female  - Get lost"]       = LLTrans::getString("Female - Get lost"); // double space after Female. EXT-8319
    mInventoryItemsDict["Female - Blow kiss"]       = LLTrans::getString("Female - Blow kiss");
    mInventoryItemsDict["Female - Boo"]             = LLTrans::getString("Female - Boo");
    mInventoryItemsDict["Female - Bored"]           = LLTrans::getString("Female - Bored");
    mInventoryItemsDict["Female - Hey"]             = LLTrans::getString("Female - Hey");
    mInventoryItemsDict["Female - Hey baby"]        = LLTrans::getString("Female - Hey baby");
    mInventoryItemsDict["Female - Laugh"]           = LLTrans::getString("Female - Laugh");
    mInventoryItemsDict["Female - Looking good"]    = LLTrans::getString("Female - Looking good");
    mInventoryItemsDict["Female - Over here"]       = LLTrans::getString("Female - Over here");
    mInventoryItemsDict["Female - Please"]          = LLTrans::getString("Female - Please");
    mInventoryItemsDict["Female - Repulsed"]        = LLTrans::getString("Female - Repulsed");
    mInventoryItemsDict["Female - Shrug"]           = LLTrans::getString("Female - Shrug");
    mInventoryItemsDict["Female - Stick tougue out"]= LLTrans::getString("Female - Stick tougue out");
    mInventoryItemsDict["Female - Wow"]             = LLTrans::getString("Female - Wow");

    //common
    mInventoryItemsDict["/bow"]                     = LLTrans::getString("/bow");
    mInventoryItemsDict["/clap"]                    = LLTrans::getString("/clap");
    mInventoryItemsDict["/count"]                   = LLTrans::getString("/count");
    mInventoryItemsDict["/extinguish"]              = LLTrans::getString("/extinguish");
    mInventoryItemsDict["/kmb"]                     = LLTrans::getString("/kmb");
    mInventoryItemsDict["/muscle"]                  = LLTrans::getString("/muscle");
    mInventoryItemsDict["/no"]                      = LLTrans::getString("/no");
    mInventoryItemsDict["/no!"]                     = LLTrans::getString("/no!");
    mInventoryItemsDict["/paper"]                   = LLTrans::getString("/paper");
    mInventoryItemsDict["/pointme"]                 = LLTrans::getString("/pointme");
    mInventoryItemsDict["/pointyou"]                = LLTrans::getString("/pointyou");
    mInventoryItemsDict["/rock"]                    = LLTrans::getString("/rock");
    mInventoryItemsDict["/scissor"]                 = LLTrans::getString("/scissor");
    mInventoryItemsDict["/smoke"]                   = LLTrans::getString("/smoke");
    mInventoryItemsDict["/stretch"]                 = LLTrans::getString("/stretch");
    mInventoryItemsDict["/whistle"]                 = LLTrans::getString("/whistle");
    mInventoryItemsDict["/yes"]                     = LLTrans::getString("/yes");
    mInventoryItemsDict["/yes!"]                    = LLTrans::getString("/yes!");
    mInventoryItemsDict["afk"]                      = LLTrans::getString("afk");
    mInventoryItemsDict["dance1"]                   = LLTrans::getString("dance1");
    mInventoryItemsDict["dance2"]                   = LLTrans::getString("dance2");
    mInventoryItemsDict["dance3"]                   = LLTrans::getString("dance3");
    mInventoryItemsDict["dance4"]                   = LLTrans::getString("dance4");
    mInventoryItemsDict["dance5"]                   = LLTrans::getString("dance5");
    mInventoryItemsDict["dance6"]                   = LLTrans::getString("dance6");
    mInventoryItemsDict["dance7"]                   = LLTrans::getString("dance7");
    mInventoryItemsDict["dance8"]                   = LLTrans::getString("dance8");
}
// <<< VKS-R4A-COPY V1

// >>> VKS-R4A-COPY V2 indra/newview/llviewerinventory.cpp lines 335-408 blob 41e2a9b23982f076e14a5de056d443438f8980b3
// LLViewerInventoryItem constructors, destructor, copyViewerItem, copyItem
// conditionals:
LLViewerInventoryItem::LLViewerInventoryItem(const LLUUID& uuid,
                                             const LLUUID& parent_uuid,
                                             const LLPermissions& perm,
                                             const LLUUID& asset_uuid,
                                             LLAssetType::EType type,
                                             LLInventoryType::EType inv_type,
                                             const std::string& name,
                                             const std::string& desc,
                                             const LLSaleInfo& sale_info,
                                             U32 flags,
                                             time_t creation_date_utc) :
    LLInventoryItem(uuid, parent_uuid, perm, asset_uuid, type, inv_type,
                    name, desc, sale_info, flags, (S32)creation_date_utc),
    mIsComplete(true)
{
}

LLViewerInventoryItem::LLViewerInventoryItem(const LLUUID& item_id,
                                             const LLUUID& parent_id,
                                             const std::string& name,
                                             LLInventoryType::EType inv_type) :
    LLInventoryItem(),
    mIsComplete(false)
{
    mUUID = item_id;
    mParentUUID = parent_id;
    mInventoryType = inv_type;
    mName = name;
}

LLViewerInventoryItem::LLViewerInventoryItem() :
    LLInventoryItem(),
    mIsComplete(false)
{
}

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

LLViewerInventoryItem::LLViewerInventoryItem(const LLInventoryItem *other) :
    LLInventoryItem(other),
    mIsComplete(true)
{
}


LLViewerInventoryItem::~LLViewerInventoryItem()
{
}

void LLViewerInventoryItem::copyViewerItem(const LLViewerInventoryItem* other)
{
    LLInventoryItem::copyItem(other);
    mIsComplete = other->mIsComplete;
    mTransactionID = other->mTransactionID;
    mIsRunning = other->mIsRunning;
    mIsFaulted = other->mIsFaulted;
}

// virtual
void LLViewerInventoryItem::copyItem(const LLInventoryItem *other)
{
    LLInventoryItem::copyItem(other);
    mIsComplete = true;
    mTransactionID.setNull();
}
// <<< VKS-R4A-COPY V2

// >>> VKS-R4A-COPY V3 indra/newview/llviewerinventory.cpp lines 550-604 blob 41e2a9b23982f076e14a5de056d443438f8980b3
// LLViewerInventoryItem::unpackMessage (LLSD and message forms), setTransactionID, packMessage, importLegacyStream
// conditionals:
// virtual
bool LLViewerInventoryItem::unpackMessage(const LLSD& item)
{
    bool rv = LLInventoryItem::fromLLSD(item);
    mIsRunning = item["running"].asBoolean();
    mIsFaulted = item["faulted"].asBoolean();

    LLLocalizedInventoryItemsDictionary::getInstance()->localizeInventoryObjectName(mName);

    mIsComplete = true;
    return rv;
}

// virtual
bool LLViewerInventoryItem::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num)
{
    bool rv = LLInventoryItem::unpackMessage(msg, block, block_num);

    LLLocalizedInventoryItemsDictionary::getInstance()->localizeInventoryObjectName(mName);

    mIsComplete = true;
    return rv;
}

void LLViewerInventoryItem::setTransactionID(const LLTransactionID& transaction_id)
{
    mTransactionID = transaction_id;
}

void LLViewerInventoryItem::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_ItemID, mUUID);
    msg->addUUIDFast(_PREHASH_FolderID, mParentUUID);
    mPermissions.packMessage(msg);
    msg->addUUIDFast(_PREHASH_TransactionID, mTransactionID);
    S8 type = static_cast<S8>(mType);
    msg->addS8Fast(_PREHASH_Type, type);
    type = static_cast<S8>(mInventoryType);
    msg->addS8Fast(_PREHASH_InvType, type);
    msg->addU32Fast(_PREHASH_Flags, mFlags);
    mSaleInfo.packMessage(msg);
    msg->addStringFast(_PREHASH_Name, mName);
    msg->addStringFast(_PREHASH_Description, mDescription);
    msg->addS32Fast(_PREHASH_CreationDate, (S32)mCreationDate);
    U32 crc = getCRC32();
    msg->addU32Fast(_PREHASH_CRC, crc);
}

// virtual
bool LLViewerInventoryItem::importLegacyStream(std::istream& input_stream)
{
    bool rv = LLInventoryItem::importLegacyStream(input_stream);
    mIsComplete = true;
    return rv;
}
// <<< VKS-R4A-COPY V3

// >>> VKS-R4A-COPY O1 indra/newview/llviewerobject.cpp lines 155-160 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// PHYSICS_TIMESTEP, MAX_INV_FILE_READ_FAILS, MAX_OBJECT_BINARY_DATA_SIZE, INVENTORY_UPDATE_WAIT_TIME_DESYNC, INVENTORY_UPDATE_WAIT_TIME_OUTDATED
// conditionals:
const F32 PHYSICS_TIMESTEP = 1.f / 45.f;
const U32 MAX_INV_FILE_READ_FAILS = 25;
const S32 MAX_OBJECT_BINARY_DATA_SIZE = 60 + 16;

const F64 INVENTORY_UPDATE_WAIT_TIME_DESYNC = 5; // seconds
const F64 INVENTORY_UPDATE_WAIT_TIME_OUTDATED = 1;
// <<< VKS-R4A-COPY O1

// >>> VKS-R4A-COPY O2 indra/newview/llviewerobject.cpp lines 2823-3263 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// deleteInventoryItem, doUpdateInventory, saveScript, moveInventory, dirtyInventory, registerInventoryListener, removeInventoryListener, isInventoryPending, clearInventoryListeners, hasInventoryListeners, requestInventory, fetchInventoryFromServer, fetchInventoryDelayed, fetchInventoryDelayedCoro, fetchInventoryFromCapCoro
// conditionals:
// delete an item in the inventory, but don't tell the server. This is
// used internally by remove, update, and savescript.
// This will only delete the first item with an item_id in the list
void LLViewerObject::deleteInventoryItem(const LLUUID& item_id)
{
    if(mInventory)
    {
        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for( ; it != end; ++it )
        {
            if((*it)->getUUID() == item_id)
            {
                // This is safe only because we return immediatly.
                mInventory->erase(it); // will deref and delete it
                return;
            }
        }
        doInventoryCallback();
    }
}

void LLViewerObject::doUpdateInventory(
    LLPointer<LLViewerInventoryItem>& item,
    U8 key,
    bool is_new)
{
    LLViewerInventoryItem* old_item = NULL;
    if(TASK_INVENTORY_ITEM_KEY == key)
    {
        // <FS:ND> Do not use C-Style cast for polymorphic upcasting
//      old_item = (LLViewerInventoryItem*)getInventoryObject(item->getUUID());
        old_item = dynamic_cast<LLViewerInventoryItem*>(getInventoryObject(item->getUUID()));
        // </FS:ND>
    }
    else if(TASK_INVENTORY_ASSET_KEY == key)
    {
        old_item = getInventoryItemByAsset(item->getAssetUUID());
    }
    LLUUID item_id;
    LLUUID new_owner;
    LLUUID new_group;
    bool group_owned = false;
    if(old_item)
    {
        item_id = old_item->getUUID();
        new_owner = old_item->getPermissions().getOwner();
        new_group = old_item->getPermissions().getGroup();
        group_owned = old_item->getPermissions().isGroupOwned();
        old_item = NULL;
    }
    else
    {
        item_id = item->getUUID();
    }
    if(!is_new && mInventory)
    {
        // Attempt to update the local inventory. If we can get the
        // object perm, we have perfect visibility, so we want the
        // serial number to match. Otherwise, take our best guess and
        // make sure that the serial number does not match.
        deleteInventoryItem(item_id);
        LLPermissions perm(item->getPermissions());
        LLPermissions* obj_perm = LLSelectMgr::getInstance()->findObjectPermissions(this);
        bool is_atomic = (S32)LLAssetType::AT_OBJECT != item->getType();
        if(obj_perm)
        {
            perm.setOwnerAndGroup(LLUUID::null, obj_perm->getOwner(), obj_perm->getGroup(), is_atomic);
        }
        else
        {
            if(group_owned)
            {
                perm.setOwnerAndGroup(LLUUID::null, new_owner, new_group, is_atomic);
            }
            else if(!new_owner.isNull())
            {
                // The object used to be in inventory, so we can
                // assume the owner and group will match what they are
                // there.
                perm.setOwnerAndGroup(LLUUID::null, new_owner, new_group, is_atomic);
            }
            // *FIX: can make an even better guess by using the mPermGroup flags
            else if(permYouOwner())
            {
                // best guess.
                perm.setOwnerAndGroup(LLUUID::null, gAgent.getID(), item->getPermissions().getGroup(), is_atomic);
                --mExpectedInventorySerialNum;
            }
            else
            {
                // dummy it up.
                perm.setOwnerAndGroup(LLUUID::null, LLUUID::null, LLUUID::null, is_atomic);
                --mExpectedInventorySerialNum;
            }
        }
        LLViewerInventoryItem* oldItem = item;
        LLViewerInventoryItem* new_item = new LLViewerInventoryItem(oldItem);
        new_item->setPermissions(perm);
        mInventory->push_front(new_item);
        doInventoryCallback();
        ++mExpectedInventorySerialNum;
    }
    else if (is_new)
    {
        ++mExpectedInventorySerialNum;
    }
}

// save a script, which involves removing the old one, and rezzing
// in the new one. This method should be called with the asset id
// of the new and old script AFTER the bytecode has been saved.
// When creating a new script, the asset should be null.  The server
// will create the new script based on the script_language (LSL or Lua)
// If a template_id is provided, the new script will be a copy of that item.
//
// *IMPORTANT* If template_id is provided, it must be the ITEM ID of a
// copy/mod-able script in the user's inventory. The simulator will verify
// permissions.
void LLViewerObject::saveScript(const LLViewerInventoryItem* item,
    bool active, bool is_new, const LLUUID& template_id)
{
    /*
     * XXXPAM Investigate not making this copy.  Seems unecessary, but I'm unsure about the
     * interaction with doUpdateInventory() called below.
     */
    LL_DEBUGS() << "LLViewerObject::saveScript() " << item->getUUID() << " " << item->getAssetUUID() << LL_ENDL;

    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());

    if (!mRegionp)
    {
        LL_WARNS() << "Object " << mID << " has no region, script " << item->getUUID() << " not saved" << LL_ENDL;
        return;
    }

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RezScript);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
    msg->nextBlockFast(_PREHASH_UpdateBlock);
    msg->addU32Fast(_PREHASH_ObjectLocalID, (mLocalID));
    U8 enabled = active;
    msg->addBOOLFast(_PREHASH_Enabled, enabled);
    msg->nextBlockFast(_PREHASH_InventoryBlock);
    task_item->packMessage(msg);

    // This is a completely new script (no asset id) and we've provided a template.
    // Note that the script subtype on the template will override any subtype on the item.
    if (task_item->getAssetUUID().isNull() && template_id.notNull())
    {
        msg->nextBlock("NewScriptInfo");
        msg->addUUID("TemplateID", template_id);
    }

    msg->sendReliable(mRegionp->getHost());

    // do the internal logic
    doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, is_new);
}

void LLViewerObject::moveInventory(const LLUUID& folder_id,
                                   const LLUUID& item_id)
{
    LL_DEBUGS() << "LLViewerObject::moveInventory " << item_id << LL_ENDL;
    if (!mRegionp)
    {
        LL_WARNS() << "Object " << mID << " has no region, item " << item_id << " not moved" << LL_ENDL;
        return;
    }
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_FolderID, folder_id);
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(mRegionp->getHost());

    LLInventoryObject* inv_obj = getInventoryObject(item_id);
    if(inv_obj)
    {
        LLViewerInventoryItem* item = (LLViewerInventoryItem*)inv_obj;
        if(!item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            deleteInventoryItem(item_id);
            ++mExpectedInventorySerialNum;
        }
    }
}

void LLViewerObject::dirtyInventory()
{
    // If there aren't any LLVOInventoryListeners, we won't be
    // able to update our mInventory when it comes back from the
    // simulator, so we should not clear the inventory either.
    if(mInventory && !mInventoryCallbacks.empty())
    {
        mInventory->clear(); // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }
    mInventoryDirty = true;
}

void LLViewerObject::registerInventoryListener(LLVOInventoryListener* listener, void* user_data)
{
    LLInventoryCallbackInfo* info = new LLInventoryCallbackInfo;
    info->mListener = listener;
    info->mInventoryData = user_data;
    mInventoryCallbacks.push_front(info);
}

void LLViewerObject::removeInventoryListener(LLVOInventoryListener* listener)
{
    if (listener == NULL)
        return;
    for (callback_list_t::iterator iter = mInventoryCallbacks.begin();
         iter != mInventoryCallbacks.end(); )
    {
        callback_list_t::iterator curiter = iter++;
        LLInventoryCallbackInfo* info = *curiter;
        if (info->mListener == listener)
        {
            delete info;
            mInventoryCallbacks.erase(curiter);
            break;
        }
    }
}

bool LLViewerObject::isInventoryPending()
{
    return mInvRequestState != INVENTORY_REQUEST_STOPPED;
}

void LLViewerObject::clearInventoryListeners()
{
    for_each(mInventoryCallbacks.begin(), mInventoryCallbacks.end(), DeletePointer());
    mInventoryCallbacks.clear();
}

bool LLViewerObject::hasInventoryListeners()
{
    return !mInventoryCallbacks.empty();
}

void LLViewerObject::requestInventory()
{
    if(mInventoryDirty && mInventory && !mInventoryCallbacks.empty())
    {
        mInventory->clear(); // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }

    if(mInventory)
    {
        // inventory is either up to date or doesn't has a listener
        // if it is dirty, leave it this way in case we gain a listener
        if (isInventoryPending())
        {
            // A request or transfer is outstanding: give the listeners the
            // contents we have, but keep the request state and xfer id that
            // doInventoryCallback() releases, so the pending reply is still
            // handled as such and later requests are not duplicated.
            const EInventoryRequestState request_state = mInvRequestState;
            const U64 request_xfer_id = mInvRequestXFerId;
            doInventoryCallback();
            mInvRequestState = request_state;
            mInvRequestXFerId = request_xfer_id;
        }
        else
        {
            doInventoryCallback();
        }
    }
    else
    {
        // since we are going to request it now
        mInventoryDirty = false;

        // Note: throws away duplicate requests
        fetchInventoryFromServer();
    }
}

void LLViewerObject::fetchInventoryFromServer()
{
    if (!isInventoryPending())
    {
        delete mInventory;
        mInventory = NULL;

        // This will get reset by doInventoryCallback or processTaskInv
        mInvRequestState = INVENTORY_REQUEST_PENDING;

        if (mRegionp && !mRegionp->getCapability("RequestTaskInventory").empty())
        {
            LLCoros::instance().launch("LLViewerObject::fetchInventoryFromCapCoro()",
                                       boost::bind(&LLViewerObject::fetchInventoryFromCapCoro, mID));
        }
        else if (!mRegionp)
        {
            // Nothing to send to: fail the request as an HTTP error does in fetchInventoryFromCapCoro.
            LL_WARNS() << "Object " << mID << " has no region, task inventory not requested" << LL_ENDL;
            mInvRequestState = INVENTORY_REQUEST_STOPPED;
        }
        else
        {
            LL_WARNS() << "Using old task inventory path!" << LL_ENDL;
        // Results in processTaskInv
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_RequestTaskInventory);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_InventoryData);
        msg->addU32Fast(_PREHASH_LocalID, mLocalID);
        msg->sendReliable(mRegionp->getHost());
        }
    }
}

void LLViewerObject::fetchInventoryDelayed(const F64 &time_seconds)
{
    // unless already waiting, drop previous request and schedule an update
    if (mInvRequestState != INVENTORY_REQUEST_WAIT)
    {
        if (mInvRequestXFerId != 0)
        {
            // abort download.
            gXferManager->abortRequestById(mInvRequestXFerId, -1);
            mInvRequestXFerId = 0;
        }
        mInvRequestState = INVENTORY_REQUEST_WAIT; // affects isInventoryPending()
        LLCoros::instance().launch("LLViewerObject::fetchInventoryDelayedCoro()",
            boost::bind(&LLViewerObject::fetchInventoryDelayedCoro, mID, time_seconds));
    }
}

//static
void LLViewerObject::fetchInventoryDelayedCoro(const LLUUID task_inv, const F64 time_seconds)
{
    llcoro::suspendUntilTimeout((float)time_seconds);
    LLViewerObject *obj = gObjectList.findObject(task_inv);
    if (obj)
    {
        // Might be good idea to prolong delay here in case expected serial changed.
        // As it is, it will get a response with obsolete serial and will delay again.

        // drop waiting state to unlock isInventoryPending()
        obj->mInvRequestState = INVENTORY_REQUEST_STOPPED;
        obj->fetchInventoryFromServer();
    }
}

//static
void LLViewerObject::fetchInventoryFromCapCoro(const LLUUID task_inv)
{
    LLViewerObject *obj = gObjectList.findObject(task_inv);
    if (obj)
    {
        LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
        LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
                                   httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("TaskInventoryRequest", httpPolicy);
        LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();
        std::string url = obj->mRegionp->getCapability("RequestTaskInventory") + "?task_id=" + obj->mID.asString();
        // If we already have a copy of the inventory then add it so the server won't re-send something we already have.
        // We expect this case to crop up in the case of failed inventory mutations, but it might happen otherwise as well.
        if (obj->mInventorySerialNum && obj->mInventory)
            url += "&inventory_serial=" + std::to_string(obj->mInventorySerialNum);

        obj->mInvRequestState = INVENTORY_XFER;
        LLSD result = httpAdapter->getAndSuspend(httpRequest, url);

        LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

        // Object may have gone away while we were suspended, double-check that it still exists
        obj = gObjectList.findObject(task_inv);
        if (!obj)
        {
            LL_WARNS() << "Object " << task_inv << " went away while fetching inventory, dropping result" << LL_ENDL;
            return;
        }

        bool potentially_stale = false;
        if (status)
        {
            // Dealing with inventory serials is kind of funky. They're monotonically increasing and 16 bits,
            // so we expect them to overflow, but we can use inv serial < expected serial as a signal that we may
            // have mutated the task inventory since we kicked off the request, and those mutations may have not
            // been taken into account yet. Of course, those mutations may have actually failed which would result
            // in the inv serial never increasing.
            //
            // When we detect this case, set the expected inv serial to the inventory serial we actually received
            // and kick off a re-request after a slight delay.
            S16 serial = (S16)result["inventory_serial"].asInteger();
            potentially_stale = serial < obj->mExpectedInventorySerialNum;
            LL_INFOS() << "Inventory loaded for " << task_inv << LL_ENDL;
            obj->mInventorySerialNum = serial;
            obj->mExpectedInventorySerialNum = serial;
            obj->loadTaskInvLLSD(result);
        }
        else if (status.getType() == 304)
        {
            LL_INFOS() << "Inventory wasn't changed on server!" << LL_ENDL;
            obj->mInvRequestState = INVENTORY_REQUEST_STOPPED;
            // Even though it wasn't necessary to send a response, we still may have mutated
            // the inventory since we kicked off the request, check for that case.
            potentially_stale = obj->mInventorySerialNum < obj->mExpectedInventorySerialNum;
            // Set this to what we already have so that we don't re-request a second time.
            obj->mExpectedInventorySerialNum = obj->mInventorySerialNum;
        }
        else
        {
            // Not sure that there's anything sensible we can do to recover here, retrying in a loop would be bad.
            LL_WARNS() << "Error status while requesting task inventory: " << status.toString() << LL_ENDL;
            obj->mInvRequestState = INVENTORY_REQUEST_STOPPED;
        }

        if (potentially_stale)
        {
            // Stale? I guess we can use what we got for now, but we'll have to re-request
            LL_WARNS() << "Stale inv_serial? Re-requesting." << LL_ENDL;
            obj->fetchInventoryDelayed(INVENTORY_UPDATE_WAIT_TIME_OUTDATED);
        }
    }
}
// <<< VKS-R4A-COPY O2

// >>> VKS-R4A-COPY O3 indra/newview/llviewerobject.cpp lines 3410-4049 blob 26ec5b036f0ad9cf9aa40b5e9da2bd641c80ae8c
// struct LLFilenameAndTask, processTaskInv, processTaskInvFile, loadTaskInvFile, loadTaskInvLLSD, doInventoryCallback, removeInventory, isAssetInInventory, updateMaterialInventory, updateInventory, updateInventoryLocal, getInventoryObject, getInventoryItem, getInventoryContents, getInventoryRoot, getInventoryItemByAsset (both), updateViewerInventoryAsset
// conditionals:
struct LLFilenameAndTask
{
    LLUUID mTaskID;
    std::string mFilename;

    // for sequencing in case of multiple updates
    S16 mSerial;
#ifdef _DEBUG
    static S32 sCount;
    LLFilenameAndTask()
    {
        ++sCount;
        LL_DEBUGS() << "Constructing LLFilenameAndTask: " << sCount << LL_ENDL;
    }
    ~LLFilenameAndTask()
    {
        --sCount;
        LL_DEBUGS() << "Destroying LLFilenameAndTask: " << sCount << LL_ENDL;
    }
private:
    LLFilenameAndTask(const LLFilenameAndTask& rhs);
    const LLFilenameAndTask& operator=(const LLFilenameAndTask& rhs) const;
#endif
};

#ifdef _DEBUG
S32 LLFilenameAndTask::sCount = 0;
#endif

// static
void LLViewerObject::processTaskInv(LLMessageSystem* msg, void** user_data)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    LLUUID task_id;
    msg->getUUIDFast(_PREHASH_InventoryData, _PREHASH_TaskID, task_id);
    LLViewerObject* object = gObjectList.findObject(task_id);
    if (!object)
    {
        LL_WARNS() << "LLViewerObject::processTaskInv object "
            << task_id << " does not exist." << LL_ENDL;
        return;
    }

    // we can receive multiple task updates simultaneously, make sure we will not rewrite newer with older update
    S16 serial = 0;
    msg->getS16Fast(_PREHASH_InventoryData, _PREHASH_Serial, serial);

    if (object->mRegionp && !object->mRegionp->getCapability("RequestTaskInventory").empty())
    {
        // It seems that simulator may ask us to re-download the task inventory if an update to the inventory
        // happened out-of-band while we had the object selected (like if a script is saved.)
        //
        // If we're meant to use the HTTP capability, ignore the contents of the UDP message and fetch the
        // inventory via the CAP so that we don't flow down the UDP inventory request path unconditionally here.
        // We shouldn't need to wait, as any updates should already be ready to fetch by this point.
        LL_INFOS() << "Handling unsolicited ReplyTaskInventory for " << task_id << LL_ENDL;
        object->mExpectedInventorySerialNum = serial;
        object->fetchInventoryFromServer();
        return;
    }

    if (serial == object->mInventorySerialNum
        && serial < object->mExpectedInventorySerialNum)
    {
        // Loop Protection.
        // We received same serial twice.
        // Viewer did some changes to inventory that couldn't be saved server side
        // or something went wrong to cause serial to be out of sync.
        // Drop xfer and restart after some time, assign server's value as expected
        LL_WARNS() << "Task inventory serial might be out of sync, server serial: " << serial << " client expected serial: " << object->mExpectedInventorySerialNum << LL_ENDL;
        object->mExpectedInventorySerialNum = serial;
        object->fetchInventoryDelayed(INVENTORY_UPDATE_WAIT_TIME_DESYNC);
    }
    else if (serial < object->mExpectedInventorySerialNum)
    {
        // Out of date message, record to current serial for loop protection, but do not load it
        // Drop xfer and restart after some time
        if (serial < object->mInventorySerialNum)
        {
            LL_WARNS() << "Task serial decreased. Potentially out of order packet or desync." << LL_ENDL;
        }
        object->mInventorySerialNum = serial;
        object->fetchInventoryDelayed(INVENTORY_UPDATE_WAIT_TIME_OUTDATED);
    }
    else if (serial >= object->mExpectedInventorySerialNum)
    {
        LLFilenameAndTask* ft = new LLFilenameAndTask;
        ft->mTaskID = task_id;
        ft->mSerial = serial;

        // We received version we expected or newer. Load it.
        object->mInventorySerialNum = ft->mSerial;
        object->mExpectedInventorySerialNum = ft->mSerial;

        std::string unclean_filename;
        msg->getStringFast(_PREHASH_InventoryData, _PREHASH_Filename, unclean_filename);
        ft->mFilename = LLDir::getScrubbedFileName(unclean_filename);

        if (ft->mFilename.empty())
        {
            LL_DEBUGS() << "Task has no inventory" << LL_ENDL;
            // mock up some inventory to make a drop target.
            if (object->mInventory)
            {
                object->mInventory->clear(); // will deref and delete it
            }
            else
            {
                object->mInventory = new LLInventoryObject::object_list_t();
            }
            LLPointer<LLInventoryObject> obj;
            obj = new LLInventoryObject(object->mID, LLUUID::null,
                LLAssetType::AT_CATEGORY,
                "Contents");
            object->mInventory->push_front(obj);
            object->doInventoryCallback();
            delete ft;
            return;
        }
        if (!object->mRegionp)
        {
            LL_WARNS() << "Object " << task_id << " has no region, task inventory file not requested" << LL_ENDL;
            // Release a pending request, as fetchInventoryFromServer() does without a
            // region, so a later fetch is not refused as a duplicate. An active
            // transfer (INVENTORY_XFER) completes on its own.
            if (object->mInvRequestState == INVENTORY_REQUEST_PENDING)
            {
                object->mInvRequestState = INVENTORY_REQUEST_STOPPED;
            }
            delete ft;
            return;
        }
        U64 new_id = gXferManager->requestFile(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, ft->mFilename),
            ft->mFilename, LL_PATH_CACHE,
            object->mRegionp->getHost(),
            true,
            &LLViewerObject::processTaskInvFile,
            (void**)ft, // This takes ownership of ft
            LLXferManager::HIGH_PRIORITY);
        if (object->mInvRequestState == INVENTORY_XFER)
        {
            if (new_id > 0 && new_id != object->mInvRequestXFerId)
            {
                // we started new download.
                gXferManager->abortRequestById(object->mInvRequestXFerId, -1);
                object->mInvRequestXFerId = new_id;
            }
        }
        else
        {
            object->mInvRequestState = INVENTORY_XFER;
            object->mInvRequestXFerId = new_id;
        }
    }
}

void LLViewerObject::processTaskInvFile(void** user_data, S32 error_code, LLExtStat ext_status)
{
    LLFilenameAndTask* ft = (LLFilenameAndTask*)user_data;
    LLViewerObject* object = NULL;

    if (ft
        && (0 == error_code)
        && (object = gObjectList.findObject(ft->mTaskID))
        && ft->mSerial >= object->mInventorySerialNum)
    {
        object->mInventorySerialNum = ft->mSerial;
        LL_DEBUGS() << "Receiving inventory task file for serial " << object->mInventorySerialNum << " taskid: " << ft->mTaskID << LL_ENDL;
        if (ft->mSerial < object->mExpectedInventorySerialNum)
        {
            // User managed to change something while inventory was loading
            LL_DEBUGS() << "Processing file that is potentially out of date for task: " << ft->mTaskID << LL_ENDL;
        }

        if (object->loadTaskInvFile(ft->mFilename))
        {

        // <FS:ND> Crashfix, not sure why object->mInventory can be 0
        if( !object->mInventory )
        {
            LL_WARNS() << "object->mInventory == 0" << LL_ENDL;
            delete ft;
            return;
        }
        // </FS:ND>

            LLInventoryObject::object_list_t::iterator it = object->mInventory->begin();
            LLInventoryObject::object_list_t::iterator end = object->mInventory->end();
            std::list<LLUUID>& pending_lst = object->mPendingInventoryItemsIDs;

            for (; it != end && pending_lst.size(); ++it)
            {
                LLViewerInventoryItem* item = dynamic_cast<LLViewerInventoryItem*>(it->get());
                if(item && item->getType() != LLAssetType::AT_CATEGORY)
                {
                    // <FS> Copy & paste error
                    //std::list<LLUUID>::iterator id_it = std::find(pending_lst.begin(), pending_lst.begin(), item->getAssetUUID());
                    std::list<LLUUID>::iterator id_it = std::find(pending_lst.begin(), pending_lst.end(), item->getAssetUUID());
                    // </FS>
                    if (id_it != pending_lst.end())
                    {
                        pending_lst.erase(id_it);
                    }
                }
            }
        }
        else
        {
            // MAINT-2597 - crash when trying to edit a no-mod object
            // Somehow get an contents inventory response, but with an invalid stream (possibly 0 size?)
            // Stated repro was specific to no-mod objects so failing without user interaction should be safe.
            LL_WARNS() << "Trying to load invalid task inventory file. Ignoring file contents." << LL_ENDL;
        }
    }
    else
    {
        // This Occurs When two requests were made, and the first one
        // has already handled it.
        LL_DEBUGS() << "Problem loading task inventory. Return code: "
                 << error_code << LL_ENDL;
    }
    delete ft;
}

bool LLViewerObject::loadTaskInvFile(const std::string& filename)
{
    std::string filename_and_local_path = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, filename);
    llifstream ifs(filename_and_local_path.c_str());
    if(ifs.good())
    {
        U32 fail_count = 0;
        char buffer[MAX_STRING];    /* Flawfinder: ignore */
        // *NOTE: This buffer size is hard coded into scanf() below.
        char keyword[MAX_STRING];   /* Flawfinder: ignore */
        if(mInventory)
        {
            mInventory->clear(); // will deref and delete it
        }
        else
        {
            mInventory = new LLInventoryObject::object_list_t;
        }
        while(ifs.good())
        {
            ifs.getline(buffer, MAX_STRING);
            if (sscanf(buffer, " %254s", keyword) == EOF) /* Flawfinder: ignore */
            {
                // Blank file?
                LL_WARNS() << "Issue reading from file '"
                        << filename << "'" << LL_ENDL;
                break;
            }
            else if(0 == strcmp("inv_item", keyword))
            {
                LLPointer<LLInventoryObject> inv = new LLViewerInventoryItem;
                inv->importLegacyStream(ifs);
                mInventory->push_front(inv);
            }
            else if(0 == strcmp("inv_object", keyword))
            {
                LLPointer<LLInventoryObject> inv = new LLInventoryObject;
                inv->importLegacyStream(ifs);
                inv->rename("Contents");
                mInventory->push_front(inv);
            }
            else if (fail_count >= MAX_INV_FILE_READ_FAILS)
            {
                LL_WARNS() << "Encountered too many unknowns while reading from file: '"
                        << filename << "'" << LL_ENDL;
                break;
            }
            else
            {
                // Is there really a point to continue processing? We already failing to display full inventory
                fail_count++;
                LL_WARNS_ONCE() << "Unknown token while reading from inventory file. Token: '"
                        << keyword << "'" << LL_ENDL;
            }
        }
        ifs.close();
        LLFile::remove(filename_and_local_path);
    }
    else
    {
        LL_WARNS() << "unable to load task inventory: " << filename_and_local_path
                << LL_ENDL;
        return false;
    }
    doInventoryCallback();

    return true;
}

void LLViewerObject::loadTaskInvLLSD(const LLSD& inv_result)
{
    if (inv_result.has("contents"))
    {
        if(mInventory)
        {
            mInventory->clear(); // will deref and delete it
        }
        else
        {
            mInventory = new LLInventoryObject::object_list_t;
        }

        // Synthesize the "Contents" category, the viewer expects it, but it isn't sent.
        LLPointer<LLInventoryObject> inv = new LLInventoryObject(mID, LLUUID::null, LLAssetType::AT_CATEGORY, "Contents");
        mInventory->push_front(inv);

        const LLSD& inventory = inv_result["contents"];
        for (const auto& inv_entry : llsd::inArray(inventory))
        {
            if (inv_entry.has("item_id"))
            {
                LLPointer<LLViewerInventoryItem> inv = new LLViewerInventoryItem;
                inv->unpackMessage(inv_entry);
                mInventory->push_front(inv);
            }
            else
            {
                LL_WARNS_ONCE() << "Unknown inventory entry while reading from inventory file. Entry: '"
                                << inv_entry << "'" << LL_ENDL;
            }
        }
    }
    else
    {
        LL_WARNS() << "unable to load task inventory: " << inv_result << LL_ENDL;
        // Fail the request as an HTTP error does in fetchInventoryFromCapCoro, so that
        // later requests are not dropped as duplicates of this one.
        mInvRequestState = INVENTORY_REQUEST_STOPPED;
        return;
    }
    doInventoryCallback();
}

void LLViewerObject::doInventoryCallback()
{
    for (callback_list_t::iterator iter = mInventoryCallbacks.begin();
         iter != mInventoryCallbacks.end(); )
    {
        callback_list_t::iterator curiter = iter++;
        LLInventoryCallbackInfo* info = *curiter;
        if (info->mListener != NULL)
        {
            info->mListener->inventoryChanged(this,
                                 mInventory,
                                 mInventorySerialNum,
                                 info->mInventoryData);
        }
        else
        {
            LL_INFOS() << "LLViewerObject::doInventoryCallback() deleting bad listener entry." << LL_ENDL;
            delete info;
            mInventoryCallbacks.erase(curiter);
        }
    }

    // release inventory loading state
    mInvRequestXFerId = 0;
    mInvRequestState = INVENTORY_REQUEST_STOPPED;
}

void LLViewerObject::removeInventory(const LLUUID& item_id)
{
    // close associated floater properties
    LLSD params;
    params["id"] = item_id;
    params["object"] = mID;
    LLFloaterReg::hideInstance("item_properties", params);

    // <FS:Ansariel> Keep legacy properties floater
    // close any associated floater properties
    LLFloaterReg::hideInstance("properties", item_id);
    // </FS:Ansariel>

    if (!mRegionp)
    {
        LL_WARNS() << "Object " << mID << " has no region, item " << item_id << " not removed" << LL_ENDL;
        return;
    }

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RemoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(mRegionp->getHost());
    deleteInventoryItem(item_id);
    ++mExpectedInventorySerialNum;
}

bool LLViewerObject::isAssetInInventory(LLViewerInventoryItem* item, LLAssetType::EType type)
{
    bool result = false;

    if (item)
    {
        // For now mPendingInventoryItemsIDs only stores textures and materials
        // but if it gets to store more types, it will need to verify type as well
        // since null can be a shared default id and it is fine to need a null
        // script and a null material simultaneously.
        std::list<LLUUID>::iterator begin = mPendingInventoryItemsIDs.begin();
        std::list<LLUUID>::iterator end = mPendingInventoryItemsIDs.end();

        bool is_fetching = std::find(begin, end, item->getAssetUUID()) != end;

        // null is the default asset for materials and default for scripts
        // so need to check type as well
        bool is_fetched = getInventoryItemByAsset(item->getAssetUUID(), type) != NULL;

        result = is_fetched || is_fetching;
    }

    return result;
}

void LLViewerObject::updateMaterialInventory(LLViewerInventoryItem* item, U8 key, bool is_new)
{
    if (!item)
    {
        return;
    }
    if (LLAssetType::AT_TEXTURE != item->getType()
        && LLAssetType::AT_MATERIAL != item->getType())
    {
        // Not supported
        return;
    }

    if (isAssetInInventory(item, item->getType()))
    {
        // already there
        return;
    }

    if (!mRegionp)
    {
        // updateInventory() sends nothing without a region; don't leave the
        // asset pending, or isAssetInInventory() refuses it from then on.
        LL_WARNS() << "Object " << mID << " has no region, item " << item->getUUID() << " not updated" << LL_ENDL;
        return;
    }

    mPendingInventoryItemsIDs.push_back(item->getAssetUUID());
    updateInventory(item, key, is_new);
}

void LLViewerObject::updateInventory(
    LLViewerInventoryItem* item,
    U8 key,
    bool is_new)
{
    // This slices the object into what we're concerned about on the
    // viewer. The simulator will take the permissions and transfer
    // ownership.
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(),
                                  item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());
    if (!mRegionp)
    {
        LL_WARNS() << "Object " << mID << " has no region, item " << item->getUUID() << " not updated" << LL_ENDL;
        return;
    }
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_UpdateTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_UpdateData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addU8Fast(_PREHASH_Key, key);
    msg->nextBlockFast(_PREHASH_InventoryData);
    task_item->packMessage(msg);
    msg->sendReliable(mRegionp->getHost());

    // do the internal logic
    doUpdateInventory(task_item, key, is_new);
}

void LLViewerObject::updateInventoryLocal(LLInventoryItem* item, U8 key)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());

    // do the internal logic
    const bool is_new = false;
    doUpdateInventory(task_item, key, is_new);
}

LLInventoryObject* LLViewerObject::getInventoryObject(const LLUUID& item_id)
{
    LLInventoryObject* rv = NULL;
    if(mInventory)
    {
        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for ( ; it != end; ++it)
        {
            if((*it)->getUUID() == item_id)
            {
                rv = *it;
                break;
            }
        }
    }
    return rv;
}

LLInventoryItem* LLViewerObject::getInventoryItem(const LLUUID& item_id)
{
    LLInventoryObject* iobj = getInventoryObject(item_id);
    if (!iobj || iobj->getType() == LLAssetType::AT_CATEGORY)
    {
        return NULL;
    }
    LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(iobj);
    return item;
}

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

LLInventoryObject* LLViewerObject::getInventoryRoot()
{
    if (!mInventory || !mInventory->size())
    {
        return NULL;
    }
    return mInventory->back();
}

LLViewerInventoryItem* LLViewerObject::getInventoryItemByAsset(const LLUUID& asset_id)
{
    if (mInventoryDirty)
        LL_WARNS() << "Peforming inventory lookup for object " << mID << " that has dirty inventory!" << LL_ENDL;

    LLViewerInventoryItem* rv = NULL;
    if(mInventory)
    {
        LLViewerInventoryItem* item = NULL;

        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for( ; it != end; ++it)
        {
            LLInventoryObject* obj = *it;
            if(obj->getType() != LLAssetType::AT_CATEGORY
               && obj->getType() != LLAssetType::AT_NONE ) // <FS:ND> check for AT_NONE too loadTaskInvFile can create such objects for "Contants"
            {
                // *FIX: gank-ass down cast!
                item = (LLViewerInventoryItem*)obj;
                if(item->getAssetUUID() == asset_id)
                {
                    rv = item;
                    break;
                }
            }
        }
    }
    return rv;
}

LLViewerInventoryItem* LLViewerObject::getInventoryItemByAsset(const LLUUID& asset_id, LLAssetType::EType type)
{
    if (mInventoryDirty)
        LL_WARNS() << "Peforming inventory lookup for object " << mID << " that has dirty inventory!" << LL_ENDL;

    LLViewerInventoryItem* rv = NULL;
    if (type == LLAssetType::AT_CATEGORY)
    {
        // Whatever called this shouldn't be trying to get a folder by asset
        // categories don't have assets
        llassert(0);
        return rv;
    }

    if (mInventory)
    {
        LLViewerInventoryItem* item = NULL;

        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for (; it != end; ++it)
        {
            LLInventoryObject* obj = *it;
            if (obj->getType() == type)
            {
                // *FIX: gank-ass down cast!
                item = (LLViewerInventoryItem*)obj;
                if (item->getAssetUUID() == asset_id)
                {
                    rv = item;
                    break;
                }
            }
        }
    }
    return rv;
}

void LLViewerObject::updateViewerInventoryAsset(
                    const LLViewerInventoryItem* item,
                    const LLUUID& new_asset)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item);
    task_item->setAssetUUID(new_asset);

    // do the internal logic
    doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, false);
}
// <<< VKS-R4A-COPY O3

// >>> VKS-R4A-COPY O4 indra/newview/llviewerobject.cpp lines 6319-6336 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// countInventoryContents
// conditionals:
// Find the number of instances of this object's inventory that are of the given type
S32 LLViewerObject::countInventoryContents(LLAssetType::EType type)
{
    S32 count = 0;
    if( mInventory )
    {
        LLInventoryObject::object_list_t::const_iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::const_iterator end = mInventory->end();
        for(  ; it != end ; ++it )
        {
            if( (*it)->getType() == type )
            {
                ++count;
            }
        }
    }
    return count;
}
// <<< VKS-R4A-COPY O4

// >>> VKS-R4A-COPY O5 indra/newview/llviewerobject.cpp lines 7296-7302 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// LLInventoryCallbackInfo::~LLInventoryCallbackInfo
// conditionals:
LLViewerObject::LLInventoryCallbackInfo::~LLInventoryCallbackInfo()
{
    if (mListener)
    {
        mListener->clearVOInventoryListener();
    }
}
// <<< VKS-R4A-COPY O5

// >>> VKS-R4A-COPY O6 indra/newview/llviewerobject.cpp lines 8271-8352 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// createInventoryItem, createInventoryItemCoro
// conditionals:
void LLViewerObject::createInventoryItem(
    LLAssetType::EType asset_type,
    LLInventoryType::EType inventory_type,
    U8 sub_type,
    const std::string& name,
    const std::string& description,
    const LLPermissions& permissions,
    const LLSD& params,
    std::function<void(bool, const LLSD&)> callback)
{
    if (!mRegionp)
    {
        if (callback) callback(false, LLSD().with("message", "No region"));
        return;
    }

    std::string cap_url = mRegionp->getCapability("CreateTaskInventoryItem");
    if (cap_url.empty())
    {
        if (callback) callback(false, LLSD().with("message", "Capability not available"));
        return;
    }

    LLSD body;
    body["object_id"]      = mID;
    body["inventory_type"] = (S32)inventory_type;
    body["asset_type"]     = (S32)asset_type;
    body["sub_type"]       = (S32)sub_type;
    body["name"]           = name;
    body["description"]    = description;

    LLSD perm_llsd;
    perm_llsd["base"]       = (S32)permissions.getMaskBase();
    perm_llsd["owner"]      = (S32)permissions.getMaskOwner();
    perm_llsd["everyone"]   = (S32)permissions.getMaskEveryone();
    perm_llsd["group"]      = (S32)permissions.getMaskGroup();
    perm_llsd["next_owner"] = (S32)permissions.getMaskNextOwner();
    body["permissions"]     = perm_llsd;

    if (params.isDefined())
    {
        body["params"] = params;
    }

    LLCoros::instance().launch("LLViewerObject::createInventoryItemCoro",
        boost::bind(&LLViewerObject::createInventoryItemCoro, cap_url, body, callback));
}


void LLViewerObject::createInventoryItemCoro(
    const std::string cap_url, const LLSD body,
    std::function<void(bool, const LLSD&)> callback)
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter =
        std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("CreateTaskInventoryItem", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    LLSD result = httpAdapter->postAndSuspend(httpRequest, cap_url, body);

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    bool success = static_cast<bool>(status) && result["success"].asBoolean();

    if (success)
    {
        LLUUID object_id = body["object_id"].asUUID();
        LLViewerObject* obj = gObjectList.findObject(object_id);
        if (obj)
        {
            ++obj->mExpectedInventorySerialNum;
            obj->dirtyInventory();
            obj->requestInventory();
        }
    }

    if (callback)
    {
        callback(success, result);
    }
}
// <<< VKS-R4A-COPY O6

} // namespace vks_task_oracle
