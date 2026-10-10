// Native CPU session and nearby transport. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsnativesession.h"
#include "fsconsoleutils.h"
#include "vsnativeim.h"
#include "llcallingcard.h"
#include "lluserrelations.h"
#include "lggcontactsets.h"
#include "llagentlanguage.h"
#include "llinventorymodel.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llviewerassetstorage.h"
#include "llviewerinventory.h"
#include "lllandmarklist.h"
#include "llexperiencecache.h"
#include "vsuiimageprovider.h"
#include "llpersistentnotificationstorage.h"
#include "lldonotdisturbnotificationstorage.h"
#include "vsplainchat.h"
#include "llappviewer.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llviewercontrol.h"
#include "llstartup.h"
#include "llagent.h"
#include "llagentbenefits.h"
#include "llviewernetwork.h"
#include "lleconomy.h"
#include "llagentdata.h"
#if LL_SDL2
#include "llwindowsdl.h"
#endif
#include "lllogininstance.h"
#include "llpanel.h"
#include "llpaneldirbrowser.h"
#include "llgroupmgr.h"
#include "llpanelgroupnotices.h"
#include "llpanelgrouplandmoney.h"
#include "llavatarpropertiesprocessor.h"
#include "llfloateravatarpicker.h"
#include "llremoteparcelrequest.h"
#include "llvieweraudio.h"
#include "llviewermessage.h"
#include "fspanellogin.h"
#include "llprogressview.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "llurlentry.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "lleventpoll.h"
#include "llavatarnamecache.h"
#include "llavatarname.h"
#include "llmutelist.h"
#include "lllogchat.h"
#include "lltranslate.h"
#include "llchat.h"
#include "llregionhandle.h"
#include "llslurl.h"
#include "llteleporthistory.h"
#include "llteleportflags.h"
#include "lluri.h"
#include "llsdutil_math.h"
#include "llquaternion.h"
#include "llfocusmgr.h"
#include "NACLantispam.h"
#include "llcachename.h"
#include "llxfermanager.h"
#include "llxfer.h"
#include "message.h"
#include "workqueue.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <charconv>
#include <cctype>
#include <limits>

extern bool gDisconnected;
extern bool gAgentMovementCompleted;
extern void reset_login();
extern bool init_benefits(LLSD& response);
namespace
{
std::weak_ptr<VSNativeSession> sOwner;
double now() { return LLFrameTimer::getTotalSeconds(); }
const std::set<std::string> transport = {"StartPingCheck", "CompletePingCheck", "PacketAck"};
const std::set<std::string> session = {"RegionIDAndHandleReply", "MapBlockReply", "RegionHandshake", "AgentMovementComplete", "ChatFromSimulator",
    "LogoutReply", "KickUser", "AlertMessage", "AgentAlertMessage", "Error", "TeleportStart", "TeleportFinish",
    "TeleportLocal", "TeleportProgress", "TeleportFailed", "ViewerFrozenMessage", "FeatureDisabled", "CrossedRegion", "CloseCircuit", "DisableSimulator", "UUIDNameReply", "UUIDGroupNameReply",
    "MuteListUpdate", "UseCachedMuteList", "SendXferPacket", "ConfirmXferPacket", "AbortXfer", "TransferInfo", "TransferPacket", "TransferAbort",
    "EconomyData", "ImprovedInstantMessage", "AgentDataUpdate", "AgentGroupDataUpdate", "AgentDropGroup",
    "OnlineNotification", "OfflineNotification", "ChangeUserRights", "MoneyBalanceReply", "UserInfoReply", "ParcelInfoReply", "PlacesReply",
    "AvatarPropertiesReply", "AvatarInterestsReply", "AvatarGroupsReply", "AvatarNotesReply",
    "AvatarPicksReply", "AvatarClassifiedReply", "AvatarPickerReply", "PickInfoReply", "ClassifiedInfoReply",
    "CreateGroupReply", "JoinGroupReply", "EjectGroupMemberReply", "LeaveGroupReply",
    "GroupProfileReply", "GroupMembersReply", "GroupRoleDataReply", "GroupRoleMembersReply",
    "DirGroupsReply", "GroupTitlesReply", "GroupNoticesListReply", "GroupAccountSummaryReply",
    "GroupAccountDetailsReply", "GroupAccountTransactionsReply",
    "UpdateCreateInventoryItem", "RemoveInventoryItem", "RemoveInventoryFolder", "RemoveInventoryObjects",
    "SaveAssetIntoInventory", "BulkUpdateInventory", "MoveInventoryItem", "InventoryDescendents", "FetchInventoryReply",
    "SetDisplayNameReply", "DisplayNameUpdate",
    "ChatterBoxSessionStartReply", "ChatterBoxSessionEventReply", "ChatterBoxSessionAgentListUpdates",
    "ChatterBoxSessionUpdate", "ChatterBoxInvitation", "ForceCloseChatterBoxSession"};
struct CircuitDelivery { std::weak_ptr<VSNativeSession> owner; U64 generation; U64 regionEpoch; };
void circuitCallback(void** userdata, S32 result)
{
    std::unique_ptr<CircuitDelivery> delivery(reinterpret_cast<CircuitDelivery*>(userdata));
    // ACK/retry processing still owns the packet and circuit while invoking us.
    // A failed delivery can retire that circuit, so handle it after dispatch.
    if (!delivery->owner.expired())
        LL::WorkQueue::getInstance("mainloop")->post(
            [weak = delivery->owner, generation = delivery->generation, regionEpoch = delivery->regionEpoch, result]()
            { if (auto owner = weak.lock()) owner->circuitResult(generation, result, regionEpoch); });
}
void receiveMessage(LLMessageSystem* msg, void**)
{
    if (auto owner = VSNativeSession::active()) owner->receive(msg);
}
bool webURL(const std::string& url)
{
    return url.size() <= 4096 && (url.compare(0, 8, "https://") == 0 || url.compare(0, 7, "http://") == 0) && !LLURI(url).hostName().empty();
}
}

