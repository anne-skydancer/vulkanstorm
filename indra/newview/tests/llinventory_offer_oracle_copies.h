/**
 * @file llinventory_offer_oracle_copies.h
 * @brief Recorder R4b (inventory rung): frozen copies of the viewer's offer and
 * give declarations, verbatim inside namespace r4b, compiled against the fakes
 * in llinventory_offer_oracle_fakes.h. Each region names its source, line range
 * and blob; tests/check_r4b_copies.py confirms the copies still match.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORY_OFFER_ORACLE_COPIES_H
#define LL_LLINVENTORY_OFFER_ORACLE_COPIES_H

#include "llinventory_offer_oracle_fakes.h"

namespace r4b
{

// R4B-COPY-BEGIN rlv_putinv_prefix
// source: indra/newview/rlvdefines.h
// function: RLV_PUTINV_PREFIX
// lines: 87-87
// blob: 5ae5bfee55ea50dfe9854690dac5a3ac84b64b54
#define RLV_PUTINV_PREFIX               "#RLV/~"
// R4B-COPY-END rlv_putinv_prefix

// R4B-COPY-BEGIN drag_enums
// source: indra/llui/llui.h
// function: enum EDragAndDropType, enum EAcceptance
// lines: 64-104
// blob: 1598ab01965f7081b0816e833d15032a4d16be5d
enum EDragAndDropType
{
    DAD_NONE            = 0,
    DAD_TEXTURE         = 1,
    DAD_SOUND           = 2,
    DAD_CALLINGCARD     = 3,
    DAD_LANDMARK        = 4,
    DAD_SCRIPT          = 5,
    DAD_CLOTHING        = 6,
    DAD_OBJECT          = 7,
    DAD_NOTECARD        = 8,
    DAD_CATEGORY        = 9,
    DAD_ROOT_CATEGORY   = 10,
    DAD_BODYPART        = 11,
    DAD_ANIMATION       = 12,
    DAD_GESTURE         = 13,
    DAD_LINK            = 14,
    DAD_MESH            = 15,
    DAD_WIDGET          = 16,
    DAD_PERSON          = 17,
    DAD_SETTINGS        = 18,
    DAD_MATERIAL        = 19,
    DAD_GLTF            = 20,
    DAD_GLTF_BIN        = 21,

    DAD_COUNT           = 22,   // number of types in this enum
};

// Reasons for drags to be denied.
// ordered by priority for multi-drag
enum EAcceptance
{
    ACCEPT_POSTPONED,   // we are asynchronously determining acceptance
    ACCEPT_NO,          // Uninformative, general purpose denial.
    ACCEPT_NO_CUSTOM,   // Denial with custom message.
    ACCEPT_NO_LOCKED,   // Operation would be valid, but permissions are set to disallow it.
    ACCEPT_YES_COPY_SINGLE, // We'll take a copy of a single item
    ACCEPT_YES_SINGLE,      // Accepted. OK to drag and drop single item here.
    ACCEPT_YES_COPY_MULTI,  // We'll take a copy of multiple items
    ACCEPT_YES_MULTI        // Accepted. OK to drag and drop multiple items here.
};
// R4B-COPY-END drag_enums

// R4B-COPY-BEGIN ior_enum
// source: indra/newview/llviewermessage.h
// function: enum InventoryOfferResponse
// lines: 61-72
// blob: 7d487ac44bf3a53472da89fa3662e0c31737705e
enum InventoryOfferResponse
{
    IOR_ACCEPT,
    IOR_DECLINE,
    IOR_MUTE,
    IOR_SHOW,
    // <FS:Ansariel> FIRE-3832: Silent accept/decline of inventory offers
    IOR_ACCEPT_SILENT,
    IOR_DECLINE_SILENT,
    IOR_SHOW_SILENT
    // </FS:Ansariel>
};
// R4B-COPY-END ior_enum

// R4B-COPY-BEGIN collect_functor
// source: indra/newview/llinventoryfunctions.h
// function: class LLInventoryCollectFunctor
// lines: 215-225
// blob: 9775eec814cc3dbc55eacb4c0be3aee9d87b1bb2
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLInventoryCollectFunctor
{
public:
    virtual ~LLInventoryCollectFunctor(){};
        virtual bool operator()(LLInventoryCategory* cat, LLInventoryItem* item) = 0;

        virtual bool exceedsLimit() { return false; }

    static bool itemTransferCommonlyAllowed(const LLInventoryItem* item);
};
// R4B-COPY-END collect_functor

// R4B-COPY-BEGIN give_inventory_class
// source: indra/newview/llgiveinventory.h
// function: class LLGiveInventory
// lines: 38-100
// blob: 499a94bb2ba1f22f58717a06d2fb4c682ff6a13f
class LLGiveInventory
{
public:
    /**
     * Checks if inventory item you are attempting to transfer to a resident can be given.
     *
     * @return true if you can give, otherwise false.
     */
    static bool isInventoryGiveAcceptable(const LLInventoryItem* item);

    /**
     * Checks if inventory item you are attempting to transfer to a group can be given.
     *
     * @return true if you can give, otherwise false.
     */
    static bool isInventoryGroupGiveAcceptable(const LLInventoryItem* item);

    /**
     * Gives passed inventory item to specified avatar in specified session.
     */
    static bool doGiveInventoryItem(const LLUUID& to_agent,
                                    const LLInventoryItem* item,
                                    const LLUUID& im_session_id = LLUUID::null);

    /**
     * Gives passed inventory category to specified avatar in specified session.
     */
    static bool doGiveInventoryCategory(const LLUUID& to_agent,
                                    const LLInventoryCategory* item,
                                    const LLUUID &session_id = LLUUID::null,
                                    const std::string& notification = std::string());

    // give inventory item functionality
    static bool handleCopyProtectedItem(const LLSD& notification, const LLSD& response);

