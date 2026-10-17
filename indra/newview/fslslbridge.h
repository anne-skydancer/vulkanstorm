/**
 * @file fslslbridge.h
 * @brief FSLSLBridge header
 *
 * $LicenseInfo:firstyear=2011&license=fsviewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (C) 2011-2015, The Phoenix Firestorm Project, Inc.
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
 * The Phoenix Firestorm Project, Inc., 1831 Oakwood Drive, Fairmont, Minnesota 56031-3225 USA
 * http://www.firestormviewer.org
 * $/LicenseInfo$
 */

#ifndef FS_LSLBRIDGE_H
#define FS_LSLBRIDGE_H

#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llinventoryobserver.h"
#include "lleventtimer.h"
#include "vsnativeim.h"

class FSLSLBridgeRequestResponder;
class VSBridgeAdapter;
std::function<bool()> fs_bridge_account_guard();

const std::string LIB_ROCK_NAME = "Rock - medium, round";
const std::string FS_BRIDGE_NAME = "#Firestorm LSL Bridge v";
const U8 FS_BRIDGE_POINT = 31;
const std::string FS_BRIDGE_ATTACHMENT_POINT_NAME = "Center 2";

class FSLSLBridge : public LLSingleton<FSLSLBridge>, public LLVOInventoryListener
{
    friend class FSLSLBridgeScriptCallback;
    friend class FSLSLBridgeRezCallback;
    friend class FSLSLBridgeInventoryObserver;
    friend class FSLSLBridgeInventoryPreCreationCleanupObserver;
    friend class FSLSLBridgeCleanupTimer;
    friend class FSLSLBridgeReAttachTimer;
    friend class FSLSLBridgeStartCreationTimer;

    LLSINGLETON(FSLSLBridge);
    ~FSLSLBridge();

public:
    enum TimerResult
    {
        START_CREATION_FINISHED,
        CLEANUP_FINISHED,
        REATTACH_FINISHED,
        SCRIPT_UPLOAD_FINISHED,
        NO_TIMER
    };

    typedef std::function<void(const LLSD &)> Callback_t;

    bool lslToViewer(std::string_view message, const LLUUID& fromID, const LLUUID& ownerID);
    bool viewerToLSL(std::string_view message, Callback_t = nullptr);

    bool updateBoolSettingValue(const std::string& msgVal);
    bool updateBoolSettingValue(const std::string& msgVal, bool contentVal);
    void updateIntegrations();

    void initBridge();
    U64 lifecycle() const { return mLifecycle; }
    // Retire account/region-owned endpoint and inventory references without rezzing objects.
    void resetNativeAccount();
    void recreateBridge();
    void processAttach(LLViewerObject* object, const LLViewerJointAttachment* attachment);
    void processDetach(LLViewerObject* object, const LLViewerJointAttachment* attachment);

    bool getBridgeCreating() const { return mBridgeCreating; };
    void setBridgeCreating(bool status) { mBridgeCreating = status; };

    void setBridge(LLViewerInventoryItem* item) { mpBridge = item; };
    LLViewerInventoryItem* getBridge() const { return mpBridge; };
    bool canUseBridge();
    bool isBridgeValid() const;

    void checkBridgeScriptName();
    std::string currentFullName() const { return mCurrentFullName; }

    LLUUID getBridgeFolder() const { return mBridgeFolderID; }
    LLUUID getAttachedID() const { return mBridgeUUID; }

    bool canDetach(const LLUUID& item_id);

    static void onIdle(void* userdata);
    void setTimerResult(TimerResult result);

    // from LLVOInventoryListener
    void inventoryChanged(LLViewerObject* object,
                          LLInventoryObject::object_list_t* inventory,
                          S32 serial_num,
                          void* user_data) override;

private:
    U64 mLifecycle = 0;
    void processAttachItem(const LLUUID&, bool, const std::function<void()>&, const std::function<void()>&);
    void processDetachItem(const LLUUID&, bool);
    void inspectBridgeContents(const LLInventoryObject::object_list_t&, bool, const std::function<void()>&);
#if VS_NATIVE_VULKAN
    std::shared_ptr<VSBridgeAdapter> mNativeAdapter;
    LLUUID mNativeConfigureItem;
    F64 mNativeConfigureDeadline = 0;
    void tickNative();
    bool uploadNativeScript(const LLUUID&, const std::string&);
    void failNativeCreation(const LLUUID&);
#endif
    std::string             mCurrentURL;
    bool                    mBridgeCreating;
    bool                    mAllowDetach;
    bool                    mFinishCreation;

