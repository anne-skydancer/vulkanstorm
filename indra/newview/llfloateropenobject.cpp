/**
 * @file llfloateropenobject.cpp
 * @brief LLFloaterOpenObject class implementation
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

/*
 * Shows the contents of an object.
 * A floater wrapper for LLPanelObjectInventory
 */

#include "llviewerprecompiledheaders.h"

#include "llfloateropenobject.h"
#include "vsnativeim.h"
#if VS_NATIVE_VULKAN
#include "vsobjectpurchase.h"
#endif

#include "llcachename.h"
#include "llbutton.h"
#include "llnotificationsutil.h"
#include "lltextbox.h"

#include "llinventorybridge.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llpanelobjectinventory.h"
#include "llfloaterreg.h"
#include "llselectmgr.h"
#include "lluiconstants.h"
#include "llviewerobject.h"
#include "lluictrlfactory.h"
#include "llviewerwindow.h"
// [RLVa:KB] - @edit
#include "rlvactions.h"
// [/RLVa:KB]

LLFloaterOpenObject::LLFloaterOpenObject(const LLSD& key)
:   LLFloater(key),
    mPanelInventoryObject(NULL),
    mDirty(true)
{
    // <FS:Ansariel> Cinder's fly-out button
    //mCommitCallbackRegistrar.add("OpenObject.MoveToInventory",    boost::bind(&LLFloaterOpenObject::onClickMoveToInventory, this));
    mCommitCallbackRegistrar.add("OpenObject.CopyAction",       boost::bind(&LLFloaterOpenObject::onClickCopy, this, _2));
    // </FS:Ansariel>
    mCommitCallbackRegistrar.add("OpenObject.Cancel",           boost::bind(&LLFloaterOpenObject::onClickCancel, this));
}

LLFloaterOpenObject::~LLFloaterOpenObject()
{
//  sInstance = NULL;
}

// virtual
bool LLFloaterOpenObject::postBuild()
{
    getChild<LLUICtrl>("object_name")->setTextArg("[DESC]", std::string("Object") ); // *Note: probably do not want to translate this
    mPanelInventoryObject = getChild<LLPanelObjectInventory>("object_contents");

    refresh();
    return true;
}

void LLFloaterOpenObject::onOpen(const LLSD& key)
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        mNativePurchase=VSObjectPurchase::capture();
        if(!mNativePurchase)
        {
            LLNotificationsUtil::add("UnableToViewContentsMoreThanOne");
            closeFloater();return;
        }
        const auto weak=getHandle();const auto state=std::weak_ptr<VSObjectPurchase>(mNativePurchase);
        mNativePurchase->selectionChanged=mNativePurchase->selection->changed([weak,state]
        {
            const auto owner=state.lock();auto* view=dynamic_cast<LLFloaterOpenObject*>(weak.get());
            if(owner && view && view->mNativePurchase==owner)
            {
                if(!owner->current())view->closeFloater();else view->dirty();
            }
        });
        refresh();return;
    }
#endif
    LLObjectSelectionHandle object_selection = LLSelectMgr::getInstance()->getSelection();
    if (object_selection->getRootObjectCount() != 1)
    {
        LLNotificationsUtil::add("UnableToViewContentsMoreThanOne");
        closeFloater();
        return;
    }
    if(!(object_selection->getPrimaryObject()))
    {
        closeFloater();
        return;
    }
    mObjectSelection = LLSelectMgr::getInstance()->getEditSelection();
    refresh();
}

void LLFloaterOpenObject::refresh()
{
    mPanelInventoryObject->refresh();

    std::string name = "";
    bool enabled = false;
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto owner=mNativePurchase;
        if(owner && owner->current())
        {
            const auto object=VSObjectContext::resolve(owner->object.getID());
            if(RlvActions::isRlvEnabled() && object && !RlvActions::canEdit(*object))
            {closeFloater();return;}
            const auto property=owner->selection->properties(owner->publication.object);
            if(property){name=property->properties->name;enabled=true;}
        }
        getChild<LLUICtrl>("object_name")->setTextArg("[DESC]",name);
        getChildView("copy_flyout")->setEnabled(enabled);
        return;
    }
#endif
    LLSelectNode* node = mObjectSelection->getFirstRootNode();
// [RLVa:KB] - @edit and @editobj
    if ( (RlvActions::isRlvEnabled()) && (node) && (!RlvActions::canEdit(node->getObject())) )
    {
        // If the floater was already open before getting edit restricted then a transient selection will allow manipulation
        // of the object's inventory without it getting selected by LLSelectMgr::deselectIfTooFar().
        // Killing the selection would result in the user not realizing why their right-click breaks so close the floater instead.
        closeFloater();
        return;
    }
// [/RLVa:KB]
    if (node)
    {
        name = node->mName;
        enabled = true;
    }
    else
    {
        name = "";
        enabled = false;
    }

    getChild<LLUICtrl>("object_name")->setTextArg("[DESC]", name);
    // <FS:Ansariel> Cinder's fly-out button
    //getChildView("copy_to_inventory_button")->setEnabled(enabled);
    //getChildView("copy_and_wear_button")->setEnabled(enabled);
    //getChildView("copy_and_replace_button")->setEnabled(enabled);
    getChildView("copy_flyout")->setEnabled(enabled);
    // </FS:Ansariel>
}