private:
    // this class is not intended to be instantiated.
    LLGiveInventory();

    /**
     * logs "Inventory item offered" to IM
     */
    static void logInventoryOffer(const LLUUID& to_agent,
                                    const LLUUID &im_session_id = LLUUID::null,
                                    const std::string& item_name = std::string(),
                                    bool is_folder = false);

// [RLVa:KB] - @share
    static bool commitGiveInventoryItem(const LLUUID& to_agent,
                                    const LLInventoryItem* item,
                                    const LLUUID &im_session_id = LLUUID::null);
// [/RLVa:KB]
//  static void commitGiveInventoryItem(const LLUUID& to_agent,
//                                  const LLInventoryItem* item,
//                                  const LLUUID &im_session_id = LLUUID::null);

    // give inventory category functionality
    static bool handleCopyProtectedCategory(const LLSD& notification, const LLSD& response);
    static bool commitGiveInventoryCategory(const LLUUID& to_agent,
                                    const LLInventoryCategory* cat,
                                    const LLUUID &im_session_id = LLUUID::null);

};
// R4B-COPY-END give_inventory_class

// R4B-COPY-BEGIN offer_info_class
// source: indra/newview/llviewermessage.h
// function: class LLOfferInfo
// lines: 246-293
// blob: 7d487ac44bf3a53472da89fa3662e0c31737705e
// substitutions: 'private:' -> 'public:'
class LLOfferInfo : public LLNotificationResponderInterface
{
public:
    LLOfferInfo();
    LLOfferInfo(const LLSD& sd);

    LLOfferInfo(const LLOfferInfo& info);

    void forceResponse(InventoryOfferResponse response);

    static std::string mResponderType;
    EInstantMessage mIM;
    LLUUID mFromID;
    bool mFromGroup;
    bool mFromObject;
    LLUUID mTransactionID;
    LLUUID mFolderID;
    LLUUID mObjectID;
    LLAssetType::EType mType;
    std::string mFromName;
    std::string mDesc;
    LLHost mHost;
    bool mPersist;

    // LLNotificationResponderInterface implementation
    /*virtual*/ LLSD asLLSD();
    /*virtual*/ void fromLLSD(const LLSD& params);
    /*virtual*/ void handleRespond(const LLSD& notification, const LLSD& response);

    void send_auto_receive_response() { sendReceiveResponse(true, mFolderID); }
    // <FS:Ansariel> Optional V1-like inventory accept messages
    void send_decline_response(void);

    // TODO - replace all references with handleRespond()
    bool inventory_offer_callback(const LLSD& notification, const LLSD& response);
    bool inventory_task_offer_callback(const LLSD& notification, const LLSD& response);

public:

    void initRespondFunctionMap();
    std::string getSanitizedDescription();
    void sendReceiveResponse(bool accept, const LLUUID &destination_folder_id);

    typedef std::function<bool (const LLSD&, const LLSD&)> respond_function_t;
    typedef std::map<std::string, respond_function_t> respond_function_map_t;

    respond_function_map_t mRespondFunctions;
};
// R4B-COPY-END offer_info_class

