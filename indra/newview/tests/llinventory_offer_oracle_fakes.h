/**
 * @file llinventory_offer_oracle_fakes.h
 * @brief Recorder R4b (inventory rung): the fakes the frozen offer and give
 * copies are compiled against. Every viewer singleton, setting, RLVa verdict,
 * mute list entry, model query, notification and message send the copies
 * touch is a fake here, inside namespace r4b, so the unqualified names in the
 * verbatim copies resolve to these. Everything a fake is asked to do that the
 * viewer would show, send or change is appended to the event log in order.
 *
 * What stays real: LLUUID, LLSD, LLPermissions, LLSaleInfo, LLInventoryItem,
 * LLInventoryCategory, LLAssetType, LLFolderType, LLHost, LLCacheName's pure
 * name helpers, the message system (the template builder through which every
 * packet is built) and pack_instant_message.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORY_OFFER_ORACLE_FAKES_H
#define LL_LLINVENTORY_OFFER_ORACLE_FAKES_H

#include "linden_common.h"

#include "llassettype.h"
#include "llavatarname.h"
#include "llcachename.h"
#include "llfoldertype.h"
#include "llhost.h"
#include "llinstantmessage.h"
#include "llinventory.h"
#include "llinventorytype.h"
#include "llpermissions.h"
#include "llpermissionsflags.h"
#include "llpointer.h"
#include "llsd.h"
#include "llsingleton.h"
#include "llstl.h"
#include "llstring.h"
#include "lluri.h"
#include "lluuid.h"
#include "lldbstrings.h"
#include "message.h"
#include "message_prehash.h"
#include "v3math.h"
#include "v4color.h"
#include "v4coloru.h"

#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The real llmessage message system; the fake below forwards to it.
typedef LLMessageSystem RealMessageSystem;

namespace r4b
{
    // ---------------------------------------------------------------- log
    struct EventLog
    {
        std::vector<std::string> lines;
        bool on = true;
        void add(const std::string& line) { if (on) lines.push_back(line); }
    };
    EventLog& ev();
    std::string hex(const void* data, size_t size);
    std::string sd_bin_hex(const LLSD& sd);
    std::string sd_notation(const LLSD& sd);
    std::string esc(const std::string& s); // printable rendering of a byte string
    // printf into a string of any length (llformat stops at 1023 bytes and,
    // on Windows, leaves its buffer unterminated when it overflows)
    std::string fmt(const char* format, ...);

    // The deterministic replacements of LLUUID::generate() and LLDate::now()
    // (documented substitutions in the frozen copies).
    void r4b_generate(LLUUID& id);
    LLDate r4b_now();

    // ---------------------------------------------------------- settings
    struct FakeSettings
    {
        const char* label = "gSavedSettings";
        std::map<std::string, bool> b;
        std::map<std::string, U32> u;
        std::map<std::string, std::string> s;
        bool getBOOL(const std::string& name) const;
        U32 getU32(const std::string& name) const;
        std::string getString(const std::string& name) const;
    };
    extern FakeSettings gSavedSettings;
    extern FakeSettings gSavedPerAccountSettings;

    template<class T> T settings_get(const FakeSettings& s, const std::string& name);
    template<> inline bool settings_get<bool>(const FakeSettings& s, const std::string& name) { return s.getBOOL(name); }
    template<> inline U32 settings_get<U32>(const FakeSettings& s, const std::string& name) { return s.getU32(name); }

    template<class T>
    class LLCachedControl
    {
    public:
        LLCachedControl(FakeSettings& settings, const std::string& name, T = T()) : mSettings(settings), mName(name) {}
        operator T() const { return settings_get<T>(mSettings, mName); }
    private:
        FakeSettings& mSettings;
        std::string mName;
    };

    // ------------------------------------------------------ message system
    // Stands in for LLMessageSystem in the copies: forwards the build calls to
    // the real message system and captures the built packet at send time.
    class LLMessageSystem
    {
    public:
        void newMessageFast(const char* name);
        void nextBlockFast(const char* block);
        void addUUIDFast(const char* var, const LLUUID& v);
        void addBOOLFast(const char* var, bool v);
        void addU8Fast(const char* var, U8 v);
        void addU32Fast(const char* var, U32 v);
        void addStringFast(const char* var, const char* v);
        void addStringFast(const char* var, const std::string& v);
        void addVector3Fast(const char* var, const LLVector3& v);
        void addBinaryDataFast(const char* var, const void* data, S32 size);
        S32 sendReliable(const LLHost& host);
        // the packet as built so far, its fields as added (review only)
        void capture(const std::string& route);
        std::vector<std::string> mFields;
    };
    extern LLMessageSystem* gMessageSystem;

    // pack_instant_message through the fake: the real function, packing into
    // the real message system.
    void pack_instant_message(
        LLMessageSystem* msg,
        const LLUUID& from_id,
        bool from_group,
        const LLUUID& session_id,
        const LLUUID& to_id,
        const std::string& name,
        const std::string& message,
        U8 offline = IM_ONLINE,
        EInstantMessage dialog = IM_NOTHING_SPECIAL,
        const LLUUID& id = LLUUID::null,
        U32 parent_estate_id = 0,
        const LLUUID& region_id = LLUUID::null,
        const LLVector3& position = LLVector3::zero,
        U32 timestamp = NO_TIMESTAMP,
        const U8* binary_bucket = (U8*)EMPTY_BINARY_BUCKET,
        S32 binary_bucket_size = EMPTY_BINARY_BUCKET_SIZE);

    // llviewerinventory.cpp's log tag
    static const char * const LOG_INV("Inventory");

    // ------------------------------------------------------------ objects
    typedef U8 LLPCode;
    const LLPCode LL_PCODE_VOLUME = 9; // llmath/llvolume.h (checked by check_r4b_copies.py)

    class LLViewerObject
    {
    public:
        LLUUID mID;
        bool mPermModify = false;
        bool mFlagAllowInventoryAdd = false;
        bool mPermYouOwner = false;
        LLPCode mPCode = LL_PCODE_VOLUME;
        bool mIsAttachment = false;
        LLViewerObject* mRoot = nullptr;
        LLInventoryObject::object_list_t* mInventory = nullptr;
        ~LLViewerObject() { delete mInventory; }

        const LLUUID& getID() const { return mID; }
        bool permModify() const { return mPermModify; }
        bool flagAllowInventoryAdd() const { return mFlagAllowInventoryAdd; }
        bool permYouOwner() const { return mPermYouOwner; }
        LLPCode getPCode() const { return mPCode; }
        bool isAttachment() const { return mIsAttachment; }
        LLViewerObject* getRootEdit() const { return mRoot ? mRoot : const_cast<LLViewerObject*>(this); }
        void getInventoryContents(LLInventoryObject::object_list_t& objects); // frozen copy
    };

    struct FakeObjectList
    {
        std::map<LLUUID, LLViewerObject*> objects;
        LLViewerObject* findObject(const LLUUID& id) const;
    };
    extern FakeObjectList gObjectList;
    LLViewerObject* get_object_to_mute_from_id(LLUUID id);

    // -------------------------------------------------------------- agent
    class LLViewerInventoryItem;
    class LLViewerInventoryCategory;
    class LLOfferInfo;

    struct FakeAgent
    {
        LLUUID mID;
        LLUUID mSessionID;
        LLUUID mGroupID;
        LLVector3 mPosition;
        bool mDoNotDisturb = false;
        LLHost mRegionHost;
        std::set<LLUUID> mGroups;
        const LLUUID& getID() const { return mID; }
        const LLUUID& getSessionID() const { return mSessionID; }
        const LLUUID& getGroupID() const { return mGroupID; }
        LLVector3 getPositionAgent() const { return mPosition; }
        bool isDoNotDisturb() const { return mDoNotDisturb; }
        bool isInGroup(const LLUUID& id, bool ignore_god = false) const { return mGroups.count(id) != 0; }
        LLColor4U getEffectColor() const { return LLColor4U(255, 255, 255, 255); }
        const LLHost& getRegionHost() const { return mRegionHost; }
        void sendReliableMessage();
    };
    extern FakeAgent gAgent;
    extern LLUUID gAgentID;
    extern LLUUID gAgentSessionID;

    struct FakeAvatar
    {
        bool mValid = true;
        std::string mFullname;
        std::set<LLUUID> mAttachments;
        bool mSitting = false;
        LLViewerObject* mRoot = nullptr;
        std::string getFullname() const { return mFullname; }
        bool isWearingAttachment(const LLUUID& id) const { return mAttachments.count(id) != 0; }
        bool isSitting() const { return mSitting; }
        LLViewerObject* getRoot() const { return mRoot; }
    };
    typedef FakeAvatar LLVOAvatarSelf;
    extern FakeAvatar* gAgentAvatarp;
    bool isAgentAvatarValid();

    struct FakeWearables
    {
        std::set<LLUUID> worn;
        bool isWearingItem(const LLUUID& id) const { return worn.count(id) != 0; }
    };
    extern FakeWearables gAgentWearables;

    class LLGestureMgr
    {
    public:
        static LLGestureMgr& instance();
        std::set<LLUUID> active;
        bool isGestureActive(const LLUUID& id) const { return active.count(id) != 0; }
    };

    class LLAgentUI
    {
    public:
        static void buildFullname(std::string& name); // frozen copy
    };

    // -------------------------------------------------------------- model
    class LLViewerInventoryItem : public LLInventoryItem
    {
    public:
        typedef std::vector<LLPointer<LLViewerInventoryItem> > item_array_t;
        LLViewerInventoryItem(const LLUUID& uuid, const LLUUID& parent_uuid, const LLPermissions& perm,
                              const LLUUID& asset_uuid, LLAssetType::EType type, LLInventoryType::EType inv_type,
                              const std::string& name, U32 flags = 0)
            : LLInventoryItem(uuid, parent_uuid, perm, asset_uuid, type, inv_type, name, std::string(), LLSaleInfo::DEFAULT, flags, 0)
        {}
        // frozen copies
        virtual LLAssetType::EType getType() const;
        virtual const std::string& getName() const;
        LLViewerInventoryItem* getLinkedItem() const;
        LLViewerInventoryCategory* getLinkedCategory() const;

        bool isFinished() const { return mFinished; }
        bool mFinished = true;
        // names the model would have corrected (llinventory caps them at 63 bytes)
        void setRawName(const std::string& name) { mName = name; }
    };

    class LLViewerInventoryCategory : public LLInventoryCategory
    {
    public:
        typedef std::vector<LLPointer<LLViewerInventoryCategory> > cat_array_t;
        LLViewerInventoryCategory(const LLUUID& uuid, const LLUUID& parent_uuid, LLFolderType::EType preferred_type, const std::string& name)
            : LLInventoryCategory(uuid, parent_uuid, preferred_type, name)
        {}
        void setRawName(const std::string& name) { mName = name; }
    };

    class LLInventoryCollectFunctor;
    class LLInventoryObserver;

    class LLInventoryModel
    {
    public:
        typedef LLViewerInventoryCategory::cat_array_t cat_array_t;
        typedef LLViewerInventoryItem::item_array_t item_array_t;
        enum { EXCLUDE_TRASH = false, INCLUDE_TRASH = true };

        // frozen copy
        void collectDescendentsIf(const LLUUID& id, cat_array_t& categories, item_array_t& items,
                                  bool include_trash, LLInventoryCollectFunctor& add, bool follow_folder_links = false);

        LLViewerInventoryItem* getItem(const LLUUID& id) const;
        LLViewerInventoryCategory* getCategory(const LLUUID& id) const;
        bool isCategoryComplete(const LLUUID& id) const;
        const LLUUID findCategoryUUIDForType(LLFolderType::EType preferred_type);
        void deleteObject(const LLUUID& id);
        void removeObject(const LLUUID& id);
        void notifyObservers();
        void addObserver(LLInventoryObserver* observer);
        void removeObserver(LLInventoryObserver* observer);

        // the fake model: parent -> children, in the order the model holds them
        void clear();
        void addCategory(LLViewerInventoryCategory* cat);
        void addItem(LLViewerInventoryItem* item);
        std::map<LLUUID, LLPointer<LLViewerInventoryCategory> > mCategoryMap;
        std::map<LLUUID, LLPointer<LLViewerInventoryItem> > mItemMap;
        std::map<LLUUID, cat_array_t*> mParentChildCategoryTree;
        std::map<LLUUID, item_array_t*> mParentChildItemTree;
        std::set<LLUUID> mIncomplete;           // categories not complete
        std::map<S32, LLUUID> mSystemFolders;   // LLFolderType -> id
        std::vector<LLInventoryObserver*> mObservers;
        ~LLInventoryModel() { clear(); }
    };
    extern LLInventoryModel gInventory;

    class LLInventoryObserver
    {
    public:
        virtual ~LLInventoryObserver() {}
        virtual void changed(U32 mask) {}
    };

    // Stands in for the fetch observers (survey 3's area): startFetch sorts
    // the ids as the real one does (complete if in the model and finished,
    // categories and null ids skipped, the rest incomplete) and logs.
    class LLInventoryFetchItemsObserver : public LLInventoryObserver
    {
    public:
        LLInventoryFetchItemsObserver(const LLUUID& item_id = LLUUID::null);
        LLInventoryFetchItemsObserver(const uuid_vec_t& item_ids);
        virtual void startFetch();
        virtual void done();
        bool isFinished() const { return mIncomplete.empty(); }
    protected:
        uuid_vec_t mComplete;
        uuid_vec_t mIncomplete;
        uuid_vec_t mIDs;
    };
    // the class name of an observer for the log (defined after the copies)
    std::string observer_kind(const LLInventoryObserver* observer);

    void open_inventory_offer(const uuid_vec_t& items, const std::string& from_name, bool from_agent_manual = false);

    class LLOpenTaskGroupOffer : public LLInventoryObserver
    {
    public:
        LLOpenTaskGroupOffer();
    };

    class LLInventoryPanel
    {
    public:
        static void openInventoryPanelAndSetSelection(bool auto_open, const LLUUID& obj_id,
                                                      bool use_main_panel = false, bool take_keyboard_focus = true, bool reset_filter = false);
    };

    struct LLMoveInv;
    typedef std::pair<LLUUID, LLUUID> two_uuids_t;
    typedef std::list<two_uuids_t> two_uuids_list_t;
    struct LLMoveInv
    {
        LLUUID mObjectID;
        LLUUID mCategoryID;
        two_uuids_list_t mMoveList;
        std::function<void(S32, void*, const LLMoveInv*)> mCallback;
        void* mUserData = nullptr;
    };
    void warn_move_inventory(LLViewerObject* object, std::shared_ptr<LLMoveInv> move_inv);
    bool move_task_inventory_callback(const LLSD& notification, const LLSD& response, std::shared_ptr<LLMoveInv> move_inv);

    class LLInventoryFilter
    {
    public:
        enum EFilterType
        {
            FILTERTYPE_NONE = 0,
            FILTERTYPE_OBJECT = 0x1 << 0,
            FILTERTYPE_DATE = 0x1 << 3,
        };
        U64 mTypes = 0;
        std::set<LLUUID> mPasses;
        U64 getFilterTypes() const { return mTypes; }
        bool check(const LLInventoryItem* item);
    };

    // ------------------------------------------------------------- names
    class LLAvatarName
    {
    public:
        std::string mUserName;
        std::string getUserName() const { return mUserName; }
        std::string getCompleteName() const { return mUserName; }
    };

    class LLAvatarNameCache
    {
    public:
        typedef std::function<void(const LLUUID&, const LLAvatarName&)> callback_slot_t;
        struct connection { void disconnect() {} bool connected() const { return false; } };
        static bool get(const LLUUID& id, LLAvatarName* name);
        static connection get(const LLUUID& id, callback_slot_t slot);
        static LLAvatarNameCache& instance();
        static LLAvatarNameCache* getInstance() { return &instance(); }
        LLUUID findIdByName(const std::string& name);
        // fake state
        std::map<LLUUID, std::string> names;   // cached display/user names
        std::map<std::string, LLUUID> ids_by_legacy_name;
    };

    // The real name helpers are pure; the instance calls are fakes.
    class LLCacheName
    {
    public:
        static std::string cleanFullName(const std::string& full_name) { return ::LLCacheName::cleanFullName(full_name); }
        static std::string buildUsername(const std::string& name) { return ::LLCacheName::buildUsername(name); }
        static std::string buildLegacyName(const std::string& name) { return ::LLCacheName::buildLegacyName(name); }
        typedef std::function<void(const LLUUID&, const std::string&, bool)> callback_t;
        void getGroup(const LLUUID& group_id, const callback_t& callback);
        bool getGroupName(const LLUUID& id, std::string& group);
        void get(const LLUUID& id, bool is_group, const callback_t& callback);
        std::map<LLUUID, std::string> group_names;
    };
    extern LLCacheName* gCacheName;

    // -------------------------------------------------------------- mutes
    class LLMute
    {
    public:
        enum EType { BY_NAME = 0, AGENT = 1, OBJECT = 2, GROUP = 3, EXTERNAL = 4, COUNT = 5 };
        enum
        {
            flagTextChat        = 0x00000001,
            flagVoiceChat       = 0x00000002,
            flagParticles       = 0x00000004,
            flagObjectSounds    = 0x00000008,
            flagAll             = 0x0000000F
        };
        LLMute(const LLUUID& id, const std::string& name = std::string(), EType type = BY_NAME, U32 flags = 0)
            : mID(id), mName(name), mType(type), mFlags(flags) {}
        LLUUID mID;
        std::string mName;
        EType mType;
        U32 mFlags;
    };

    class LLMuteList
    {
    public:
        enum EAutoReason { AR_IM = 0, AR_MONEY = 1, AR_INVENTORY = 2, AR_COUNT };
        static LLMuteList* getInstance();
        static bool isLinden(const std::string& name);                                          // frozen copy
        bool isMuted(const LLUUID& id, const std::string& name = LLStringUtil::null, U32 flags = 0) const; // frozen copy
        bool isMuted(const LLUUID& id, U32 flags) const { return isMuted(id, LLStringUtil::null, flags); };
        bool autoRemove(const LLUUID& agent_id, const EAutoReason reason);                     // frozen copy
        bool add(const LLMute& mute, U32 flags = 0);
        bool remove(const LLMute& mute, U32 flags = 0);

        struct compare_by_id
        {
            bool operator()(const LLMute& a, const LLMute& b) const { return a.mID < b.mID; }
        };
        typedef std::set<LLMute, compare_by_id> mute_set_t;
        mute_set_t mMutes;
        typedef std::set<std::string> string_set_t;
        string_set_t mLegacyMutes;
    };
    void notify_automute_callback(const LLUUID& agent_id, const LLAvatarName& av_name, LLMuteList::EAutoReason reason);

    class LLPanelBlockedList
    {
    public:
        static void showPanelAndSelect(const LLUUID& idToSelect = LLUUID::null);
    };

    // ------------------------------------------------------ notifications
    class LLNotificationForm
    {
    public:
        LLNotificationForm() : mFormData(LLSD::emptyArray()) {}
        LLNotificationForm(const LLSD& sd) : mFormData(sd.isArray() ? sd : LLSD::emptyArray()) {}
        S32 getNumElements() const { return (S32)mFormData.size(); }
        LLSD getElement(S32 index) const { return mFormData.get(index); }
        void setElementEnabled(const std::string& element_name, bool enabled) {}
        LLSD asLLSD() const { return mFormData; }
    private:
        LLSD mFormData;
    };
    typedef std::shared_ptr<LLNotificationForm> LLNotificationFormPtr;

    class LLNotificationResponderInterface
    {
    public:
        LLNotificationResponderInterface();
        virtual ~LLNotificationResponderInterface();
        virtual LLSD asLLSD() = 0;
        virtual void fromLLSD(const LLSD& params) = 0;
        virtual void handleRespond(const LLSD& notification, const LLSD& response) = 0;
    };
    typedef std::shared_ptr<LLNotificationResponderInterface> LLNotificationResponderPtr;

    typedef std::function<void(const LLSD&, const LLSD&)> ResponseFunctor;

    class LLNotification;
    typedef std::shared_ptr<LLNotification> LLNotificationPtr;

    class LLNotification
    {
    public:
        enum EResponseTemplateType { WITHOUT_DEFAULT_BUTTON, WITH_DEFAULT_BUTTON };
        // A fluent stand-in for the LLInitParam block.
        struct Params
        {
            template<class T, class O>
            struct Opt
            {
                O* owner = nullptr;
                T value{};
                bool provided = false;
                O& operator()(const T& v) { value = v; provided = true; return *owner; }
                Opt& operator=(const T& v) { value = v; provided = true; return *this; }
                operator const T&() const { return value; }
                bool isProvided() const { return provided; }
            };
            struct Functor
            {
                Opt<std::string, Functor> name;
                Opt<ResponseFunctor, Functor> function;
                Opt<LLNotificationResponderPtr, Functor> responder;
                Functor() { bind(); }
                Functor(const Functor& o) : name(o.name), function(o.function), responder(o.responder) { bind(); }
                Functor& operator=(const Functor& o) { name = o.name; function = o.function; responder = o.responder; bind(); return *this; }
                void bind() { name.owner = this; function.owner = this; responder.owner = this; }
            };
            Opt<std::string, Params> name;
            Opt<LLSD, Params> substitutions;
            Opt<LLSD, Params> payload;
            Opt<void*, Params> responder;
            Opt<bool, Params> offer_from_agent;
            Functor functor;
            Params() { bind(); }
            Params(const std::string& n) { bind(); name = n; }
            Params(const Params& o) : name(o.name), substitutions(o.substitutions), payload(o.payload),
                responder(o.responder), offer_from_agent(o.offer_from_agent), functor(o.functor) { bind(); }
            Params& operator=(const Params& o)
            {
                name = o.name; substitutions = o.substitutions; payload = o.payload; responder = o.responder;
                offer_from_agent = o.offer_from_agent; functor = o.functor; bind(); return *this;
            }
            void bind() { name.owner = this; substitutions.owner = this; payload.owner = this; responder.owner = this; offer_from_agent.owner = this; }
        };

        LLNotification(const Params& p);
        LLNotificationFormPtr getForm() { return mForm; }
        LLSD getResponseTemplate(EResponseTemplateType type = WITHOUT_DEFAULT_BUTTON); // frozen copy
        static S32 getSelectedOption(const LLSD& notification, const LLSD& response); // frozen copy
        void respond(const LLSD& response);
        LLSD asLLSD() const;
        const std::string& getName() const { return mName; }
        const LLSD& getPayload() const { return mPayload; }
        const LLUUID& id() const { return mID; }

        std::string mName;
        LLUUID mID;
        LLSD mPayload;
        LLSD mSubstitutions;
        LLNotificationFormPtr mForm;
        ResponseFunctor mFunctor;
        LLNotificationResponderPtr mResponder;
    };

    class LLNotifications
    {
    public:
        static LLNotifications& instance();
        LLNotificationPtr find(LLUUID uuid);
        void forceResponse(const LLNotification::Params& params, S32 option); // frozen copy
        std::map<LLUUID, LLNotificationPtr> mLive;
    };

    class LLNotificationsUtil
    {
    public:
        static LLNotificationPtr add(const std::string& name);
        static LLNotificationPtr add(const std::string& name, const LLSD& substitutions);
        static LLNotificationPtr add(const std::string& name, const LLSD& substitutions, const LLSD& payload);
        static LLNotificationPtr add(const std::string& name, const LLSD& substitutions, const LLSD& payload,
                                     const ResponseFunctor& functor);
        static S32 getSelectedOption(const LLSD& notification, const LLSD& response); // frozen copy
    };

    // The template forms the copies respond through (frozen from
    // notifications.xml; check_r4b_copies.py confirms them).
    LLSD notification_form(const std::string& name);

    class LLPostponedNotification
    {
    public:
        template<class T>
        static void add(const LLNotification::Params& params, const LLUUID& id, bool is_group)
        {
            LLPostponedNotification* thiz = new T();
            thiz->mParams = params;
            thiz->lookupName(id, is_group);
        }
        virtual ~LLPostponedNotification() {}
    protected:
        LLPostponedNotification() {}
        virtual void modifyNotificationParams() = 0;
        LLNotification::Params mParams;
        std::string mName;
    private:
        void lookupName(const LLUUID& id, bool is_group);
        void finishLookup(const LLUUID& id, bool is_group);
    };

    namespace LLNotificationsUI
    {
        extern const LLUUID NOTIFICATION_CHANNEL_UUID;
        class LLScreenChannel
        {
        public:
            struct Matcher
            {
                virtual ~Matcher() {}
                virtual bool matches(const LLNotificationPtr) const = 0;
            };
        };
        class LLChannelManager
        {
        public:
            static LLChannelManager* getInstance();
            void killToastsFromChannel(const LLUUID& channel_id, const LLScreenChannel::Matcher& matcher);
        };
    }

    // ------------------------------------------------- strings and SLURLs
    class LLTrans
    {
    public:
        static std::string getString(const std::string& key);
        static std::string getString(const std::string& key, const LLStringUtil::format_map_t& args);
    };

    class LLUIString
    {
    public:
        LLUIString(const std::string& s) : mString(s) {}
        void setArgs(const LLSD& args);
        std::string getString() const { return mString; }
    private:
        std::string mString;
    };

    class LLSLURL
    {
    public:
        LLSLURL() {}
        LLSLURL(const std::string& slurl) : mString(slurl) {}
        LLSLURL(const std::string& command, const LLUUID& id, const std::string& verb);
        std::string getSLURLString() const { return mString; }
        std::string getLocationString() const { return "location(" + mString + ")"; }
    private:
        std::string mString;
    };

    struct LLChat
    {
        std::string mText;
        bool mMuted = false;
    };

    // ------------------------------------------------------ viewer misc
    class LLFrameTimer
    {
    public:
        bool hasExpired() const;
        bool checkExpirationAndReset(F32 expiration);
        F64 mExpiry = 0.0;
    };
    F64& fake_clock(); // seconds, injected
    extern LLFrameTimer gThrottleTimer; // defined by the frozen copy of llviewermessage.cpp

    enum EStartupState { STATE_FIRST, STATE_STARTED = 50 };
    class LLStartUp
    {
    public:
        static EStartupState getStartupState();
    };

    class LLAppViewer
    {
    public:
        static LLAppViewer* instance();
        void addOnIdleCallback(const std::function<void()>& cb);
        void postToMainCoro(const std::function<void()>& cb);
        std::string getSecondLifeTitle() const { return "Vulkanstorm"; }
        std::vector<std::function<void()> > mIdle;
    };

    class LLRecentPeople
    {
    public:
        static LLRecentPeople& instance();
        bool add(const LLUUID& id, const LLSD& userdata = LLSD());
    };

    class LLFirstUse
    {
    public:
        static void newInventory(bool enable = true);
    };

    class LLFloaterReg
    {
    public:
        static void hideInstance(const std::string& name, const LLSD& key = LLSD());
    };

    namespace FSCommon
    {
        void report_to_nearby_chat(const std::string& message);
    }
    void make_ui_sound(const char* name);

    class LLIMProcessing
    {
    public:
        // forwards to the harness's process_offer_im (the ReadOfflineMsgs loop calls it)
        static void processNewMessage(LLUUID from_id, bool from_group, LLUUID to_id, U8 offline, EInstantMessage dialog,
                                      LLUUID session_id, U32 timestamp, std::string agentName, std::string message,
                                      U32 parent_estate_id, LLUUID region_id, LLVector3 position, U8* binary_bucket,
                                      S32 binary_bucket_size, LLHost& sender, LLSD metadata = LLSD(), LLUUID aux_id = LLUUID());
        static std::string getAutoresponseTextForAvatar(const LLUUID& from_id, bool is_do_not_disturb, bool is_autorespond,
                                                        bool is_autorespond_nonfriends, bool is_afk, bool send_away_response, bool is_friend);
    };
    void send_do_not_disturb_message(LLMessageSystem* msg, const LLUUID& from_id, const LLUUID& session_id = LLUUID::null); // frozen copy

    class LLIMMgr
    {
    public:
        static LLUUID computeSessionID(EInstantMessage dialog, const LLUUID& other_participant_id); // frozen copy
        void addSystemMessage(const LLUUID& session_id, const std::string& message_name, const LLSD& args);
    };
    extern LLIMMgr* gIMMgr;

    class LLIMModel
    {
    public:
        static LLIMModel* getInstance();
        static LLIMModel& instance() { return *getInstance(); }
        struct LLIMSession { std::string mName; };
        LLIMSession* findIMSession(const LLUUID& session_id) const;
        bool logToFile(const std::string& file_name, const std::string& from, const LLUUID& from_id, const std::string& line);
        std::set<LLUUID> open_sessions;
        mutable LLIMSession mSession;
    };

    void notification_group_name_cb(const std::string& group_name, const std::string& sender, const std::string& subject,
                                    const std::string& message, const LLSD& payload, U32 timestamp);

    class LLPanelGroup
    {
    public:
        static void showNotice(const std::string& subject, const std::string& message, const LLUUID& group_id,
                               bool has_inventory, const std::string& inventory_name, LLOfferInfo* inventory_offer);
    };

    // ------------------------------------------------------------- HUD
    class LLHUDObject
    {
    public:
        enum { LL_HUD_EFFECT_BEAM = 9 };
    };
    class LLHUDEffect
    {
    public:
        virtual ~LLHUDEffect() {}
        void setSourceObject(FakeAvatar* avatar);
        void setTargetObject(LLViewerObject* object);
        void setDuration(F32 duration);
        void setColor(const LLColor4U& color);
    };
    class LLHUDEffectSpiral : public LLHUDEffect {};
    class LLHUDManager
    {
    public:
        static LLHUDManager* getInstance();
        LLHUDEffect* createViewerEffect(S32 type, bool send_to_sim = true, bool originated_here = true);
    };
    const F32 LL_HUD_DUR_SHORT = 1.f;

    struct FakeFloaterTools { void dirty(); };
    extern FakeFloaterTools* gFloaterTools;

    // --------------------------------------------------------------- RLVa
    enum ERlvBehaviour { RLV_BHVR_UNSIT, RLV_BHVR_SITTP, RLV_BHVR_SHARE };
    struct FakeRlvHandler
    {
        static bool isEnabled();
        bool hasBehaviour(ERlvBehaviour b) const;
    };
    typedef FakeRlvHandler rlv_handler_t;
    extern FakeRlvHandler gRlvHandler;

    struct FakeRlvAttachmentLocks
    {
        bool isLockedAttachment(const LLViewerObject* obj) const;
    };
    extern FakeRlvAttachmentLocks gRlvAttachmentLocks;

    class RlvActions
    {
    public:
        enum EShowNamesContext { SNC_DEFAULT = 0, SNC_NAMETAG, SNC_TELEPORTOFFER, SNC_TELEPORTREQUEST, SNC_COUNT };
        static bool isRlvEnabled();
        static bool canShowName(EShowNamesContext context, const LLUUID& id = LLUUID::null);
        static bool canGiveInventory(const LLUUID& id);
    };

    class RlvSettings
    {
    public:
        static bool getForbidGiveToRLV();
    };

    class RlvInventory
    {
    public:
        static RlvInventory& instance();
        bool isGiveToRLVOffer(const LLOfferInfo& offerInfo); // frozen copy
        const LLUUID& getSharedRootID() const;
        LLViewerInventoryCategory* getSharedRoot() const;
    };

    class RlvUtil
    {
    public:
        static bool isNearbyAgent(const LLUUID& id);
        static void notifyBlocked(const std::string& strMsg, const LLSD& sdArgs = LLSD(), bool fLogToChat = false);
    };

    class RlvUIEnabler
    {
    public:
        static bool hasOpenProfile(const LLUUID& id);
        static bool hasOpenIM(const LLUUID& id);
    };

    class RlvStrings
    {
    public:
        static std::string getAnonym(const LLAvatarName& av_name);
        static std::string getAnonym(const std::string& name);
    };

    namespace RlvStringKeys
    {
        namespace Blocked
        {
            extern const std::string Share;
        }
    }

    class RlvBehaviourNotifyHandler
    {
    public:
        static void sendNotification(const std::string& strText, const std::string& strSuffix = LLStringUtil::null);
    };

    class RlvGiveToRLVAgentOffer : public LLInventoryFetchItemsObserver
    {
    public:
        RlvGiveToRLVAgentOffer(const LLUUID& idFolder);
        void done() override;
    };

    class RlvGiveToRLVTaskOffer : public LLInventoryObserver
    {
    public:
        RlvGiveToRLVTaskOffer(const LLUUID& idTransaction);
    };

    // ------------------------------------------------------------ world
    class LLWorld
    {
    public:
        static LLWorld* getInstance();
        int getMaxInventoryItemsTransfer() const { return mMaxInventoryItemsTransfer; }
        int mMaxInventoryItemsTransfer = 42;
    };

    // ------------------------------------------------- the world the fakes see
    struct World
    {
        // RLVa verdicts
        bool rlv_enabled = false;
        bool rlv_forbid_give_to_rlv = false;
        LLUUID rlv_shared_root;
        bool rlv_can_show_names = true;        // canShowName for every id
        std::set<LLUUID> rlv_share_blocked;    // canGiveInventory false for these
        std::set<LLUUID> rlv_nearby;           // isNearbyAgent
        bool rlv_locked_attachment = false;
        bool rlv_unsit = false;
        bool rlv_sittp = false;
        // name lookups (avatar names, group names, postponed notifications)
        // answer from idle instead of at once, as the real caches usually do
        bool defer_name_lookups = false;
        // notifications marked live (interactive responses find them)
        EStartupState startup_state = STATE_STARTED;
        std::map<LLUUID, LLOfferInfo*> group_panel_offers;
        // the last notification posted through LLNotificationsUtil::add with
        // a functor, for the harness to respond to
        struct Pending
        {
            std::string name;
            LLSD payload;
            ResponseFunctor functor;
        };
        std::vector<Pending> pending;
        // the last postponed notification (offers)
        struct Posted
        {
            LLNotification::Params params;
            std::string resolved_name;
            LLUUID id;
        };
        // the payload of the last GroupNotice notification
        LLSD group_notice_payload;
        std::vector<Posted> posted;
        void reset();
    };
    World& world();

    // per-case reset of every fake (model, mutes, settings, log, clock)
    void reset_fakes();

    // Every live responder (LLOfferInfo), and the hook the harness installs to
    // log their state when a notification is posted or answered, or an offer
    // is handed to the group panel.
    std::set<LLNotificationResponderInterface*>& live_responders();
    std::function<void(const char* where)>& offer_snapshot_hook();
    void snapshot_offers(const char* where);
}

#endif