std::shared_ptr<VSNativeSession> VSNativeSession::create()
{
    auto owner = std::shared_ptr<VSNativeSession>(new VSNativeSession);
    sOwner = owner;
    const std::weak_ptr<VSNativeSession> weak = owner;
    for (const char* name : {"FontScreenDPI", "FSFontSettingsFile", "FSFontSizeAdjustment"})
        if (auto control = gSavedSettings.getControl(name))
            owner->mSettingConnections.emplace_back(control->getSignal()->connect(
                [weak](LLControlVariable*, const LLSD&, const LLSD&)
                { if (weak.lock() && gViewerWindow) gViewerWindow->refreshNativeFonts(); }));
    if (auto control = gSavedSettings.getControl("FSEnableLogThrottle"))
        owner->mSettingConnections.emplace_back(control->getSignal()->connect(
            [weak](LLControlVariable*, const LLSD& value, const LLSD&)
            { if (weak.lock()) { extern void handleLogThrottleChanged(const LLSD&); handleLogThrottleChanged(value); } }));
#if LL_SDL2
    if (auto control = gSavedSettings.getControl("SDL2IMEEnabled"))
        owner->mSettingConnections.emplace_back(control->getSignal()->connect(
            [weak](LLControlVariable*, const LLSD& value, const LLSD&)
            { if (weak.lock() && gViewerWindow) static_cast<LLWindowSDL*>(gViewerWindow->getWindow())->enableIME(value.asBoolean()); }));
#endif
    return owner;
}
std::shared_ptr<VSNativeSession> VSNativeSession::active() { return sOwner.lock(); }
VSNativeSession::~VSNativeSession() { if (!mShutdown) reset(); }
void VSNativeSession::install(LLMessageSystem& msg)
{
    const std::weak_ptr<VSNativeSession> owner = shared_from_this();
    msg.setMessageAdmission([owner](const std::string& name, const LLHost& sender)
    {
        auto locked = owner.lock();
        return locked && locked->admit(name, sender);
    });
    // Mixed viewer handlers are replaced before any session messages can execute.
    for (const char* name : {"RegionIDAndHandleReply", "MapBlockReply", "RegionHandshake", "AgentMovementComplete", "ChatFromSimulator", "LogoutReply",
         "KickUser", "AlertMessage", "AgentAlertMessage", "Error", "ViewerFrozenMessage", "FeatureDisabled", "MoneyBalanceReply", "TeleportStart", "TeleportFinish", "TeleportLocal", "TeleportProgress", "TeleportFailed", "CrossedRegion", "CloseCircuit", "DisableSimulator"})
        msg.setHandlerFunc(name, receiveMessage);
    LLAvatarTracker::instance().registerCallbacks(&msg);
    LLInventoryModel::registerCallbacks(&msg);
    msg.setHandlerFunc("AgentDataUpdate", LLAgent::processAgentDataUpdate);
    msg.setHandlerFunc("AgentGroupDataUpdate", LLAgent::processAgentGroupDataUpdate);
    msg.setHandlerFunc("AgentDropGroup", LLAgent::processAgentDropGroup);
    msg.setHandlerFunc("DirGroupsReply", LLPanelDirBrowser::processDirGroupsReply);
    // These CPU services normally register during the world startup path,
    // which native text startup deliberately does not execute. Admission
    // alone cannot substitute for installing their production reply handlers.
    msg.setHandlerFunc("CreateGroupReply", LLGroupMgr::processCreateGroupReply);
    msg.setHandlerFunc("JoinGroupReply", LLGroupMgr::processJoinGroupReply);
    msg.setHandlerFunc("EjectGroupMemberReply", LLGroupMgr::processEjectGroupMemberReply);
    msg.setHandlerFunc("LeaveGroupReply", LLGroupMgr::processLeaveGroupReply);
    msg.setHandlerFunc("GroupProfileReply", LLGroupMgr::processGroupPropertiesReply);
    msg.setHandlerFunc("GroupMembersReply", LLGroupMgr::processGroupMembersReply);
    msg.setHandlerFunc("GroupRoleDataReply", LLGroupMgr::processGroupRoleDataReply);
    msg.setHandlerFunc("GroupRoleMembersReply", LLGroupMgr::processGroupRoleMembersReply);
    msg.setHandlerFunc("GroupTitlesReply", LLGroupMgr::processGroupTitlesReply);
    msg.setHandlerFunc("GroupNoticesListReply", LLPanelGroupNotices::processGroupNoticesListReply);
    msg.setHandlerFunc("GroupAccountSummaryReply", LLPanelGroupLandMoney::processGroupAccountSummaryReply);
    msg.setHandlerFunc("GroupAccountDetailsReply", LLPanelGroupLandMoney::processGroupAccountDetailsReply);
    msg.setHandlerFunc("GroupAccountTransactionsReply", LLPanelGroupLandMoney::processGroupAccountTransactionsReply);
    // Group land listings are CPU rows. Null/personal land queries do not
    // instantiate the deferred scene Land Holdings floater in this route.
    msg.setHandlerFunc("PlacesReply", LLPanelGroupLandMoney::processPlacesReply);
    msg.setHandlerFunc("AvatarPropertiesReply", LLAvatarPropertiesProcessor::processAvatarLegacyPropertiesReply);
    msg.setHandlerFunc("AvatarInterestsReply", LLAvatarPropertiesProcessor::processAvatarInterestsReply);
    msg.setHandlerFunc("AvatarGroupsReply", LLAvatarPropertiesProcessor::processAvatarGroupsReply);
    msg.setHandlerFunc("AvatarNotesReply", LLAvatarPropertiesProcessor::processAvatarNotesReply);
    msg.setHandlerFunc("AvatarPicksReply", LLAvatarPropertiesProcessor::processAvatarPicksReply);
    msg.setHandlerFunc("AvatarClassifiedReply", LLAvatarPropertiesProcessor::processAvatarClassifiedsReply);
    msg.setHandlerFunc("PickInfoReply", LLAvatarPropertiesProcessor::processPickInfoReply);
    msg.setHandlerFunc("ClassifiedInfoReply", LLAvatarPropertiesProcessor::processClassifiedInfoReply);
    msg.setHandlerFunc("ParcelInfoReply", LLRemoteParcelInfoProcessor::processParcelInfoReply);
    msg.setHandlerFunc("AvatarPickerReply", LLFloaterAvatarPicker::processAvatarPickerReply);
    msg.setHandlerFunc("UserInfoReply", [](LLMessageSystem* message, void** data)
    {
        LLUUID agent; message->getUUID("AgentData", "AgentID", agent);
        if (agent == gAgentID) process_user_info_reply(message, data);
    });

    msg.setHandlerFunc("EconomyData", [](LLMessageSystem* message, void**)
    {
        const auto current = VSNativeSession::active();
        if (!current || current->phase() != Phase::Connected || current->host() != message->getSender()) return;
        if (!LLGridManager::instance().isInSecondLife())
            LLGlobalEconomy::processEconomyData(message, LLGlobalEconomy::getInstance());
    });
    vs_native_im_install(msg);
}
bool VSNativeSession::acceptLogin(const LLSD& response)
{
    reset();
    LLUUID agent(response["agent_id"].asString()), identity(response["session_id"].asString());
    const S32 port = response["sim_port"].asInteger();
    LLHost host(response["sim_ip"].asString(), port > 0 && port <= 65535 ? port : 0);
    const U32 circuit = U32(response["circuit_code"].asInteger());
    const std::string seed = response["seed_capability"].asString();
    if (agent.isNull() || identity.isNull() || !host.isOk() || !circuit || !webURL(seed)) return false;
    mWidth = response.has("region_size_x") ? U32(response["region_size_x"].asInteger()) : 256;
    mHeight = response.has("region_size_y") ? U32(response["region_size_y"].asInteger()) : 256;
    if (!mWidth || !mHeight || mWidth > 65536 || mHeight > 65536) return false;
    if (LLGridManager::instance().isInSecondLife())
    {
        LLSD benefitsResponse = response;
        if (!init_benefits(benefitsResponse))
        {
            LLAgentBenefitsMgr::resetAccountBenefits();
            LLSD payload; vs_native_im_stamp_notification(payload);
            LLNotificationsUtil::add("FailedToGetBenefits", LLSD(), payload);
            LL_WARNS("NativeSession") << "Login benefits are incomplete; unavailable account costs remain blocked" << LL_ENDL;
        }
    }
    mLastChannel = 0;
    mAgent = agent; mSession = identity; mHost = host; mCircuit = circuit; mSeed = seed;
    mHandle = to_region_handle(U32(response["region_x"].asInteger()), U32(response["region_y"].asInteger()));
    gAgentID = agent; gAgentSessionID = identity;
    gAgent.mSecureSessionID = response["secure_session_id"].asUUID();
    gAgent.mMOTD = response["message"].asString();
    gAgentUsername = response["first_name"].asString();
    LLStringUtil::replaceChar(gAgentUsername, '"', ' '); LLStringUtil::trim(gAgentUsername);
    std::string last = response["last_name"].asString();
    LLStringUtil::replaceChar(last, '"', ' '); LLStringUtil::trim(last);
    if (!last.empty() && last != "Resident") gAgentUsername += " " + last;
    LLUrlEntryBase::setAgentID(mAgent);
    LLUrlEntryParcel::setSessionID(mSession);
    if (response.has("agent_access_max"))
    {
        const auto access = response["agent_access_max"].asString();
        if (!access.empty()) gAgent.setMaturity(access[0]);
    }
    const auto preferred = response["agent_region_access"].asString();
    if (!preferred.empty()) gSavedSettings.setU32("PreferredMaturity", LLAgent::convertTextToMaturity(preferred[0]));
    if (response.has("help_url_format")) gSavedSettings.setString("HelpURLFormat", response["help_url_format"].asString());
    LLAvatarTracker::buddy_map_t buddies;
    const auto& buddyList = response["buddy-list"];
    for (auto iter = buddyList.beginArray(); iter != buddyList.endArray(); ++iter)
    {
        const auto& buddy = *iter;
        const auto id = buddy["buddy_id"].asUUID();
        if (id.notNull()) buddies.emplace(id, new LLRelationship(buddy["buddy_rights_given"].asInteger(), buddy["buddy_rights_has"].asInteger(), false));
    }
    LLAvatarTracker::instance().addBuddyList(buddies);
    auto* contacts = LGGContactSets::getInstance();
    contacts->loadFromDisk();
    LLAvatarNameCache::instance().setCustomNameCheckCallback(boost::bind(&LGGContactSets::checkCustomName, contacts, _1, _2, _3));
    gMessageSystem->mOurCircuitCode = circuit;
    // Retain CPU identity/origin, without agent init's camera/animation observers.
    gAgent.initOriginGlobal(from_region_handle(mHandle));
    LLLoginInstance::getInstance()->saveMFAHash(response);
    LLAppViewer::instance()->recordSessionToMarker();
    return true;
}
void VSNativeSession::begin()
{
    if (!mHost.isOk() || !gMessageSystem) { disconnect(LLTrans::getString("NativeSessionInvalidLogin")); return; }
    install(*static_cast<LLMessageSystem*>(gMessageSystem));
    mPhase = Phase::Connecting; mDeadline = now() + 60;
    gDisconnected = false; gAgentMovementCompleted = false;
    gMessageSystem->enableCircuit(mHost, true);
    LLStartUp::initNameCache();
    if (gCacheName) gCacheName->setUpstream(mHost);
    if (gAssetStorage) gAssetStorage->setUpstream(mHost);
    if (gXferManager) gXferManager->registerCallbacks(gMessageSystem);
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessage("UseCircuitCode"); msg->nextBlock("CircuitCode");
    msg->addU32("Code", mCircuit); msg->addUUID("SessionID", mSession); msg->addUUID("ID", mAgent);
    auto delivery = new CircuitDelivery{weak_from_this(), mGeneration, mRegionEpoch};
    msg->sendReliable(mHost, 3, false, F32Seconds(5), circuitCallback, reinterpret_cast<void**>(delivery));
    const auto owner = shared_from_this(); const U64 generation = mGeneration, epoch = mRegionEpoch;
    const std::string seed = mSeed;
    LLCoros::instance().launch("nativeSeedCapabilities", [owner, generation, epoch, seed]() { owner->seedCoro(seed, generation, epoch); });
    LLStartUp::setStartupState(STATE_WORLD_WAIT);
}
void VSNativeSession::circuitResult(U64 generation, S32 result, U64 regionEpoch)
{
    if (generation != mGeneration || (regionEpoch && regionEpoch != mRegionEpoch) || (mPhase != Phase::Connecting && !mChangingRegion)) { ++mExpired; return; }
    if (result) { disconnect(LLTrans::getString("NativeSessionConnectionTimeout")); return; }
    mCircuitAck = true;
    send("CompleteAgentMovement", LLSD());
    connected();
}
void VSNativeSession::seedCoro(std::string url, U64 generation, U64 regionEpoch)
{
    if (generation != mGeneration || regionEpoch != mRegionEpoch || (mPhase != Phase::Connecting && !mChangingRegion)) return;
    auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeSeedCapabilities", LLCore::HttpRequest::DEFAULT_POLICY_ID);
    const std::weak_ptr<LLCoreHttpUtil::HttpCoroutineAdapter> weak = adapter;
    mCancelSeed = [weak]() { if (auto request = weak.lock()) request->cancelSuspendedOperation(); };
    auto request = std::make_shared<LLCore::HttpRequest>();
    auto options = std::make_shared<LLCore::HttpOptions>(); options->setTransferTimeout(15); options->setRetries(0);
    LLSD wanted = LLSD::emptyArray();
    for (const char* name : {"EventQueueGet", "UntrustedSimulatorMessage", "GetDisplayNames", "ChatSessionRequest", "GetTexture", "AvatarPickerSearch",
         "GroupAPIv1", "GroupMemberData", "AgentProfile", "UploadAgentProfileImage", "NewFileAgentInventory", "InventoryThumbnailUpload", "SetDisplayName", "RemoteParcelRequest", "AcceptFriendship", "DeclineFriendship",
         "AcceptGroupInvite", "DeclineGroupInvite", "FetchInventory2", "FetchLib2",
         "FetchInventoryDescendents2", "FetchLibDescendents2", "InventoryAPIv3", "LibraryAPIv3", "CreateInventoryCategory", "ViewerAsset", "CopyInventoryFromNotecard", "UpdateAgentLanguage", "UpdateAgentInformation", "UpdateNotecardAgentInventory", "UpdateNotecardTaskInventory", "UpdateScriptAgent", "UserInfo", "AgentPreferences", "SearchStatRequest", "Tracking",
         "GetExperiences", "GetExperienceInfo", "GroupExperiences", "GetMetadata"}) wanted.append(name);
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        LLSD result = adapter->postAndSuspend(request, url, wanted, options);
        if (generation != mGeneration || (regionEpoch && regionEpoch != mRegionEpoch) || (mPhase != Phase::Connecting && !mChangingRegion)) { ++mExpired; return; }
        if (LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]))
        { mCancelSeed = {}; result.erase("http_result"); capabilities(result, generation, regionEpoch); return; }
    }
    disconnect(LLTrans::getString("NativeSessionConnectionTimeout"));
}
void VSNativeSession::capabilities(const LLSD& caps, U64 generation, U64 regionEpoch)
{
    if (generation != mGeneration || (regionEpoch && regionEpoch != mRegionEpoch) || (mPhase != Phase::Connecting && !mChangingRegion)) { ++mExpired; return; }
    if (!caps.isMap() || !webURL(caps["EventQueueGet"].asString())) { disconnect(LLTrans::getString("NativeSessionInvalidCapabilities")); return; }
    mCapabilities = LLSD::emptyMap();
    for (const char* name : {"EventQueueGet", "UntrustedSimulatorMessage", "GetDisplayNames", "ChatSessionRequest", "GetTexture", "AvatarPickerSearch",
         "GroupAPIv1", "GroupMemberData", "AgentProfile", "UploadAgentProfileImage", "NewFileAgentInventory", "InventoryThumbnailUpload", "SetDisplayName", "RemoteParcelRequest", "AcceptFriendship", "DeclineFriendship",
         "AcceptGroupInvite", "DeclineGroupInvite", "FetchInventory2", "FetchLib2",
         "FetchInventoryDescendents2", "FetchLibDescendents2", "InventoryAPIv3", "LibraryAPIv3", "CreateInventoryCategory", "ViewerAsset", "CopyInventoryFromNotecard", "UpdateAgentLanguage", "UpdateAgentInformation", "UpdateNotecardAgentInventory", "UpdateNotecardTaskInventory", "UpdateScriptAgent", "UserInfo", "AgentPreferences", "SearchStatRequest", "Tracking",
         "GetExperiences", "GetExperienceInfo", "GroupExperiences", "GetMetadata"})
        if (webURL(caps[name].asString())) mCapabilities[name] = caps[name];
    // LLSD messages are sent through the host's granted HTTP transport.
    // LLAgent, cache-name and asset services retain copies of this host.
    mHost.setUntrustedSimulatorCap(capability("UntrustedSimulatorMessage"));
    if (gCacheName) gCacheName->setUpstream(mHost);
    if (gAssetStorage) gAssetStorage->setUpstream(mHost);
    LLAvatarNameCache::getInstance()->setNameLookupURL(capability("GetDisplayNames"));
    mPoll = std::make_unique<LLEventPoll>(capability("EventQueueGet"), mHost);
    LLExperienceCache::instance().setCapabilityQuery([](const std::string& name) { return gAgent.getRegionCapability(name); });
    mSeedReady = true;
    connected();
}
std::string VSNativeSession::capability(const std::string& name) const { return mCapabilities[name].asString(); }
std::string VSNativeSession::locationURL() const
{
    return mName.empty() ? std::string() : LLSLURL(mName, mPosition).getSLURLString();
}
bool VSNativeSession::resolveLocation(const LLVector3d& global, LocationCallback callback)
{
    if (mPhase != Phase::Connected || mChangingRegion || !callback || !global.isFinite() ||
        global.mdV[VX] < 0 || global.mdV[VY] < 0 || global.mdV[VX] >= 16777216 || global.mdV[VY] >= 16777216) return false;
    const auto origin = from_region_handle(mHandle);
    if (!mName.empty() && global.mdV[VX] >= origin.mdV[VX] && global.mdV[VX] < origin.mdV[VX] + mWidth &&
        global.mdV[VY] >= origin.mdV[VY] && global.mdV[VY] < origin.mdV[VY] + mHeight)
    {
        callback(mName, LLVector3(global - origin)); return true;
    }
    if (!gMessageSystem || mLocationRequests.size() >= 32) return false;
    const U16 x = U16(global.mdV[VX] / 256), y = U16(global.mdV[VY] / 256);
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("MapBlockRequest"); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession);
    msg->addU32("Flags", 0x10000); msg->addU32("EstateID", 0); msg->addBOOL("Godlike", false);
    msg->nextBlock("PositionData"); msg->addU16("MinX", x); msg->addU16("MaxX", x); msg->addU16("MinY", y); msg->addU16("MaxY", y);
    if (msg->sendReliable(mHost) <= 0) return false;
    mLocationRequests.push_back({global, std::move(callback), now() + 30}); ++mSent; return true;
}
bool VSNativeSession::teleportRequest(U64 handle, const LLVector3& pos)
{
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || !pos.isFinite() || pos.mV[VX] < 0 || pos.mV[VY] < 0 || pos.mV[VX] > 65536 || pos.mV[VY] > 65536) return false;
    typing(false); mRequestedRegion.clear();
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessage("TeleportLocationRequest"); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession);
    msg->nextBlock("Info"); msg->addU64("RegionHandle", handle); msg->addVector3("Position", pos); msg->addVector3("LookAt", LLVector3::x_axis);
    if (msg->sendReliable(mHost) <= 0) return false;
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::teleportToLocation(const LLVector3d& global)
{
    if (!global.isFinite() || global.mdV[VX] < 0 || global.mdV[VY] < 0 || global.mdV[VX] >= std::numeric_limits<U32>::max() || global.mdV[VY] >= std::numeric_limits<U32>::max()) return false;
    const auto origin = from_region_handle(mHandle);
    const bool local = global.mdV[VX] >= origin.mdV[VX] && global.mdV[VX] < origin.mdV[VX] + mWidth && global.mdV[VY] >= origin.mdV[VY] && global.mdV[VY] < origin.mdV[VY] + mHeight;
    const U64 handle = local ? mHandle : to_region_handle(global);
    const auto destinationOrigin = from_region_handle(handle);
    return teleportRequest(handle, LLVector3(global - destinationOrigin));
}
bool VSNativeSession::teleportToRegion(const std::string& input, const LLVector3& pos)
{
    std::string region = input; LLStringUtil::trim(region);
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || region.empty() || region.size() > 255 || !pos.isFinite()) return false;
    for (unsigned char c : region) if (c < 32 || c == 127) return false;
    if (LLStringUtil::compareInsensitive(region, mName) == 0) return teleportRequest(mHandle, pos);
    mRequestedRegion = region; mRequestedPosition = pos;
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("MapNameRequest"); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession); msg->addU32("Flags", 0); msg->addU32("EstateID", 0); msg->addBOOL("Godlike", false);
    msg->nextBlock("NameData"); msg->addString("Name", region);
    if (msg->sendReliable(mHost) <= 0) { mRequestedRegion.clear(); return false; }
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::teleportToLandmark(const LLUUID& asset)
{
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem) return false;
    typing(false); mRequestedRegion.clear();
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("TeleportLandmarkRequest"); msg->nextBlock("Info");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession); msg->addUUID("LandmarkID", asset);
    if (msg->sendReliable(mHost) <= 0) return false;
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::teleportToLure(const LLUUID& lure, bool godlike)
{
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || lure.isNull()) return false;
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("TeleportLureRequest"); msg->nextBlock("Info");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession); msg->addUUID("LureID", lure);
    msg->addU32("TeleportFlags", godlike ? TELEPORT_FLAGS_VIA_GODLIKE_LURE : TELEPORT_FLAGS_VIA_LURE);
    if (msg->sendReliable(mHost) <= 0) return false;
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::changeRegion(const LLSD& body)
{
    const S32 port = body["port"].asInteger(); const U32 ip = U32(body["ip"].asInteger());
    const LLHost destination(ip, port > 0 && port <= 65535 ? port : 0);
    const std::string seed = body["seed"].asString();
    if (!destination.isOk() || !webURL(seed) || mChangingRegion ||
        (body.has("width") && (body["width"].asInteger() <= 0 || body["width"].asInteger() > 65536)) ||
        (body.has("height") && (body["height"].asInteger() <= 0 || body["height"].asInteger() > 65536))) return false;
    // Only an authenticated current simulator can grant the next host. Inbound
    // packets and capability/circuit completions from the retired region expire.
    mLocationRequests.clear();
    ++mRegionEpoch; mPreviousHost = mHost; mHost = destination;
    mHandle = ll_U64_from_sd(body["handle"]); mSeed = seed; mName.clear();
    mWidth = body.has("width") ? U32(body["width"].asInteger()) : 256; mHeight = body.has("height") ? U32(body["height"].asInteger()) : 256;
    mChangingRegion = true; mNavigationDeadline = now() + 60;
    mHandshake = mMovement = mCircuitAck = mSeedReady = false;
    auto cancel = std::move(mCancelSeed); if (cancel) cancel();
    mPoll.reset(); mCapabilities = LLSD::emptyMap();
    gAgent.initOriginGlobal(from_region_handle(mHandle));
    if (gCacheName) gCacheName->setUpstream(mHost);
    if (gAssetStorage) gAssetStorage->setUpstream(mHost);
    gMessageSystem->enableCircuit(mHost, true);
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("UseCircuitCode"); msg->nextBlock("CircuitCode");
    msg->addU32("Code", mCircuit); msg->addUUID("SessionID", mSession); msg->addUUID("ID", mAgent);
    auto delivery = new CircuitDelivery{weak_from_this(), mGeneration, mRegionEpoch};
    msg->sendReliable(mHost, 3, false, F32Seconds(5), circuitCallback, reinterpret_cast<void**>(delivery));
    const auto owner = shared_from_this(); const U64 generation = mGeneration, epoch = mRegionEpoch;
    LLCoros::instance().launch("nativeRegionCapabilities", [owner, generation, epoch, seed]() { owner->seedCoro(seed, generation, epoch); });
    return true;
}

