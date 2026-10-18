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

    childSetAction("button new script",&LLPanelContents::onClickNewScript, this);
    childSetAction("button permissions",&LLPanelContents::onClickPermissions, this);
    childSetAction("btn_reset_scripts", &LLPanelContents::onClickResetScripts, this); // <FS> Script reset in edit floater
    childSetAction("button refresh",&LLPanelContents::onClickRefresh, this);

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
    // Children all cleaned up by default view destructor.
}


void LLPanelContents::getState(const VSTaskInventoryObject& objectp)
{
    const VSContentsSelection selection;
    if( !objectp )
    {
        getChildView("button new script")->setEnabled(false);
        getChildView("btn_reset_scripts")->setEnabled(false); // <FS> Script reset in edit floater
        return;
    }

    LLUUID group_id;            // used for SL-23488
    selection.group(group_id);  // sets group_id as a side effect SL-23488

    // BUG? Check for all objects being editable?
    bool editable = gAgent.isGodlike()
                    || (objectp->permModify() && !objectp->isPermanentEnforced()
                           && ( objectp->permYouOwner() || ( !group_id.isNull() && gAgent.isInGroup(group_id) )));  // solves SL-23488
    bool all_volume = selection.allVolumes();

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

    // Edit script buttons - ok if object is editable and there's an unambiguous destination for the object.
    // <FS:PP> FIRE-3219: Reset Scripts button in Build floater
    //  getChildView("button new script")->setEnabled(
    //      editable &&
    //      all_volume &&
    //      ((selection.roots() == 1)
    //          || (selection.objects() == 1)));

    bool objectIsOK = false;
    if( editable && all_volume && ( (selection.roots() == 1) || (selection.objects() == 1) ) )
    {
        objectIsOK = true;
    }

    getChildView("button new script")->setEnabled(objectIsOK);
    getChildView("btn_reset_scripts")->setEnabled(objectIsOK);
    // </FS:PP>

    getChildView("button permissions")->setEnabled(!objectp->isPermanentEnforced());
    mPanelInventoryObject->setEnabled(!objectp->isPermanentEnforced());
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
void LLPanelContents::onClickNewScript(void *userdata)
{
    auto* self=static_cast<LLPanelContents*>(userdata);
    if (!self || !self->mAccountCurrent()) return;
    const auto object=VSContentsSelection().firstRoot();
    if(object)
    {
// [RLVa:KB] - Checked: 2010-03-31 (RLVa-1.2.0c) | Modified: RLVa-1.0.5a
        if (rlv_handler_t::isEnabled()) // Fallback code [see LLPanelContents::getState()]
        {
            if (object->attachmentLocked())
            {
                return;                 // Disallow creating new scripts in a locked attachment
            }
            else if ( (gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP)) )
            {
                if ( object->agentSittingOnRoot() )
                    return;             // .. or in a linkset the avie is sitting on under @unsit=n/@sittp=n
            }
        }
// [/RLVa:KB]

        // <FS:PP> FIRE-36059 Optional custom script template for New Script button
        if (gSavedPerAccountSettings.getBOOL("FSBuildPrefs_UseCustomScript"))
        {
            if (LLUUID custom_script_id(gSavedPerAccountSettings.getString("FSBuildPrefs_CustomScriptItem")); custom_script_id.notNull())
            {
                if (auto custom_script = gInventory.getItem(custom_script_id); custom_script && custom_script->getType() == LLAssetType::AT_LSL_TEXT)
                {
                    LLToolDragAndDrop::dropScript(object, custom_script, true, LLToolDragAndDrop::SOURCE_AGENT, gAgentID);
                    return;
                }
            }
        }
        // </FS:PP>

        LLPermissions perm;
        perm.init(gAgent.getID(), gAgent.getID(), LLUUID::null, LLUUID::null);

        // Parameters are base, owner, everyone, group, next
        perm.initMasks(
            PERM_ALL,
            PERM_ALL,
            LLFloaterPerms::getEveryonePerms("Scripts"),
            LLFloaterPerms::getGroupPerms("Scripts"),
            PERM_MOVE | LLFloaterPerms::getNextOwnerPerms("Scripts"));
        std::string desc;
        LLViewerAssetType::generateDescriptionFor(LLAssetType::AT_LSL_TEXT, desc);
        LLPointer<LLViewerInventoryItem> new_item =
            new LLViewerInventoryItem(
                LLUUID::null,
                LLUUID::null,
                perm,
                LLUUID::null,
                LLAssetType::AT_LSL_TEXT,
                LLInventoryType::IT_LSL,
                "New Script",
                desc,
                LLSaleInfo::DEFAULT,
                LLInventoryItemFlags::II_FLAGS_NONE,
                time_corrected());
        object->saveScript(new_item, true, true);

        std::string name = new_item->getName();

        // *NOTE: In order to resolve SL-22177, we needed to create
        // the script first, and then you have to click it in
        // inventory to edit it.
        // *TODO: The script creation should round-trip back to the
        // viewer so the viewer can auto-open the script and start
        // editing ASAP.
    }
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
