// Native CPU session and nearby transport. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "llmeshrepository.h"
#include "llagentpilot.h"
#include "vscameraview.h"
#include "vsteleportpolicy.h"
#include "llagentcamera.h"
#include "llviewerinput.h"
#include "llfloatercamera.h"
#include "lljoystickbutton.h"
#include "lleventnotifier.h"
#include "lfsimfeaturehandler.h"
#include "llviewergenericmessage.h"
#include "vsnativesession.h"
#include "llfloaterbeacons.h"
#include "llpathfindingmanager.h"
#include "llmenuoptionpathfindingrebakenavmesh.h"
#include "llfloaterpay.h"
#include "vssnapshot.h"
#include "vsworldsound.h"
#include "vsparticleeffects.h"
#include "fsassetblacklist.h"
#include "lltoolpie.h"
#include "llviewermediafocus.h"
#include "vscapabilities.h"
#include "vsnativeappearance.h"
#include "vstaskinventory.h"
#include "vsobjectphysics.h"
#include "vsobjectcosts.h"
#include "llmaterialmgr.h"
#include "vsobjectselection.h"
#include "vsobjectattention.h"
#include "lltoolmgr.h"
#include "llviewermenu.h"
#include "aoengine.h"
#include "fsavatarrenderpersistence.h"
#include "llperfstats.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "vsobjectcontext.h"
#include "vsregioncontext.h"
#include "llslurl.h"
#include "vsworldavatar.h"
#include "llhudobject.h"
#include "llcharacter.h"
#include "fsconsoleutils.h"
#include "fscommon.h"
#include "rlvhandler.h"
#include "chatbar_as_cmdline.h"
#include "fslslbridge.h"
#include "vsnativeim.h"
#include "fsareasearch.h"
#include "llvoiceclient.h"
#include "vsscenepreview.h"
#include "llcallingcard.h"
#include "llfriendcard.h"
#include "llappearancemgr.h"
#include "llgesturemgr.h"
#include "llpresetsmanager.h"
#include "fsfloaterwearablefavorites.h"
#include "fsfavoritegroups.h"
#include "fsgrouptitleregionmgr.h"
#include "lluserrelations.h"
#include "lggcontactsets.h"
#include "llagentlanguage.h"
#include "llinventorymodel.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llviewerassetstorage.h"
#include "llgltfmateriallist.h"
#include "llviewertexture.h"
#include "llviewerinventory.h"
#include "lllandmarklist.h"
#include "llexperiencecache.h"
#include "vsuiimageprovider.h"
#include "llpersistentnotificationstorage.h"
#include "lldonotdisturbnotificationstorage.h"
#include "vsplainchat.h"
#include "llappviewer.h"
#include "llviewerwindow.h"
#include "llpreviewscript.h"
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
#include "llaudioengine.h"
#include "llviewerparcelmedia.h"
#include "llregioninfomodel.h"
#include "llestateinfomodel.h"
#include "llfloaterregioninfo.h"
#include "llfloatergodtools.h"
#include "llfloatertelehub.h"
#include "llfloatertopobjects.h"
#include "llagent.h"
#include "llparcel.h"
#include "llviewerparcelmgr.h"
#include "vsestatecovenant.h"
#include "llenvironment.h"
#include "llfloaterland.h"
#include "indra_constants.h"
#include "llfloaterwebcontent.h"
#include "llfloaterreg.h"
#include "llmediactrl.h"
#include "llpluginclassmedia.h"
#include "llviewermessage.h"
#include "llworldmap.h"
#include "vsregionlimits.h"
#include "llviewercamera.h"
#include "llfloaterscriptdebug.h"
#include "lltracker.h"
#include "llworldmapview.h"
#include "llworldmapmessage.h"
#include "fspanellogin.h"
#include "llprogressview.h"
#include "llnotificationsutil.h"
#include "llnotificationmanager.h"
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
#include "llsdutil.h"
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
#include <utility>
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
void notify_native_startup(const char* name)
{
    LLSD payload;
    vs_native_im_stamp_notification(payload);
    LLNotificationsUtil::add(name, LLSD(), payload);
}

void restore_native_account_policies()
{
    // LLAgent::init performs these same account-only transitions for OpenGL.
    // Native startup omits that method's reference scene bootstrap.
    gAgent.selectAutorespond(gSavedPerAccountSettings.getBOOL("FSAutorespondMode"));
    gAgent.selectAutorespondNonFriends(gSavedPerAccountSettings.getBOOL("FSAutorespondNonFriendsMode"));
    gAgent.selectRejectTeleportOffers(gSavedPerAccountSettings.getBOOL("FSRejectTeleportOffersMode"));
    gAgent.selectRejectFriendshipRequests(gSavedPerAccountSettings.getBOOL("FSRejectFriendshipRequestsMode"));
    gAgent.selectRejectAllGroupInvites(gSavedPerAccountSettings.getBOOL("FSRejectAllGroupInvitesMode"));
}