bool VSNativeSession::admit(const std::string& name, const LLHost& sender)
{
    if (transport.count(name)) return mHost.isOk() && sender == mHost && mPhase != Phase::Disconnected;
    if ((mPhase == Phase::Connecting || mPhase == Phase::Connected || mPhase == Phase::Logout)
        && sender == mHost && session.count(name)) return true;
    // Consume after transport decoding/ACK; do not invoke a scene or HTTP node.
    if (++mRejected <= 8) LL_WARNS("NativeSession") << "Deferred native message: " << name << LL_ENDL;
    return false;
}
void VSNativeSession::receive(LLMessageSystem* msg)
{
    const std::string name = msg->getMessageName();
    LLSD body = LLSD::emptyMap();
    if (name == "RegionIDAndHandleReply")
    {
        LLUUID id; U64 handle; msg->getUUID("ReplyBlock", "RegionID", id); msg->getU64("ReplyBlock", "RegionHandle", handle);
        body["id"] = id; body["handle"] = ll_sd_from_U64(handle);
    }
    else if (name == "MapBlockReply")
    {
        LLUUID agent; msg->getUUID("AgentData", "AgentID", agent); body["agent"] = agent;
        body["regions"] = LLSD::emptyArray();
        for (S32 i = 0; i < msg->getNumberOfBlocks("Data"); ++i)
        {
            std::string region; U16 x, y; U8 access;
            msg->getString("Data", "Name", region, i); msg->getU16("Data", "X", x, i); msg->getU16("Data", "Y", y, i);
            msg->getU8("Data", "Access", access, i);
            LLSD row; row["name"] = region; row["handle"] = ll_sd_from_U64(to_region_handle(U32(x) * 256, U32(y) * 256)); row["access"] = access;
            U16 width = 256, height = 256;
            if (i < msg->getNumberOfBlocks("Size")) { msg->getU16("Size", "SizeX", width, i); msg->getU16("Size", "SizeY", height, i); }
            row["width"] = width ? width : 256; row["height"] = height ? height : 256;
            body["regions"].append(row);
        }
    }
    else if (name == "TeleportFinish" || name == "CrossedRegion")
    {
        const char* block = name == "TeleportFinish" ? "Info" : "RegionData";
        LLUUID agent; U32 ip; U16 port; U64 handle; std::string seed;
        msg->getUUID(name == "TeleportFinish" ? "Info" : "AgentData", "AgentID", agent);
        msg->getIPAddr(block, "SimIP", ip); msg->getIPPort(block, "SimPort", port);
        msg->getU64(block, "RegionHandle", handle); msg->getString(block, "SeedCapability", seed);
        body["agent"] = agent; body["ip"] = LLSD::Integer(ip); body["port"] = port; body["handle"] = ll_sd_from_U64(handle); body["seed"] = seed;
        if (msg->getSize(block, "RegionSizeX") > 0)
        { U32 width, height; msg->getU32(block, "RegionSizeX", width); msg->getU32(block, "RegionSizeY", height); body["width"] = LLSD::Integer(width); body["height"] = LLSD::Integer(height); }
        if (name == "CrossedRegion") { LLUUID identity; msg->getUUID("AgentData", "SessionID", identity); body["session"] = identity; }
    }
    else if (name == "TeleportLocal")
    { LLUUID agent; LLVector3 pos; msg->getUUID("Info", "AgentID", agent); msg->getVector3("Info", "Position", pos); body["agent"] = agent; body["position"] = ll_sd_from_vector3(pos); }
    else if (name == "TeleportFailed")
    { LLUUID agent; std::string reason; msg->getUUID("Info", "AgentID", agent); msg->getString("Info", "Reason", reason); body["agent"] = agent; body["text"] = reason; }
    else if (name == "RegionHandshake")
    {
        U32 flags; U8 access; LLUUID id, owner; std::string region;
        msg->getU32("RegionInfo", "RegionFlags", flags); msg->getU8("RegionInfo", "SimAccess", access);
        msg->getString("RegionInfo", "SimName", region); msg->getUUID("RegionInfo", "SimOwner", owner);
        msg->getUUID("RegionInfo2", "RegionID", id);
        U64 extended = flags;
        if (msg->getNumberOfBlocks("RegionInfo4") > 0) msg->getU64("RegionInfo4", "RegionFlagsExtended", extended);
        body["flags64"] = ll_sd_from_U64(extended);
        body["name"] = region; body["id"] = id; body["owner"] = owner; body["flags"] = LLSD::Integer(flags); body["access"] = access;
    }
    else if (name == "AgentMovementComplete")
    {
        LLUUID agent, identity; U64 handle; LLVector3 pos;
        msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", identity);
        msg->getU64("Data", "RegionHandle", handle); msg->getVector3("Data", "Position", pos);
        body["agent"] = agent; body["session"] = identity; body["handle"] = ll_sd_from_U64(handle); body["position"] = ll_sd_from_vector3(pos);
    }
    else if (name == "MoneyBalanceReply")
    {
        LLUUID agent; S32 balance;
        msg->getUUID("MoneyData", "AgentID", agent); msg->getS32("MoneyData", "MoneyBalance", balance);
        body["agent"] = agent; body["balance"] = balance;
        S32 credit, committed;
        msg->getS32("MoneyData", "SquareMetersCredit", credit);
        msg->getS32("MoneyData", "SquareMetersCommitted", committed);
        body["land_credit"] = credit; body["land_committed"] = committed;
    }
    else if (name == "ChatFromSimulator")
    {
        std::string text, from; LLUUID source, owner; U8 type, kind, audible; LLVector3 position;
        msg->getString("ChatData", "Message", text); msg->getString("ChatData", "FromName", from);
        msg->getUUID("ChatData", "SourceID", source); msg->getUUID("ChatData", "OwnerID", owner);
        msg->getU8("ChatData", "ChatType", type); msg->getU8("ChatData", "SourceType", kind); msg->getU8("ChatData", "Audible", audible);
        body["text"] = text; body["from"] = from; body["source"] = source; body["owner"] = owner;
        msg->getVector3("ChatData", "Position", position);
        body["type"] = type; body["kind"] = kind; body["audible"] = audible;
        body["position"] = ll_sd_from_vector3(position);
    }
    else if (name == "LogoutReply")
    {
        LLUUID agent, identity; msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", identity);
        body["agent"] = agent; body["session"] = identity;
    }
    else if (name == "KickUser")
    {
        LLUUID agent, identity; std::string reason;
        msg->getUUID("UserInfo", "AgentID", agent); msg->getUUID("UserInfo", "SessionID", identity); msg->getString("UserInfo", "Reason", reason);
        body["agent"] = agent; body["session"] = identity; body["text"] = reason;
    }
    else if (name == "AlertMessage" || name == "AgentAlertMessage")
    { std::string text; msg->getString("AlertData", "Message", text); body["text"] = text; }
    else if (name == "ViewerFrozenMessage")
    { bool frozen; msg->getBOOL("FrozenData", "Data", frozen); body["frozen"] = frozen; }
    else if (name == "FeatureDisabled")
    {
        LLUUID agent; std::string text;
        msg->getUUID("FailureInfo", "AgentID", agent); msg->getString("FailureInfo", "ErrorMessage", text);
        body["agent"] = agent; body["text"] = text;
    }
    else if (name == "Error")
    { std::string text; msg->getString("Data", "Message", text); body["text"] = text; }
    deliver(name, body, msg->getSender(), mGeneration);
}
bool VSNativeSession::deliver(const std::string& name, const LLSD& body, const LLHost& sender, U64 generation)
{
    if (generation != mGeneration) { ++mExpired; return false; }
    if (!body.isMap() || !admit(name, sender)) return false;
    if (name == "RegionHandshake" && (mPhase == Phase::Connecting || mPhase == Phase::Connected))
    {
        if (!body["name"].isString() || body["id"].asUUID().isNull()) return false;
        mName = body["name"].asString().substr(0, 256); mRegionID = body["id"].asUUID(); mOwner = body["owner"].asUUID();
        mFlags = body.has("flags64") ? ll_U64_from_sd(body["flags64"]) : U32(body["flags"].asInteger()); mAccess = U8(body["access"].asInteger());
        LLLandmark::setRegionHandle(mRegionID, mHandle);
        mHandshake = true; send("RegionHandshakeReply", LLSD()); connected();
    }
    else if (name == "AgentMovementComplete" && (mPhase == Phase::Connecting || mPhase == Phase::Connected))
    {
        if (body["agent"].asUUID() != mAgent || body["session"].asUUID() != mSession) return false;
        if (ll_U64_from_sd(body["handle"]) != mHandle) return false;
        if (!body["position"].isArray() || body["position"].size() != 3) return false;
        LLVector3 pos = ll_vector3_from_sd(body["position"]); if (!pos.isFinite()) return false;
        mPosition = pos; gAgent.setPositionAgent(pos); mMovement = true; connected();
    }
    else if (name == "MoneyBalanceReply" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent) return false;
        updateBalance(body["balance"].asInteger());
        mLandCredit = body["land_credit"].asInteger(); mLandCommitted = body["land_committed"].asInteger();
    }
    else if (name == "ChatFromSimulator" && mPhase == Phase::Connected) appendChat(body, generation);
    else if (name == "LogoutReply" && mPhase == Phase::Logout)
    { if (body["agent"].asUUID() != mAgent || body["session"].asUUID() != mSession) return false; finishLogout(); }
    else if (name == "KickUser")
    { if (body["agent"].asUUID() != mAgent || body["session"].asUUID() != mSession) return false; disconnect(body["text"].asString()); }
    else if (name == "ViewerFrozenMessage")
    {
        if (!body["frozen"].isBoolean()) return false;
        mFrozen = body["frozen"].asBoolean(); gViewerWindow->getWindow()->resetBusyCount();
    }
    else if (name == "FeatureDisabled")
    {
        if (body["agent"].asUUID() != mAgent || !body["text"].isString()) return false;
        LL_WARNS("NativeSession") << "Simulator feature unavailable: " << body["text"].asString().substr(0, 4096) << LL_ENDL;
    }
    else if (name == "AlertMessage" || name == "AgentAlertMessage" || name == "Error")
    { if (!body["text"].isString()) return false; LLSD args; args["ERROR_MESSAGE"] = body["text"].asString().substr(0, 4096); LLNotificationsUtil::add("ErrorMessage", args); }
    else if (name == "RegionIDAndHandleReply" && mPhase == Phase::Connected && !mChangingRegion)
    {
        if (body["id"].asUUID().isNull() || !body.has("handle") ||
            !LLLandmark::hasPendingRegionHandle(body["id"].asUUID())) return false;
        LLLandmark::receiveRegionHandle(body["id"].asUUID(), ll_U64_from_sd(body["handle"]));
    }
    else if (name == "MapBlockReply" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent) return false;
        for (auto iter = body["regions"].beginArray(); iter != body["regions"].endArray(); ++iter)
        {
            const auto& region = *iter;
            if (region["access"].asInteger() != 255 && !region["name"].asString().empty())
            {
                const auto origin = from_region_handle(ll_U64_from_sd(region["handle"]));
                const S32 width = region.has("width") ? region["width"].asInteger() : 256;
                const S32 height = region.has("height") ? region["height"].asInteger() : 256;
                std::vector<LocationRequest> resolved;
                for (auto request = mLocationRequests.begin(); request != mLocationRequests.end();)
                {
                    const auto& pos = request->position;
                    if (width > 0 && width <= 65536 && height > 0 && height <= 65536 && pos.mdV[VX] >= origin.mdV[VX] && pos.mdV[VX] < origin.mdV[VX] + width && pos.mdV[VY] >= origin.mdV[VY] && pos.mdV[VY] < origin.mdV[VY] + height)
                    { resolved.push_back(std::move(*request)); request = mLocationRequests.erase(request); }
                    else ++request;
                }
                const U64 generation = mGeneration;
                for (const auto& request : resolved)
                    if (mGeneration == generation && mPhase == Phase::Connected) request.callback(region["name"].asString(), LLVector3(request.position - origin));
            }
            if (LLStringUtil::compareInsensitive(region["name"].asString(), mRequestedRegion) == 0 && region["access"].asInteger() != 255)
            {
                const auto pos = mRequestedPosition; mRequestedRegion.clear();
                return teleportRequest(ll_U64_from_sd(region["handle"]), pos);
            }
        }
    }
    else if ((name == "TeleportFinish" || name == "CrossedRegion") && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent || (name == "CrossedRegion" && body["session"].asUUID() != mSession)) return false;
        return changeRegion(body);
    }
    else if (name == "TeleportLocal" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent || !body["position"].isArray() || body["position"].size() != 3) return false;
        const auto pos = ll_vector3_from_sd(body["position"]); if (!pos.isFinite()) return false;
        mPosition = pos; gAgent.setPositionAgent(pos); mNavigationDeadline = 0;
        LLTeleportHistory::instance().updateNativeLocation(gAgent.getPositionGlobal());
    }
    else if (name == "TeleportFailed" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent) return false;
        mRequestedRegion.clear(); mNavigationDeadline = 0;
        LLSD args; args["ERROR_MESSAGE"] = body["text"].asString().substr(0, 4096); LLNotificationsUtil::add("ErrorMessage", args);
    }
    else if (name == "TeleportStart" || name == "TeleportProgress") { /* Keep CPU account UI and IM ownership alive during travel. */ }
    else if (name == "CloseCircuit" || name == "DisableSimulator")
        disconnect(LLTrans::getString("NativeSessionConnectionTimeout"));
    else return false;
    ++mReceived; return true;
}
void VSNativeSession::connected()
{
    if ((mPhase != Phase::Connecting && !mChangingRegion) || !mHandshake || !mMovement || !mSeedReady || !mCircuitAck) return;
    if (mChangingRegion)
    {
        mChangingRegion = false; mNavigationDeadline = 0; mDeadline = now() + 1;
        if (gMessageSystem && mPreviousHost.isOk() && mPreviousHost != mHost)
        { gMessageSystem->disableCircuit(mPreviousHost); gMessageSystem->mCircuitInfo.removeCircuitData(mPreviousHost); }
        mPreviousHost = LLHost();
        LLTeleportHistory::instance().updateNativeLocation(gAgent.getPositionGlobal());
        return;
    }
    mPhase = Phase::Connected; mDeadline = now() + 1;
    gAgentMovementCompleted = true;
    LLTeleportHistory::instance().updateNativeLocation(gAgent.getPositionGlobal());
    LLStartUp::setStartupState(STATE_STARTED);
    LLAppViewer::instance()->handleLoginComplete();
    FSPanelLogin::closePanel();
    gViewerWindow->setNativeConnected(true);
    gViewerWindow->getProgressView()->setVisible(false);
    init_audio();
    if (!LLGridManager::instance().isInSecondLife())
    {
        gMessageSystem->newMessage("EconomyDataRequest");
        gMessageSystem->sendReliable(mHost);
    }
    mChat = gViewerWindow->nativeChat();
    if (mChat)
    {
        mChat->clear(); mChat->setVisible(true);
        const std::weak_ptr<VSNativeSession> owner = shared_from_this(); const U64 generation = mGeneration;
        mChat->setSender([owner, generation](const std::string& text)
        { auto session = owner.lock(); return session && session->generation() == generation && session->sendChat(text); });
        mChat->setSession(mName, [owner, generation]()
        { if (auto session = owner.lock(); session && session->generation() == generation) session->requestLogout(false); });
        mChat->input()->setKeystrokeCallback([owner, generation](LLLineEditor* editor, void*)
        { if (auto session = owner.lock(); session && session->generation() == generation) session->typing(!editor->getText().empty()); }, nullptr);
        gFocusMgr.setKeyboardFocus(mChat->input());
        if (gSavedPerAccountSettings.getBOOL("LogShowHistory"))
        {
            std::list<LLSD> history; LLLogChat::loadChatHistory("chat", history);
            for (const auto& entry : history) mChat->append(entry["from"].asString() + ": " + entry["message"].asString());
        }
        // Ordinary sessions use the same skinned nearby-chat floater as OpenGL.
        // The protocol fixture retains its private transcript for its pixel oracle
        // and separately exercises the shared frontend before accepting the run.
        bool shared_frontend = true;
#if VS_VULKAN_DIAGNOSTICS
        shared_frontend = !std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY");
#endif
        if (shared_frontend && !mChat->useSharedFrontend(true))
            throw std::runtime_error("Native nearby-chat frontend construction failed");
    }
    LLMuteList::getInstance()->requestFromServer(mAgent);
    gAgent.sendAgentDataUpdateRequest();
    send("MoneyBalanceRequest", LLSD());
    LLAgentLanguage::update();
    LL_INFOS("NativeSession") << "Native CPU region ready: " << mName << LL_ENDL;
}
void VSNativeSession::appendChat(LLSD chat, U64 generation)
{
    if (!chat["text"].isString() || !chat["from"].isString() || chat["text"].asString().size() > 4096) return;
    const auto source = chat["source"].asUUID(), owner = chat["owner"].asUUID();
    const auto kind = chat["kind"].asInteger(), type = chat["type"].asInteger();
    if (kind < CHAT_SOURCE_SYSTEM || kind > CHAT_SOURCE_OBJECT || type < CHAT_TYPE_WHISPER || type > CHAT_TYPE_OWNER || type == CHAT_TYPE_START || type == CHAT_TYPE_STOP) return;
    if (chat["audible"].asInteger() != CHAT_AUDIBLE_FULLY) return;
    if (LLMuteList::getInstance()->isMuted(source, chat["from"].asString(), LLMute::flagTextChat)
        || LLMuteList::getInstance()->isMuted(owner, LLMute::flagTextChat)) return;
    if (gSavedSettings.getBOOL("UseAntiSpam") && NACLAntiSpamRegistry::instance().checkQueue(
        ANTISPAM_QUEUE_CHAT, source, kind == CHAT_SOURCE_OBJECT ? ANTISPAM_SOURCE_OBJECT : ANTISPAM_SOURCE_AGENT)) return;
    const std::weak_ptr<VSNativeSession> weak = shared_from_this();
    if (kind == CHAT_SOURCE_AGENT && source.notNull())
    {
        LLAvatarName name;
        if (auto found = mNames.find(source); found != mNames.end()) chat["from"] = found->second;
        else if (LLAvatarNameCache::get(source, &name)) chat["from"] = name.getCompleteName();
        else if (mNameConnections.size() < 1024 && mPendingNames.insert(source).second)
        {
            // Keep immediate simulator-name delivery ordered. Resolved names are
            // used by subsequent messages, and never publish into a later login.
            mNameConnections.emplace_back(LLAvatarNameCache::get(source,
                [weak, generation](const LLUUID& id, const LLAvatarName& resolved)
                {
                    if (auto session = weak.lock())
                    {
                        if (session->generation() != generation || session->phase() != Phase::Connected) { ++session->mExpired; return; }
                        session->mNames[id] = resolved.getCompleteName(); session->mPendingNames.erase(id);
                    }
                }));
        }
    }
    if (gSavedSettings.getBOOL("TranslateChat") && LLTranslate::isTranslationConfigured() && source != mAgent)
    {
        const auto original = chat["text"].asString();
        LLTranslate::translateMessage("", LLTranslate::getTranslateLanguage(), original,
            [weak, generation, chat, original](std::string translated, std::string)
            { if (auto session = weak.lock()) session->publishChat(chat, original + " (" + translated + ")", generation); },
            [weak, generation, chat, original](int, std::string)
            { if (auto session = weak.lock()) session->publishChat(chat, original, generation); });
    }
    else publishChat(chat, chat["text"].asString(), generation);
}
void VSNativeSession::publishChat(const LLSD& chat, const std::string& text, U64 generation)
{
    if (generation != mGeneration || mPhase != Phase::Connected || !mChat) { ++mExpired; return; }
    LLChat message;
    message.mFromName = chat["from"].asString(); message.mFromID = chat["source"].asUUID();
    message.mOwnerID = chat["owner"].asUUID(); message.mText = text;
    message.mSourceType = static_cast<EChatSourceType>(chat["kind"].asInteger());
    message.mChatType = static_cast<EChatType>(chat["type"].asInteger());
    if (chat["position"].isArray() && chat["position"].size() == 3)
    {
        const auto position = ll_vector3_from_sd(chat["position"]);
        if (position.isFinite()) message.mPosAgent = position;
    }
    mChat->appendChat(message);
    FSConsoleUtils::ProcessChatMessage(message, LLSD());
    if (gSavedPerAccountSettings.getBOOL("LogNearbyChat")) LLLogChat::saveHistory("chat", chat["from"].asString(), chat["source"].asUUID(), text);
}
bool VSNativeSession::sendChat(const std::string& input, U8 type, S32 channel)
{
    if (mPhase != Phase::Connected || !gMessageSystem || input.empty() || input.size() > 1023 || type > CHAT_TYPE_SHOUT) return false;
    std::string text = input;
    if (text.compare(0, 9, "/whisper ") == 0) { type = CHAT_TYPE_WHISPER; text.erase(0, 9); }
    else if (text.compare(0, 7, "/shout ") == 0) { type = CHAT_TYPE_SHOUT; text.erase(0, 7); }
    if (text.compare(0, 2, "//") == 0) { channel = mLastChannel; text.erase(0, 2); }
    else if (text.size() > 1 && text[0] == '/' && (std::isdigit(static_cast<unsigned char>(text[1])) || text[1] == '-'))
    {
        S32 parsed = 0;
        auto result = std::from_chars(text.data() + 1, text.data() + text.size(), parsed);
        if (result.ec != std::errc() || (result.ptr != text.data() + text.size() && *result.ptr != ' ')) return false;
        channel = parsed; mLastChannel = parsed;
        text.erase(0, result.ptr - text.data());
        LLStringUtil::trimHead(text);
    }
    return sendChatProcessed(text, type, channel);
}
bool VSNativeSession::sendChatProcessed(const std::string& text, U8 type, S32 channel)
{
    // Skinned FS chat bars already process channel/volume/pose syntax. Never
    // reinterpret their resulting literal text as a second command here.
    if (mPhase != Phase::Connected || !gMessageSystem || text.empty() || text.size() > 1023 || type > CHAT_TYPE_SHOUT)
        return false;
    LLSD data; data["text"] = text; data["type"] = type; data["channel"] = channel;
    typing(false);
    return send(channel >= 0 ? "ChatFromViewer" : "ScriptDialogReply", data);
}
void VSNativeSession::typing(bool active)
{
    if (mPhase != Phase::Connected) return;
    mTypingDeadline = now() + 5;
    if (active == mTyping) return;
    LLSD data; data["text"] = ""; data["type"] = active ? CHAT_TYPE_START : CHAT_TYPE_STOP; data["channel"] = 0;
    if (send("ChatFromViewer", data)) mTyping = active;
}
bool VSNativeSession::send(const std::string& name, const LLSD& data)
{
    if (!gMessageSystem || !mHost.isOk()) return false;
    LLMessageSystem* msg = gMessageSystem; msg->newMessage(name.c_str()); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession);
    if (name == "RegionHandshakeReply")
    { msg->nextBlock("RegionInfo"); msg->addU32("Flags", 0); } // No cache, appearance or mesh promises.
    else if (name == "CompleteAgentMovement") msg->addU32("CircuitCode", mCircuit);
    else if (name == "MoneyBalanceRequest")
    { msg->nextBlock("MoneyData"); msg->addUUID("TransactionID", LLUUID::null); }
    else if (name == "ChatFromViewer")
    { msg->nextBlock("ChatData"); msg->addString("Message", data["text"].asString()); msg->addU8("Type", U8(data["type"].asInteger())); msg->addS32("Channel", data["channel"].asInteger()); }
    else if (name == "ScriptDialogReply")
    {
        msg->nextBlock("Data"); msg->addUUID("ObjectID", mAgent);
        msg->addS32("ChatChannel", data["channel"].asInteger()); msg->addS32("ButtonIndex", 0);
        msg->addString("ButtonLabel", data["text"].asString());
    }
    else if (name == "AgentUpdate")
    {
        msg->addQuat("BodyRotation", LLQuaternion()); msg->addQuat("HeadRotation", LLQuaternion()); msg->addU8("State", 0);
        msg->addVector3("CameraCenter", mPosition); msg->addVector3("CameraAtAxis", LLVector3::x_axis);
        msg->addVector3("CameraLeftAxis", LLVector3::y_axis); msg->addVector3("CameraUpAxis", LLVector3::z_axis);
        msg->addF32("Far", 16.f); msg->addU32("ControlFlags", 0); msg->addU8("Flags", 0);
    }
    if (msg->sendReliable(mHost) <= 0) return false;
    ++mSent; return true;
}
void VSNativeSession::tick()
{
    // Do not let late packets revive a retired connected circuit before the
    // session notices its loss. Login/connecting still have their own deadline.
    mLocationRequests.erase(std::remove_if(mLocationRequests.begin(), mLocationRequests.end(), [](const LocationRequest& request) { return now() >= request.deadline; }), mLocationRequests.end());
    if (mPhase == Phase::Connected && gMessageSystem && !gMessageSystem->checkCircuitAlive(mHost))
    { disconnect(LLTrans::getString("NativeSessionConnectionTimeout")); return; }
    if (gMessageSystem)
    {
        gMessageSystem->resetReceiveCounts(); LockMessageChecker checker(gMessageSystem);
        for (unsigned i = 0; i < 100 && checker.checkMessages(gFrameCount); ++i) {}
        checker.processAcks();
    }
    if (mPhase == Phase::Connecting && now() > mDeadline) disconnect(LLTrans::getString("NativeSessionConnectionTimeout"));
    else if (mPhase == Phase::Logout && now() > mDeadline) finishLogout();
    else if (mPhase == Phase::Connected && gMessageSystem && !gMessageSystem->checkCircuitAlive(mHost))
        disconnect(LLTrans::getString("NativeSessionConnectionTimeout"));
    else if (mPhase == Phase::Connected && !mChangingRegion && now() > mDeadline)
    { send("AgentUpdate", LLSD()); mDeadline = now() + 1; }
    if (mNavigationDeadline && now() > mNavigationDeadline)
    {
        mNavigationDeadline = 0; mRequestedRegion.clear();
        if (mChangingRegion) { disconnect(LLTrans::getString("NativeSessionConnectionTimeout")); return; }
        LLSD args; args["ERROR_MESSAGE"] = LLTrans::getString("NativeSessionConnectionTimeout"); LLNotificationsUtil::add("ErrorMessage", args);
    }
    if (mTyping && now() > mTypingDeadline) typing(false);
    if (mPhase == Phase::Connecting || mPhase == Phase::Connected)
    {
        // Do not dispatch legacy/cache lookups before the seed grant. A failed
        // pre-cap request otherwise remains pending for five minutes even once
        // the proper directory/UntrustedSimulatorMessage endpoints arrive.
        if (mSeedReady)
        {
            if (gCacheName) gCacheName->processPending();
            if (LLAvatarNameCache::instanceExists()) LLAvatarNameCache::getInstance()->idle();
        }
        if (gXferManager) gXferManager->retransmitUnackedPackets();
    }
    if (mPhase == Phase::Connected && LLMuteList::instanceExists())
    {
        LLMuteList::getInstance()->updateLoadState();
        if (LLMuteList::getInstance()->isLoadedFromServer()) vs_native_im_request_offline();
        LLAvatarTracker::instance().idleNotifyObservers();
        vs_pump_ui_images();
        if (LLInventoryModelBackgroundFetch::instanceExists()) LLInventoryModelBackgroundFetch::instance().pumpAccountFetch();
    }
}
void VSNativeSession::unbind()
{
    if (gViewerWindow) gViewerWindow->setNativeConnected(false);
    if (mChat) { mChat->input()->setKeystrokeCallback({}, nullptr); mChat->clear(); mChat->setVisible(false); mChat = nullptr; }
}
void VSNativeSession::requestLogout(bool quit)
{
    mQuit = mQuit || quit;
    if (mPhase == Phase::Logout) return;
    if (mPhase == Phase::Connected || mPhase == Phase::Connecting)
    {
        typing(false);
        unbind(); mPoll.reset(); send("LogoutRequest", LLSD()); mPhase = Phase::Logout; mDeadline = now() + 5;
    }
    else if (mQuit) LLAppViewer::instance()->forceQuit();
}
void VSNativeSession::finishLogout()
{
    const bool quit = mQuit; reset();
    if (quit) LLAppViewer::instance()->forceQuit(); else reset_login();
}
void VSNativeSession::disconnect(const std::string& reason)
{
    if (mPhase == Phase::Disconnected) return;
    if ((mPhase == Phase::Connecting || mPhase == Phase::Connected) && gMessageSystem) send("LogoutRequest", LLSD());
    reset(); mPhase = Phase::Disconnected;
    LLSD args; args["ERROR_MESSAGE"] = reason.substr(0, 4096);
    const std::weak_ptr<VSNativeSession> owner = weak_from_this(); const U64 generation = mGeneration;
    LLNotificationsUtil::add("ErrorMessage", args, LLSD(), [owner, generation](const LLSD&, const LLSD&)
    { if (auto session = owner.lock(); session && session->generation() == generation) reset_login(); return false; });
}
void VSNativeSession::reset()
{
    if (mShutdown) return;
    if (mAgent.notNull() && (mPhase == Phase::Connected || mPhase == Phase::Logout || mPhase == Phase::Disconnected))
    {
        if (LLPersistentNotificationStorage::instanceExists()) LLPersistentNotificationStorage::getInstance()->saveNotifications();
        if (LLDoNotDisturbNotificationStorage::instanceExists()) LLDoNotDisturbNotificationStorage::getInstance()->saveNotifications();
    }
    ++mGeneration; ++mRegionEpoch; mLocationRequests.clear(); mChangingRegion = false; mRequestedRegion.clear(); mNavigationDeadline = 0; unbind();
    if (LLNotifications::instanceExists())
    {
        std::vector<LLNotificationPtr> retired;
        if (auto channel = LLNotifications::instance().getChannel("System"))
            channel->forEachNotification([&retired](LLNotificationPtr notification)
            {
                if (notification->getPayload().has("vs_notification_epoch")) retired.push_back(notification);
            });
        for (const auto& notification : retired) LLNotifications::instance().cancel(notification);
    }
    if (auto* assets = dynamic_cast<LLViewerAssetStorage*>(gAssetStorage)) assets->resetAccountRequests();
    LLLandmark::resetRegionHandles(); gLandmarkList.resetAccount();
    vs_native_im_reset(); clearBalance();
    if (LLTeleportHistory::instanceExists()) LLTeleportHistory::instance().resetNativeSession();
    LLAgentBenefitsMgr::resetAccountBenefits();
    if (LLGlobalEconomy::instanceExists())
    {
        auto& economy = LLGlobalEconomy::instance();
        economy.setObjectCount(-1); economy.setObjectCapacity(-1);
        economy.setPriceObjectClaim(-1); economy.setPricePublicObjectDecay(-1);
        economy.setPricePublicObjectDelete(-1); economy.setPriceEnergyUnit(-1);
        economy.setPriceUpload(-1); economy.setPriceRentLight(-1);
        economy.setTeleportMinPrice(-1); economy.setTeleportPriceExponent(-1.f);
        economy.setPriceGroupCreate(-1);
    }
    if (LLInventoryModelBackgroundFetch::instanceExists()) LLInventoryModelBackgroundFetch::instance().resetAccountFetch();
    gInventoryCallbacks.resetAccountCallbacks();
    gInventory.clearAccountInventory();
    LLAvatarTracker::instance().clearBuddyList();
    gAgent.resetGroups();
    mNameConnections.clear(); mNames.clear(); mPendingNames.clear();
    mPhase = Phase::Login;
    auto cancel = std::move(mCancelSeed); if (cancel) cancel();
    mPoll.reset();
    // Native file transfers are the admitted CPU mute-list queue. Complete its
    // cancellation callbacks before releasing the transfer objects, even during
    // partial initialization; their delivery generation has already expired.
    if (gXferManager)
    {
        auto transfers = std::move(gXferManager->mReceiveList);
        gXferManager->mReceiveList.clear();
        for (auto* transfer : transfers)
        { transfer->mCallbackResult = LL_ERR_CIRCUIT_GONE; transfer->processEOF(); delete transfer; }
        gXferManager->cleanup();
    }
    if (LLMuteList::instanceExists())
    {
        if (mAgent.notNull()) LLMuteList::getInstance()->cache(mAgent);
        LLMuteList::deleteSingleton();
    }
    if (gCacheName) gCacheName->setUpstream(LLHost());
    if (gMessageSystem && mHost.isOk())
    {
        gMessageSystem->disableCircuit(mHost);
        // Viewer-created circuits can have no inbound circuit-code mapping;
        // disableCircuit alone deliberately retains those neighbor-style entries.
        // This owner has exactly one admitted session host and must retire it.
        gMessageSystem->mCircuitInfo.removeCircuitData(mHost);
    }
    if (gMessageSystem && mPreviousHost.isOk() && mPreviousHost != mHost)
    { gMessageSystem->disableCircuit(mPreviousHost); gMessageSystem->mCircuitInfo.removeCircuitData(mPreviousHost); }
    mPreviousHost = LLHost();
    mPhase = Phase::Login; mHost = LLHost(); mCapabilities = LLSD();
    mAgent.setNull(); mSession.setNull(); mRegionID.setNull(); mOwner.setNull();
    mName.clear(); mSeed.clear(); mHandle = mFlags = 0; mCircuit = 0;
    gAgentID.setNull(); gAgentSessionID.setNull(); gAgent.mSecureSessionID.setNull(); gAgent.mMOTD.clear(); gAgentMovementCompleted = false;
    mHandshake = mMovement = mSeedReady = mCircuitAck = mQuit = false;
    mTyping = mFrozen = false;
    gDisconnected = true;
    if (LLAvatarNameCache::instanceExists()) LLAvatarNameCache::getInstance()->setNameLookupURL("");
}
void VSNativeSession::shutdown()
{
    if (mShutdown) return;
    reset(); mSettingConnections.clear();
    // Seed coroutines retain the owner while suspended. Their eventual release
    // must not run account/UI teardown again after application services retire.
    mShutdown = true;
    if (active().get() == this) sOwner.reset();
}
LLSD VSNativeSession::evidence() const
{
    LLSD result; result["generation"] = LLSD::Integer(mGeneration); result["phase"] = S32(mPhase);
    result["received"] = LLSD::Integer(mReceived); result["sent"] = LLSD::Integer(mSent);
    result["rejected"] = LLSD::Integer(mRejected); result["expired"] = LLSD::Integer(mExpired);
    result["region"] = mName; result["handshake"] = mHandshake; result["movement"] = mMovement;
    result["capabilities"] = mSeedReady; result["circuit_ack"] = mCircuitAck;
    result["region_width"] = S32(mWidth); result["region_height"] = S32(mHeight);
    result["frozen"] = mFrozen;
    result["region_epoch"] = LLSD::Integer(mRegionEpoch); result["changing_region"] = mChangingRegion;
    result["location_url"] = locationURL(); result["world_owners"] = 0; return result;
}