void LLFloaterOpenObject::draw()
{
    if (mDirty)
    {
        refresh();
        mDirty = false;
    }
    LLFloater::draw();
}

void LLFloaterOpenObject::dirty()
{
    mDirty = true;
}



void LLFloaterOpenObject::moveToInventory(bool wear, bool replace)
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto owner=mNativePurchase;
        if(!owner || !owner->current())return;
        const auto object=VSObjectContext::resolve(owner->object.getID());
        if(!object || (RlvActions::isRlvEnabled() && !RlvActions::canEdit(*object)))return;
        const auto property=owner->selection->properties(owner->publication.object);
        if(!property)return;
        const auto parent=wear?gInventory.findCategoryUUIDForType(LLFolderType::FT_CLOTHING):gInventory.getRootFolderID();
        const auto current=vs_native_im_guard();
        // Closing the dialog after accepting Copy must not cancel the original
        // category transaction. Retain its exact source/account, not a raw view.
        gInventory.createNewCategory(parent,LLFolderType::FT_NONE,property->properties->name,
            [owner,current,wear,replace](const LLUUID& category)
            {
                if(current() && owner->current() && category.notNull())
                    callbackCreateInventoryCategory(category,owner->object.getID(),wear,replace,current);
            });
        return;
    }
#endif
    if (mObjectSelection->getRootObjectCount() != 1)
    {
        LLNotificationsUtil::add("OnlyCopyContentsOfSingleItem");
        return;
    }

    LLSelectNode* node = mObjectSelection->getFirstRootNode();
    if (!node) return;
    LLViewerObject* object = node->getObject();
    if (!object) return;

    LLUUID object_id = object->getID();
    std::string name = node->mName;

    // Either create a sub-folder of clothing, or of the root folder.
    LLUUID parent_category_id;
    if (wear)
    {
        parent_category_id = gInventory.findCategoryUUIDForType(
            LLFolderType::FT_CLOTHING);
    }
    else
    {
        parent_category_id = gInventory.getRootFolderID();
    }

    inventory_func_type func = boost::bind(LLFloaterOpenObject::callbackCreateInventoryCategory,_1,object_id,wear,replace,std::function<bool()>{});
    // D567 copy thumbnail info
    gInventory.createNewCategory(
        parent_category_id,
        LLFolderType::FT_NONE,
        name,
        func);
}

// static
void LLFloaterOpenObject::callbackCreateInventoryCategory(const LLUUID& category_id, LLUUID object_id, bool wear, bool replace, std::function<bool()> current)
{
    LLCatAndWear* wear_data = new LLCatAndWear;

    wear_data->mCurrent=std::move(current);
    wear_data->mCatID = category_id;
    wear_data->mWear = wear;
    wear_data->mFolderResponded = true;
    wear_data->mReplace = replace;

    // Copy and/or move the items into the newly created folder.
    // Ignore any "you're going to break this item" messages.
    bool success = move_inv_category_world_to_agent(object_id,
                                                    category_id,
                                                    true,
                                                    [](S32 result, void* data, const LLMoveInv*)
                                                    {
                                                        callbackMoveInventory(result, data);
                                                    },
                                                    (void*)wear_data);
    if (!success)
    {
        delete wear_data;
        wear_data = NULL;

        LLNotificationsUtil::add("OpenObjectCannotCopy");
    }
}

// static
void LLFloaterOpenObject::callbackMoveInventory(S32 result, void* data)
{
    LLCatAndWear* cat = (LLCatAndWear*)data;

    if (result == 0 && (!cat->mCurrent || cat->mCurrent()))
    {
        LLInventoryPanel *active_panel = LLInventoryPanel::getActiveInventoryPanel();
        if (active_panel)
        {
            active_panel->setSelection(cat->mCatID, TAKE_FOCUS_NO);
        }
    }

    delete cat;
}

// <FS:Ansariel> Cinder's fly-out button
//void LLFloaterOpenObject::onClickMoveToInventory()
//{
//  moveToInventory(false);
//  closeFloater();
//}
// </FS:Ansariel>

void LLFloaterOpenObject::onClickCancel()
{
    closeFloater();
}

// <FS:Ansariel> Cinder's fly-out button
void LLFloaterOpenObject::onClickCopy(const LLSD& value)
{
    const std::string& action = value.asString();
    if (action == "replace")
    {
        moveToInventory(true, true);
    }
    else if (action == "add")
    {
        moveToInventory(true, false);
    }
    else
    {
        moveToInventory(false);
    }
    closeFloater();
}
// </FS:Ansariel>

void LLFloaterOpenObject::onClose(bool)
{
    mObjectSelection.clear();
#if VS_NATIVE_VULKAN
    mNativePurchase.reset();
#endif
}
