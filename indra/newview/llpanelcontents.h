/**
 * @file llpanelcontents.h
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

#ifndef LL_LLPANELCONTENTS_H
#define LL_LLPANELCONTENTS_H

#include "llfolderview.h"
#include "llinventory.h"
#include "llpanel.h"
#include "lluuid.h"
#include <functional>
#include "llviewerobject.h"
#include "llvoinventorylistener.h"
#include "v3math.h"
#include <boost/signals2/connection.hpp>

class LLButton;
class LLPanelObjectInventory;
class LLViewerObject;
class VSTaskInventoryObject;
class LLCheckBoxCtrl;
class LLSpinCtrl;

class LLPanelContents : public LLPanel
{
public:
    virtual bool postBuild();
    LLPanelContents();
    virtual ~LLPanelContents();

    void            refresh();
    void            clearContents();


    void            onNewScriptFlyoutCommit(LLUICtrl* ctrl);
    void            onNewNotecardCommit();
    static void     onClickNewNotecard(void* userdata);
    void            onFinishCreateItem();

    void            onClickPublish();

    void            createTaskInventoryItemHelper(LLViewerObject* object,
                        LLAssetType::EType asset_type,
                        LLInventoryType::EType inventory_type,
                        U8 sub_type,
                        const std::string& name,
                        const LLSD& params);


    static void     onClickPermissions(void*);
    static void     onClickResetScripts(void*); // <FS> Script reset in edit floater
    static void     onClickRefresh(void*);

    // Key suffix for "tentative" fields
    static const char* TENTATIVE_SUFFIX;

    // These aren't fields in LLMediaEntry, so we have to define them ourselves for checkbox control
    static const char* PERMS_OWNER_INTERACT_KEY;
    static const char* PERMS_OWNER_CONTROL_KEY;
    static const char* PERMS_GROUP_INTERACT_KEY;
    static const char* PERMS_GROUP_CONTROL_KEY;
    static const char* PERMS_ANYONE_INTERACT_KEY;
    static const char* PERMS_ANYONE_CONTROL_KEY;

protected:
    void getState(const VSTaskInventoryObject& object);
    void onFilterEdit();
    void onSimulatorFeaturesReceived(const LLUUID&);
    LLUUID mLastScriptObjectID;
    bool mLastLuaRegion{false};
    bool mNewScriptIsLua{false};
    boost::signals2::connection mSimulatorFeaturesConnection;
    LLButton* mPublishButton{nullptr};

    std::function<bool()> mAccountCurrent;
    bool mDirtyFilter { false };

public:
    class LLFilterEditor* mFilterEditor;
    LLSaveFolderState mSavedFolderState;
    LLPanelObjectInventory* mPanelInventoryObject;
};

#endif // LL_LLPANELCONTENTS_H