// R4B-COPY-BEGIN open_agent_offer
// source: indra/newview/llviewermessage.h
// function: class LLOpenAgentOffer
// lines: 295-331
// blob: 7d487ac44bf3a53472da89fa3662e0c31737705e
// <FS:Ansariel> Moved from source; needed in llimprocessing.cpp
class LLOpenAgentOffer : public LLInventoryFetchItemsObserver
{
public:
    LLOpenAgentOffer(const LLUUID& object_id,
                     const std::string& from_name,
                     bool is_manuelly_accepted) :
        LLInventoryFetchItemsObserver(object_id),
        mFromName(from_name),
        mIsManuallyAccepted(is_manuelly_accepted) {}
    /*virtual*/ void startFetch()
    {
        for (uuid_vec_t::const_iterator it = mIDs.begin(); it < mIDs.end(); ++it)
        {
            LLViewerInventoryCategory* cat = gInventory.getCategory(*it);
            if (cat)
            {
                mComplete.push_back((*it));
            }
        }
        LLInventoryFetchItemsObserver::startFetch();
    }
    /*virtual*/ void done()
    {
        // <FS:Ansariel> FIRE-3234: Don't need a check for ShowNewInventory here;
        // This only gets called if the user explicity clicks "Show" or
        // AutoAcceptNewInventory and ShowNewInventory are true.
        //open_inventory_offer(mComplete, mFromName);
        open_inventory_offer(mComplete, mFromName, mIsManuallyAccepted);
        gInventory.removeObserver(this);
        delete this;
    }
private:
    std::string mFromName;
    bool        mIsManuallyAccepted;
};
// </FS:Ansariel>
// R4B-COPY-END open_agent_offer

// Harness declarations (not copies): the copied definitions in
// llinventory_offer_oracle_copies.cpp and the wrappers the harness calls.
class LLToolDragAndDrop
{
public:
    enum ESource { SOURCE_AGENT, SOURCE_WORLD, SOURCE_NOTECARD, SOURCE_LIBRARY, SOURCE_VIEWER, SOURCE_PEOPLE };
    static EAcceptance willObjectAcceptInventory(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type);
    static bool handleGiveDragAndDrop(LLUUID dest_agent, LLUUID session_id, bool drop, EDragAndDropType cargo_type,
                                      void* cargo_data, EAcceptance* accept, const LLSD& dest = LLSD());
    EAcceptance dad3dGiveInventoryObject(LLViewerObject* obj, S32 face, MASK mask, bool drop);
    EAcceptance dad3dGiveInventory(LLViewerObject* obj, S32 face, MASK mask, bool drop);
    EAcceptance dad3dGiveInventoryCategory(LLViewerObject* obj, S32 face, MASK mask, bool drop);
    // the drag state the harness sets (locateInventory's answer)
    ESource mSource = SOURCE_AGENT;
    LLViewerInventoryItem* mItem = nullptr;
    LLViewerInventoryCategory* mCat = nullptr;
    void locateInventory(LLViewerInventoryItem*& item, LLViewerInventoryCategory*& cat) { item = mItem; cat = mCat; }
};
void process_offline_messages(LLSD messages);
bool move_inv_category_world_to_agent(const LLUUID& object_id, const LLUUID& category_id, bool drop,
                                      std::function<void(S32, void*, const LLMoveInv*)> callback = NULL,
                                      void* user_data = NULL, LLInventoryFilter* filter = NULL);
bool check_offer_throttle(const std::string& from_name, bool check_only);
void inventory_offer_handler(LLOfferInfo* info);
void process_offer_im(LLUUID from_id, bool from_group, EInstantMessage dialog, LLUUID session_id,
                      U32 timestamp, std::string agentName, std::string message,
                      U8* binary_bucket, S32 binary_bucket_size, LLHost& sender, LLUUID aux_id);
// the collect functors of llgiveinventory.cpp and lltooldraganddrop.cpp, by name:
// giveable, uncopyable, droppable_transfer, droppable_no_transfer, drop_copyable
bool eval_functor(const std::string& functor, LLInventoryCategory* cat, LLInventoryItem* item, S32* count);
void collect_with(const std::string& functor, const LLUUID& root, LLInventoryModel::cat_array_t& cats,
                  LLInventoryModel::item_array_t& items, S32* count);
// the share dialog (llavataractions.cpp)
void share_set_avatars(const uuid_vec_t& ids);
void share_give_inventory_cb(const LLSD& notification, const LLSD& response, std::set<LLUUID> selected);

} // namespace r4b

#endif
