/**
 * @file llinventory_task_oracle.h
 * @brief The task-inventory ("object contents") decision code of the viewer
 * as it was before vkstorm-rust rung seven, frozen in the test namespace
 * vks_task_oracle, and the fakes that stand in for the viewer singletons it
 * uses. Test-only; not used by the viewer.
 *
 * The copied function bodies (llinventory_task_oracle.cpp) are verbatim: every
 * name they use that is a viewer singleton or a viewer class resolves, by
 * ordinary name lookup inside this namespace, to a declaration here. Nothing
 * in the copied text is edited; tests/check_r4a_copies.py confirms it against
 * the live source. Real classes are used wherever they link standalone:
 * llinventory (LLInventoryObject, LLInventoryItem, LLPermissions, LLSaleInfo
 * and their legacy readers and LLSD forms), the message system with the
 * shipped template (LLMessageSystem, its builder and reader), the xfer
 * manager, LLDir and LLFile, LLSD.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORY_TASK_ORACLE_H
#define LL_LLINVENTORY_TASK_ORACLE_H

// LLViewerInventoryItem's packMessage/unpackMessage take this namespace's
// LLMessageSystem (a subclass of the real one), as the frozen copies are
// written against that name; they hide the base overloads on purpose. GCC
// reports the warning at the base declaration, so this precedes the
// llinventory.h include.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

#include "linden_common.h"

#include "llassettype.h"
#include "llextendedstatus.h"
#include "llhost.h"
#include "llinventory.h"
#include "llinventorydefines.h"
#include "llinventorytype.h"
#include "llpermissions.h"
#include "llpointer.h"
#include "llsaleinfo.h"
#include "llsd.h"
#include "llsingleton.h"
#include "lluuid.h"
#include "lldir.h"
#include "message.h"
#include "httpcommon.h"

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

class LLXferManager;
class LLTemplateMessageReader;

namespace vks_task_oracle
{
    class LLViewerObject;

    //------------------------------------------------------------------
    // Recorder of side effects. Every fake appends one line per effect, in
    // the order the copied code produced them.
    //------------------------------------------------------------------
    struct Recorder
    {
        std::vector<std::string> actions;
        void add(const std::string& line) { actions.push_back(line); }
    };
    Recorder& recorder();

    // Short stable name for a UUID in recorded text: the registered alias
    // ("obj", "itemA", ...) or the UUID string.
    std::string uuid_name(const LLUUID& id);
    void set_uuid_alias(const LLUUID& id, const std::string& alias);
    void clear_uuid_aliases();

    //------------------------------------------------------------------
    // The message system: the real one, built from the shipped template.
    // sendReliable is hidden so that the copied code's sends are recorded;
    // in capture mode they go on to the real UDP path (to a loopback
    // receiver), otherwise the built message is discarded. newMessage is
    // hidden only to note the name of the message being built. The getters
    // processTaskInv uses read, while mInjected is set, from a message the
    // test built and read with its own template reader (the message
    // system's reader and builder are private).
    //------------------------------------------------------------------
    class LLMessageSystem : public ::LLMessageSystem
    {
    public:
        LLMessageSystem(const std::string& template_file);
        void newMessageFast(const char* name);
        void newMessage(const char* name);
        S32 sendReliable(const LLHost& host);
        void getUUIDFast(const char* block, const char* var, LLUUID& uuid, S32 blocknum = 0);
        void getS16Fast(const char* block, const char* var, S16& data, S32 blocknum = 0);
        void getStringFast(const char* block, const char* var, std::string& outstr, S32 blocknum = 0);
        bool mCapture = false;
        std::string mBuildingName;
        ::LLTemplateMessageReader* mInjected = NULL;
    };
    extern LLMessageSystem* gMessageSystem;

    //------------------------------------------------------------------
    // Viewer singletons.
    //------------------------------------------------------------------
    struct FakeAgent
    {
        LLUUID mID;
        LLUUID mSessionID;
        LLUUID mGroupID;
        const LLUUID& getID() const { return mID; }
        const LLUUID& getSessionID() const { return mSessionID; }
        const LLUUID& getGroupID() const { return mGroupID; }
    };
    extern FakeAgent gAgent;

    struct FakeObjectList
    {
        std::map<LLUUID, LLViewerObject*> mObjects;
        LLViewerObject* findObject(const LLUUID& id);
    };
    extern FakeObjectList gObjectList;

    class LLViewerRegion
    {
    public:
        std::map<std::string, std::string> mCaps;
        LLHost mHost;
        std::string getCapability(const std::string& name) const;
        const LLHost& getHost() const { return mHost; }
    };

    class LLSelectMgr
    {
    public:
        static LLSelectMgr* getInstance();
        // The permissions the selection manager has for the object when it
        // is selected (ObjectProperties received), NULL otherwise.
        LLPermissions* findObjectPermissions(const LLViewerObject* object);
        const LLViewerObject* mSelectedObject = NULL;
        LLPermissions mSelectedPermissions;
    };

    class LLFloaterReg
    {
    public:
        static bool hideInstance(const std::string& name, const LLSD& key = LLSD());
    };

    class LLTrans
    {
    public:
        // The fake translation marks the value, so that the recorded names
        // show where localisation was applied and with which key.
        static std::string getString(const std::string& xml_desc);
    };

    //------------------------------------------------------------------
    // Coroutines: LLCoros launches with boost::fibers::launch::dispatch
    // (llcommon/llcoros.cpp), so a launched coroutine runs at once until its
    // first suspension. The fakes do the same on real fibers; the
    // suspensions wait for the test to deliver a result.
    //------------------------------------------------------------------
    class LLCoros
    {
    public:
        static LLCoros& instance();
        std::string launch(const std::string& prefix, const std::function<void()>& callable);
    };

    namespace llcoro
    {
        void suspendUntilTimeout(float seconds);
    }

    namespace LLCore
    {
        using HttpStatus = ::LLCore::HttpStatus;
        class HttpRequest
        {
        public:
            typedef int policy_t;
            static const policy_t DEFAULT_POLICY_ID = 0;
            typedef std::shared_ptr<HttpRequest> ptr_t;
        };
    }

    namespace LLCoreHttpUtil
    {
        class HttpCoroutineAdapter
        {
        public:
            typedef std::shared_ptr<HttpCoroutineAdapter> ptr_t;
            HttpCoroutineAdapter(const std::string& name, LLCore::HttpRequest::policy_t policy_id);
            LLSD getAndSuspend(LLCore::HttpRequest::ptr_t request, const std::string& url);
            LLSD postAndSuspend(LLCore::HttpRequest::ptr_t request, const std::string& url, const LLSD& body);
            static const std::string& HTTP_RESULTS;
            static ::LLCore::HttpStatus getStatusFromLLSD(const LLSD& httpResults);
            std::string mName;
        };
    }

    // Raised inside a suspended coroutine when the test abandons it.
    struct Abandoned {};

    // Pending suspensions, oldest first.
    struct PendingHttp;
    struct PendingTimer;
    size_t pending_http_count();
    size_t pending_timer_count();
    std::string pending_http_url(size_t index);
    std::string pending_http_method(size_t index);
    LLSD pending_http_body(size_t index);
    // Deliver the HTTP response (body as the server sent it, the status) to
    // the oldest pending request (of the given method, when one is named)
    // and let the coroutines run.
    void complete_http(const LLSD& body, const ::LLCore::HttpStatus& status, const char* method = NULL);
    // Fire the oldest pending timer.
    void fire_timer();
    // Unwind every suspended coroutine (between cases).
    void abandon_all();

    //------------------------------------------------------------------
    // The xfer manager. In trace mode a fake that hands out the ids 1001,
    // 1002, ... and records requests and aborts; with set_real_xfer() the
    // requests and aborts go to a real LLXferManager (whose ids are random).
    //------------------------------------------------------------------
    class LLXferManager
    {
    public:
        enum
        {
            LOW_PRIORITY = false,
            HIGH_PRIORITY = true,
        };
        U64 requestFile(const std::string& local_filename,
                        const std::string& remote_filename,
                        ELLPath remote_path,
                        const LLHost& remote_host,
                        bool delete_remote_on_completion,
                        void (*callback)(void**, S32, LLExtStat),
                        void** user_data,
                        bool is_priority = false,
                        bool use_big_packets = false);
        void abortRequestById(U64 xfer_id, S32 result_code);
    };
    extern LLXferManager* gXferManager;

    struct FakeXfer
    {
        U64 id;
        std::string local_filename;
        std::string remote_filename;
        void (*callback)(void**, S32, LLExtStat);
        void** user_data;
        bool aborted;
    };
    std::deque<FakeXfer>& fake_xfers();
    void reset_fake_xfers();
    void set_real_xfer(::LLXferManager* real); // NULL: back to the fake
    // An xfer id as recorded: the fake's number, or real#<k> for the k-th id
    // the real manager returned in this case (its ids are random).
    std::string xfer_id_text(U64 id);

    //------------------------------------------------------------------
    // The viewer's LLVOInventoryListener (llvoinventorylistener.h), reduced
    // to what LLViewerObject calls.
    //------------------------------------------------------------------
    class LLVOInventoryListener
    {
    public:
        virtual ~LLVOInventoryListener() {}
        virtual void inventoryChanged(LLViewerObject* object,
                                      LLInventoryObject::object_list_t* inventory,
                                      S32 serial_num,
                                      void* user_data) = 0;
        void clearVOInventoryListener() { mListenerVObject = NULL; }
        LLViewerObject* mListenerVObject = NULL;
    };

    //------------------------------------------------------------------
    // LLViewerInventoryItem (llviewerinventory.h), reduced to the members the
    // task-inventory code uses. Its methods are frozen copies. The overrides
    // that follow links into the agent's inventory model (getType, getName,
    // ...) are not reproduced: they return the item's own values unless the
    // item is a link whose target is in gInventory, and the oracle runs with
    // an empty agent model.
    //------------------------------------------------------------------
    class LLViewerInventoryItem : public LLInventoryItem
    {
    public:
        LLViewerInventoryItem(const LLUUID& uuid,
                              const LLUUID& parent_uuid,
                              const LLPermissions& permissions,
                              const LLUUID& asset_uuid,
                              LLAssetType::EType type,
                              LLInventoryType::EType inv_type,
                              const std::string& name,
                              const std::string& desc,
                              const LLSaleInfo& sale_info,
                              U32 flags,
                              time_t creation_date_utc);
        LLViewerInventoryItem(const LLUUID& item_id,
                              const LLUUID& parent_id,
                              const std::string& name,
                              LLInventoryType::EType inv_type);
        LLViewerInventoryItem();
        LLViewerInventoryItem(const LLViewerInventoryItem* other);
        LLViewerInventoryItem(const LLInventoryItem* other);
        void copyViewerItem(const LLViewerInventoryItem* other);
        /*virtual*/ void copyItem(const LLInventoryItem* other);

        virtual void packMessage(LLMessageSystem* msg) const;
        virtual bool unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num = 0);
        virtual bool unpackMessage(const LLSD& item);
        virtual bool importLegacyStream(std::istream& input_stream);
        virtual void setTransactionID(const LLTransactionID& transaction_id);
        LLTransactionID getTransactionID() const { return mTransactionID; }
        bool isFinished() const { return mIsComplete; }
        bool getIsRunning() const { return mIsRunning; }
        bool getIsFaulted() const { return mIsFaulted; }

    protected:
        ~LLViewerInventoryItem();

    public:
        bool mIsComplete;
        LLTransactionID mTransactionID;
        bool mIsRunning = false;
        bool mIsFaulted = false;
    };

    //------------------------------------------------------------------
    // LLViewerObject (llviewerobject.h), reduced to the task-inventory state
    // (members and their initial values as in the real class) and the
    // task-inventory methods, which are frozen copies.
    //------------------------------------------------------------------
    class LLViewerObject
    {
    public:
        LLViewerObject(const LLUUID& id, U32 local_id, LLViewerRegion* regionp);
        ~LLViewerObject();

        void registerInventoryListener(LLVOInventoryListener* listener, void* user_data);
        void removeInventoryListener(LLVOInventoryListener* listener);
        bool isInventoryPending();
        void clearInventoryListeners();
        bool hasInventoryListeners();
        void requestInventory();
        static void processTaskInv(LLMessageSystem* msg, void** user_data);
        void removeInventory(const LLUUID& item_id);
        void updateInventory(LLViewerInventoryItem* item, U8 key, bool is_new);
        void updateInventoryLocal(LLInventoryItem* item, U8 key);
        void updateMaterialInventory(LLViewerInventoryItem* item, U8 key, bool is_new);
        LLInventoryObject* getInventoryObject(const LLUUID& item_id);
        LLInventoryItem* getInventoryItem(const LLUUID& item_id);
        void getInventoryContents(LLInventoryObject::object_list_t& objects);
        LLInventoryObject* getInventoryRoot();
        LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID& asset_id);
        LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID& asset_id, LLAssetType::EType type);
        S16 getInventorySerial() const { return mInventorySerialNum; }
        void updateViewerInventoryAsset(const LLViewerInventoryItem* item, const LLUUID& new_asset);
        void dirtyInventory();
        bool isInventoryDirty() { return mInventoryDirty; }
        void saveScript(const LLViewerInventoryItem* item, bool active, bool is_new, const LLUUID& template_id = LLUUID::null);
        void createInventoryItem(LLAssetType::EType asset_type,
                                 LLInventoryType::EType inventory_type,
                                 U8 sub_type,
                                 const std::string& name,
                                 const std::string& description,
                                 const LLPermissions& permissions,
                                 const LLSD& params,
                                 std::function<void(bool, const LLSD&)> callback);
        void moveInventory(const LLUUID& agent_folder, const LLUUID& item_id);
        S32 countInventoryContents(LLAssetType::EType type);
        bool isAssetInInventory(LLViewerInventoryItem* item, LLAssetType::EType type);
        void fetchInventoryFromServer();
        void fetchInventoryDelayed(const F64& time_seconds);
        static void fetchInventoryDelayedCoro(const LLUUID task_inv, const F64 time_seconds);
        static void fetchInventoryFromCapCoro(const LLUUID task_inv);
        static void createInventoryItemCoro(const std::string cap_url, const LLSD body, std::function<void(bool, const LLSD&)> callback);
        void deleteInventoryItem(const LLUUID& item_id);
        void doUpdateInventory(LLPointer<LLViewerInventoryItem>& item, U8 key, bool is_new);
        static void processTaskInvFile(void** user_data, S32 error_code, LLExtStat ext_status);
        bool loadTaskInvFile(const std::string& filename);
        void loadTaskInvLLSD(const LLSD& inv_result);
        void doInventoryCallback();

        // LLViewerObject::permYouOwner() reads the object's flags; the fake
        // answers what the case sets.
        bool permYouOwner() const { return mFakeYouOwner; }
        bool mFakeYouOwner = true;

        LLUUID mID;
        U32 mLocalID;

        std::list<LLUUID> mPendingInventoryItemsIDs;
        LLInventoryObject::object_list_t* mInventory;
        class LLInventoryCallbackInfo
        {
        public:
            ~LLInventoryCallbackInfo();
            LLVOInventoryListener* mListener;
            void* mInventoryData;
        };
        typedef std::list<LLInventoryCallbackInfo*> callback_list_t;
        callback_list_t mInventoryCallbacks;
        S16 mInventorySerialNum;
        S16 mExpectedInventorySerialNum;
        enum EInventoryRequestState
        {
            INVENTORY_REQUEST_STOPPED,
            INVENTORY_REQUEST_WAIT,    // delay before requesting
            INVENTORY_REQUEST_PENDING, // just did fetchInventoryFromServer()
            INVENTORY_XFER             // processed response from 'fetch', now doing an xfer
        };
        EInventoryRequestState mInvRequestState;
        U64 mInvRequestXFerId;
        bool mInventoryDirty;
        LLViewerRegion* mRegionp;
    };
}

#endif // LL_LLINVENTORY_TASK_ORACLE_H