const std::set<std::string> transport = {"StartPingCheck", "CompletePingCheck", "PacketAck"};
const std::set<std::string>    worldMessages = {
    "ObjectUpdate",      "ObjectUpdateCompressed", "ObjectUpdateCached", "ImprovedTerseObjectUpdate", "KillObject",
    "LayerData",         "AvatarAppearance",       "AvatarAnimation",    "ObjectAnimation",           "ViewerEffect",
    "ReplyTaskInventory", "GenericStreamingMessage"
};
const std::set<std::string> session = { "SoundTrigger", "PreloadSound", "AttachedSound", "AttachedSoundGainChange", "MeanCollisionAlertMessage", "SimStats",
                                        "AvatarSitResponse", "RegionInfo", "EstateOwnerMessage", "TelehubInfo", "LandStatReply",
                                        "ParcelProperties",
                                        "ParcelMediaUpdate",
                                        "ParcelMediaCommandMessage",
                                        "RegionIDAndHandleReply",
                                        "MapBlockReply",
                                        "MapItemReply",
                                        "FindAgent",
                                        "RegionHandshake",
                                        "AgentMovementComplete",
                                        "ChatFromSimulator",
                                        "LogoutReply",
                                        "KickUser",
                                        "AlertMessage",
                                        "AgentAlertMessage",
                                        "Error",
                                        "TeleportStart",
                                        "TeleportFinish",
                                        "TeleportLocal",
                                        "TeleportProgress",
                                        "TeleportFailed",
                                        "ViewerFrozenMessage",
                                        "FeatureDisabled",
                                        "CrossedRegion",
                                        "CloseCircuit",
                                        "DisableSimulator",
                                        "UUIDNameReply",
                                        "UUIDGroupNameReply",
                                        "MuteListUpdate",
                                        "UseCachedMuteList",
                                        "RequestXfer",
                                        "SendXferPacket",
                                        "ConfirmXferPacket",
                                        "AbortXfer",
                                        "TransferInfo",
                                        "TransferPacket",
                                        "TransferAbort",
                                        "AssetUploadComplete",
                                        "GenericMessage",
                                        "EventInfoReply",
                                        "ScriptQuestion",
                                        "ScriptDialog",
                                        "ScriptRunningReply",
                                        "ScriptControlChange",
                                        "LoadURL",
                                        "EconomyData",
                                        "ImprovedInstantMessage",
                                        "AgentDataUpdate",
                                        "AgentGroupDataUpdate",
                                        "AgentDropGroup",
                                        "AgentWearablesUpdate",
                                        "OnlineNotification",
                                        "OfflineNotification",
                                        "ChangeUserRights",
                                        "MoneyBalanceReply",
                                        "UserInfoReply",
                                        "ParcelInfoReply",
                                        "PlacesReply",
                                        "AvatarPropertiesReply",
                                        "AvatarInterestsReply",
                                        "AvatarGroupsReply",
                                        "AvatarNotesReply",
                                        "AvatarPicksReply",
                                        "AvatarClassifiedReply",
                                        "AvatarPickerReply",
                                        "PickInfoReply",
                                        "ClassifiedInfoReply",
                                        "CreateGroupReply",
                                        "JoinGroupReply",
                                        "EjectGroupMemberReply",
                                        "LeaveGroupReply",
                                        "GroupProfileReply",
                                        "GroupMembersReply",
                                        "GroupRoleDataReply",
                                        "GroupRoleMembersReply",
                                        "DirGroupsReply",
                                        "DirPeopleReply",
                                        "DirPlacesReply",
                                        "DirEventsReply",
                                        "DirClassifiedReply",
                                        "DirLandReply",
                                        "GroupTitlesReply",
                                        "GroupNoticesListReply",
                                        "GroupAccountSummaryReply",
                                        "GroupAccountDetailsReply",
                                        "GroupAccountTransactionsReply",
                                        "UpdateCreateInventoryItem",
                                        "RemoveInventoryItem",
                                        "RemoveInventoryFolder",
                                        "RemoveInventoryObjects",
                                        "SaveAssetIntoInventory",
                                        "BulkUpdateInventory",
                                        "MoveInventoryItem",
                                        "InventoryDescendents",
                                        "FetchInventoryReply",
                                        "SetDisplayNameReply",
                                        "DisplayNameUpdate",
                                        "NavMeshStatusUpdate",
                                        "AgentStateUpdate",
                                        "ChatterBoxSessionStartReply",
                                        "ChatterBoxSessionEventReply",
                                        "ChatterBoxSessionAgentListUpdates",
                                        "ChatterBoxSessionUpdate",
                                        "ChatterBoxInvitation",
                                        "ParcelVoiceInfo",
                                        "ForceCloseChatterBoxSession" };
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
bool                             VSNativeSession::resetAvatar(const LLUUID& id, bool resetAnimations)
{
    if (mPhase != Phase::Connected || !gViewerWindow || !gViewerWindow->nativeContext())
        return false;
    auto* avatar = gViewerWindow->nativeContext()->world().avatarForObject(id);
    if (!avatar || !avatar->resetSkeleton(resetAnimations))
        return false;
    if (avatar->character().getID() != mAgent)
        return true; // local reset of another avatar
    if (!gMessageSystem)
        return false;
    std::array<U8, 17> data{};
    std::copy(mAgent.mData, mAgent.mData + UUID_BYTES, data.begin());
    data[16] = resetAnimations ? 1 : 0;
    const U8         colour[4]{ 255, 255, 255, 255 };
    LLMessageSystem* message = gMessageSystem;
    message->newMessage("ViewerEffect");
    message->nextBlock("AgentData");
    message->addUUID("AgentID", mAgent);
    message->addUUID("SessionID", mSession);
    message->nextBlock("Effect");
    message->addUUID("ID", LLUUID::generateNewID());
    message->addUUID("AgentID", mAgent);
    message->addU8("Type", LLHUDObject::LL_HUD_EFFECT_RESET_SKELETON);
    message->addF32("Duration", 0);
    message->addBinaryData("Color", colour, 4);
    message->addBinaryData("TypeData", data.data(), S32(data.size()));
    return message->sendReliable(mHost) > 0;
}
bool VSNativeSession::requestAnimations(const std::vector<LLUUID>& animations, bool start)
{
    if (!regionReady() || !gMessageSystem || animations.empty() || animations.size() > 256)
        return false;
    std::vector<LLUUID> valid;
    for (const auto& animation : animations)
        if (animation.notNull() || (!start && animations.size() == 1))
            valid.push_back(animation);
    if (valid.empty())
        return false;
    gMessageSystem->newMessage("AgentAnimation");
    gMessageSystem->nextBlock("AgentData");
    gMessageSystem->addUUID("AgentID", mAgent);
    gMessageSystem->addUUID("SessionID", mSession);
    for (const auto& animation : valid)
    {
        gMessageSystem->nextBlock("AnimationList");
        gMessageSystem->addUUID("AnimID", animation);
        gMessageSystem->addBOOL("StartAnim", start);
    }
    gMessageSystem->nextBlock("PhysicalAvatarEventList");
    gMessageSystem->addBinaryData("TypeData", nullptr, 0);
    if (gMessageSystem->sendReliable(mHost) <= 0)
        return false;
    ++mSent;
    return true;
}
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
    for (const auto& name : worldMessages) msg.setHandlerFunc(name.c_str(), receiveMessage);
    msg.setHandlerFunc("AgentWearablesUpdate",receiveMessage);
    msg.setHandlerFunc("EnableSimulator",receiveMessage);
    for (const char* name : {"SimStats", "AvatarSitResponse", "RegionInfo", "EstateOwnerMessage", "TelehubInfo", "LandStatReply", "ParcelProperties", "ParcelMediaUpdate", "ParcelMediaCommandMessage", "RegionIDAndHandleReply", "MapBlockReply", "MapItemReply", "ParcelOverlay", "CoarseLocationUpdate", "RegionHandshake", "AgentMovementComplete", "ChatFromSimulator", "LogoutReply",
         "KickUser", "AlertMessage", "AgentAlertMessage", "Error", "ViewerFrozenMessage", "FeatureDisabled", "MoneyBalanceReply", "TeleportStart", "TeleportFinish", "TeleportLocal", "TeleportProgress", "TeleportFailed", "CrossedRegion", "CloseCircuit", "DisableSimulator"})
        msg.setHandlerFunc(name, receiveMessage);
    msg.setHandlerFunc("PayPriceReply", receiveMessage);
    msg.setHandlerFunc("ObjectProperties", receiveMessage);
    msg.setHandlerFunc("ObjectPropertiesFamily", receiveMessage);
    msg.setHandlerFunc("ForceObjectSelect", receiveMessage);
    LLAvatarTracker::instance().registerCallbacks(&msg);
    LLInventoryModel::registerCallbacks(&msg);
    msg.setHandlerFunc("SoundTrigger", process_sound_trigger);
    msg.setHandlerFunc("PreloadSound", process_preload_sound);
    msg.setHandlerFunc("AttachedSound", process_attached_sound);
    msg.setHandlerFunc("AttachedSoundGainChange", process_attached_sound_gain_change);
    msg.setHandlerFunc("SimStats", process_sim_stats);
    msg.setHandlerFunc("MeanCollisionAlertMessage", process_mean_collision_alert_message);
    msg.setHandlerFunc("ScriptQuestion", process_script_question);
    msg.setHandlerFunc("ScriptDialog", process_script_dialog);
    msg.setHandlerFunc("ScriptRunningReply", LLLiveLSLEditor::processScriptRunningReply);
    msg.setHandlerFunc("ScriptControlChange", LLAgent::processScriptControlChange);
    msg.setHandlerFunc("LoadURL", process_load_url);
    msg.setHandlerFunc("AlertMessage", process_alert_message);
    msg.setHandlerFunc("AgentAlertMessage", process_agent_alert_message);
    msg.setHandlerFunc("EventInfoReply", LLEventNotifier::processEventInfoReply);
    msg.setHandlerFunc("GenericMessage", [](LLMessageSystem* message, void** context)
    {
        auto owner = VSNativeSession::active();
        if (!owner || owner->phase() != Phase::Connected || owner->host() != message->getSender()) return;
        std::string method;
        message->getString("MethodData", "Method", method);
        if ((method == "ExperienceEvent" || method == "URLFloater")) process_generic_message(message, context);
    });
    msg.setHandlerFunc("AgentDataUpdate", LLAgent::processAgentDataUpdate);
    msg.setHandlerFunc("AgentGroupDataUpdate", LLAgent::processAgentGroupDataUpdate);
    msg.setHandlerFunc("AgentDropGroup", LLAgent::processAgentDropGroup);
    msg.setHandlerFunc("DirGroupsReply", LLPanelDirBrowser::processDirGroupsReply);
    msg.setHandlerFunc("DirPeopleReply", LLPanelDirBrowser::processDirPeopleReply);
    msg.setHandlerFunc("DirPlacesReply", LLPanelDirBrowser::processDirPlacesReply);
    msg.setHandlerFunc("DirEventsReply", LLPanelDirBrowser::processDirEventsReply);
    msg.setHandlerFunc("DirClassifiedReply", LLPanelDirBrowser::processDirClassifiedReply);
    msg.setHandlerFunc("DirLandReply", LLPanelDirBrowser::processDirLandReply);
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
    // The original dispatcher routes null-query account holdings and genuine
    // group queries; both use authenticated current-account CPU controllers.
    msg.setHandlerFunc("PlacesReply", process_places_reply);
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
VSRegionLimits& VSNativeSession::regionLimits()
{
    if(!mRegionLimits)
    {
        mRegionLimits=std::make_unique<VSRegionLimits>();
        mRegionLimits->refreshLimits();
    }
    return *mRegionLimits;
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
    gEventNotifier.load(response["event_notifications"]);
    gAgent.mSecureSessionID = response["secure_session_id"].asUUID();
    gAgent.mMOTD = response["message"].asString();
    std::string mapServer=response["map-server-url"].asString();
    if(mapServer.empty())mapServer=gSavedSettings.getString("MapServerURL");
    gSavedSettings.setString("CurrentMapServerURL",mapServer);
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
    // Account paths/identity are installed before begin(); load the genuine
    // asset policy before enabling circuit traffic or native texture requests.
    FSAssetBlacklist::instance().init();
    mPhase = Phase::Connecting; mDeadline = now() + 60;
    auto& circuit=mCircuits[mHost];circuit.handle=mHandle;circuit.width=mWidth;circuit.height=mHeight;
    circuit.incarnation=mNextCircuitGeneration++;circuit.deadline=now()+60;
    if (gViewerWindow && gViewerWindow->nativeContext() &&
        !gViewerWindow->nativeContext()->world().activateRegion(mHost,mHandle,mWidth,mHeight,circuit.incarnation))
    { disconnect(LLTrans::getString("NativeSessionInvalidLogin"));return; }
    FSAvatarRenderPersistence::instance().init();
    LLPerfStats::resetNativeStats();
    if (gViewerWindow && gViewerWindow->nativeContext() && !gViewerWindow->nativeContext()->world().initializeSelf(mAgent,mGeneration))
    { disconnect(LLTrans::getString("NativeSessionInvalidLogin"));return; }
    mAppearanceOwnerInitialized=true;
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
    LLStartUp::multimediaInit();
    LLStartUp::fontInit();
    LLStartUp::setStartupState(STATE_WORLD_WAIT);
    publishProgress();
}
void VSNativeSession::circuitResult(U64 generation, S32 result, U64 regionEpoch)
{
    if (generation != mGeneration || (regionEpoch && regionEpoch != mRegionEpoch) || (mPhase != Phase::Connecting && !mChangingRegion)) { ++mExpired; return; }
    if (result) { disconnect(LLTrans::getString("NativeSessionConnectionTimeout")); return; }
    mCircuitAck = true;
    if (auto circuit=mCircuits.find(mHost);circuit!=mCircuits.end()) circuit->second.acknowledged=true;
    send("CompleteAgentMovement", LLSD());
    gMessageSystem->newMessage("AgentWearablesRequest");gMessageSystem->nextBlock("AgentData");
    gMessageSystem->addUUID("AgentID",mAgent);gMessageSystem->addUUID("SessionID",mSession);gMessageSystem->sendReliable(mHost);
    publishProgress();
    connected();
}
void VSNativeSession::seedCoro(std::string url, U64 generation, U64 regionEpoch)
{
    if (generation != mGeneration || regionEpoch != mRegionEpoch || (mPhase != Phase::Connecting && !mChangingRegion)) return;
    auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeSeedCapabilities", LLCore::HttpRequest::DEFAULT_POLICY_ID);
    const std::weak_ptr<LLCoreHttpUtil::HttpCoroutineAdapter> requestOwner = adapter;
    mCancelSeed = [requestOwner]() { if (auto request = requestOwner.lock()) request->cancelSuspendedOperation(); };
    auto request = std::make_shared<LLCore::HttpRequest>();
    auto options = std::make_shared<LLCore::HttpOptions>(); options->setTransferTimeout(15); options->setRetries(0);
    const LLSD wanted = vs_native_capability_names();
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        LLSD result = adapter->postAndSuspend(request, url, wanted, options);
        if (generation != mGeneration || (regionEpoch && regionEpoch != mRegionEpoch) || (mPhase != Phase::Connecting && !mChangingRegion)) { ++mExpired; return; }
        if (LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]))
        {
            result.erase("http_result");
            // Capability admission can finish login and construct ordinary UI.
            // HTTP continuations run in a coroutine, not the main-loop owner.
            const auto weak = weak_from_this();
            LL::WorkQueue::getInstance("mainloop")->post([weak, result, generation, regionEpoch, wanted]()
            {
                auto owner = weak.lock();
                if (!owner || owner->mShutdown || generation != owner->mGeneration ||
                    regionEpoch != owner->mRegionEpoch ||
                    (owner->mPhase != Phase::Connecting && !owner->mChangingRegion)) return;
                owner->mCancelSeed = {};
                owner->capabilities(result, generation, regionEpoch, wanted);
            });
            return;
        }
    }
    const auto weak = weak_from_this();
    LL::WorkQueue::getInstance("mainloop")->post([weak, generation, regionEpoch]()
    {
        auto owner = weak.lock();
        if (!owner || owner->mShutdown || generation != owner->mGeneration ||
            regionEpoch != owner->mRegionEpoch ||
            (owner->mPhase != Phase::Connecting && !owner->mChangingRegion)) return;
        owner->disconnect(LLTrans::getString("NativeSessionConnectionTimeout"));
    });
}
void VSNativeSession::capabilities(const LLSD& caps, U64 generation, U64 regionEpoch, const LLSD& requested)
{
    if (generation != mGeneration || (regionEpoch && regionEpoch != mRegionEpoch) || (mPhase != Phase::Connecting && !mChangingRegion)) { ++mExpired; return; }
    if (!caps.isMap() || !webURL(caps["EventQueueGet"].asString())) { disconnect(LLTrans::getString("NativeSessionInvalidCapabilities")); return; }
    clearSimulatorFeatures();
    mCapabilities = LLSD::emptyMap();
    const LLSD requestedCapabilities=requested.isArray() ? requested : vs_native_capability_names();
    for (auto requested=requestedCapabilities.beginArray();requested!=requestedCapabilities.endArray();++requested)
    {
        const std::string name=requested->asString();
        if (webURL(caps[name].asString())) mCapabilities[name]=caps[name];
    }
    // LLSD messages are sent through the host's granted HTTP transport.
    // LLAgent, cache-name and asset services retain copies of this host.
    mHost.setUntrustedSimulatorCap(capability("UntrustedSimulatorMessage"));
    if (auto circuit=mCircuits.find(mHost);circuit!=mCircuits.end())
    { circuit->second.capabilities=mCapabilities;circuit->second.capabilitiesReady=true; }
    if (gCacheName) gCacheName->setUpstream(mHost);
    if (gAssetStorage) gAssetStorage->setUpstream(mHost);
    LLAvatarNameCache::getInstance()->setNameLookupURL(capability("GetDisplayNames"));
    mPoll = std::make_unique<LLEventPoll>(capability("EventQueueGet"), mHost);
    LLExperienceCache::instance().setCapabilityQuery([](const std::string& name) { return gAgent.getRegionCapability(name); });
    LLExperienceCache::instance().setAccountGuardQuery([] { return vs_native_im_guard(); });
    mSeedReady = true;
    publishProgress();
    const std::string features = capability("SimulatorFeatures");
    if (!features.empty())
    {
        const auto weak = weak_from_this(); const U64 epoch = mRegionEpoch;
        LLCoros::instance().launch("nativeSimulatorFeatures", [weak, features, generation, epoch]()
        { simulatorFeaturesCoro(weak, features, generation, epoch); });
    }
    connected();
}
void VSNativeSession::clearSimulatorFeatures()
{
    auto cancel = std::exchange(mCancelSimulatorFeatures, {}); if (cancel) cancel();
    mSimulatorFeatures = LLSD::emptyMap(); mSimulatorFeaturesReceived = false;
    LFSimFeatureHandler::instance().setSupportedFeatures(mSimulatorFeatures, false);
}
bool VSNativeSession::applySimulatorFeatures(const LLSD& features, U64 generation, U64 regionEpoch)
{
    if (mShutdown || generation != mGeneration || regionEpoch != mRegionEpoch ||
        (mPhase != Phase::Connecting && mPhase != Phase::Connected) || !features.isMap()) return false;
    mSimulatorFeatures = features; mSimulatorFeaturesReceived = true;
    if (const auto circuit=mCircuits.find(mHost);circuit!=mCircuits.end())
    { circuit->second.simulatorFeatures=features;circuit->second.featuresReceived=true; }
    LFSimFeatureHandler::instance().setSupportedFeatures(mSimulatorFeatures, false);
    if (mShutdown || generation!=mGeneration || regionEpoch!=mRegionEpoch) return false;
    mSimulatorFeaturesChanged();
    return !mShutdown && generation==mGeneration && regionEpoch==mRegionEpoch;
}
void VSNativeSession::simulatorFeaturesCoro(std::weak_ptr<VSNativeSession> weak, std::string url, U64 generation, U64 regionEpoch)
{
    auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeSimulatorFeatures", LLCore::HttpRequest::DEFAULT_POLICY_ID);
    const std::weak_ptr<LLCoreHttpUtil::HttpCoroutineAdapter> requestOwner = adapter;
    {
        auto owner = weak.lock();
        if (!owner || owner->mShutdown || generation != owner->mGeneration || regionEpoch != owner->mRegionEpoch) return;
        owner->mCancelSimulatorFeatures = [requestOwner]() { if (auto request = requestOwner.lock()) request->cancelSuspendedOperation(); };
    }
    auto request = std::make_shared<LLCore::HttpRequest>();
    auto options = std::make_shared<LLCore::HttpOptions>(); options->setTransferTimeout(15); options->setRetries(0);
    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        LLSD result = adapter->getAndSuspend(request, url, options);
        auto owner = weak.lock();
        if (!owner || owner->mShutdown || generation != owner->mGeneration || regionEpoch != owner->mRegionEpoch ||
            (owner->mPhase != Phase::Connecting && owner->mPhase != Phase::Connected)) return;
        if (result.isMap() && LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]))
        {
            result.erase("http_result");
            LL::WorkQueue::getInstance("mainloop")->post([weak, result, generation, regionEpoch]()
            {
                auto current = weak.lock();
                if (!current || current->mShutdown || generation != current->mGeneration ||
                    regionEpoch != current->mRegionEpoch ||
                    (current->mPhase != Phase::Connecting && current->mPhase != Phase::Connected)) return;
                current->mCancelSimulatorFeatures = {};
                current->applySimulatorFeatures(result, generation, regionEpoch);
            });
            return;
        }
    }
    if (auto owner = weak.lock(); owner && generation == owner->mGeneration && regionEpoch == owner->mRegionEpoch)
    {
        owner->mCancelSimulatorFeatures = {};
        LL_WARNS("NativeSession") << "SimulatorFeatures request failed; advertised features remain unavailable" << LL_ENDL;
    }
}
std::string VSNativeSession::capability(const std::string& name) const { return mCapabilities[name].asString(); }
void VSNativeSession::requestParcel()
{
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || !mPosition.isFinite()) return;
    // Retain a monotonic location sequence across region changes and relogins.
    mParcelSequence = mParcelSequence == std::numeric_limits<S32>::max() ? 1 : mParcelSequence + 1;
    const F32 west = llclamp(mPosition.mV[VX], 0.f, F32(mWidth) - .01f);
    const F32 south = llclamp(mPosition.mV[VY], 0.f, F32(mHeight) - .01f);
    mParcelCellX = S32(west / 4); mParcelCellY = S32(south / 4); mParcelQueryTime = now();
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessage("ParcelPropertiesRequest"); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession);
    msg->nextBlock("ParcelData"); msg->addS32("SequenceID", mParcelSequence);
    msg->addF32("West", west); msg->addF32("South", south);
    msg->addF32("East", llmin(west + .01f, F32(mWidth))); msg->addF32("North", llmin(south + .01f, F32(mHeight)));
    msg->addBOOL("SnapSelection", false);
    if (msg->sendReliable(mHost) > 0) ++mSent;
}
void VSNativeSession::clearParcelPlayback()
{
    ++mParcelRevision; ++mMusicRequest; ++mMediaRequest;
    mMusicRequested = mMediaRequested = false;
    if (LLViewerAudio::instanceExists()) LLViewerAudio::instance().stopInternetStreamWithAutoFade();
    if (auto window = mParcelMediaWindow.get()) window->closeFloater();
    mParcelMediaWindow.markDead(); mMediaPresented = false;
    mMediaLoop = mMediaPaused = mMediaStateDirty = false; mMediaAutoScale = true;
    mMediaWidth = mMediaHeight = 0; mMediaPendingSeek = -1.f;
    mParcelPermissionsKnown = false; mParcelOwner.setNull(); mParcelFlags = 0; mParcelSeeAvatars = true;
    mParcelID = -1; mParcelName.clear(); mMusicURL.clear(); mMediaURL.clear(); mMediaType.clear();
}
void VSNativeSession::updateParcelPlayback()
{
    if (mPhase != Phase::Connected) return;
    if (mMusicRequested && !gSavedSettings.getBOOL("AudioStreamingMusic")) setMusicEnabled(false);
    if (mMediaRequested && !gSavedSettings.getBOOL("AudioStreamingMedia")) setMediaEnabled(false);
    applyParcelMediaState();
    // A pending consent prompt has no window yet. Closing an existing browser
    // is an explicit stop; it must not cause autoplay on the next parcel update.
    if (mMediaRequested && mMediaPresented && mParcelMediaWindow.isDead())
    { mMediaRequested = false; mMediaUserStopped = true; ++mMediaRequest; }
}
bool VSNativeSession::mediaEnabled() const
{
    return mMediaRequested && !mMediaPaused && !mParcelMediaWindow.isDead();
}
bool VSNativeSession::playbackCurrent(bool music, U64 generation, U64 epoch, U64 revision, U64 request, const std::string& url) const
{
    return mPhase == Phase::Connected && !mChangingRegion && mGeneration == generation && mRegionEpoch == epoch &&
        mParcelRevision == revision && (music ? mMusicRequest : mMediaRequest) == request &&
        (music ? mMusicRequested : mMediaRequested) && (music ? mMusicURL : mMediaURL) == url;
}
bool VSNativeSession::setMusicEnabled(bool enable)
{
    ++mMusicRequest; mMusicRequested = enable; mMusicUserStopped = !enable;
    if (!enable)
    {
        if (LLViewerAudio::instanceExists()) LLViewerAudio::instance().stopInternetStreamWithAutoFade();
        return true;
    }
    if (!gAudiop || mMusicURL.empty() || !gSavedSettings.getBOOL("AudioStreamingMusic") || !requestParcelPlayback(true))
    { mMusicRequested = false; return false; }
    return true;
}
bool VSNativeSession::setMediaEnabled(bool enable)
{
    ++mMediaRequest; mMediaRequested = enable; mMediaUserStopped = !enable;
    if (!enable)
    {
        mMediaPaused = mMediaStateDirty = false; mMediaPendingSeek = -1.f;
        if (auto window = mParcelMediaWindow.get()) window->closeFloater();
        mParcelMediaWindow.markDead(); mMediaPresented = false; return true;
    }
    if (!mParcelMediaWindow.isDead() && gSavedSettings.getBOOL("AudioStreamingMedia"))
    { setMediaPaused(false); return true; }
    mMediaPaused = false; mMediaStateDirty = true;
    if (mMediaURL.empty() || !gSavedSettings.getBOOL("AudioStreamingMedia") || !requestParcelPlayback(false))
    { mMediaRequested = false; return false; }
    return true;
}
void VSNativeSession::rejectParcelPlayback(bool music)
{
    // A filter denial concerns this source, not the user's global autoplay
    // choice. Keep explicit toolbar stops distinct from consent rejection.
    const bool stopped = music ? mMusicUserStopped : mMediaUserStopped;
    if (music) { setMusicEnabled(false); mMusicUserStopped = stopped; }
    else { setMediaEnabled(false); mMediaUserStopped = stopped; }
}
void VSNativeSession::playParcelAllowed(bool music)
{
    if (mPhase != Phase::Connected || mChangingRegion) return;
    if (music)
    {
        if (gAudiop && mMusicRequested && gSavedSettings.getBOOL("AudioStreamingMusic"))
            LLViewerAudio::instance().startInternetStreamWithAutoFade(mMusicURL);
        return;
    }
    if (!mMediaRequested || !gSavedSettings.getBOOL("AudioStreamingMedia")) return;
    // A parcel surface is represented by the existing 2D media browser. Its
    // LLMediaCtrl copies plugin pixels into the native UI resource provider.
    LLFloaterWebContent::Params params;
    params.url = mMediaURL; params.mime_type = mMediaType; params.target = "native-parcel-media"; params.window_class = "web_content";
    params.trusted_content = false; params.show_chrome = true;
    if (auto window = LLFloaterReg::showInstance("web_content", params))
    { mParcelMediaWindow = window->getHandle(); mMediaPresented = true; mMediaStateDirty = true; applyParcelMediaState(); }
}
bool VSNativeSession::requestParcelPlayback(bool music)
{
    if (mPhase != Phase::Connected || mChangingRegion || mParcelID < 0) return false;
    const auto url = music ? mMusicURL : mMediaURL;
    if (!webURL(url)) return false;
    const std::weak_ptr<VSNativeSession> weak = shared_from_this();
    const U64 generation = mGeneration, epoch = mRegionEpoch, revision = mParcelRevision;
    const U64 request = music ? mMusicRequest : mMediaRequest;
    const auto current = [weak, music, generation, epoch, revision, request, url]()
    {
        auto owner = weak.lock();
        return owner && owner->playbackCurrent(music, generation, epoch, revision, request, url) ? owner : nullptr;
    };
    LLSD payload; vs_native_im_stamp_notification(payload);
    if (!music && gWarningSettings.getBOOL("FirstStreamingVideo"))
    {
        LLNotificationsUtil::add("ParcelCanPlayMedia", LLSD(), payload,
            [current](const LLSD& notification, const LLSD& response)
            {
                if (auto owner = current())
                {
                    gWarningSettings.setBOOL("FirstStreamingVideo", false);
                    const S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
                    if (option == 0 || option == 1)
                    {
                        if (option == 1) gSavedSettings.setBOOL("AudioStreamingVideo", true);
                        owner->requestParcelPlayback(false);
                    }
                    else { gSavedSettings.setBOOL("AudioStreamingVideo", false); owner->setMediaEnabled(false); }
                }
                return false;
            });
        return true;
    }
    if (!gSavedSettings.getBOOL("MediaEnableFilter")) { playParcelAllowed(music); return true; }
    if (gWarningSettings.getBOOL("FirstMediaFilter"))
    {
        LLNotificationsUtil::add("EnableMediaFilter", LLSD(), payload,
            [current, music](const LLSD& notification, const LLSD& response)
            {
                if (auto owner = current())
                {
                    gWarningSettings.setBOOL("FirstMediaFilter", false);
                    if (LLNotificationsUtil::getSelectedOption(notification, response) != 0) gSavedSettings.setBOOL("MediaEnableFilter", false);
                    owner->requestParcelPlayback(music);
                }
                return false;
            });
        return true;
    }
    auto& filter = LLViewerParcelMedia::instance();
    const std::string domain = filter.extractDomain(url);
    for (auto it = filter.mMediaFilterList.beginArray(); it != filter.mMediaFilterList.endArray(); ++it)
    {
        const auto& row = *it;
        const std::string listed = row["domain"].asString();
        const bool domainMatch = !listed.empty() && domain.size() >= listed.size() &&
            domain.compare(domain.size() - listed.size(), listed.size(), listed) == 0 &&
            (domain.size() == listed.size() || domain[domain.size() - listed.size() - 1] == '.');
        if (listed == url || domainMatch)
        {
            if (row["action"].asString() == "allow") playParcelAllowed(music);
            else rejectParcelPlayback(music);
            return true;
        }
    }
    LLSD args; args[music ? "AUDIOURL" : "MEDIAURL"] = url; args[music ? "AUDIODOMAIN" : "MEDIADOMAIN"] = domain;
    const auto resolve = [current, music, domain, url](S32 option, bool rememberURL = false)
    {
        if (auto owner = current())
        {
            if (option == 3 || option == 4)
            {
                LLSD row; row["domain"] = rememberURL ? url : domain; row["action"] = option == 3 ? "allow" : "deny";
                auto& filter = LLViewerParcelMedia::instance(); filter.mMediaFilterList.append(row); filter.saveDomainFilterList();
            }
            if (option == 0 || option == 3) owner->playParcelAllowed(music);
            else owner->rejectParcelPlayback(music);
        }
    };
    if (gSavedSettings.getBOOL("MediaFilterSinglePrompt"))
        LLNotificationsUtil::add(music ? "AudioAlertSingle" : "MediaAlertSingle", args, payload,
            [resolve](const LLSD& notification, const LLSD& response)
            {
                const S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
                resolve(option == 2 ? 4 : option); return false;
            });
    else
        LLNotificationsUtil::add(music ? "AudioAlert" : "MediaAlert", args, payload,
            [current, resolve, args, payload, music](const LLSD& notification, const LLSD& response)
            {
                if (!current()) return false;
                const bool allow = LLNotificationsUtil::getSelectedOption(notification, response) == 0;
                LLSD secondArgs = args;
                secondArgs["ACTION"] = LLTrans::getString(allow ? "MediaFilterActionAllow" : "MediaFilterActionDeny");
                secondArgs["CONDITION"] = LLTrans::getString(allow ? "MediaFilterConditionAlways" : "MediaFilterConditionNever");
                secondArgs["LCONDITION"] = LLTrans::getString(allow ? "MediaFilterConditionAlwaysLower" : "MediaFilterConditionNeverLower");
                LLNotificationsUtil::add(music ? "AudioAlert2" : "MediaAlert2", secondArgs, payload,
                    [resolve, allow](const LLSD& n, const LLSD& r)
                    {
                        const S32 option = LLNotificationsUtil::getSelectedOption(n, r);
                        resolve(option == 1 || option == 2 ? (allow ? 3 : 4) : (option == 0 && allow ? 0 : 1), option == 2); return false;
                    });
                return false;
            });
    return true;
}
LLViewerMediaImpl* VSNativeSession::parcelMedia() const
{
    if (mPhase != Phase::Connected || mChangingRegion) return nullptr;
    auto window = mParcelMediaWindow.get();
    auto browser = window ? window->findChild<LLMediaCtrl>("webbrowser", true) : nullptr;
    return browser ? LLViewerMedia::getInstance()->getMediaImplFromTextureID(browser->getTextureID()) : nullptr;
}
void VSNativeSession::setMediaPaused(bool pause)
{
    if (!mMediaRequested || mMediaUserStopped) return;
    mMediaPaused = pause; mMediaStateDirty = true; applyParcelMediaState();
}
bool VSNativeSession::updateParcelMediaMetadata(const LLSD& body)
{
    for (const char* key : {"loop", "autoscale", "width", "height"})
        if (body.has(key) && !body[key].isInteger()) return false;
    const S32 width = body.has("width") ? body["width"].asInteger() : 0;
    const S32 height = body.has("height") ? body["height"].asInteger() : 0;
    if (width < 0 || height < 0 || width > 4096 || height > 4096) return false;
    if ((body.has("loop") && (body["loop"].asInteger() < 0 || body["loop"].asInteger() > 1)) ||
        (body.has("autoscale") && (body["autoscale"].asInteger() < 0 || body["autoscale"].asInteger() > 1))) return false;
    const bool loop = body.has("loop") && body["loop"].asInteger() != 0;
    const bool autoscale = !body.has("autoscale") || body["autoscale"].asInteger() != 0;
    mMediaStateDirty |= loop != mMediaLoop || autoscale != mMediaAutoScale || width != mMediaWidth || height != mMediaHeight;
    mMediaLoop = loop; mMediaAutoScale = autoscale; mMediaWidth = width; mMediaHeight = height;
    return true;
}
void VSNativeSession::applyParcelMediaState()
{
    if (mPhase != Phase::Connected || mChangingRegion || !mMediaRequested || mMediaUserStopped) return;
    auto window = mParcelMediaWindow.get();
    auto browser = window ? window->findChild<LLMediaCtrl>("webbrowser", true) : nullptr;
    if(browser)
        if(auto* impl=LLViewerMedia::instance().getMediaImplFromTextureID(browser->getTextureID()))
            impl->setIsParcelMedia(true);
    auto plugin = browser ? browser->getMediaPlugin() : nullptr;
    // Consent and plugin startup are asynchronous. Retain seek/pause/loop until
    // this current parcel's actual plugin is ready; never fetch to satisfy them.
    if (!plugin || !plugin->isPluginRunning()) return;
    if (mMediaStateDirty)
    {
        browser->setDecoupleTextureSize(!mMediaAutoScale && mMediaWidth > 0 && mMediaHeight > 0);
        if (!mMediaAutoScale && mMediaWidth > 0 && mMediaHeight > 0) browser->setTextureSize(mMediaWidth, mMediaHeight);
        if (plugin->pluginSupportsMediaTime())
        { plugin->setLoop(mMediaLoop); if (mMediaPaused) plugin->pause(); else plugin->start(); }
        mMediaStateDirty = false;
    }
    if (!plugin->pluginSupportsMediaTime()) { mMediaPendingSeek = -1.f; return; }
    if (mMediaPendingSeek >= 0 && plugin->getStatus() != LLPluginClassMediaOwner::MEDIA_LOADING)
    { plugin->seek(mMediaPendingSeek); mMediaPendingSeek = -1.f; }
}
void VSNativeSession::parcelMediaCommand(U32 flags, U32 command, F32 time)
{
    if (command >= 32 || !(flags & (U32(1) << command))) return;
    if (command == PARCEL_MEDIA_COMMAND_STOP || command == PARCEL_MEDIA_COMMAND_UNLOAD) { setMediaEnabled(false); return; }
    if (mMediaUserStopped || (!gSavedSettings.getBOOL("PermAllowScriptedMedia") && !gSavedSettings.getBOOL("TempAllowScriptedMedia"))) return;
    if (command == PARCEL_MEDIA_COMMAND_PAUSE) setMediaPaused(true);
    else if (command == PARCEL_MEDIA_COMMAND_TIME && std::isfinite(time) && time >= 0)
    { mMediaPendingSeek = time; applyParcelMediaState(); }
    else if (command == PARCEL_MEDIA_COMMAND_PLAY || command == PARCEL_MEDIA_COMMAND_LOOP)
    {
        if (command == PARCEL_MEDIA_COMMAND_LOOP) mMediaLoop = true;
        mMediaStateDirty = true; setMediaEnabled(true);
    }
}