    LLPointer<LLViewerInventoryItem> mpBridge;
    std::string             mCurrentFullName;
    LLUUID                  mScriptItemID;  //internal script ID
    LLUUID                  mBridgeFolderID;
    LLUUID                  mBridgeContainerFolderID;
    LLUUID                  mBridgeUUID;
    LLUUID                  mReattachBridgeUUID; // used when re-attachming the bridge after creation

    bool                    mIsFirstCallDone; //initialization conversation

    uuid_vec_t              mAllowedDetachables;

    TimerResult             mTimerResult;

protected:
    LLViewerInventoryItem* findInvObject(const std::string& obj_name, const LLUUID& catID);
    void setupFSCategory(inventory_func_type callback);
    LLUUID findFSCategory();
    LLUUID findFSBridgeContainerCategory();

    bool isItemAttached(const LLUUID& iID);
    void createNewBridge();
    void create_script_inner();
    void cleanUpBridgeFolder();
    void cleanUpBridgeFolder(const std::string& nameToCleanUp);
    void setupBridgePrim(LLViewerObject* object);
    void cleanUpBridge();
    void startCreation();
    void finishBridge();
    void cleanUpOldVersions();
    void detachOtherBridges();
    void configureBridgePrim(LLViewerObject* object);
    void cleanUpPreCreation();
    void finishCleanUpPreCreation();
};


class FSLSLBridgeRezCallback : public LLInventoryCallback
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeRezCallback() {}
    void fire(const LLUUID& inv_item);

protected:
    ~FSLSLBridgeRezCallback() {}
};

class FSLSLBridgeScriptCallback : public LLInventoryCallback
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeScriptCallback() {}
    void fire(const LLUUID& inv_item);
    std::string prepUploadFile(std::string& aBuffer);

protected:
    ~FSLSLBridgeScriptCallback() {}
};

class FSLSLBridgeInventoryObserver : public LLInventoryFetchDescendentsObserver
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeInventoryObserver(const LLUUID& cat_id = LLUUID::null):LLInventoryFetchDescendentsObserver(cat_id) {}
    FSLSLBridgeInventoryObserver(const uuid_vec_t& cat_ids):LLInventoryFetchDescendentsObserver(cat_ids) {}
    /*virtual*/ void done() { gInventory.removeObserver(this); if (mAccountCurrent()) FSLSLBridge::instance().startCreation(); delete this; }

protected:
    ~FSLSLBridgeInventoryObserver() {}
};

class FSLSLBridgeInventoryPreCreationCleanupObserver : public LLInventoryFetchDescendentsObserver
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeInventoryPreCreationCleanupObserver(const LLUUID& cat_id = LLUUID::null):LLInventoryFetchDescendentsObserver(cat_id) {}
    /*virtual*/ void done() { gInventory.removeObserver(this); if (mAccountCurrent()) FSLSLBridge::instance().cleanUpPreCreation(); delete this; }

protected:
    ~FSLSLBridgeInventoryPreCreationCleanupObserver() {}
};

class FSLSLBridgeCleanupTimer : public LLEventTimer
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeCleanupTimer() : LLEventTimer(12.f) {}
    bool tick();
};

class FSLSLBridgeReAttachTimer : public LLEventTimer
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeReAttachTimer() : LLEventTimer(5.f) {}
    bool tick();
};

class FSLSLBridgeStartCreationTimer : public LLEventTimer
{
    std::function<bool()> mAccountCurrent = fs_bridge_account_guard();
public:
    FSLSLBridgeStartCreationTimer() : LLEventTimer(5.f) {}
    bool tick();
};
#endif // FS_LSLBRIDGE_H
