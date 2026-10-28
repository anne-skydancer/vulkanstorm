/**
 * @file llpanelcontents.cpp
 * @brief Object contents panel in the tools floater.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

// file include
#include "llpanelcontents.h"
#include "llcombobox.h"
#include "llscripteditorws.h"
#include "vstaskinventoryobject.h"
#include "vsnativeim.h"
#include "vsavatarservices.h"
#include "llviewerwindow.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsobjectselection.h"
#endif

// linden library includes
#include "llerror.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llfontgl.h"
#include "llinventorydefines.h"
#include "llmaterialtable.h"
#include "llpermissionsflags.h"
#include "llrect.h"
#include "llstring.h"
#include "llui.h"
#include "m3math.h"
#include "material_codes.h"

// project includes
#include "llagent.h"
#include "llpanelobjectinventory.h"
#include "llpreviewscript.h"
#include "llresmgr.h"
#include "llselectmgr.h"
#include "lltool.h"
#include "lltoolcomp.h"
#include "lltoolmgr.h"
#include "lltooldraganddrop.h" // <FS:FIRE-36059> For custom script template
#include "lltrans.h"
#include "llviewerassettype.h"
#include "llviewercontrol.h" // <FS:FIRE-36059> For custom script template
#include "llviewerinventory.h"
#include "llviewermenu.h" // <FS> Script reset in edit floater
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llworld.h"
#include "llfloaterperms.h"
// [RLVa:KB] - Checked: 2011-05-22 (RLVa-1.3.1a)
#include "rlvhandler.h"
#include "rlvlocks.h"
#include "fsnewitemctrl.h"
// [/RLVa:KB]

//
// Imported globals
//


//
// Globals
//
const char* LLPanelContents::TENTATIVE_SUFFIX = "_tentative";
const char* LLPanelContents::PERMS_OWNER_INTERACT_KEY = "perms_owner_interact";
const char* LLPanelContents::PERMS_OWNER_CONTROL_KEY = "perms_owner_control";
const char* LLPanelContents::PERMS_GROUP_INTERACT_KEY = "perms_group_interact";
const char* LLPanelContents::PERMS_GROUP_CONTROL_KEY = "perms_group_control";
const char* LLPanelContents::PERMS_ANYONE_INTERACT_KEY = "perms_anyone_interact";
const char* LLPanelContents::PERMS_ANYONE_CONTROL_KEY = "perms_anyone_control";

namespace
{
// Query the authoritative selection owners. This has no private selection or
// permission cache; each getter retains the original controller's evaluation.
class VSContentsSelection
{
#if VS_NATIVE_VULKAN
    std::shared_ptr<VSObjectSelection> mNative;
#endif
    bool native() const { return gViewerWindow && gViewerWindow->isNativeVulkan(); }
public:
    VSContentsSelection()
    {
#if VS_NATIVE_VULKAN
        if (const auto owner=VSNativeSession::active()) mNative=owner->retainObjectSelection();
#endif
    }
    VSTaskInventoryObject firstRoot() const
    {
#if VS_NATIVE_VULKAN
        if (native())
        {
            const auto object=mNative ? mNative->firstRootObject(true) : std::nullopt;
            return object ? VSTaskInventoryObject::find(object->id) : VSTaskInventoryObject{};
        }
#endif
        const auto* object=LLSelectMgr::instance().getSelection()->getFirstRootObject(true);
        return object ? VSTaskInventoryObject::find(object->getID()) : VSTaskInventoryObject{};
    }
    void group(LLUUID& id) const
    {
#if VS_NATIVE_VULKAN
        if (native()) { id.setNull();if (mNative) mNative->selectGetGroup(id);return; }
#endif
        LLSelectMgr::instance().selectGetGroup(id);
    }
    bool allVolumes() const
    {
#if VS_NATIVE_VULKAN
        if (native()) return mNative && mNative->selectionAllPCode(LL_PCODE_VOLUME);
#endif
        return LLSelectMgr::instance().selectionAllPCode(LL_PCODE_VOLUME);
    }
    S32 roots() const
    {
#if VS_NATIVE_VULKAN
        if (native()) return mNative ? mNative->rootObjectCount() : 0;
#endif
        return LLSelectMgr::instance().getSelection()->getRootObjectCount();
    }
    S32 objects() const
    {
#if VS_NATIVE_VULKAN
        if (native()) return mNative ? mNative->objectCount() : 0;
#endif
        return LLSelectMgr::instance().getSelection()->getObjectCount();
    }
};
}

bool LLPanelContents::postBuild()
{
    setMouseOpaque(false);

    getChild<LLUICtrl>("button new script")->setCommitCallback(boost::bind(&LLPanelContents::onNewScriptFlyoutCommit, this, _1));
    childSetAction("button new notecard", boost::bind(&LLPanelContents::onNewNotecardCommit, this));
    childSetAction("button permissions",&LLPanelContents::onClickPermissions, this);
    childSetAction("btn_reset_scripts", &LLPanelContents::onClickResetScripts, this);
    childSetAction("button refresh", &LLPanelContents::onClickRefresh, this);

    mPublishButton = getChild<LLButton>("button publish");
    mPublishButton->setClickedCallback([this](LLUICtrl*, const LLSD&) { onClickPublish(); });

    mFilterEditor = getChild<LLFilterEditor>("contents_filter");
    mFilterEditor->setCommitCallback([&](LLUICtrl*, const LLSD&) { onFilterEdit(); });

    mPanelInventoryObject = getChild<LLPanelObjectInventory>("contents_inventory");

    // update permission filter once UI is fully initialized
    mSavedFolderState.setApply(false);

    return true;
}

LLPanelContents::LLPanelContents()
    :   LLPanel(),
        mAccountCurrent(vs_native_im_guard()),
        mPanelInventoryObject(NULL)
{
}


LLPanelContents::~LLPanelContents()
{
    mSimulatorFeaturesConnection.disconnect();
    // Children all cleaned up by default view destructor.
}


void LLPanelContents::getState(const VSTaskInventoryObject& objectp)
{
    const VSContentsSelection selection;
    if( !objectp )
    {
        mSimulatorFeaturesConnection.disconnect();
        mLastScriptObjectID.setNull();
        getChildView("button new script")->setEnabled(false);
        getChildView("btn_reset_scripts")->setEnabled(false);
        getChildView("button new notecard")->setEnabled(false);
        mPublishButton->setEnabled(false);
        mPublishButton->setToggleState(false);
        return;
    }

    LLUUID group_id;            // used for SL-23488
    selection.group(group_id);  // sets group_id as a side effect SL-23488

    // BUG? Check for all objects being editable?
    bool editable = gAgent.isGodlike()
                    || (objectp->permModify() && !objectp->isPermanentEnforced()
                           && ( objectp->permYouOwner() || ( !group_id.isNull() && gAgent.isInGroup(group_id) )));  // solves SL-23488
    bool all_volume = selection.allVolumes();

    S32  object_count = selection.objects();
    S32  root_count = selection.roots();
    bool single_root  = (root_count == 1);

// [RLVa:KB] - Checked: 2010-04-01 (RLVa-1.2.0c) | Modified: RLVa-1.0.5a
    if ( (rlv_handler_t::isEnabled()) && (editable) )
    {
        // Don't allow creation of new scripts if it's non-detachable
        if (objectp->isAttachment())
            editable = !objectp->attachmentLocked();

        // Don't allow creation of new scripts if we're @unsit=n or @sittp=n restricted and we're sitting on the selection
        if ( (editable) && ((gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP))) )
        {
            // Only check the first (non-)root object because nothing else would result in enabling the button (see below)
            const auto pObj=selection.firstRoot();

            editable =
                (pObj) && vs_avatar_character() && !pObj->agentSittingOnRoot();
        }
    }
// [/RLVa:KB]

    bool new_button_enabled = editable && all_volume && (single_root || (object_count == 1));

    // Edit script button - ok if object is editable and there's an unambiguous destination for the object.
    getChildView("button new script")->setEnabled(new_button_enabled);
    getChildView("btn_reset_scripts")->setEnabled(new_button_enabled);

    // Enable the Lua script option only when the region supports it.
    bool lua_region = false;
    LLViewerObject* legacy_object = objectp->legacy();
    LLViewerRegion* region = legacy_object ? legacy_object->getRegion() : nullptr;
    if (region && region->simulatorFeaturesReceived())
    {
        LLSD simulatorFeatures;
        region->getSimulatorFeatures(simulatorFeatures);
        lua_region = simulatorFeatures["LuaScriptsEnabled"].asBoolean();
    }
    mSimulatorFeaturesConnection.disconnect();
    if (region && !region->simulatorFeaturesReceived())
    {
        mSimulatorFeaturesConnection = region->setSimulatorFeaturesReceivedCallback(
            boost::bind(&LLPanelContents::onSimulatorFeaturesReceived, this, _1));
    }
    LLComboBox* new_script = getChild<LLComboBox>("button new script");
    new_script->setEnabledByValue("lua", lua_region);
    if (mLastScriptObjectID != objectp->getID() || mLastLuaRegion != lua_region)
    {
        mNewScriptIsLua = lua_region;
        new_script->setValue(mNewScriptIsLua ? "lua" : "lsl");
    }
    mLastScriptObjectID = objectp->getID();
    mLastLuaRegion = lua_region;

    getChildView("button permissions")->setEnabled(!objectp->isPermanentEnforced());
    mPanelInventoryObject->setEnabled(!objectp->isPermanentEnforced());



    // Canary retains avatar-inventory creation when the direct capability is absent.
    getChildView("button new notecard")->setEnabled(new_button_enabled);

    // Publish button - enabled only when WS server is configured, and a single editable root object is selected.
    mPublishButton->setEnabled(LLScriptEditorWSServer::isEnabled() && new_button_enabled);

    // Sync toggle state to reflect whether the object is currently published.
    if (LLScriptEditorWSServer::isEnabled())
    {
        auto server = LLScriptEditorWSServer::getServer();
        mPublishButton->setToggleState(server && server->isObjectPublished(objectp->getID()));
    }
    else
    {
        mPublishButton->setToggleState(false);
    }
}

void LLPanelContents::onFilterEdit()
{
    if (!mAccountCurrent()) return;
    const std::string& filter_substring = mFilterEditor->getText();
    if (!mPanelInventoryObject->hasInventory())
    {
        mDirtyFilter = true;
    }
    else
    {
        LLFolderView* root_folder = mPanelInventoryObject->getRootFolder();
        if (filter_substring.empty())
        {
            if (mPanelInventoryObject->getFilter().getFilterSubString().empty())
            {
                // The current filter and the new filter are empty, nothing to do
                return;
            }

            if (mDirtyFilter && !mSavedFolderState.hasOpenFolders())
            {
                if (root_folder)
                {
                    root_folder->setOpenArrangeRecursively(true, LLFolderViewFolder::ERecurseType::RECURSE_DOWN);
                }
            }
            else
            {
                mSavedFolderState.setApply(true);
                if (root_folder)
                {
                    root_folder->applyFunctorRecursively(mSavedFolderState);
                }
            }
            mDirtyFilter = false;

            // Add a folder with the current item to the list of previously opened folders
            if (root_folder)
            {
                LLOpenFoldersWithSelection opener;
                root_folder->applyFunctorRecursively(opener);
                root_folder->scrollToShowSelection();
            }
        }
        else if (mPanelInventoryObject->getFilter().getFilterSubString().empty())
        {
            // The first letter in search term, save existing folder open state
            if (!mPanelInventoryObject->getFilter().isNotDefault())
            {
                mSavedFolderState.setApply(false);
                if (root_folder)
                {
                    root_folder->applyFunctorRecursively(mSavedFolderState);
                }
                mDirtyFilter = false;
            }
        }
    }
    mPanelInventoryObject->getFilter().setFilterSubString(filter_substring);
}

void LLPanelContents::refresh()
{
    if (!mAccountCurrent()) return;
    const auto object=VSContentsSelection().firstRoot();

    getState(object);
    if (mPanelInventoryObject)
    {
        mPanelInventoryObject->refresh();
    }
}

void LLPanelContents::clearContents()
{
    if (mPanelInventoryObject)
    {
        mPanelInventoryObject->clearInventoryTask();
    }
}

//
// Static functions
//

// static
void LLPanelContents::onNewScriptFlyoutCommit(LLUICtrl* ctrl)
{
    if (!mAccountCurrent()) return;
    const bool children_ok = true;
    LLViewerObject* object = LLSelectMgr::getInstance()->getSelection()->getFirstRootObject(children_ok);
    if (!object) return;

    const std::string value = ctrl->getValue().asString();
    if (value == "lsl")
    {
        mNewScriptIsLua = false;
    }
    else if (value == "lua")
    {
        mNewScriptIsLua = true;
    }
    ctrl->setValue(mNewScriptIsLua ? "lua" : "lsl");

    const U8 script_language = mNewScriptIsLua ? SST_LUA : SST_LSL;
    std::string vm = (script_language == SST_LUA) ? "luau" : "mono";

    LLSD params;
    params["enabled"] = true;
    params["vm"] = vm;

    createTaskInventoryItemHelper(object,
        LLAssetType::AT_LSL_TEXT,
        LLInventoryType::IT_LSL,
        script_language,
        "New Script",
        params);
}

// static
void LLPanelContents::onClickPermissions(void *userdata)
{
    LLPanelContents* self = (LLPanelContents*)userdata;
    if (!self || !self->mAccountCurrent()) return;
    const auto weak = self->getHandle();
    const auto current = self->mAccountCurrent;
    const auto parent = gFloaterView->getParentFloater(self);
    if (!parent) return;
    const auto parentHandle = parent->getHandle();
    auto* bulk = LLFloaterReg::showInstance("bulk_perms");
    if (!weak.isDead() && current() && !parentHandle.isDead() && bulk) parent->addDependentFloater(bulk);
}

// <FS> Script reset in edit floater
// static
void LLPanelContents::onClickResetScripts(void *userdata)
{
    auto* self = static_cast<LLPanelContents*>(userdata);
    if (!self || !self->mAccountCurrent()) return;
    handle_selected_script_action("reset");
}
// </FS>

// static
void LLPanelContents::onClickRefresh(void *userdata)
{
    LLPanelContents* self = (LLPanelContents*)userdata;
    self->refresh();
}


void LLPanelContents::onSimulatorFeaturesReceived(const LLUUID&)
{
    refresh();
}


void LLPanelContents::createTaskInventoryItemHelper(
    LLViewerObject* object,
    LLAssetType::EType asset_type,
    LLInventoryType::EType inventory_type,
    U8 sub_type,
    const std::string& name,
    const LLSD& params)
{
// [RLVa:KB] - Checked: 2010-03-31 (RLVa-1.2.0c) | Modified: RLVa-1.0.5a
        if (rlv_handler_t::isEnabled()) // Fallback code [see LLPanelContents::getState()]
        {
            if (gRlvAttachmentLocks.isLockedAttachment(object->getRootEdit()))
            {
                return;                 // Disallow creating new scripts in a locked attachment
            }
            else if ( (gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP)) )
            {
                if ( (isAgentAvatarValid()) && (gAgentAvatarp->isSitting()) && (gAgentAvatarp->getRoot() == object->getRootEdit()) )
                    return;             // .. or in a linkset the avie is sitting on under @unsit=n/@sittp=n
            }
        }
// [/RLVa:KB]

        // <FS:PP> FIRE-36059 Optional custom script template for New Script button
        if (asset_type == LLAssetType::AT_LSL_TEXT && gSavedPerAccountSettings.getBOOL("FSBuildPrefs_UseCustomScript"))
        {
            if (LLUUID custom_script_id(gSavedPerAccountSettings.getString("FSBuildPrefs_CustomScriptItem")); custom_script_id.notNull())
            {
                if (auto custom_script = gInventory.getItem(custom_script_id); custom_script && custom_script->getType() == LLAssetType::AT_LSL_TEXT && custom_script->getInventorySubType() == sub_type)
                {
                    LLHandle<LLPanel> handle = getHandle();
                    FSNewItemCtrl::getInstance()->startDNDInvToObject(LLUUID::null,
                        [handle]() { if (!handle.isDead()) static_cast<LLPanelContents*>(handle.get())->onFinishCreateItem(); });
                    LLToolDragAndDrop::dropScript(object, custom_script, true, LLToolDragAndDrop::SOURCE_AGENT, gAgentID);
                    return;
                }
            }
        }
        // </FS:PP>

    const char* perm_key = (asset_type == LLAssetType::AT_LSL_TEXT) ? "Scripts" : "Notecards";

    LLPermissions perm;
    perm.init(gAgent.getID(), gAgent.getID(), LLUUID::null, LLUUID::null);
    perm.initMasks(
        PERM_ALL,
        PERM_ALL,
        LLFloaterPerms::getEveryonePerms(perm_key),
        LLFloaterPerms::getGroupPerms(perm_key),
        PERM_MOVE | LLFloaterPerms::getNextOwnerPerms(perm_key));

    std::string desc;
    LLViewerAssetType::generateDescriptionFor(asset_type, desc);

    // Use cap if available, fall back to saveScript for scripts
    if (!object->getRegion()->getCapability("CreateTaskInventoryItem").empty())
    {
        object->createInventoryItem(asset_type, inventory_type, sub_type,
            name, desc, perm, params,
            [](bool success, const LLSD& response)
            {
                if (!success)
                {
                    LL_WARNS() << "CreateTaskInventoryItem failed: "
                               << response["message"].asString() << LL_ENDL;
                }
            });
    }
    else if (asset_type == LLAssetType::AT_LSL_TEXT)
    {
        // Fallback: use legacy RezScript UDP
                    LLHandle<LLPanel> handle = getHandle();
                    FSNewItemCtrl::getInstance()->startDNDInvToObject(LLUUID::null,
                        [handle]() { if (!handle.isDead()) static_cast<LLPanelContents*>(handle.get())->onFinishCreateItem(); });
        LLPointer<LLViewerInventoryItem> new_item =
            new LLViewerInventoryItem(
                LLUUID::null, LLUUID::null, perm,
                LLUUID::null, asset_type, inventory_type,
                name, desc, LLSaleInfo::DEFAULT,
                LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK & sub_type,
                time_corrected());
        object->saveScript(new_item, true, true, LLUUID::null);
    }
    else
    {
        LL_WARNS() << "Cannot create " << LLAssetType::lookup(asset_type)
                   << " — capability not available" << LL_ENDL;
    }
}


void LLPanelContents::onNewNotecardCommit()
{
    const bool children_ok = true;
    LLViewerObject* object = LLSelectMgr::getInstance()->getSelection()->getFirstRootObject(children_ok);
    if (!object) return;

    if (object->getRegion()->getCapability("CreateTaskInventoryItem").empty())
    {
        onClickNewNotecard(this);
        return;
    }

    createTaskInventoryItemHelper(object,
        LLAssetType::AT_NOTECARD,
        LLInventoryType::IT_NOTECARD,
        0,
        "New Notecard",
        LLSD());
}


void LLPanelContents::onClickPublish()
{
    const bool children_ok = true;
    LLViewerObject* object = LLSelectMgr::getInstance()->getSelection()->getFirstRootObject(children_ok);
    if (!object)
    {
        LL_WARNS() << "No root object selected for publish/unpublish" << LL_ENDL;
        return;
    }

    auto server = LLScriptEditorWSServer::ensureServerRunning();
    if (!server)
    {
        LL_WARNS() << "Cannot publish/unpublish: WebSocket server failed to start" << LL_ENDL;
        return;
    }

    const LLUUID object_id = object->getID();
    if (server->getConnectionCount())
    { // if we already have at least one connection, then we can toggle the publish state of the object
        if (server->isObjectPublished(object_id))
        {
            server->unpublishObject(object_id, "user");
        }
        else
        {
            server->publishObject(object_id);
        }
    }
    else
    {   // if we don't have any connections, we need to build the url and launch vscode
        // Launch VSCode
        LLScriptEditorWSServer::launchVSCode(object_id);

    }
}

void LLPanelContents::onClickNewNotecard(void* userdata)
{
    FSNewItemCtrl* new_item_ctrl = FSNewItemCtrl::getInstance();
    // Get the Panel Contents from the userdata void pointer.
    LLPanelContents* self = (LLPanelContents*)userdata;
    // Maintain RLV support for notecard object type being added.
    const bool children_ok = true;
    LLViewerObject* object = LLSelectMgr::getInstance()->getSelection()->getFirstRootObject(children_ok);

    if (!object) return;

    // [RLVa:KB] - Checked: 2010-03-31 (RLVa-1.2.0c) | Modified: RLVa-1.0.5a
    if (rlv_handler_t::isEnabled()) // Fallback code [see LLPanelContents::getState()]
    {
        if (gRlvAttachmentLocks.isLockedAttachment(object->getRootEdit()))
        {
            return; // Disallow creating new scripts in a locked attachment
        }
        else if ((gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP)))
        {
            if ((isAgentAvatarValid()) && (gAgentAvatarp->isSitting()) && (gAgentAvatarp->getRoot() == object->getRootEdit()))
                return; // .. or in a linkset the avie is sitting on under @unsit=n/@sittp=n
        }
    }
    // [/RLVa:KB]

    // Need to disable the new notecard button to prevent spaming the button and causing inventory issues
    self->getChildView("button new notecard")->setEnabled(false);
    // Need to save the filter first so it is not lost
    std::string save_filter = self->mFilterEditor->getText();
    // Clear the actual filter
    self->mFilterEditor->setText(LLStringExplicit(""));
    // Update the filter state
    self->onFilterEdit();
    // Create the LLSD paramater for the new notecard
    LLSD component("notecard");

    // Callback handle to allow for moving the new item to the object from the avatar inventory
    // Also moves the new item in the users avatar inventory to the trash.
    // Will also open up the dialog for Gesture and Notecards in the object's inventory.
    LLHandle<LLPanel> handle = self->getHandle();
    std::function<void(const LLUUID&)> callback_item_created = [handle](const LLUUID& new_id)
    {
        // Call the on Create Inventory Done of the new item controller which will kick off next steps
        if (!handle.isDead())
        {
            FSNewItemCtrl::getInstance()->onCreateInvDone(handle, new_id,
                [handle]() { if (!handle.isDead()) static_cast<LLPanelContents*>(handle.get())->onFinishCreateItem(); });
        }
    };

    // Save and clear the Show Inventroy flags used to prevent the floaters appearing
    // when items that have floaters like the Notecard, Gesture, Material and Script
    // from opening the temp item in the avatars inventory.
    new_item_ctrl->saveAndClearInventoryFlags();
    // Create the new item using the menu system in the user's avatar inventory.
    // The callback from above will move the item to the object and remove it from the user's inventory after.
    menu_create_inventory_item(NULL, LLUUID::null, component, LLUUID::null, callback_item_created);
    // Restore the filter
    self->mFilterEditor->setText(LLStringExplicit(save_filter));
}

// Callback method for when the item finishes
void LLPanelContents::onFinishCreateItem()
{
    getChildView("button new notecard")->setEnabled(true);
}
