// Original object inventory policy shared by genuine reference/native owners.
// LGPL-2.1, like LLViewerObject. Getters are supplied lazily by typed peers.
#pragma once
#include "llagent.h"
#include "llviewerinventory.h"
#include "llinventorydefines.h"
#include "llfloaterreg.h"
#include "llviewerobject.h"
#include "message.h"
template<class Peer>
void vs_do_update_task_inventory(Peer& peer, LLPointer<LLViewerInventoryItem>& item, U8 key, bool is_new)
{
    LLViewerInventoryItem* old_item = NULL;
    if(TASK_INVENTORY_ITEM_KEY == key)
    {
        // <FS:ND> Do not use C-Style cast for polymorphic upcasting
//      old_item = (LLViewerInventoryItem*)peer.getInventoryObject(item->getUUID());
        old_item = dynamic_cast<LLViewerInventoryItem*>(peer.getInventoryObject(item->getUUID()));
        // </FS:ND>
    }
    else if(TASK_INVENTORY_ASSET_KEY == key)
    {
        old_item = peer.getInventoryItemByAsset(item->getAssetUUID());
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
    if(!is_new && peer.inventory())
    {
        // Attempt to update the local inventory. If we can get the
        // object perm, we have perfect visibility, so we want the
        // serial number to match. Otherwise, take our best guess and
        // make sure that the serial number does not match.
        peer.deleteInventoryItem(item_id);
        LLPermissions perm(item->getPermissions());
        LLPermissions* obj_perm = peer.objectPermissions();
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
            else if(peer.permYouOwner())
            {
                // best guess.
                perm.setOwnerAndGroup(LLUUID::null, gAgent.getID(), item->getPermissions().getGroup(), is_atomic);
                --peer.expectedSerial();
            }
            else
            {
                // dummy it up.
                perm.setOwnerAndGroup(LLUUID::null, LLUUID::null, LLUUID::null, is_atomic);
                --peer.expectedSerial();
            }
        }
        LLViewerInventoryItem* oldItem = item;
        LLViewerInventoryItem* new_item = new LLViewerInventoryItem(oldItem);
        new_item->setPermissions(perm);
        peer.inventory()->push_front(new_item);
        peer.doInventoryCallback();
        ++peer.expectedSerial();
    }
    else if (is_new)
    {
        ++peer.expectedSerial();
    }
}

template<class Peer>
void vs_update_task_inventory(Peer& peer, LLViewerInventoryItem* item, U8 key, bool is_new)
{
    // This slices the object into what we're concerned about on the
    // viewer. The simulator will take the permissions and transfer
    // ownership.
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), peer.id(), item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(),
                                  item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());
    if (!peer.hasRegion())
    {
        LL_WARNS() << "Object " << peer.id() << " has no region, item " << item->getUUID() << " not updated" << LL_ENDL;
        return;
    }
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_UpdateTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_UpdateData);
    msg->addU32Fast(_PREHASH_LocalID, peer.localID());
    msg->addU8Fast(_PREHASH_Key, key);
    msg->nextBlockFast(_PREHASH_InventoryData);
    task_item->packMessage(msg);
    msg->sendReliable(peer.host());

    // do the internal logic
    peer.doUpdateInventory(task_item, key, is_new);
}

template<class Peer>
void vs_save_task_script(Peer& peer, const LLViewerInventoryItem* item, bool active, bool is_new)
{
    /*
     * XXXPAM Investigate not making this copy.  Seems unecessary, but I'm unsure about the
     * interaction with peer.doUpdateInventory() called below.
     */
    LL_DEBUGS() << "LLViewerObject::saveScript() " << item->getUUID() << " " << item->getAssetUUID() << LL_ENDL;

    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), peer.id(), item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());

    if (!peer.hasRegion())
    {
        LL_WARNS() << "Object " << peer.id() << " has no region, script " << item->getUUID() << " not saved" << LL_ENDL;
        return;
    }

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RezScript);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
    msg->nextBlockFast(_PREHASH_UpdateBlock);
    msg->addU32Fast(_PREHASH_ObjectLocalID, (peer.localID()));
    U8 enabled = active;
    msg->addBOOLFast(_PREHASH_Enabled, enabled);
    msg->nextBlockFast(_PREHASH_InventoryBlock);
    task_item->packMessage(msg);
    msg->sendReliable(peer.host());

    // do the internal logic
    peer.doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, is_new);
}

template<class Peer>
void vs_move_task_inventory(Peer& peer, const LLUUID& folder_id, const LLUUID& item_id)
{
    LL_DEBUGS() << "LLViewerObject::moveInventory " << item_id << LL_ENDL;
    if (!peer.hasRegion())
    {
        LL_WARNS() << "Object " << peer.id() << " has no region, item " << item_id << " not moved" << LL_ENDL;
        return;
    }
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_FolderID, folder_id);
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, peer.localID());
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(peer.host());

    LLInventoryObject* inv_obj = peer.getInventoryObject(item_id);
    if(inv_obj)
    {
        LLViewerInventoryItem* item = (LLViewerInventoryItem*)inv_obj;
        if(!item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            peer.deleteInventoryItem(item_id);
            ++peer.expectedSerial();
        }
    }
}

template<class Peer>
void vs_remove_task_inventory(Peer& peer, const LLUUID& item_id)
{
    // close associated floater properties
    LLSD params;
    params["id"] = item_id;
    params["object"] = peer.id();
    LLFloaterReg::hideInstance("item_properties", params);

    // <FS:Ansariel> Keep legacy properties floater
    // close any associated floater properties
    LLFloaterReg::hideInstance("properties", item_id);
    // </FS:Ansariel>

    if (!peer.hasRegion())
    {
        LL_WARNS() << "Object " << peer.id() << " has no region, item " << item_id << " not removed" << LL_ENDL;
        return;
    }

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RemoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, peer.localID());
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(peer.host());
    peer.deleteInventoryItem(item_id);
    ++peer.expectedSerial();
}

template<class Peer>
void vs_update_task_inventory_local(Peer& peer, LLInventoryItem* item, U8 key)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), peer.id(), item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());

    // do the internal logic
    const bool is_new = false;
    peer.doUpdateInventory(task_item, key, is_new);
}

template<class Peer>
void vs_update_task_inventory_asset(Peer& peer, const LLViewerInventoryItem* item, const LLUUID& new_asset)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item);
    task_item->setAssetUUID(new_asset);

    // do the internal logic
    peer.doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, false);
}