// Publish one native camera into the existing backend-neutral CPU frame.
bool VSNativeSession::publishCamera()
{
    if(mShutdown || mPhase!=Phase::Connected || mChangingRegion || !gViewerWindow)return false;
    synchronizeCameraMode();
    const auto state=cameraState();
    const auto viewport=gViewerWindow->getWindowRectRaw();
    if(viewport.getWidth()<=0 || viewport.getHeight()<=0 || !state.center.isFinite() ||
       !state.focus.isFinite() || !state.at.isFinite() || !state.left.isFinite() || !state.up.isFinite() || !state.bodyRotation.isFinite())return false;
    if(!LLViewerCamera::instanceExists())LLViewerCamera::createInstance();
    auto* camera=LLViewerCamera::getInstance();
    camera->setOrigin(state.center);camera->setAxes(state.at,state.left,state.up);
    gAgentCamera.publishNativeFocus(from_region_handle(mHandle)+LLVector3d(state.focus));
    // The shared CameraAngle observer and FOV/RLVa clamps own the lens.
    // Rendering, UI projection and native picking all consume that same view.
    camera->setNear(.1f);camera->setFar(vs_camera_far_clip());
    camera->setAspect(F32(viewport.getWidth())/viewport.getHeight());
    camera->setViewHeightInPixels(viewport.getHeight());
    camera->updateNativeFrustum();
    const auto forward=LLVector3::x_axis*state.bodyRotation;
    if(forward.isFinite() && !forward.isNull())gAgent.resetAxes(forward);
    return true;
}

VSNativeCamera::State VSNativeSession::cameraState() const
{
    auto view=mCamera.state(mPosition,gAgentCamera.getCameraOffsetInitial(),LLVector3(gAgentCamera.getFocusOffsetInitial()),gAgentCamera.getCameraOffsetScale());
    LLVector3d eye,focus;LLVector3 up;
    if(gAgentCamera.nativeCameraPose(eye,focus,up))
    {
        view.center=LLVector3(eye-from_region_handle(mHandle));view.focus=LLVector3(focus-from_region_handle(mHandle));
        LLCoordFrame frame;frame.lookAt(view.center,view.focus,up);
        view.at=frame.getAtAxis();view.left=frame.getLeftAxis();view.up=frame.getUpAxis();view.headRotation=frame.getQuaternion();
    }
    return view;
}
LLVector3d VSNativeSession::cameraTargetGlobal() const
{
    const auto target=mCamera.state(mPosition,gAgentCamera.getCameraOffsetInitial(),LLVector3(gAgentCamera.getFocusOffsetInitial()),gAgentCamera.getCameraOffsetScale());
    return from_region_handle(mHandle)+LLVector3d(target.center);
}

bool VSNativeSession::movementAction(U32 key, const std::string& action, bool down, bool repeated)
{
    if (!down) { const bool handled = mCamera.action(key, action, false, repeated); if (handled) mAgentUpdateTime = 0; return handled; }
    if (mPhase != Phase::Connected || mChangingRegion || mFrozen) return false;
    const bool handled = mCamera.action(key, action, true, repeated);
    if (handled && action == "toggle_fly")
    {
        gAgent.setFlying((mCamera.controls() & AGENT_CONTROL_FLY) != 0);
        mCamera.setFlying(gAgent.getFlying());
    }
    if (handled) mAgentUpdateTime = 0;
    return handled;
}
void VSNativeSession::releaseMovement()
{
    mCamera.release();
    gViewerInput.resetNativeKeys();
    mAgentUpdateTime = 0;
}
bool VSNativeSession::canFly() const
{
    if (!regionReady() || !mParcelPermissionsKnown || (mFlags & REGION_FLAGS_BLOCK_FLY)) return false;
    return bool(mParcelFlags & PF_ALLOW_FLY) || (mParcelOwner.notNull() &&
        (mParcelOwner == gAgent.getID() || gAgent.hasPowerInGroup(mParcelOwner, GP_LAND_ALLOW_FLY)));
}
void VSNativeSession::setFlying(bool flying)
{
    mCamera.setFlying(flying);
    mAgentUpdateTime = 0;
}
void VSNativeSession::mouseButton(bool down)
{
    if (!regionReady() || (!down && !mWorldMouseDown))
        return;
    if (down)
        mWorldMouseMouselook = mCamera.firstPerson();
    mWorldMouseDown  = down;
    mMouseControls   = mWorldMouseMouselook ? (down ? AGENT_CONTROL_ML_LBUTTON_DOWN : AGENT_CONTROL_ML_LBUTTON_UP)
                                            : (down ? AGENT_CONTROL_LBUTTON_DOWN : AGENT_CONTROL_LBUTTON_UP);
    mAgentUpdateTime = 0;
}
void VSNativeSession::faceBody(const LLVector3& direction)
{
    if (!regionReady() || !direction.isFinite() || direction.isNull()) return;
    const auto forward = LLVector3::x_axis * cameraState().bodyRotation;
    mCamera.turnBody(std::atan2(direction.mV[VY], direction.mV[VX]) - std::atan2(forward.mV[VY], forward.mV[VX]));
    mAgentUpdateTime = 0;
}
void VSNativeSession::cameraOrbit(F32 yaw, F32 pitch)
{ if (mPhase == Phase::Connected && !mChangingRegion) { mCamera.orbit(yaw, pitch); mAgentUpdateTime = 0; } }
void VSNativeSession::synchronizeCameraMode(bool reset_mode)
{
    if (!gAgentCamera.completeNativeCameraMode(mCamera.firstPerson(), reset_mode))
        mCamera.setZoomFraction(1.f); // Remain at the nearest third-person zoom.
}
void VSNativeSession::cameraZoom(S32 clicks)
{
    if(!regionReady())return;
    if(!gAgentCamera.getFocusOnAvatar()){gAgentCamera.handleScrollWheel(clicks);mAgentUpdateTime=0;return;}
    mCamera.zoom(clicks);synchronizeCameraMode();mAgentUpdateTime=0;
}
void VSNativeSession::cameraPan(const LLVector3& displacement)
{
    if (!regionReady()) return;
    mCamera.pan(displacement, gAgentCamera.getCameraOffsetInitial(), LLVector3(gAgentCamera.getFocusOffsetInitial()), gAgentCamera.getCameraOffsetScale());
    mAgentUpdateTime = 0;
}
void VSNativeSession::cameraRoll(F32 angle) { if(regionReady()) { gAgentCamera.cameraRollOver(angle);mAgentUpdateTime=0; } }
bool VSNativeSession::cameraLookAt(const LLVector3d& eye,const LLVector3d& focus)
{
    if(!gAgentCamera.setNativeCameraPose(eye,focus,LLUUID::null,false))return false;
    gAgentCamera.changeCameraToThirdPerson(false);mAgentUpdateTime=0;return true;
}
void VSNativeSession::cameraReset() { mCamera.resetView(); synchronizeCameraMode(true); mAgentUpdateTime = 0; }
void VSNativeSession::cameraResetOrbit() { mCamera.resetOrbit(); mAgentUpdateTime = 0; }
void VSNativeSession::cameraResetPan() { mCamera.resetPan(); mAgentUpdateTime = 0; }
void VSNativeSession::cameraResetRoll() { gAgentCamera.resetCameraRoll();mAgentUpdateTime=0; }
void VSNativeSession::cameraFirstPerson(bool first) { mCamera.setFirstPerson(first); synchronizeCameraMode(true); mAgentUpdateTime = 0; }
void VSNativeSession::cameraControls(F32 dt)
{
    if (std::isfinite(dt) && dt > 0)
    {
        const F32 elapsed = std::min(dt, .1f);
        if (!gAgent.rotateGrabbed()) mCamera.turnBody(gAgentCamera.getYawKey() * elapsed * 1.57079632679f);
        mCamera.orbit(0, -gAgentCamera.getPitchKey() * elapsed * 1.57079632679f);
    }
    if(!gAgentCamera.getFocusOnAvatar() || gAgentCamera.nativeHUDSelection())
    {
        gAgentCamera.applyNativeCameraKeyboard();gAgentCamera.clearGeneralKeys();return;
    }
    mCamera.controls(dt, gAgentCamera.getOrbitLeftKey() - gAgentCamera.getOrbitRightKey(),
        gAgentCamera.getOrbitUpKey() - gAgentCamera.getOrbitDownKey(),
        gAgentCamera.getOrbitInKey() - gAgentCamera.getOrbitOutKey(),
        LLVector3(gAgentCamera.getPanInKey() - gAgentCamera.getPanOutKey(),
                  gAgentCamera.getPanLeftKey() - gAgentCamera.getPanRightKey(),
                  gAgentCamera.getPanUpKey() - gAgentCamera.getPanDownKey()),
        0.f, // The original shared LLAgentCamera owns roll and its reset.
        gAgentCamera.getCameraOffsetInitial(), LLVector3(gAgentCamera.getFocusOffsetInitial()), gAgentCamera.getCameraOffsetScale());
    gAgentCamera.cameraRollOver((gAgentCamera.getRollRightKey()-gAgentCamera.getRollLeftKey())*45.f*DEG_TO_RAD/gFPSClamped);
    // The reference updateCamera also publishes input to the joystick artwork
    // and camera mode selection. Do this before consuming the held UI keys.
    synchronizeCameraMode();
    LLFloaterCamera::updateNativeControls();
    // Floater held callbacks republish intent each frame, exactly as in updateCamera.
    gAgentCamera.clearGeneralKeys();
    gAgentCamera.clearOrbitKeys();
    gAgentCamera.clearPanKeys();
}
bool VSNativeSession::updateAgentPosition(const LLVector3& position, const LLHost& sender, U64 generation, U64 epoch)
{
    if (mPhase != Phase::Connected || mChangingRegion || sender != mHost || generation != mGeneration || epoch != mRegionEpoch || !position.isFinite()) return false;
    mPosition = position; gAgent.setPositionAgent(position); return true;
}

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
bool VSNativeSession::teleportRequest(U64 handle, const LLVector3& pos, const LLVector3* lookAt)
{
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || !pos.isFinite() || pos.mV[VX] < 0 || pos.mV[VY] < 0 || pos.mV[VX] > 65536 || pos.mV[VY] > 65536) return false;
    if ((lookAt && !lookAt->isFinite()) || !vs_teleport_location_allowed(from_region_handle(handle) + LLVector3d(pos))) return false;
    LLVector3 direction = lookAt ? *lookAt : pos + LLVector3::x_axis;
    typing(false); mRequestedRegion.clear();
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessage("TeleportLocationRequest"); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession);
    msg->nextBlock("Info"); msg->addU64("RegionHandle", handle); msg->addVector3("Position", pos); msg->addVector3("LookAt", direction);
    if (msg->sendReliable(mHost) <= 0) return false;
    mTeleportFlags = 0;
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::teleportToLocation(const LLVector3d& global, const LLVector3* lookAt)
{
    if (!global.isFinite() || global.mdV[VX] < 0 || global.mdV[VY] < 0 || global.mdV[VX] >= std::numeric_limits<U32>::max() || global.mdV[VY] >= std::numeric_limits<U32>::max()) return false;
    const auto origin = from_region_handle(mHandle);
    const bool local = global.mdV[VX] >= origin.mdV[VX] && global.mdV[VX] < origin.mdV[VX] + mWidth && global.mdV[VY] >= origin.mdV[VY] && global.mdV[VY] < origin.mdV[VY] + mHeight;
    const U64 handle = local ? mHandle : to_region_handle(global);
    const auto destinationOrigin = from_region_handle(handle);
    return teleportRequest(handle, LLVector3(global - destinationOrigin), lookAt);
}
bool VSNativeSession::teleportToRegion(const std::string& input, const LLVector3& pos)
{
    std::string region = input; LLStringUtil::trim(region);
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || region.empty() || region.size() > 255 || !pos.isFinite()) return false;
    for (unsigned char c : region) if (c < 32 || c == 127) return false;
    if (LLStringUtil::compareInsensitive(region, mName) == 0) return teleportRequest(mHandle, pos);
    mTeleportFlags = 0;
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
    if (!vs_teleport_landmark_allowed(asset)) return false;
    if (asset.isNull()) gAgentCamera.resetView();
    typing(false); mRequestedRegion.clear();
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("TeleportLandmarkRequest"); msg->nextBlock("Info");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession); msg->addUUID("LandmarkID", asset);
    if (msg->sendReliable(mHost) <= 0) return false;
    mTeleportFlags = 0;
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::teleportToLure(const LLUUID& lure, bool godlike)
{
    if (mPhase != Phase::Connected || mChangingRegion || !gMessageSystem || lure.isNull()) return false;
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("TeleportLureRequest"); msg->nextBlock("Info");
    msg->addUUID("AgentID", mAgent); msg->addUUID("SessionID", mSession); msg->addUUID("LureID", lure);
    const U32 flags = godlike ? TELEPORT_FLAGS_VIA_GODLIKE_LURE | TELEPORT_FLAGS_DISABLE_CANCEL : TELEPORT_FLAGS_VIA_LURE;
    msg->addU32("TeleportFlags", flags);
    if (msg->sendReliable(mHost) <= 0) return false;
    mTeleportFlags = flags;
    ++mSent; mNavigationDeadline = now() + 60; return true;
}
bool VSNativeSession::changeRegion(const LLSD& body)
{
    const LLVector3d globalPosition=from_region_handle(mHandle)+LLVector3d(mPosition);
    const S32 port = body["port"].asInteger(); const U32 ip = U32(body["ip"].asInteger());
    const LLHost destination(ip, port > 0 && port <= 65535 ? port : 0);
    const std::string seed = body["seed"].asString();
    if (!destination.isOk() || !webURL(seed) || mChangingRegion ||
        (body.has("width") && (body["width"].asInteger() <= 0 || body["width"].asInteger() > 65536)) ||
        (body.has("height") && (body["height"].asInteger() <= 0 || body["height"].asInteger() > 65536))) return false;
    // Only an authenticated current simulator can grant the next host. Inbound
    // packets and capability/circuit completions from the retired region expire.
    mLocationRequests.clear();
    releaseMovement(); mCameraTime = 0;
    clearParcelPlayback();
    const U64 nextHandle=ll_U64_from_sd(body["handle"]);
    const U32 width=body.has("width")?U32(body["width"].asInteger()):256;
    const U32 height=body.has("height")?U32(body["height"].asInteger()):256;
    if(destination==mHost && nextHandle!=mHandle)
    {
        auto& reused=mCircuits[destination];if(reused.cancel)reused.cancel();
        reused=RegionCircuit{};reused.handle=nextHandle;reused.width=width;reused.height=height;
        reused.incarnation=mNextCircuitGeneration++;reused.deadline=now()+60;
    }
    else if (!enableNeighbour(destination,nextHandle,width,height)) return false;
    auto& destinationCircuit=mCircuits.at(destination);
    if (gViewerWindow && gViewerWindow->nativeContext() &&
        !gViewerWindow->nativeContext()->world().activateRegion(destination,nextHandle,width,height,destinationCircuit.incarnation)) return false;
    if (auto old=mCircuits.find(mHost);old!=mCircuits.end()) old->second.poll=std::move(mPoll);
    auto cancelNeighbour=std::exchange(destinationCircuit.cancel,{});if (cancelNeighbour) cancelNeighbour();
    destinationCircuit.poll.reset();
    if (FSLSLBridge::instanceExists()) FSLSLBridge::instance().resetNativeAccount();
    ++mRegionEpoch; mRegionLimits.reset(); clearSimulatorFeatures(); mPreviousHost = mHost; mHost = destination;
    mHandle = ll_U64_from_sd(body["handle"]); mSeed = seed; mName.clear();
    if(body["teleport"].asBoolean())
    {
        std::vector<LLHost> retired;
        for(const auto& [host,region]:mCircuits) if(host!=mHost)retired.push_back(host);
        for(const auto& host:retired)retireNeighbour(host);
        mPreviousHost=LLHost();
    }
    mWidth = body.has("width") ? U32(body["width"].asInteger()) : 256; mHeight = body.has("height") ? U32(body["height"].asInteger()) : 256;
    mTeleportArrival = body["teleport"].asBoolean();
    mChangingRegion = true; mInterestListArriving = true; mNavigationDeadline = now() + 60;
    mHandshake = mMovement = mCircuitAck = mSeedReady = false;
    auto cancel = std::move(mCancelSeed); if (cancel) cancel();
    mPoll.reset(); mCapabilities = LLSD::emptyMap();
    gAgent.initOriginGlobal(from_region_handle(mHandle));
    mPosition=LLVector3(globalPosition-from_region_handle(mHandle));gAgent.setPositionAgent(mPosition);
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
bool VSNativeSession::cancelTeleport()
{
    if(mPhase!=Phase::Connected || mChangingRegion || !mNavigationDeadline || !gMessageSystem || !vs_teleport_cancel_allowed(mTeleportFlags))return false;
    gMessageSystem->newMessage("TeleportCancel");gMessageSystem->nextBlock("Info");
    gMessageSystem->addUUID("AgentID",mAgent);gMessageSystem->addUUID("SessionID",mSession);
    if(gMessageSystem->sendReliable(mHost)<=0)return false;
    ++mSent;mRequestedRegion.clear();mNavigationDeadline=0;
    if(gViewerWindow && mStartup == Startup::Complete)gViewerWindow->setShowProgress(false,true);
    return true;
}

bool VSNativeSession::admit(const std::string& name, const LLHost& sender)
{
    const auto peer=mCircuits.find(sender);
    const bool known=sender==mHost || peer!=mCircuits.end();
    if (transport.count(name)) return mHost.isOk() && known && mPhase != Phase::Disconnected;
    if(name=="AvatarSitResponse")return regionReady() && peer!=mCircuits.end() && peer->second.acknowledged && peer->second.handshake;
    if(name=="RegionInfo" || name=="EstateOwnerMessage" || name=="TelehubInfo" || name=="LandStatReply" || name=="SimConsoleResponse")return regionReady() && sender==mHost;
    if (name=="NavMeshStatusUpdate") return mPhase==Phase::Connected && peer!=mCircuits.end() && peer->second.acknowledged && peer->second.handshake;
    if (name=="AgentStateUpdate") return regionReady() && sender==mHost;
    if (name=="LayerData") return mHost.isOk() && known && (mPhase==Phase::Connecting || mPhase==Phase::Connected);
    if(name=="ParcelProperties" || name=="ParcelAccessListReply" || name=="ParcelDwellReply" || name=="ParcelObjectOwnersReply" || name=="EstateCovenantReply")
        return mPhase==Phase::Connected && !mChangingRegion && peer!=mCircuits.end() &&
            peer->second.acknowledged && peer->second.handshake;
    if (name=="ParcelOverlay" || name=="CoarseLocationUpdate") return mPhase==Phase::Connected && peer!=mCircuits.end() &&
        peer->second.acknowledged && peer->second.handshake;
    if (name=="ObjectPhysicsProperties" || name=="PayPriceReply" || name=="ObjectProperties" || name=="ObjectPropertiesFamily" || name=="ForceObjectSelect")
        return mPhase==Phase::Connected && peer!=mCircuits.end() && peer->second.acknowledged && peer->second.handshake;
    if (worldMessages.count(name)) return (mPhase==Phase::Connecting || mPhase==Phase::Connected) &&
        (sender==mHost || (peer!=mCircuits.end() && peer->second.acknowledged && peer->second.handshake));
    if (known && (name=="RegionHandshake" || name=="DisableSimulator" || name=="CloseCircuit"))
        return mPhase==Phase::Connecting || mPhase==Phase::Connected;
    if (sender==mHost && (name=="EnableSimulator" || name=="EstablishAgentCommunication")) return mPhase==Phase::Connecting || mPhase==Phase::Connected;
    if ((mPhase == Phase::Connecting || mPhase == Phase::Connected || mPhase == Phase::Logout)
        && sender == mHost && session.count(name)) return true;
    // Consume after transport decoding/ACK; do not invoke a scene or HTTP node.
    if (++mRejected <= 8) LL_WARNS("NativeSession") << "Deferred native message: " << name << LL_ENDL;
    return false;
}
void VSNativeSession::receive(LLMessageSystem* msg)
{
    const std::string name = msg->getMessageName();
    if(name=="PayPriceReply")
    {if(admit(name,msg->getSender()))LLFloaterPayUtil::processNativePriceReply(msg);return;}
    if(name=="AvatarSitResponse")
    {
        if(admit(name,msg->getSender()))process_avatar_sit_response(msg,nullptr);
        else ++mRejected;
        return;
    }
    if(name=="RegionInfo")
    {
        if(!applyRegionInfo(*msg))++mRejected;
        else if(LLFloaterReg::instanceVisible("god_tools"))LLFloaterGodTools::processRegionInfo(msg);
        return;
    }
    if(name=="TelehubInfo"){if(admit(name,msg->getSender()))LLFloaterTelehub::processTelehubInfo(msg,nullptr);return;}
    if(name=="LandStatReply"){if(admit(name,msg->getSender()))LLFloaterTopObjects::handle_land_reply(msg,nullptr);return;}
    if(name=="EstateOwnerMessage")
    {
        if(admit(name,msg->getSender()))LLFloaterRegionInfo::processEstateOwnerRequest(msg,nullptr);
        else ++mRejected;
        return;
    }
    if (name=="PayPriceReply" || name=="ObjectProperties" || name=="ObjectPropertiesFamily" || name=="ForceObjectSelect")
    {
        if (!admit(name,msg->getSender())) return;
        const auto current=accountGuard();
        if (mObjectSelection) mObjectSelection->receive(*msg);
        if (current() && name=="ObjectProperties")
            if (auto* view=LLFloaterReg::findTypedInstance<FSAreaSearch>("area_search"))
            {
                const auto viewCurrent=view->accountGuard();
                if (viewCurrent && viewCurrent()) view->processObjectProperties(msg);
            }
        return;
    }
    if (worldMessages.count(name))
    {
        // Apply the same admission at the consumer boundary, including direct
        // callers; stale/foreign simulator traffic cannot mutate this scene.
        if (admit(name,msg->getSender()) && gViewerWindow && gViewerWindow->nativeContext())
        {
            auto& world = gViewerWindow->nativeContext()->world();
            const auto circuit=mCircuits.find(msg->getSender());
            worldSound(); // Subscribe before accepted object updates publish sound state.
            world.receive(*msg,circuit==mCircuits.end()?mHandle:circuit->second.handle,mAgent);
            if (name == "ReplyTaskInventory" && mTaskInventory)
            {
                const auto current = accountGuard(true);
                mTaskInventory->receive(*msg);
                if (!current()) return; // listeners may synchronously retire the scene/account
            }
            LLVector3 position;
            if (world.agentPosition(position)) updateAgentPosition(position, msg->getSender(), mGeneration, mRegionEpoch);
        }
        return;
    }
    if(name=="ParcelAccessListReply" || name=="ParcelDwellReply" || name=="ParcelObjectOwnersReply" || name=="EstateCovenantReply")
    {
        if(!admit(name,msg->getSender()))return;
        if(name=="ParcelAccessListReply")LLViewerParcelMgr::processParcelAccessListReply(msg,nullptr);
        else if(name=="ParcelDwellReply")LLViewerParcelMgr::processParcelDwellReply(msg,nullptr);
        else if(name=="ParcelObjectOwnersReply")LLPanelLandObjects::processParcelObjectOwnersReply(msg,nullptr);
        else vs_receive_covenant(msg);
        return;
    }
    if(name=="ParcelProperties")
    {
        if(admit(name,msg->getSender()) && regionReady())LLViewerParcelMgr::processParcelProperties(msg,nullptr);
        return;
    }
    LLSD body = LLSD::emptyMap();
    if(name=="ParcelOverlay")
    {
        const S32 size=msg->getSize("ParcelData","Data");
        if(size!=1024){++mRejected;return;}
        S32 sequence;msg->getS32("ParcelData","SequenceID",sequence);
        std::vector<U8> bytes(1024);msg->getBinaryData("ParcelData","Data",bytes.data(),1024,0,1024);
        if(!receiveParcelOverlay(msg->getSender(),mGeneration,circuitGeneration(msg->getSender()),sequence,bytes))++mRejected;
        return;
    }
    if(name=="CoarseLocationUpdate")
    {
        const S32 count=msg->getNumberOfBlocks("Location"),identities=msg->getNumberOfBlocks("AgentData");
        if(count<0 || count>4096 || (identities!=0 && identities!=count)) {++mRejected;return;}
        S16 you,prey;msg->getS16("Index","You",you);msg->getS16("Index","Prey",prey);
        LLSD index;index["You"]=S32(you);index["Prey"]=S32(prey);body["Index"].append(index);
        body["Location"]=LLSD::emptyArray();
        if(identities)body["AgentData"]=LLSD::emptyArray();
        for(S32 i=0;i<count;++i)
        {
            U8 x,y,z;msg->getU8("Location","X",x,i);msg->getU8("Location","Y",y,i);msg->getU8("Location","Z",z,i);
            LLSD row;row["X"]=S32(x);row["Y"]=S32(y);row["Z"]=S32(z);body["Location"].append(row);
            if(identities){LLUUID id;msg->getUUID("AgentData","AgentID",id,i);LLSD agent;agent["AgentID"]=id;body["AgentData"].append(agent);}
        }
        if(!receiveCoarse(body,msg->getSender(),mGeneration,circuitGeneration(msg->getSender()),true))++mRejected;
        return;
    }
    if(name=="AgentWearablesUpdate")
    {
        LLUUID agent,identity;U32 serial;
        msg->getUUID("AgentData","AgentID",agent);msg->getUUID("AgentData","SessionID",identity);msg->getU32("AgentData","SerialNum",serial);
        body["agent"]=agent;body["session"]=identity;body["serial"]=ll_sd_from_U32(serial);body["wearables"]=LLSD::emptyArray();
        const S32 count=msg->getNumberOfBlocks("WearableData");
        if(count<0 || count>255){++mRejected;return;}
        for(S32 i=0;i<count;++i)
        {
            LLUUID item,asset;U8 type;msg->getUUID("WearableData","ItemID",item,i);msg->getUUID("WearableData","AssetID",asset,i);
            msg->getU8("WearableData","WearableType",type,i);
            LLSD row;row["item"]=item;row["asset"]=asset;row["type"]=S32(type);body["wearables"].append(row);
        }
    }
    else if (name=="EnableSimulator")
    {
        U64 handle;U32 ip;U16 port;msg->getU64("SimulatorInfo","Handle",handle);
        msg->getIPAddr("SimulatorInfo","IP",ip);msg->getIPPort("SimulatorInfo","Port",port);
        body["handle"]=ll_sd_from_U64(handle);body["ip"]=LLSD::Integer(ip);body["port"]=port;
        if (msg->getSize("SimulatorInfo","RegionSizeX")>0)
        { U32 width,height;msg->getU32("SimulatorInfo","RegionSizeX",width);msg->getU32("SimulatorInfo","RegionSizeY",height);body["width"]=S32(width);body["height"]=S32(height); }
    }
    else if (name == "ParcelProperties")
    {
        S32 result, sequence, id; std::string parcel, music, media;
        msg->getS32("ParcelData", "RequestResult", result); msg->getS32("ParcelData", "SequenceID", sequence);
        msg->getS32("ParcelData", "LocalID", id); msg->getString("ParcelData", "Name", parcel);
        msg->getString("ParcelData", "MusicURL", music); msg->getString("ParcelData", "MediaURL", media);
        body["result"] = result; body["sequence"] = sequence; body["id"] = id;
        body["name"] = parcel; body["music"] = music; body["media"] = media;
        U32 flags; msg->getU32("ParcelData", "ParcelFlags", flags);
        body["parcel_flags"] = LLSD::Integer(flags);
        LLUUID owner; msg->getUUID("ParcelData", "OwnerID", owner); body["parcel_owner"] = owner;
        // As in LLParcel::unpackMessage, legacy servers default SeeAVs to true.
        bool seeAvatars = true;
        if (msg->getSize("ParcelData", "SeeAVs") > 0) msg->getBOOL("ParcelData", "SeeAVs", seeAvatars);
        body["see_avatars"] = seeAvatars;
        U8 autoscale; msg->getU8("ParcelData", "MediaAutoScale", autoscale); body["autoscale"] = LLSD::Integer(autoscale);
        if (msg->getNumberOfBlocks("MediaData"))
        {
            std::string type; msg->getString("MediaData", "MediaType", type); body["type"] = type;
            U8 loop; S32 width, height; msg->getU8("MediaData", "MediaLoop", loop);
            msg->getS32("MediaData", "MediaWidth", width); msg->getS32("MediaData", "MediaHeight", height);
            body["loop"] = LLSD::Integer(loop); body["width"] = width; body["height"] = height;
        }
    }
    else if (name == "ParcelMediaUpdate")
    {
        std::string media, type; msg->getString("DataBlock", "MediaURL", media);
        U8 autoscale; msg->getU8("DataBlock", "MediaAutoScale", autoscale); body["autoscale"] = LLSD::Integer(autoscale);
        if (msg->getNumberOfBlocks("DataBlockExtended"))
        {
            msg->getString("DataBlockExtended", "MediaType", type);
            U8 loop; S32 width, height; msg->getU8("DataBlockExtended", "MediaLoop", loop);
            msg->getS32("DataBlockExtended", "MediaWidth", width); msg->getS32("DataBlockExtended", "MediaHeight", height);
            body["loop"] = LLSD::Integer(loop); body["width"] = width; body["height"] = height;
        }
        body["media"] = media; body["type"] = type;
    }
    else if (name == "ParcelMediaCommandMessage")
    {
        U32 flags, command; F32 time; msg->getU32("CommandBlock", "Flags", flags);
        msg->getU32("CommandBlock", "Command", command); msg->getF32("CommandBlock", "Time", time);
        body["flags"] = LLSD::Integer(flags); body["command"] = LLSD::Integer(command); body["time"] = time;
    }
    else if (name == "RegionIDAndHandleReply")
    {
        LLUUID id; U64 handle; msg->getUUID("ReplyBlock", "RegionID", id); msg->getU64("ReplyBlock", "RegionHandle", handle);
        body["id"] = id; body["handle"] = ll_sd_from_U64(handle);
    }
    else if (name == "MapItemReply")
    {
        LLUUID agent;msg->getUUID("AgentData","AgentID",agent);body["agent"]=agent;
    }
    else if (name == "MapBlockReply")
    {
        const S32 blocks=msg->getNumberOfBlocks("Data"),sizes=msg->getNumberOfBlocks("Size");
        if(blocks<0 || blocks>4096 || (sizes!=0 && sizes!=blocks)){++mRejected;return;}
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
    else if(name=="TeleportStart" || name=="TeleportProgress")
    {
        U32 flags;msg->getU32("Info","TeleportFlags",flags);body["flags"]=ll_sd_from_U32(flags);
        if(name=="TeleportProgress")
        {LLUUID agent;std::string message;msg->getUUID("AgentData","AgentID",agent);msg->getString("Info","Message",message);body["agent"]=agent;body["text"]=message;}
    }
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
        bool estateManager;msg->getBOOL("RegionInfo","IsEstateManager",estateManager);
        body["estate_manager"]=estateManager;
        F32 billable;msg->getF32("RegionInfo","BillableFactor",billable);body["billable_factor"]=billable;
        if(msg->getNumberOfBlocks("RegionInfo4")>0)
        {
            U64 protocols;msg->getU64("RegionInfo4","RegionProtocols",protocols);
            body["region_protocols"]=ll_sd_from_U64(protocols);
        }
        if(msg->getSize("RegionInfo3","ColoName")>0 || msg->getSize("RegionInfo3","ProductSKU")>0 ||
           msg->getSize("RegionInfo3","ProductName")>0)
        {
            S32 cpuClass,cpuRatio;std::string colo,sku,product;
            msg->getS32("RegionInfo3","CPUClassID",cpuClass);msg->getS32("RegionInfo3","CPURatio",cpuRatio);
            msg->getString("RegionInfo3","ColoName",colo);msg->getString("RegionInfo3","ProductSKU",sku);
            msg->getString("RegionInfo3","ProductName",product);
            body["product"]["cpu_class"]=cpuClass;body["product"]["cpu_ratio"]=cpuRatio;
            body["product"]["colo"]=colo;body["product"]["sku"]=sku;body["product"]["name"]=product;
        }
        F32 water; msg->getF32("RegionInfo", "WaterHeight", water); body["water_height"] = water;
        const char* details[]={"TerrainDetail0","TerrainDetail1","TerrainDetail2","TerrainDetail3"};
        const char* starts[]={"TerrainStartHeight00","TerrainStartHeight01","TerrainStartHeight10","TerrainStartHeight11"};
        const char* ranges[]={"TerrainHeightRange00","TerrainHeightRange01","TerrainHeightRange10","TerrainHeightRange11"};
        for (unsigned i=0;i<4;++i)
        {
            LLUUID texture; F32 start,range; msg->getUUID("RegionInfo",details[i],texture);
            msg->getF32("RegionInfo",starts[i],start);msg->getF32("RegionInfo",ranges[i],range);
            body["terrain_textures"].append(texture);body["terrain_starts"].append(start);body["terrain_ranges"].append(range);
        }
        body["name"] = region; body["id"] = id; body["owner"] = owner; body["flags"] = LLSD::Integer(flags); body["access"] = access;
    }
    else if (name == "AgentMovementComplete")
    {
        LLUUID agent, identity; U64 handle; LLVector3 pos;
        msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", identity);
        msg->getU64("Data", "RegionHandle", handle); msg->getVector3("Data", "Position", pos);
        body["agent"] = agent; body["session"] = identity; body["handle"] = ll_sd_from_U64(handle); body["position"] = ll_sd_from_vector3(pos);
        LLVector3 look; msg->getVector3("Data", "LookAt", look); body["look_at"] = ll_sd_from_vector3(look);
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
    const U64 generation=mGeneration;
    const LLHost source=msg->getSender();
    if(deliver(name,body,source,generation) && mGeneration==generation && mPhase==Phase::Connected && source==mHost)
    {
        // Keep complete wire fields and existing shared map/transaction semantics.
        if(name=="MoneyBalanceReply")process_money_balance_reply(msg,nullptr);
        else if(name=="MapBlockReply")LLWorldMapMessage::processMapBlockReply(msg,nullptr);
        else if(name=="MapItemReply")LLWorldMapMessage::processMapItemReply(msg,nullptr);
    }
}
namespace
{
std::optional<VSNativeSession::RegionMetadata> nativeRegionMetadata(
    const LLSD& body,const VSNativeSession::RegionMetadata& previous)
{
    auto result=previous;
    if(body.has("billable_factor"))
    {
        if(!body["billable_factor"].isReal() || !std::isfinite(body["billable_factor"].asReal()) ||
           !std::isfinite(F32(body["billable_factor"].asReal())))return {};
        result.billableFactor=F32(body["billable_factor"].asReal());result.billableKnown=true;
    }
    if(body.has("region_protocols"))
    {
        if(!body["region_protocols"].isBinary() || body["region_protocols"].asBinary().size()!=8)return {};
        result.protocols=ll_U64_from_sd(body["region_protocols"]);result.protocolsKnown=true;
    }
    if(body.has("product"))
    {
        const auto& product=body["product"];
        if(!product.isMap() || !product["cpu_class"].isInteger() || !product["cpu_ratio"].isInteger())return {};
        for(const char* name:{"colo","sku","name"})
            if(!product[name].isString() || product[name].asString().size()>255)return {};
        result.cpuClass=product["cpu_class"].asInteger();result.cpuRatio=product["cpu_ratio"].asInteger();
        result.coloName=product["colo"].asString();result.productSKU=product["sku"].asString();
        result.productName=product["name"].asString();result.productKnown=true;
    }
    return result;
}
}
bool VSNativeSession::deliver(const std::string& name, const LLSD& body, const LLHost& sender, U64 generation)
{
    if (generation != mGeneration) { ++mExpired; return false; }
    if (!body.isMap() || !admit(name, sender)) return false;
    std::optional<RegionMetadata> handshakeMetadata;
    if(name=="RegionHandshake")
    {
        const auto circuit=mCircuits.find(sender);
        if(circuit==mCircuits.end())return false;
        handshakeMetadata=nativeRegionMetadata(body,circuit->second.metadata);
        if(!handshakeMetadata)return false;
    }
    if(name=="RegionHandshake" && body.has("estate_manager") && !body["estate_manager"].isBoolean())return false;
    if (name=="EnableSimulator")
    {
        if (sender!=mHost) return false;
        const U64 handle=ll_U64_from_sd(body["handle"]);
        if (gSavedSettings.getBOOL("FSDisableNeighbourRegionConnections") && handle!=mHandle) return true;
        const S32 port=body["port"].asInteger();if (port<1 || port>65535) return false;
        return enableNeighbour(LLHost(U32(body["ip"].asInteger()),port),handle,
            body.has("width")?U32(body["width"].asInteger()):256,body.has("height")?U32(body["height"].asInteger()):256);
    }
    if (sender!=mHost)
    {
        auto region=mCircuits.find(sender);if (region==mCircuits.end()) return false;
        if (name=="DisableSimulator" || name=="CloseCircuit") {retireNeighbour(sender);return true;}
        if (name!="RegionHandshake" || !body["name"].isString() || body["id"].asUUID().isNull()) return false;
        if (gViewerWindow && gViewerWindow->nativeContext() && !gViewerWindow->nativeContext()->world().configureRegion(sender,body)) return false;
        region->second.id=body["id"].asUUID();region->second.owner=body["owner"].asUUID();
        region->second.name=body["name"].asString().substr(0,256);
        region->second.flags=body.has("flags64")?ll_U64_from_sd(body["flags64"]):U32(body["flags"].asInteger());
        region->second.access=U8(body["access"].asInteger());
        region->second.waterHeight=F32(body["water_height"].asReal());
        region->second.waterKnown=body["water_height"].isReal()&&std::isfinite(region->second.waterHeight);
        region->second.metadata=*handshakeMetadata;
        region->second.estateManagerKnown=body["estate_manager"].isBoolean();
        region->second.estateManager=region->second.estateManagerKnown && body["estate_manager"].asBoolean();
        region->second.handshake=true;if (region->second.acknowledged) region->second.deadline=0;
        gMessageSystem->newMessage("RegionHandshakeReply");gMessageSystem->nextBlock("AgentData");
        gMessageSystem->addUUID("AgentID",mAgent);gMessageSystem->addUUID("SessionID",mSession);
        gMessageSystem->nextBlock("RegionInfo");gMessageSystem->addU32("Flags",0);gMessageSystem->sendReliable(sender);
        updateInterestListMode();
        ++mReceived;return true;
    }
    if (name == "ParcelProperties" && mPhase == Phase::Connected && !mChangingRegion)
    {
        if (!body["result"].isInteger() || !body["sequence"].isInteger() || !body["id"].isInteger() ||
            !body["name"].isString() || !body["music"].isString() || !body["media"].isString() ||
            (body.has("type") && !body["type"].isString())) return false;
        if ((body.has("parcel_flags") && !body["parcel_flags"].isInteger()) ||
            (body.has("see_avatars") && !body["see_avatars"].isBoolean())) return false;
        const S32 sequence = body["sequence"].asInteger();
        // Negative sequences describe selection/hover/collision, not the
        // agent parcel. Positive replies match the current location request.
        if (body["result"].asInteger() != PARCEL_RESULT_SUCCESS || body["id"].asInteger() < 0 ||
            (sequence != 0 && sequence != mParcelSequence)) return false;
        std::string music = body["music"].asString(), media = body["media"].asString();
        LLStringUtil::trim(music); LLStringUtil::trim(media);
        if ((!music.empty() && !webURL(music)) || (!media.empty() && !webURL(media))) return false;
        for (const char* key : {"loop", "autoscale", "width", "height"})
            if (body.has(key) && (!body[key].isInteger() || body[key].asInteger() < 0 ||
                body[key].asInteger() > ((std::string(key) == "loop" || std::string(key) == "autoscale") ? 1 : 4096))) return false;
        const bool changed = mParcelID != body["id"].asInteger() || mMusicURL != music || mMediaURL != media || mMediaType != body["type"].asString();
        const bool resumeMusic = mMusicRequested;
        const bool resumeMedia = mMediaRequested;
        const bool pausedMedia = mMediaPaused;
        if (changed) clearParcelPlayback();
        mParcelID = body["id"].asInteger(); mParcelName = body["name"].asString();
        mParcelPermissionsKnown = body.has("parcel_flags");
        mParcelFlags = U32(body["parcel_flags"].asInteger());
        mParcelOwner = body["parcel_owner"].asUUID();
        mParcelSeeAvatars = !body.has("see_avatars") || body["see_avatars"].asBoolean();
        mMusicURL = music; mMediaURL = media; mMediaType = body["type"].asString();
        updateParcelMediaMetadata(body);
        if (changed && (resumeMusic || (!mMusicUserStopped && gSavedSettings.getBOOL("FSParcelMusicAutoPlay") && gSavedSettings.getBOOL("MediaTentativeAutoPlay")))) setMusicEnabled(true);
        if (changed && (resumeMedia || (!mMediaUserStopped && gSavedSettings.getS32("ParcelMediaAutoPlayEnable") == 1 && gSavedSettings.getBOOL("MediaTentativeAutoPlay"))))
        { setMediaEnabled(true); if (resumeMedia) setMediaPaused(pausedMedia); }
    }
    else if (name == "ParcelMediaUpdate" && mPhase == Phase::Connected && !mChangingRegion)
    {
        if (!body["media"].isString() || !body["type"].isString()) return false;
        std::string media = body["media"].asString(); LLStringUtil::trim(media);
        if (mParcelID < 0 || (!media.empty() && !webURL(media))) return false;
        for (const char* key : {"loop", "autoscale", "width", "height"})
            if (body.has(key) && (!body[key].isInteger() || body[key].asInteger() < 0 ||
                body[key].asInteger() > ((std::string(key) == "loop" || std::string(key) == "autoscale") ? 1 : 4096))) return false;
        const bool resume = mMediaRequested;
        const bool paused = mMediaPaused;
        const bool sourceChanged = mMediaURL != media || mMediaType != body["type"].asString();
        const bool userStopped = mMediaUserStopped;
        if (sourceChanged) { setMediaEnabled(false); mMediaUserStopped = userStopped; ++mParcelRevision; }
        mMediaURL = media; mMediaType = body["type"].asString();
        updateParcelMediaMetadata(body);
        if (sourceChanged && resume) { setMediaEnabled(true); setMediaPaused(paused); }
        else applyParcelMediaState();
    }
    else if (name == "ParcelMediaCommandMessage" && mPhase == Phase::Connected && !mChangingRegion)
    {
        if (!body["flags"].isInteger() || !body["command"].isInteger() || !body["time"].isReal()) return false;
        parcelMediaCommand(U32(body["flags"].asInteger()), U32(body["command"].asInteger()), F32(body["time"].asReal()));
    }
    else if (name == "RegionHandshake" && (mPhase == Phase::Connecting || mPhase == Phase::Connected))
    {
        if (!body["name"].isString() || body["id"].asUUID().isNull()) return false;
        mName = body["name"].asString().substr(0, 256); mRegionID = body["id"].asUUID(); mOwner = body["owner"].asUUID();
        mFlags = body.has("flags64") ? ll_U64_from_sd(body["flags64"]) : U32(body["flags"].asInteger()); mAccess = U8(body["access"].asInteger());
        mWaterHeight = F32(body["water_height"].asReal());
        mWaterKnown  = body["water_height"].isReal() && std::isfinite(mWaterHeight);
        LLLandmark::setRegionHandle(mRegionID, mHandle);
        if (gViewerWindow && gViewerWindow->nativeContext()) gViewerWindow->nativeContext()->world().configureRegion(sender,body);
        if(auto region=mCircuits.find(sender);region!=mCircuits.end())
        {
        region->second.id=body["id"].asUUID();region->second.owner=body["owner"].asUUID();
        region->second.name=body["name"].asString().substr(0,256);
        region->second.flags=body.has("flags64")?ll_U64_from_sd(body["flags64"]):U32(body["flags"].asInteger());
        region->second.access=U8(body["access"].asInteger());
        region->second.waterHeight=F32(body["water_height"].asReal());
        region->second.waterKnown=body["water_height"].isReal()&&std::isfinite(region->second.waterHeight);
        region->second.metadata=*handshakeMetadata;
        region->second.estateManagerKnown=body["estate_manager"].isBoolean();
        region->second.estateManager=region->second.estateManagerKnown && body["estate_manager"].asBoolean();
            region->second.handshake=true;
        }
        mHandshake = true; send("RegionHandshakeReply", LLSD()); connected();
    }
    else if (name == "AgentMovementComplete" && (mPhase == Phase::Connecting || mPhase == Phase::Connected))
    {
        if (body["agent"].asUUID() != mAgent || body["session"].asUUID() != mSession) return false;
        if (ll_U64_from_sd(body["handle"]) != mHandle) return false;
        if (!body["position"].isArray() || body["position"].size() != 3) return false;
        LLVector3 pos = ll_vector3_from_sd(body["position"]); if (!pos.isFinite()) return false;
        mPosition = pos; gAgent.setPositionAgent(pos); releaseMovement();
        if (body.has("look_at")) mCamera.face(ll_vector3_from_sd(body["look_at"]));
        mMovement = true; connected();
    }
    else if(name=="AgentWearablesUpdate" && (mPhase==Phase::Connecting || mPhase==Phase::Connected))
    {
        if(body["agent"].asUUID()!=mAgent || body["session"].asUUID()!=mSession || !body["wearables"].isArray() || body["wearables"].size()>255)return false;
        const U32 serial=ll_U32_from_sd(body["serial"]);
        if(mWearablesKnown && (U32(serial-mWearablesSerial)>0x7fffffffu ||
           (serial==mWearablesSerial && body["wearables"]!=mWearablesMetadata)))return false;
        for(const auto& row:llsd::inArray(body["wearables"]))
            if(!row.isMap() || !row["type"].isInteger() || row["type"].asInteger()<0 || row["type"].asInteger()>255)return false;
        mWearablesSerial=serial;mWearablesMetadata=body["wearables"];mWearablesKnown=true;publishProgress();
        if (mAppearance)
            mAppearance->fallback(mWearablesMetadata);
    }
    else if (name == "MoneyBalanceReply" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent) return false;
        updateBalance(body["balance"].asInteger());
        mLandCredit = body["land_credit"].asInteger(); mLandCommitted = body["land_committed"].asInteger();
    }
    else if (name == "ChatFromSimulator" && mPhase == Phase::Connected) appendChat(body, generation);
    else if (name == "LogoutReply" && mPhase == Phase::Logout)
    {
        if (body["agent"].asUUID() != mAgent || body["session"].asUUID() != mSession) return false;
        mLogoutReply = true;
        if (mLogoutPresented) finishLogout();
    }
    else if (name == "KickUser")
    { if (body["agent"].asUUID() != mAgent || body["session"].asUUID() != mSession) return false; disconnect(body["text"].asString()); }
    else if (name == "ViewerFrozenMessage")
    {
        if (!body["frozen"].isBoolean()) return false;
        mFrozen = body["frozen"].asBoolean(); if (mFrozen) releaseMovement(); gViewerWindow->getWindow()->resetBusyCount();
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
    else if (name == "MapItemReply" && mPhase == Phase::Connected)
    {
        if(body["agent"].asUUID()!=mAgent)return false;
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
        LLSD destination=body;destination["teleport"]=name=="TeleportFinish";
        return changeRegion(destination);
    }
    else if (name == "TeleportLocal" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent || !body["position"].isArray() || body["position"].size() != 3) return false;
        const auto pos = ll_vector3_from_sd(body["position"]); if (!pos.isFinite()) return false;
        mPosition = pos; gAgent.setPositionAgent(pos); releaseMovement(); mNavigationDeadline = 0;
        if (LLVoiceClient::instanceExists()) LLVoiceClient::instance().setHidden(false);
        if(gViewerWindow && mStartup == Startup::Complete)gViewerWindow->setShowProgress(false,true);
        LLTeleportHistory::instance().updateNativeLocation(gAgent.getPositionGlobal());
        const U64 accountGeneration=mGeneration;
        LLViewerParcelMgr::instance().onTeleportFinished(true,gAgent.getPositionGlobal());
        if(mGeneration!=accountGeneration || mPhase!=Phase::Connected)return false;
        mTeleportFlags=0;
        requestParcel();
    }
    else if (name == "TeleportFailed" && mPhase == Phase::Connected)
    {
        if (body["agent"].asUUID() != mAgent) return false;
        mRequestedRegion.clear(); mNavigationDeadline = 0;mTeleportFlags=0;
        if (LLVoiceClient::instanceExists()) LLVoiceClient::instance().setHidden(false);
        const U64 accountGeneration=mGeneration;
        LLViewerParcelMgr::instance().onTeleportFailed();
        if(mGeneration!=accountGeneration || mPhase!=Phase::Connected)return false;
        if(gViewerWindow && mStartup == Startup::Complete)gViewerWindow->setShowProgress(false,true);
        LLSD args; args["ERROR_MESSAGE"] = body["text"].asString().substr(0, 4096); LLNotificationsUtil::add("ErrorMessage", args);
    }
    else if ((name == "TeleportStart" || name == "TeleportProgress") && mPhase==Phase::Connected)
    {
        if(name=="TeleportProgress" && body["agent"].asUUID()!=mAgent)return false;
        if (LLVoiceClient::instanceExists()) LLVoiceClient::instance().setHidden(true);
        mTeleportFlags = ll_U32_from_sd(body["flags"]);
        mNavigationDeadline=now()+60;
        const std::string text=body["text"].asString().substr(0,4096);
        const auto translated=LLAgent::sTeleportProgressMessages.find(text);
        const std::string message=translated==LLAgent::sTeleportProgressMessages.end()?text:translated->second;
        gAgent.setTeleportMessage(message);
        if(gViewerWindow)
        {
            gViewerWindow->setShowProgress(true,!gSavedSettings.getBOOL("FSDisableTeleportScreens"));
            gViewerWindow->setProgressString(message);
            gViewerWindow->setProgressCancelButtonVisible(vs_teleport_cancel_allowed(mTeleportFlags),LLTrans::getString("Cancel"));
        }
        publishProgress();
    }
    else if (name == "CloseCircuit" || name == "DisableSimulator")
        disconnect(LLTrans::getString("NativeSessionConnectionTimeout"));
    else return false;
    ++mReceived; return true;
}
void VSNativeSession::initializeEnvironment()
{
    // LLEnvironment is an explicit LLSimpleton. Match the reference create +
    // seed-granted initialization before any connected callback uses its CPU
    // settings or region state, including crossings before inventory is ready.
    if (!LLEnvironment::instanceExists())
    {
        LLEnvironment::createInstance();
        LLEnvironment::instance().initSingleton();
    }
}
void VSNativeSession::connected()
{
    publishProgress();
    if ((mPhase != Phase::Connecting && !mChangingRegion) || !mHandshake || !mMovement || !mSeedReady || !mCircuitAck) return;
    if (auto circuit=mCircuits.find(mHost);circuit!=mCircuits.end()) circuit->second.deadline=0;
    if (mChangingRegion)
    {
        mChangingRegion = false; mInterestListArriving = true; mNavigationDeadline = 0; mDeadline = now() + 1;
        initializeEnvironment();
        if(gViewerWindow && mStartup == Startup::Complete)gViewerWindow->setShowProgress(false,true);
        // The former current simulator remains an independently owned neighbour.
        // DisableSimulator or circuit retirement, not crossing, removes its scene.
        mPreviousHost = LLHost();
        publishProgress();
        LLTeleportHistory::instance().updateNativeLocation(gAgent.getPositionGlobal());
        if(std::exchange(mTeleportArrival,false))
        {
            if (LLVoiceClient::instanceExists()) LLVoiceClient::instance().setHidden(false);
            const U64 accountGeneration=mGeneration;
            LLViewerParcelMgr::instance().onTeleportFinished(false,gAgent.getPositionGlobal());
            if(mGeneration!=accountGeneration || mPhase!=Phase::Connected)return;
        }
        mTeleportFlags=0;
        requestParcel();
        LLEnvironment::instance().onRegionChange();
        if (gSavedSettings.getBOOL("UseLSLBridge")) FSLSLBridge::instance().initBridge();
        updateInterestListMode();
        mCurrentRegionChanged();
        return;
    }
    mPhase = Phase::Connected; mDeadline = now() + 1;
    LLMaterialMgr::instance(); // Existing idle pump, shared regional material owner.
    particles();
    initializeEnvironment();
    if (gViewerWindow)
    {
        // Same startup order as STATE_AGENT_SEND: aspect affects FOV limits.
        // LLViewerCamera is an explicit simple singleton: circuit completion
        // can precede the first connected frame that normally creates it.
        if (!LLViewerCamera::instanceExists()) LLViewerCamera::createInstance();
        auto* camera = LLViewerCamera::getInstance();
        if (gViewerWindow->getWorldViewHeightRaw() > 0 && gViewerWindow->getWorldViewWidthRaw() > 0)
        {
            camera->setViewHeightInPixels(gViewerWindow->getWorldViewHeightRaw());
            camera->setAspect(gViewerWindow->getWorldViewAspectRatio());
        }
        gAgentCamera.init(); // Same CPU camera/settings initialization as ordinary startup.
        camera->setDefaultFOV(gSavedSettings.getF32("CameraAngle"));
    }
    if (!mAppearance)
    {
        mAppearance = std::make_unique<VSNativeAppearance>(shared_from_this());
        mAppearance->fallback(mWearablesMetadata);
    }
    // Filtered inventory tabs require the initial recursive account fetch,
    // just as they do after the ordinary inventory startup stages.
    LLInventoryModelBackgroundFetch::instance().start();
    LLStartUp::initExperiences();
    gAgentMovementCompleted = true;
    LLTeleportHistory::instance().updateNativeLocation(gAgent.getPositionGlobal());
    mStartup = Startup::Inventory;
    mStartupTime = now();
    mStartupTimedOut = false;
    LLStartUp::setStartupState(STATE_INVENTORY_CALLBACKS);
    if (!gInventory.isInventoryUsable())
        notify_native_startup("InventoryUnusable");
    else
        gInventory.createCommonSystemCategories();
    restore_native_account_policies();
    FSPanelLogin::closePanel();
    LLToolMgr::instance().initNativeTools();
    gViewerWindow->setNativeConnected(true);
    // Same genuine credential/account source as ordinary STATE_AGENT_SEND.
    // A protocol fixture without credentials must not fabricate authorization.
    const std::string voiceUser = LLStartUp::getUserId();
    if (!voiceUser.empty())
        LLVoiceClient::instance().authorizeNativeAccount(voiceUser, mAgent);
    requestParcel();
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
    publishProgress();
    updateInterestListMode();
    mCurrentRegionChanged();
}
std::function<bool()> VSNativeSession::accountGuard(bool allowConnecting) const
{
    const auto weak=weak_from_this();const U64 generation=mGeneration;
    return [weak,generation,allowConnecting]
    {
        const auto owner=weak.lock();
        return owner && !owner->mShutdown && owner->mGeneration==generation &&
            (owner->mPhase==Phase::Connected || (allowConnecting && owner->mPhase==Phase::Connecting));
    };
}
bool VSNativeSession::current(const AccountHandle& token) const
{
    return !mShutdown && token.agent==mAgent && token.session==mSession && token.generation==mGeneration &&
        (mPhase==Phase::Connecting || mPhase==Phase::Connected);
}
std::optional<VSNativeSession::InventoryHandle> VSNativeSession::inventory(const LLUUID& id) const
{
    if(!current(account()))return std::nullopt;
    const auto* item=gInventory.getItem(id);const U64 revision=gInventory.objectRevision(id);
    if(!item || !revision)return std::nullopt;
    return InventoryHandle{account(),id,item->getAssetUUID(),revision};
}
bool VSNativeSession::current(const InventoryHandle& token) const
{
    if(!current(token.account))return false;
    const auto* item=gInventory.getItem(token.item);
    return item && item->getAssetUUID()==token.asset && gInventory.objectRevision(token.item)==token.revision;
}
std::function<bool()> VSNativeSession::interactionGuard(const VSWorldScene::FaceHandle& token) const
{
    if(mPhase!=Phase::Connected || token.object.account!=mAgent || token.object.accountGeneration!=mGeneration ||
       !gViewerWindow || !gViewerWindow->nativeContext())return []{return false;};
    const auto objectCurrent=gViewerWindow->nativeContext()->world().guard(token);
    const auto accountCurrent=accountGuard();
    return [accountCurrent,objectCurrent]{return accountCurrent() && objectCurrent();};
}
bool VSNativeSession::requestTaskInventory(const VSWorldScene::ObjectHandle& token)
{
    const auto peer=mCircuits.find(token.host);
    if(mPhase!=Phase::Connected || token.account!=mAgent || token.accountGeneration!=mGeneration ||
       peer==mCircuits.end() || !peer->second.acknowledged || !peer->second.handshake || !gMessageSystem ||
       !gViewerWindow || !gViewerWindow->nativeContext())return false;
    return gViewerWindow->nativeContext()->world().requestTaskInventory(*static_cast<LLMessageSystem*>(gMessageSystem),token,mAgent,mSession);
}
VSTaskInventory* VSNativeSession::taskInventory()
{
    if (!regionReady()) return nullptr;
    if (!mTaskInventory) mTaskInventory = std::make_unique<VSTaskInventory>(shared_from_this());
    return mTaskInventory.get();
}
VSObjectCosts* VSNativeSession::objectCosts()
{
    if(!regionReady())return nullptr;
    if(!mObjectCosts)mObjectCosts=std::make_unique<VSObjectCosts>(shared_from_this());
    return mObjectCosts.get();
}
VSObjectPhysics* VSNativeSession::objectPhysics()
{
    if(!regionReady())return nullptr;
    if(!mObjectPhysics)mObjectPhysics=std::make_unique<VSObjectPhysics>(shared_from_this());
    return mObjectPhysics.get();
}
VSObjectSelection* VSNativeSession::objectSelection()
{
    if (mPhase != Phase::Connected || !gViewerWindow || !gViewerWindow->nativeContext()) return nullptr;
    if (!mObjectSelection) mObjectSelection = std::make_shared<VSObjectSelection>(shared_from_this());
    return mObjectSelection.get();
}
VSObjectAttention* VSNativeSession::objectAttention()
{
    if(mPhase!=Phase::Connected || !gViewerWindow || !gViewerWindow->nativeContext())return nullptr;
    if(!mObjectAttention)mObjectAttention=std::make_unique<VSObjectAttention>(weak_from_this());
    return mObjectAttention.get();
}


void VSNativeSession::advanceStartup()
{
    if (mPhase != Phase::Connected || mChangingRegion)
        return;
    switch (mStartup)
    {
    case Startup::Inventory:
        // Category creation has asynchronous server callbacks. Do not publish a
        // ready UI or start COF-dependent controllers before those complete.
        if (!gInventory.isInventoryUsable() || !LLInventoryModel::isSysFoldersReady())
            return;
        LLAppearanceMgr::instance().initCOFID();
        gInventory.addChangedMask(LLInventoryObserver::ALL, LLUUID::null);
        gInventory.notifyObservers();
        mStartup = Startup::Misc;
        LLStartUp::setStartupState(STATE_MISC);
        return;
    case Startup::Misc:
        // Same per-account local settings restoration as ordinary STATE_MISC.
        LLEnvironment::instance().loadFromSettings();
        init_audio();
        LLFloaterBeacons::initializeSettings();
        LLPathfindingManager::instance().initSystem();
        LLMenuOptionPathfindingRebakeNavmesh::instance().initialize();
        LLStartUp::initActiveGestures();
        FSGroupTitleRegionMgr::getInstance()->loadFromDisk();
        FSFavoriteGroups::getInstance()->loadFavorites();
        LLPresetsManager::getInstance()->createMissingDefault(PRESETS_CAMERA);
        LLInventoryModelBackgroundFetch::instance().findLostItems();
        gViewerWindow->sendShapeToSim();
        gRenderStartTime.reset();
        gForegroundTime.reset();
        mStartup = Startup::Precache;
        mStartupTime = now();
        LLStartUp::setStartupState(STATE_PRECACHE);
        return;
    case Startup::Precache:
    {
        // The native frame pumps the same texture-fetch/cache workers and the
        // appearance owner every frame. Retain the reference saved pre-cache
        // interval; it is a loading opportunity, not a synthetic message delay.
        const double delay = llmax(0.f, gSavedSettings.getF32("PrecachingDelay"));
        const bool avatar = gViewerWindow->nativeContext() && gViewerWindow->nativeContext()->world().selfAvatar();
        const double elapsed = now() - mStartupTime;
        gViewerWindow->setProgressPercent(60.f + 30.f * F32(delay > 0 ? llmin(1.0, elapsed / delay) : 1.0));
        gViewerWindow->setProgressString(LLTrans::getString("LoginPrecaching"));
        if ((elapsed >= delay && avatar) || elapsed > 10.0 * delay)
        {
            mStartup = Startup::Wearables;
            mStartupTime = now();
            LLStartUp::setStartupState(STATE_WEARABLES_WAIT);
            gViewerWindow->setProgressString(LLTrans::getString("LoginDownloadingClothing"));
        }
        return;
    }
    case Startup::Wearables:
        gViewerWindow->setProgressPercent(90.f);
        gViewerWindow->setProgressString(LLTrans::getString("LoginDownloadingClothing"));
        if (!mAppearance || !mAppearance->startupReady())
        {
            // Same ten-second clothing wait and visible warning as OpenGL.
            // A timeout releases login, but never labels the appearance ready.
            if (now() - mStartupTime < 10.0)
                return;
            mStartupTimedOut = true;
            notify_native_startup("ClothingLoading");
            LL_WARNS("NativeSession") << "Startup clothing wait exceeded; appearance continues loading" << LL_ENDL;
        }
        LLStartUp::setStartupState(STATE_CLEANUP);
        if (gSavedSettings.getBOOL("UseLSLBridge") && !FSLSLBridge::instance().getBridgeCreating())
            FSLSLBridge::instance().initBridge();
        FSFloaterWearableFavorites::initCategory();
        if (!gSavedSettings.getBOOL("FSCreateCallingCards"))
            LLFriendCardsManager::createAgentCallingCard();
        gAgent.observeFriends();
        FSAvatarRenderPersistence::instance().init();
        AOEngine::onLoginComplete();
        mStartup = Startup::Complete;
        LLStartUp::setStartupState(STATE_STARTED);
        LLAppViewer::instance()->handleLoginComplete();
        gViewerWindow->setProgressPercent(100.f);
        gViewerWindow->setProgressString("");
        gViewerWindow->showCursor();
        gViewerWindow->getWindow()->resetBusyCount();
        gViewerWindow->getWindow()->setCursor(UI_CURSOR_ARROW);
        gViewerWindow->setStartupComplete();
        gViewerWindow->setProgressCancelButtonVisible(false);
        return;
    case Startup::Idle:
    case Startup::Complete:
        return;
    }
}

void VSNativeSession::publishProgress()
{
    LLSD progress;progress["account_generation"]=ll_sd_from_U64(mGeneration);
    progress["region_generation"]=ll_sd_from_U64(mRegionEpoch);progress["phase"]=S32(mPhase);
    progress["circuit"]=mCircuitAck;progress["capabilities"]=mSeedReady;
    progress["handshake"]=mHandshake;progress["movement"]=mMovement;
    progress["inventory"]=gInventory.isInventoryUsable();
    progress["appearance_owner"]=mAppearanceOwnerInitialized;progress["wearable_metadata"]=mWearablesKnown;
    progress["connected"]=regionReady();
    if (mPhase==Phase::Connecting && gViewerWindow)
    {
        // Use the reference stage fractions, localized strings and MOTD.
        // Authentication already reached 30%; connection must not reset to 0%.
        const F32 percent = !mSeedReady ? 40.f : !mHandshake ? 59.f : 60.f;
        const char* message = !mSeedReady ? "LoginRequestSeedCapGrant" :
                              !mHandshake ? "LoginWaitingForRegionHandshake" : "LoginConnectingToRegion";
        gViewerWindow->setProgressPercent(percent);
        gViewerWindow->setProgressString(LLTrans::getString(message));
        gViewerWindow->setProgressMessage(gAgent.mMOTD);
    }
    LLEventPumps::instance().obtain("native_session_progress").post(progress);
}
namespace
{
class VSNativeChatObject final : public VSChatReceiveObject
{
public:
    explicit VSNativeChatObject(VSObjectContext source) : mSource(std::move(source)) {}
    bool permYouOwner() const override
    {
        const auto source = currentSource();
        return source && source->youOwner;
    }
    bool isAttachment() const override
    {
        const auto source = currentSource();
        return source && source->attachment;
    }
    bool isTempAttachment() const override
    {
        if (!currentSource() || !gViewerWindow || !gViewerWindow->nativeContext()) return true;
        auto& world = gViewerWindow->nativeContext()->world();
        const auto object = world.object(mSource.id);
        const auto item = object ? world.objectInventoryItem(*object) : std::optional<LLUUID>{};
        return !item || *item == mSource.id;
    }
    bool isAvatar() const override
    {
        const auto source = currentSource();
        return source && source->pcode == LL_PCODE_LEGACY_AVATAR;
    }
    LLVector3 getPositionAgent() const override
    {
        const auto source = currentSource();
        return source ? gAgent.getPosAgentFromGlobal(source->position) : LLVector3::zero;
    }
    void startTyping() override { typing(true); }
    void stopTyping() override { typing(false); }
    void addChat(const LLChat& chat) override
    {
        if (isAvatar() && gViewerWindow && gViewerWindow->nativeContext())
            gViewerWindow->nativeContext()->world().nameTagChat(chat);
    }
    void chatParticles(const LLColor4& color, const LLUUID& owner) override
    {
        if (const auto source = currentSource()) vs_native_chat_particles(*source, color, owner);
    }
private:
    std::optional<VSObjectContext> currentSource() const
    {
        // The initial guard admits this exact incarnation, never a reused UUID.
        if (!mSource.current || !mSource.current()) return {};
        return VSObjectContext::resolve(mSource.id);
    }
    void typing(bool active)
    {
        if (isAvatar() && gViewerWindow && gViewerWindow->nativeContext())
            gViewerWindow->nativeContext()->world().nameTagTyping(mSource.id, active);
    }
    VSObjectContext mSource;
};
}

void VSNativeSession::appendChat(LLSD data, U64 generation)
{
    if (generation != mGeneration || mPhase != Phase::Connected) return;
    if (!data["text"].isString() || !data["from"].isString() || data["text"].asString().size() > 4096) return;
    if (!data["kind"].isInteger() || !data["type"].isInteger() || !data["audible"].isInteger()) return;
    const auto kind = data["kind"].asInteger(), type = data["type"].asInteger();
    if (kind < CHAT_SOURCE_SYSTEM || kind > CHAT_SOURCE_REGION || type < CHAT_TYPE_WHISPER || type > CHAT_TYPE_RADAR) return;
    LLChat chat;
    chat.mFromID = data["source"].asUUID();
    chat.mSourceType = static_cast<EChatSourceType>(kind);
    chat.mChatType = static_cast<EChatType>(type);
    if (data["position"].isArray() && data["position"].size() == 3)
    {
        const auto position = ll_vector3_from_sd(data["position"]);
        if (position.isFinite()) chat.mPosAgent = position;
    }
    const auto alive = accountGuard();
    const std::weak_ptr<VSNativeSession> weak = shared_from_this();
    VSChatReceiveServices services;
    services.current = alive;
    services.findObject = [alive](const LLUUID& id) -> std::shared_ptr<VSChatReceiveObject>
    {
        if (!alive()) return {};
        if (const auto object = VSObjectContext::resolve(id)) return std::make_shared<VSNativeChatObject>(*object);
        return {};
    };
    services.audible = [data] { return static_cast<EChatAudible>(data["audible"].asInteger()); };
    services.message = [data] { return data["text"].asString(); };
    services.location = [weak, alive](const LLVector3& position) -> std::optional<std::string>
    {
        const auto session = weak.lock();
        if (!session || !alive()) return {};
        const auto region = vs_region_context(from_region_handle(session->handle()) + LLVector3d(position));
        if (!region) return {};
        return LLSLURL(region->getName(), position).getLocationString();
    };
    services.deliver = [weak, generation, alive](const LLChat& message, const LLSD& args)
    {
        if (!alive()) return;
        if (const auto session = weak.lock()) session->publishChat(message, args, generation);
    };
    vs_receive_chat(chat, data["from"].asString(), data["owner"].asUUID(), services);
}
void VSNativeSession::publishChat(const LLChat& message, const LLSD& args, U64 generation)
{
    if (generation != mGeneration || mPhase != Phase::Connected || !mChat) { ++mExpired; return; }
    LLChat delivered(message);
    LLSD delivery(args);
    delivery["native_receive_generation"] = std::to_string(generation);
    // The same handler owns RLVa presentation filtering, script output, logging,
    // console/history, recent speakers and toasts for both renderer peers.
    LLNotificationsUI::LLNotificationManager::instance().onChat(delivered, delivery);
}
void VSNativeSession::recordNearbyChat(const LLChat& chat, U64 generation)
{
    if (generation == mGeneration && mPhase == Phase::Connected && mChat && !chat.mMuted)
        mChat->recordChat(chat);
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
U32 VSNativeSession::agentControlFlags() const
{
    return mFrozen ? 0 : mCamera.controls() | mMouseControls | gAgent.getControlFlags();
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
        const auto camera = cameraState();
        msg->addQuat("BodyRotation", camera.bodyRotation); msg->addQuat("HeadRotation", camera.headRotation); msg->addU8("State", 0);
        msg->addVector3("CameraCenter", camera.center); msg->addVector3("CameraAtAxis", camera.at);
        msg->addVector3("CameraLeftAxis", camera.left); msg->addVector3("CameraUpAxis", camera.up);
        msg->addF32("Far", vs_agent_draw_distance(mInterestListArriving || LLStartUp::getStartupState()<STATE_MISC));
        msg->addU32("ControlFlags", agentControlFlags());
        msg->addU8("Flags", 0);
    }
    // Periodic movement/camera snapshots supersede older state. Reliable retries
    // would replay stale input and accumulate under packet loss.
    if ((name == "AgentUpdate" ? msg->sendMessage(mHost) : msg->sendReliable(mHost)) <= 0) return false;
    if (name == "AgentUpdate")
    {
        mInterestListArriving=false;
        mLastAgentControls = agentControlFlags();
        mMouseControls &= ~(AGENT_CONTROL_LBUTTON_UP | AGENT_CONTROL_ML_LBUTTON_UP);
    }
    ++mSent; return true;
}
VSWorldSound* VSNativeSession::worldSound()
{
    if (!mWorldSound && (mPhase == Phase::Connecting || mPhase == Phase::Connected) && gViewerWindow && gViewerWindow->nativeContext())
        mWorldSound = std::make_unique<VSWorldSound>(weak_from_this());
    return mWorldSound.get();
}
VSParticleEffects* VSNativeSession::particles()
{
    if(!mParticles && mPhase==Phase::Connected && gViewerWindow && gViewerWindow->nativeContext())
        mParticles=std::make_unique<VSParticleEffects>(weak_from_this());
    return mParticles.get();
}
void VSNativeSession::tick()
{
    if (mWorldSound) mWorldSound->tick();
    if (mParticles) mParticles->tick();
    LLTracker::updateFollowLocation();
    if (mTaskInventory) mTaskInventory->tick();
    if (mObjectPhysics) mObjectPhysics->tick();
    if (mObjectCosts) mObjectCosts->tick();
    if (mObjectSelection)
    {
        mObjectSelection->tick();
        mObjectSelection->overrideAvatarUpdates();
        mObjectSelection->overrideObjectUpdates();
    }
    if (mObjectAttention) mObjectAttention->tick();
    if (mAppearance)
        mAppearance->tick();
    if (mPhase == Phase::Connected && AOEngine::instanceExists())
    {
        AOEngine::instance().inMouselook(mCamera.firstPerson());
        if (mWaterKnown)
            AOEngine::instance().checkBelowWater(mPosition.mV[VZ] < mWaterHeight);
    }
    if (mPhase == Phase::Connected && gViewerWindow && gViewerWindow->nativeContext())
        if (auto* avatar = gViewerWindow->nativeContext()->world().selfAvatar(); avatar && avatar->ownerGeneration() != mAttentionAvatar)
        {
            const auto weak  = weak_from_this();
            const auto token = account();
            avatar->attention(
                false,
                [weak, token]() -> std::optional<LLVector3d>
                {
                    const auto session = weak.lock();
                    if (!session || !session->current(token))
                        return {};
                    const auto camera = session->cameraState();
                    return from_region_handle(session->handle()) + LLVector3d(camera.center + camera.at * 10);
                },
                0);
            mAttentionAvatar = avatar->ownerGeneration();
        }
    std::vector<LLHost> retired;
    for (const auto& [host,region]:mCircuits)
        if (host!=mHost && ((region.deadline && now()>region.deadline) ||
            (region.acknowledged && region.handshake && gMessageSystem && !gMessageSystem->checkCircuitAlive(host)))) retired.push_back(host);
    for (const auto& host:retired) retireNeighbour(host);

    if (mPhase==Phase::Connected && !mChangingRegion && gMessageSystem && gViewerWindow && gViewerWindow->nativeContext())
        gViewerWindow->nativeContext()->world().requestUpdates(*static_cast<LLMessageSystem*>(gMessageSystem),mHost,mAgent,mSession);
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

    const double cameraTime = now();
    if (mPhase == Phase::Connected && !mChangingRegion)
    {
        if (mCameraTime > 0)
            mCamera.step(F32(cameraTime - mCameraTime), gAgent.rotateGrabbed(),
                gAgentCamera.getCameraOffsetInitial(), LLVector3(gAgentCamera.getFocusOffsetInitial()), gAgentCamera.getCameraOffsetScale());
        cameraControls(mCameraTime > 0 ? F32(cameraTime - mCameraTime) : 0.f);
        // The same CPU pilot controls feed the real simulator AgentUpdate.
        gAgentPilot.updateTarget();
        F32 yaw = 0;
        gAgent.autoPilot(&yaw);
        mCamera.turnBody(yaw);
        const auto target=mCamera.state(mPosition,gAgentCamera.getCameraOffsetInitial(),LLVector3(gAgentCamera.getFocusOffsetInitial()),gAgentCamera.getCameraOffsetScale());
        const auto origin=from_region_handle(mHandle);
        gAgentCamera.updateNativeCamera(origin+LLVector3d(target.center),origin+LLVector3d(target.focus),target.up);
        publishCamera();
        if (cameraTime - mParcelQueryTime >= 1 &&
            (S32(mPosition.mV[VX] / 4) != mParcelCellX || S32(mPosition.mV[VY] / 4) != mParcelCellY))
        {
            mParcelPermissionsKnown = false;
            requestParcel();
        }
        if (mCamera.firstPerson() != gAgentCamera.cameraMouselook())
        {
            if (mCamera.firstPerson()) gAgentCamera.changeCameraToMouselook(false);
            else gAgentCamera.changeCameraToThirdPerson(false);
        }
        // Shared callbacks republish held controls each frame. Send transitions
        // immediately so one-shot input reaches the wire before the reference
        // per-frame reset; unchanged poses retain the normal 10 Hz heartbeat.
        if (cameraTime >= mAgentUpdateTime || mLastAgentControls != agentControlFlags())
        {
            if (send("AgentUpdate", LLSD())) mAgentUpdateTime = cameraTime + .1;
        }
    }
    mCameraTime = cameraTime;
    if (mNavigationDeadline && now() > mNavigationDeadline)
    {
        mNavigationDeadline = 0; mRequestedRegion.clear();
        if (mChangingRegion) { disconnect(LLTrans::getString("NativeSessionConnectionTimeout")); return; }
        LLSD args; args["ERROR_MESSAGE"] = LLTrans::getString("NativeSessionConnectionTimeout"); LLNotificationsUtil::add("ErrorMessage", args);
    }
    if (mTyping && now() > mTypingDeadline) typing(false);
    updateParcelPlayback();
    if (mPhase == Phase::Connected) gEventNotifier.update();
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
    advanceStartup();
    if(LLEnvironment::instanceExists() && LLViewerCamera::instanceExists())
        LLEnvironment::instance().update(LLViewerCamera::getInstance());
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
        mLogoutPresented = mLogoutReply = false;
        typing(false);
        releaseMovement(); send("AgentUpdate", LLSD());
        clearParcelPlayback();
        unbind(); mPoll.reset();
        for(auto& [host,region]:mCircuits) {if(region.cancel)region.cancel();region.cancel={};region.poll.reset();}
        send("LogoutRequest", LLSD()); mPhase = Phase::Logout; mDeadline = now() + gLogoutMaxTime;
        gViewerWindow->setShowProgress(true, !gSavedSettings.getBOOL("FSDisableLogoutScreens"));
        gViewerWindow->setProgressPercent(100.f);
        gViewerWindow->setProgressString(LLTrans::getString("LoggingOut"));
    }
    else if (mQuit) LLAppViewer::instance()->forceQuit();
}
void VSNativeSession::logoutFramePresented()
{
    if (mPhase != Phase::Logout) return;
    mLogoutPresented = true;
    // A fast authenticated reply must not retire the UI before its first frame.
    if (mLogoutReply) finishLogout();
}
void VSNativeSession::finishLogout()
{
    const bool quit = mQuit;
    // Preserve the logout surface through application teardown. Only a return
    // to the login screen hides it before restoring the shared login panel.
    if (!quit) gViewerWindow->setShowProgress(false, true);
    reset();
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
    mStartup = Startup::Idle;
    mStartupTime = 0;
    mStartupTimedOut = false;
    if (mShutdown) return;
    gAgent.retireNativeAutoPilot();
    // Retire CPU tracking without sending an untrack on the retiring circuit.
    // The old account's buddy owner is cleared below, before another login.
    LLTracker::cleanupInstance();
    LLFloaterReg::destroyInstance("world_map");LLFloaterReg::destroyInstance("mini_map");
    LLWorldMapView::cleanupClass();
    if(LLWorldMapMessage::instanceExists())LLWorldMapMessage::instance().resetNativeAccount();
    if(LLWorldMap::instanceExists())LLWorldMap::instance().reset();
    gViewerInput.resetNativeGlobalKeys();
    // Drop focus, paired plugin capture and the selection lease while their
    // actual object/face and account owners are still alive. Never instantiate
    // a controller or a selection merely to tear an account down.
    if(LLToolPie::instanceExists())LLToolPie::instance().retireNativeMediaCapture();
    if(LLViewerMediaFocus::instanceExists())
    {
        LLViewerMediaFocus::instance().clearHover();
        LLViewerMediaFocus::instance().clearFocus();
    }
    if(gMenuHolder)gMenuHolder->hideMenus();
    if (LLVoiceClient::instanceExists()) LLVoiceClient::instance().retireNativeAccount();
    vs_retire_native_object_ui();
    if (LLMaterialMgr::instanceExists()) LLMaterialMgr::instance().retireNativeAccount();
    vs_retire_snapshot_floaters();
    vs_retire_scene_preview_floaters();
    gMeshRepo.retireNativeUploads();
    if(LLToolMgr::instanceExists())LLToolMgr::instance().retireNativeTools();
    mParticles.reset();
    mWorldSound.reset();
    if (gAudiop)
    {
        gAudiop->mMaxWindGain = 0.f;
        gAudiop->updateWind(LLVector3::zero, 0.f);
        gAudiop->setListener(LLVector3::zero, LLVector3::zero, LLVector3::z_axis, LLVector3::x_axis);
    }
    gWindVec = gRelativeWindVec = LLVector3::zero;
    mObjectAttention.reset();
    if(mObjectSelection)mObjectSelection->reset();
    mObjectSelection.reset();
    mObjectCosts.reset();
    mObjectPhysics.reset();
    mTaskInventory.reset();
    mAppearance.reset();
    if (LLGestureMgr::instanceExists()) LLGestureMgr::instance().resetNativeAccount();
    // The AO editor retains set/state pointers into the retiring account.
    // Destroy it and disconnect its observers before the engine clears them.
    LLFloaterReg::destroyInstance("animation_overrider");
    if (AOEngine::instanceExists())
        AOEngine::instance().resetAccount();
    gAgent.clearScriptControlState();
    gAgent.clearControlFlags(std::numeric_limits<U32>::max());
    mCamera.reset(); mCameraTime = mAgentUpdateTime = 0; mLastAgentControls.reset(); mInterestListArriving=true;
    mTeleportFlags=0;mTeleportArrival=false;
    mLogoutPresented = mLogoutReply = false;
    if (mAgent.notNull() && (mPhase == Phase::Connected || mPhase == Phase::Logout || mPhase == Phase::Disconnected))
    {
        if (LLPersistentNotificationStorage::instanceExists()) LLPersistentNotificationStorage::getInstance()->saveNotifications();
        if (LLDoNotDisturbNotificationStorage::instanceExists()) LLDoNotDisturbNotificationStorage::getInstance()->saveNotifications();
    }
    clearParcelPlayback();
    auto circuits=std::exchange(mCircuits,{});
    for (auto& [host,region]:circuits)
    {
        if (region.cancel)
        {
            region.cancel();
        }
        region.poll.reset();
        if (gMessageSystem && host!=mHost)
        {
            gMessageSystem->newMessage("AgentQuitCopy");gMessageSystem->nextBlock("AgentData");
            gMessageSystem->addUUID("AgentID",mAgent);gMessageSystem->addUUID("SessionID",mSession);
            gMessageSystem->nextBlock("FuseBlock");gMessageSystem->addU32("ViewerCircuitCode",mCircuit);gMessageSystem->sendMessage(host);
            gMessageSystem->disableCircuit(host);gMessageSystem->mCircuitInfo.removeCircuitData(host);
        }
    }
    if (gViewerWindow && gViewerWindow->nativeContext()) gViewerWindow->nativeContext()->world().reset();
    // Release account camera effects while their settings and HUD owners live.
    gAgentCamera.cleanup();
    if (FSLSLBridge::instanceExists()) FSLSLBridge::instance().resetNativeAccount();
    mMusicUserStopped = mMediaUserStopped = false;
    if (LLViewerParcelMedia::instanceExists()) LLViewerParcelMedia::deleteSingleton();
    ++mGeneration; ++mRegionEpoch; clearSimulatorFeatures(); mLocationRequests.clear(); mChangingRegion = false; mRequestedRegion.clear(); mNavigationDeadline = 0; unbind();
    LLViewerTextureManager::resetCPULocalImages();
    gGLTFMaterialList.resetNativeMaterials();
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
    // Availability is temporary account state; release its notification mute
    // while the retiring account's IM/storage owners still exist.
    if (gAgent.isDoNotDisturb()) gAgent.setDoNotDisturb(false);
    vs_native_im_reset();
    mRegionLimits.reset();
    if(LLRegionInfoModel::instanceExists())LLRegionInfoModel::instance().reset();
    if(LLEnvironment::instanceExists())LLEnvironment::instance().saveToSettings();
    LLEnvironment::deleteSingleton();
    vs_reset_covenants();
    if(LLViewerParcelMgr::instanceExists())LLViewerParcelMgr::instance().resetNativeAccount();
    if(LLEstateInfoModel::instanceExists())LLEstateInfoModel::instance().reset();
    LLFloaterRegionInfo::nextInvoice();
    clearBalance();
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
    publishProgress();
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
    mWorldMouseDown = mWorldMouseMouselook = false;
    mMouseControls                         = 0;
    mLastAgentControls.reset();
    mAttentionAvatar                       = 0;
    mWaterKnown                            = false;
    mWaterHeight                           = 20;
    mAppearanceOwnerInitialized=mWearablesKnown=false;mWearablesMetadata=LLSD();mWearablesSerial=0;
    gDisconnected = true;
    if (LLAvatarNameCache::instanceExists()) LLAvatarNameCache::getInstance()->setNameLookupURL("");
    publishProgress();
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
    result["simulator_features_received"] = mSimulatorFeaturesReceived;
    result["startup_stage"] = S32(mStartup);
    result["startup_complete"] = mStartup == Startup::Complete;
    result["startup_clothing_timed_out"] = mStartupTimedOut;
    result["appearance_owner_initialized"]=mAppearanceOwnerInitialized;
    result["wearable_metadata_known"]=mWearablesKnown;result["wearable_metadata"]=mWearablesMetadata;
    if (mAppearance)
        result["appearance"] = mAppearance->evidence();
    result["circuits"]=LLSD::emptyArray();
    for (const auto& [host,region]:mCircuits)
    {
        LLSD row;row["host"]=host.getIPandPort();row["handle"]=ll_sd_from_U64(region.handle);
        row["generation"]=ll_sd_from_U64(region.incarnation);row["width"]=S32(region.width);row["height"]=S32(region.height);
        row["acknowledged"]=region.acknowledged;row["handshake"]=region.handshake;row["current"]=host==mHost;
        row["seed_failed"]=region.seedFailed;row["capability_generation"]=ll_sd_from_U64(region.requestRevision);
        result["circuits"].append(row);
    }
    result["capabilities"] = mSeedReady; result["circuit_ack"] = mCircuitAck;
    result["region_width"] = S32(mWidth); result["region_height"] = S32(mHeight);
    result["frozen"] = mFrozen;
    result["region_epoch"] = LLSD::Integer(mRegionEpoch); result["changing_region"] = mChangingRegion;
    result["location_url"] = locationURL(); result["world_owners"] = 0; return result;
}

// Playback and capability presentation consume the existing fully unpacked LLParcel.
void VSNativeSession::publishAgentParcel(const LLParcel& parcel)
{
    if(!regionReady() || parcel.getRegionID()!=mRegionID || parcel.getLocalID()<0)return;
    const auto currentAccount=accountGuard();const auto epoch=mRegionEpoch;
    std::string music=parcel.getMusicURL(),media=parcel.getMediaURL();
    LLStringUtil::trim(music);LLStringUtil::trim(media);
    // Invalid stream URLs stop that playback, as on the reference path. They do
    // not discard valid parcel ownership, permissions, names or progress updates.
    if(!music.empty() && !webURL(music))music.clear();
    if(!media.empty() && !webURL(media))media.clear();
    const bool changed=mParcelID!=parcel.getLocalID() || mMusicURL!=music || mMediaURL!=media || mMediaType!=parcel.getMediaType();
    const bool resumeMusic=mMusicRequested,resumeMedia=mMediaRequested,pausedMedia=mMediaPaused;
    if(changed)clearParcelPlayback();
    if(!currentAccount() || mRegionEpoch!=epoch)return;
    mParcelID=parcel.getLocalID();mParcelName=parcel.getName();
    mParcelPermissionsKnown=true;mParcelFlags=parcel.getParcelFlags();mParcelOwner=parcel.getOwnerID();mParcelSeeAvatars=parcel.getSeeAVs();
    mMusicURL=music;mMediaURL=media;mMediaType=parcel.getMediaType();
    LLSD metadata;metadata["loop"]=S32(parcel.getMediaLoop());metadata["autoscale"]=S32(parcel.getMediaAutoScale());
    metadata["width"]=parcel.getMediaWidth();metadata["height"]=parcel.getMediaHeight();
    const bool validMedia=updateParcelMediaMetadata(metadata);
    if(!validMedia)LL_WARNS("NativeSession")<<"Parcel media parameters exceed the native media endpoint contract"<<LL_ENDL;
    if(changed && (resumeMusic || (!mMusicUserStopped && gSavedSettings.getBOOL("FSParcelMusicAutoPlay") && gSavedSettings.getBOOL("MediaTentativeAutoPlay"))))setMusicEnabled(true);
    if(!currentAccount() || mRegionEpoch!=epoch)return;
    if(changed && validMedia && (resumeMedia || (!mMediaUserStopped && gSavedSettings.getS32("ParcelMediaAutoPlayEnable")==1 && gSavedSettings.getBOOL("MediaTentativeAutoPlay"))))
    {
        setMediaEnabled(true);
        if(currentAccount() && mRegionEpoch==epoch && resumeMedia)setMediaPaused(pausedMedia);
    }
}
