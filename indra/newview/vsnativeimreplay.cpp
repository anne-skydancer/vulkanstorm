// Real skinned contacts/IM controls and encoded loopback messages. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsnativeimreplay.h"
#include "vsnativeim.h"
#include "vsnativesession.h"
#include "vsuiimageprovider.h"
#include "llfloaterprofiletexture.h"
#include "llfloaterprofile.h"
#include "llpanelprofile.h"
#include "llavatarpropertiesprocessor.h"
#include "llavatarnamecache.h"
#include "fsradar.h"
#include "lltexturectrl.h"
#include "llinventorypanel.h"
#include "llfolderviewitem.h"
#include "llviewermenufile.h"
#include "llfloaterimagepreview.h"
#include "llfloaternamedesc.h"
#include "llinventoryfunctions.h"
#include "llfloaterproperties.h"
#include "llsidepaneliteminfo.h"
#include "llfloaterchangeitemthumbnail.h"
#include "llpreviewsound.h"
#include "llpreviewscript.h"
#include "llscripteditor.h"
#include "llfile.h"
#include "lldir.h"
#include "fsfloaterpartialinventory.h"
#include "lltooldraganddrop.h"
#include "lltransactiontypes.h"
#include "llmenubutton.h"
#include "lltoggleablemenu.h"
#include "llmenugl.h"
#include "llavataractions.h"
#include "fsfloatergroup.h"
#include "fsfloatergrouptitles.h"
#include "llpaneldirgroups.h"
#include "llsearchcombobox.h"
#include "llqueryflags.h"
#include "llgroupmgr.h"
#include "llgroupactions.h"
#include "llnamelistctrl.h"
#include "lltabcontainer.h"
#include "llaccordionctrltab.h"
#include "llui.h"
#include "llagent.h"
#include "llagentbenefits.h"
#include "llviewernetwork.h"
#include "llimview.h"
#include "llspeakers.h"
#include "llfloaterreg.h"
#include "fsfloaterim.h"
#include "fsfloaterimcontainer.h"
#include "fsfloatercontacts.h"
#include "fsscrolllistctrl.h"
#include "llgrouplist.h"
#include "llchatentry.h"
#include "llbutton.h"
#include "lltextbox.h"
#include "llcheckboxctrl.h"
#include "lllineeditor.h"
#include "llfloaterpreference.h"
#include "llfloaterconversationpreview.h"
#include "fschathistory.h"
#include "lggcontactsets.h"
#include "llinstantmessage.h"
#include "roles_constants.h"
#include "llviewercontrol.h"
#include "llnotifications.h"
#include "llnotificationsutil.h"
#include "llsdserialize.h"
#include "lltrans.h"
#include "llmutelist.h"
#include "lltoastgroupnotifypanel.h"
#include "llpreviewtexture.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "message.h"
#include "llsdutil.h"
#include <stdexcept>
#include <fstream>

namespace
{
const LLUUID peer("50000000-0000-0000-0000-000000000005");
const LLUUID group("80000000-0000-0000-0000-000000000008");
void require(bool value, const char* text)
{
    if (!value)
    {
        LL_WARNS("NativeIMReplay") << "Qualification failed: " << text << LL_ENDL;
        throw std::runtime_error(text);
    }
}
void clickCheckbox(LLCheckBoxCtrl* control)
{
    require(control && control->getEnabled() && control->isInVisibleChain(),
        "Real checkbox is not visible and enabled");
    LLButton* button = nullptr;
    for (auto* child : *control->getChildList())
        if (auto* candidate = dynamic_cast<LLButton*>(child)) { button = candidate; break; }
    require(button != nullptr, "Checkbox has no production toggle button");
    const auto& rect = button->getRect();
    const S32 x = rect.mLeft + rect.getWidth() / 2, y = rect.mBottom + rect.getHeight() / 2;
    require(control->handleMouseDown(x, y, MASK_NONE), "Real checkbox rejected mouse down");
    require(control->handleMouseUp(x, y, MASK_NONE), "Real checkbox rejected mouse up");
}
void event(VSNativeSession& owner, const char* name, const LLSD& body)
{
    LLSD input; input["sender"] = owner.host().getIPandPort(); input["body"] = body;
    LLMessageSystem::dispatch(name, input);
}
bool contains(const LLUUID& id, const std::string& text)
{
    auto* session = LLIMModel::getInstance()->findIMSession(id);
    if (!session) return false;
    for (const auto& entry : session->mMsgs)
        if (entry["message"].asString().find(text) != std::string::npos) return true;
    return false;
}
void finishServerIM()
{
    // The server-to-viewer template extends the historical client IM block.
    // Emit the required EstateBlock; the builder encodes empty MetaData count.
    gMessageSystem->nextBlock("EstateBlock");
    gMessageSystem->addU32("EstateID", 0);
}
void incoming(VSNativeSession& owner, EInstantMessage dialog, const LLUUID& id, const std::string& text, U8 offline = IM_ONLINE)
{
    pack_instant_message(gMessageSystem, peer, false, LLUUID::null, gAgentID, "CPU Sender", text,
        offline, dialog, id, 0, LLUUID::null, LLVector3::zero, 1);
    finishServerIM();
    gMessageSystem->sendReliable(owner.host());
}
struct Replay : public LLAvatarPropertiesObserver
{
    unsigned step = 0;
    unsigned loggedStep = ~0u;
    bool namesSent = false;
    bool membershipSent = false;
    bool muteQueued = false;
    bool mutedDelivered = false;
    LLUUID direct;
    bool directSend = false, groupStart = false, groupSend = false, left = false;
    bool incomingText = false, incomingGroup = false, incomingTyping = false;
    bool typingStart = false, typingStop = false;
    std::function<bool()> retiredGuard;
    boost::signals2::scoped_connection notificationConnection;
    LLUUID startError, eventError, forceClose;
    std::string expectedEventText;
    bool eventTipDelivered = false;
    LLUUID friendshipOffer, groupOffer, groupNotice;
    bool notificationSent = false, groupDeclined = false;
    bool inventorySent = false, inventoryResponded = false, inventoryAccepted = false, attachmentAccepted = false;
    LLUUID inventoryOffer;
    const LLUUID offeredItem{"71000000-0000-0000-0000-000000000001"};
    const LLUUID offerTransaction{"72000000-0000-0000-0000-000000000001"};
    const LLUUID attachmentTransaction{"72000000-0000-0000-0000-000000000002"};
    const LLUUID textureFolder{"70000000-0000-0000-0000-000000000003"};
    LLSD retiredNotification;
    const LLUUID profileImage{"60000000-0000-0000-0000-000000000006"};
    LLHandle<LLFloater> imageFloater;
    LLHandle<LLFloater> preferencesFloater, transcriptFloater, residentFloater, groupFloater;
    bool originalItalic = false;
    bool groupRolesMembersSent = false, groupRequestsSent = false;
    LLUUID groupMemberRequest, groupRoleRequest, groupRoleMemberRequest;
    LLUUID titleRequest, searchRequest;
    LLHandle<LLFloater> titleFloater, searchFloater, selfProfileFloater, profilePicker;
    bool selfSecondLifeSaved = false, selfFirstLifeSaved = false;
    bool outgoingSharedItem = false;
    unsigned outgoingPayments = 0;
    unsigned pickerWaitTicks = 0;
    unsigned propertyWrites = 0;
    bool savedLegacyProperties = false;
    LLHandle<LLFloater> legacyProperties, modernProperties, soundPreview, thumbnailEditor, scriptPreview;
    bool savedPreprocessor = false;
    bool scriptRequested = false;
    LLHandle<LLFloater> textureUpload, soundUpload;
    std::string waveFilename;
    LLUUID shareConfirmation, payConfirmation, unknownBalanceAlert, insufficientBalanceAlert;
    LLHandle<LLFloater> shareInventory, payFloater;
    bool savedPaymentConfirm = false;
    S32 savedPaymentThreshold = 0;
    boost::signals2::scoped_connection accountNotifications;
    void openResidentPay()
    {
        auto* profile = residentFloater.get();
        require(profile != nullptr, "Real resident profile unavailable for Pay");
        profile->setVisible(true);
        auto* tabs = profile->getChild<LLTabContainer>("panel_profile_tabs");
        require(tabs->selectTabByName("panel_profile_secondlife"), "Resident Pay profile tab unavailable");
        auto* pay = profile->getChild<LLButton>("pay");
        require(pay->getEnabled() && pay->isInVisibleChain(), "Actual resident Pay control unavailable");
        pay->onCommit();
        auto* floater = LLFloaterReg::findInstance("pay_resident", peer);
        require(floater && floater->getVisible(), "Real Pay control failed to construct resident payment UI");
        payFloater = floater->getHandle();
    }
    void sendBalance(VSNativeSession& owner, S32 balance)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessage("MoneyBalanceReply"); msg->nextBlock("MoneyData");
        msg->addUUID("AgentID", gAgentID); msg->addUUID("TransactionID", LLUUID::null);
        msg->addBOOL("TransactionSuccess", true); msg->addS32("MoneyBalance", balance);
        msg->addS32("SquareMetersCredit", 0); msg->addS32("SquareMetersCommitted", 0);
        msg->addString("Description", "Native replay account balance");
        msg->nextBlock("TransactionInfo"); msg->addS32("TransactionType", 0);
        msg->addUUID("SourceID", LLUUID::null); msg->addBOOL("IsSourceGroup", false);
        msg->addUUID("DestID", gAgentID); msg->addBOOL("IsDestGroup", false);
        msg->addS32("Amount", 0); msg->addString("ItemDescription", ""); msg->sendReliable(owner.host());
    }
    static void moneyPacket(LLMessageSystem* message, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID agent, session, source, destination; S32 amount, kind;
        message->getUUID("AgentData", "AgentID", agent); message->getUUID("AgentData", "SessionID", session);
        message->getUUID("MoneyData", "SourceID", source); message->getUUID("MoneyData", "DestID", destination);
        message->getS32("MoneyData", "Amount", amount); message->getS32("MoneyData", "TransactionType", kind);
        require(agent == gAgentID && session == gAgentSessionID && source == gAgentID && destination == peer &&
            amount == 1 && kind == TRANS_GIFT, "Encoded resident payment lost account/recipient/amount ownership");
        ++replay.outgoingPayments;
    }
    static void inventoryPacket(LLMessageSystem* message, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID agent, session; message->getUUID("AgentData", "AgentID", agent);
        message->getUUID("AgentData", "SessionID", session);
        require(agent == gAgentID && session == gAgentSessionID, "Encoded inventory edit lost account identity");
        for (S32 i = 0; i < message->getNumberOfBlocks("InventoryData"); ++i)
        {
            LLUUID item; std::string description;
            message->getUUID("InventoryData", "ItemID", item, i);
            message->getString("InventoryData", "Description", description, i);
            if (item == LLUUID("71000000-0000-0000-0000-000000000002") &&
                (description == "Native legacy metadata edit" || description == "Native modern metadata edit"))
                ++replay.propertyWrites;
        }
    }
    void processProperties(void* value, EAvatarProcessorType type) override
    {
        if (!value || type != APT_PROPERTIES) return;
        const auto& data = *static_cast<LLAvatarData*>(value);
        if (data.avatar_id != gAgentID) return;
        selfSecondLifeSaved = data.image_id == profileImage;
        selfFirstLifeSaved = data.fl_image_id == profileImage;
    }
    LLFloaterTexturePicker* findProfilePicker(LLFloater* profile)
    {
        for (auto* child : *gFloaterView->getChildList())
            if (auto* picker = dynamic_cast<LLFloaterTexturePicker*>(child))
                if (picker->getVisible() && picker->getDependee() == profile) return picker;
        return nullptr;
    }
    const std::string contactSetName = "Native qualification contact set";
    const LLUUID inviteGroup{"90000000-0000-0000-0000-000000000009"};
    const LLUUID inviteTransaction{"91000000-0000-0000-0000-000000000009"};
    void respond(const LLUUID& id)
    {
        auto notification = LLNotifications::instance().find(id);
        require(notification != nullptr, "IM error did not produce real notification");
        notification->respond(notification->getResponseTemplate(LLNotification::WITH_DEFAULT_BUTTON));
    }
    static void packet(LLMessageSystem* message, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID from, identity, target, id; U8 dialog; std::string text;
        message->getUUID("AgentData", "AgentID", from);
        message->getUUID("AgentData", "SessionID", identity);
        message->getUUID("MessageBlock", "ToAgentID", target);
        message->getUUID("MessageBlock", "ID", id);
        message->getU8("MessageBlock", "Dialog", dialog);
        message->getString("MessageBlock", "Message", text);
        if (from == gAgentID)
        {
            require(identity == gAgentSessionID, "IM packet lost session identity");
            if (dialog == IM_NOTHING_SPECIAL && text == "Native direct Unicode \xCE\xA9")
            { require(target == peer && id == replay.direct, "Direct IM target/session changed"); replay.directSend = true; }
            else if (dialog == IM_SESSION_GROUP_START)
            { require(target == group && id == group, "Group start target/session changed"); replay.groupStart = true; }
            else if (dialog == IM_SESSION_SEND && text == "Native group Unicode \xCE\xA9")
            { require(target == group && id == group, "Group IM target/session changed"); replay.groupSend = true; }
            else if ((dialog == IM_TYPING_START || dialog == IM_TYPING_STOP) && id == replay.direct)
            {
                require(target == peer, "Typing target changed");
                if (dialog == IM_TYPING_START) replay.typingStart = true; else replay.typingStop = true;
            }
            else if (dialog == IM_SESSION_LEAVE && id == group) replay.left = true;
            else if (dialog == IM_GROUP_INVITATION_DECLINE && id == replay.inviteTransaction)
            { require(target == replay.inviteGroup, "Group decline target changed"); replay.groupDeclined = true; }
            else if (dialog == IM_INVENTORY_OFFERED)
            {
                U8 bucket[1 + UUID_BYTES]{};
                require(target == peer && id.notNull() && message->getSize("MessageBlock", "BinaryBucket") == static_cast<S32>(sizeof(bucket)),
                    "Encoded Share omitted recipient/transaction/item identity");
                message->getBinaryData("MessageBlock", "BinaryBucket", bucket, static_cast<S32>(sizeof(bucket)));
                LLUUID item; memcpy(item.mData, bucket + 1, UUID_BYTES);
                require(bucket[0] == LLAssetType::AT_TEXTURE && item == replay.offeredItem,
                    "Share encoded a different selected inventory item");
                replay.outgoingSharedItem = true;
            }
            else if (dialog == IM_INVENTORY_ACCEPTED && id == replay.offerTransaction)
            {
                require(target == peer, "Inventory acceptance target changed");
                replay.inventoryAccepted = true;
            }
            else if (dialog == IM_GROUP_NOTICE_INVENTORY_ACCEPTED && id == replay.attachmentTransaction)
            {
                require(target == peer, "Group attachment acceptance sender changed");
                const S32 size = message->getSize("MessageBlock", "BinaryBucket");
                LLUUID folder;
                require(size == UUID_BYTES, "Group attachment acceptance omitted destination folder");
                message->getBinaryData("MessageBlock", "BinaryBucket", folder.mData, UUID_BYTES);
                require(folder == replay.textureFolder, "Group attachment routed to wrong account folder");
                replay.attachmentAccepted = true;
            }
            return; // The fixture is the simulator endpoint for client packets.
        }
        vs_native_im_receive(message, nullptr);
        if (text == "Native muted text") replay.mutedDelivered = true;
        if (dialog == IM_TYPING_START || dialog == IM_TYPING_STOP) replay.incomingTyping = true;
        if (dialog == IM_NOTHING_SPECIAL) replay.incomingText = contains(replay.direct, "Native incoming offline");
        if (dialog == IM_SESSION_SEND) replay.incomingGroup = contains(group, "Native incoming group");
    }
    static void groupRequest(LLMessageSystem* message, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID agent, sessionID, groupID, requestID;
        message->getUUID("AgentData", "AgentID", agent);
        message->getUUID("AgentData", "SessionID", sessionID);
        message->getUUID("GroupData", "GroupID", groupID);
        message->getUUID("GroupData", "RequestID", requestID);
        require(agent == gAgentID && sessionID == gAgentSessionID && groupID == group && requestID.notNull(),
            "Encoded group request lost account/group/transaction ownership");
        const std::string name = message->getMessageName();
        if (name == "GroupMembersRequest") replay.groupMemberRequest = requestID;
        else if (name == "GroupRoleDataRequest") replay.groupRoleRequest = requestID;
        else if (name == "GroupRoleMembersRequest") replay.groupRoleMemberRequest = requestID;
    }
    static void accountRequest(LLMessageSystem* message, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID agent, sessionID;
        message->getUUID("AgentData", "AgentID", agent);
        message->getUUID("AgentData", "SessionID", sessionID);
        require(agent == gAgentID && sessionID == gAgentSessionID,
            "Connected account request lost authenticated ownership");
        const std::string name = message->getMessageName();
        if (name == "GroupTitlesRequest")
        {
            LLUUID groupID;
            message->getUUID("AgentData", "GroupID", groupID);
            require(groupID == group, "Group title request selected wrong group");
            message->getUUID("AgentData", "RequestID", replay.titleRequest);
            require(replay.titleRequest.notNull(), "Group title request omitted transaction");
        }
        else if (name == "DirFindQuery")
        {
            std::string text; U32 flags; S32 start;
            message->getUUID("QueryData", "QueryID", replay.searchRequest);
            message->getString("QueryData", "QueryText", text);
            message->getU32("QueryData", "QueryFlags", flags);
            message->getS32("QueryData", "QueryStart", start);
            require(replay.searchRequest.notNull() && text == "Native replay group" &&
                (flags & DFQ_GROUPS) != 0 && start == 0,
                "Real group Search encoded wrong query, scope or page");
        }
    }
    bool tick(VSNativeSession& owner, LLSD& evidence)
    {
        if (loggedStep != step)
        {
            loggedStep = step;
            LL_INFOS("NativeIMReplay") << "Entering connected IM qualification step " << step << LL_ENDL;
        }
        if (step == 0)
        {
            require(owner.phase() == VSNativeSession::Phase::Connected && gIMMgr, "IM replay requires connected shared services");
            if (!membershipSent)
            {
                LLSD membership;
                membership["AgentData"][0]["AgentID"] = gAgentID;
                membership["GroupData"][0]["GroupID"] = group;
                membership["GroupData"][0]["GroupPowers"] = ll_sd_from_U64(GP_SESSION_JOIN);
                membership["GroupData"][0]["AcceptNotices"] = true;
                membership["GroupData"][0]["GroupInsigniaID"] = LLUUID::null;
                membership["GroupData"][0]["Contribution"] = 0;
                membership["GroupData"][0]["GroupName"] = "Native replay group";
                membership["NewGroupData"][0]["ListInProfile"] = true;
                event(owner, "AgentGroupDataUpdate", membership);
                membershipSent = true;
                LL_INFOS("NativeIMReplay") << "Decoded group membership fixture" << LL_ENDL;
                return false;
            }
            if (!gAgent.isInGroup(group)) return false; // Require decoded simulator membership before UI initiation.
            if (!namesSent)
            {
                LLSD names;
                names["UUIDNameBlock"][0]["ID"] = peer;
                names["UUIDNameBlock"][0]["FirstName"] = "CPU";
                names["UUIDNameBlock"][0]["LastName"] = "Sender";
                event(owner, "UUIDNameReply", names);
                LLSD groups;
                groups["UUIDNameBlock"][0]["ID"] = group;
                groups["UUIDNameBlock"][0]["GroupName"] = "Native replay group";
                event(owner, "UUIDGroupNameReply", groups);
                LL_INFOS("NativeIMReplay") << "Decoded fixture UUID name replies" << LL_ENDL;
                namesSent = true; return false;
            }
            gSavedSettings.setBOOL("TranslateChat", false);
            gSavedSettings.setBOOL("FSAnnounceIncomingIM", false);
            gSavedPerAccountSettings.setBOOL("LogInstantMessages", true);
            gSavedPerAccountSettings.setS32("KeepConversationLogTranscripts", 2);
            gSavedPerAccountSettings.setBOOL("FetchGroupChatHistory", false);
            retiredGuard = vs_native_im_guard();
            notificationConnection = LLNotifications::instance().getChannel("System")->connectChanged(
                [this](const LLSD& payload)
                {
                    auto notification = LLNotifications::instance().find(payload["id"].asUUID());
                    if (!notification) return false;
                    if (notification->getName() == "ChatterBoxSessionStartError") startError = notification->getID();
                    if (notification->getName() == "ChatterBoxSessionEventError")
                    {
                        eventError = notification->getID();
                        // Notifytips are normally acknowledged/deleted during delivery.
                        // Capture the real translated notification before that lifecycle completes.
                        eventTipDelivered = notification->getType() == "notifytip" &&
                            notification->getSubstitutions()["EVENT"].asString() == expectedEventText &&
                            notification->getMessage().find(expectedEventText) != std::string::npos;
                    }
                    if (notification->getName() == "ForceCloseChatterBoxSession") forceClose = notification->getID();
                    if (notification->getName() == "OfferFriendshipNoMessage") friendshipOffer = notification->getID();
                    if (notification->getName() == "JoinGroup") groupOffer = notification->getID();
                    if (notification->getName() == "GroupNotice") groupNotice = notification->getID();
                    if (notification->getName() == "UserGiveItem" || notification->getName() == "UserGiveItemLegacy") inventoryOffer = notification->getID();
                    return false;
                });
            gMessageSystem->setHandlerFunc("ImprovedInstantMessage", packet, reinterpret_cast<void**>(this));
            auto* contacts = LLFloaterReg::showTypedInstance<FSFloaterContacts>("imcontacts", "friends");
            require(contacts != nullptr, "Actual skinned contacts unavailable");
            auto* friends = contacts->getPanelByName("friends_panel");
            require(friends != nullptr, "Contacts friends panel missing");
            auto* list = friends->getChild<FSScrollListCtrl>("friend_list");
            if (!list->selectByID(peer)) return false;
            LL_INFOS("NativeIMReplay") << "Selected real contacts friend row" << LL_ENDL;
            list->onCommit(); friends->getChild<LLButton>("im_btn")->onCommit();
            direct = LLIMMgr::computeSessionID(IM_NOTHING_SPECIAL, peer);
            auto* floater = FSFloaterIM::findInstance(direct);
            require(floater && gIMMgr->hasSession(direct), "Contacts IM action did not construct shared conversation");
            floater->getChild<LLChatEntry>("chat_editor")->setText(LLStringExplicit("Native direct Unicode \xCE\xA9"));
            floater->getChild<LLButton>("send_chat")->onCommit();
            incoming(owner, IM_NOTHING_SPECIAL, direct, "Native incoming offline", IM_OFFLINE);
            incoming(owner, IM_TYPING_START, direct, "typing");
            incoming(owner, IM_TYPING_STOP, direct, "typing");
            gSavedSettings.setBOOL("FSSendTypingState", true);
            LLIMModel::sendTypingState(direct, peer, true);
            LLIMModel::sendTypingState(direct, peer, false);
            contacts->openTab("groups");
            step = -1;
            return false; // Let the production group list refresh/draw before selection.
        }
        if (step == -1)
        {
            auto* contacts = FSFloaterContacts::getInstance();
            auto* groups = contacts->getPanelByName("groups_panel");
            auto* list = groups->getChild<LLGroupList>("group_list");
            if (!list->getItemByValue(group)) return false;
            list->resetSelection(); // Match an unmodified user click, replacing the initial None row.
            if (!list->selectItemByUUID(group)) return false;
            list->onCommit();
            require(list->getSelectedUUID() == group, "Actual group row selection lost identity");
            require(groups->getChild<LLButton>("chat_btn")->getEnabled(), "Real group chat control disabled for qualified membership");
            groups->getChild<LLButton>("chat_btn")->onCommit();
            require(gIMMgr->hasSession(group), "Contacts group action did not construct shared conversation");
            step = 1; return false;
        }
        if (step == 1)
        {
            if (!directSend || !incomingText || !incomingTyping || !typingStart || !typingStop || !groupStart) return false;

            LLSD reply; reply["success"] = true; reply["temp_session_id"] = group; reply["session_id"] = group;
            reply["agents"].append(gAgentID); reply["agents"].append(peer);
            event(owner, "ChatterBoxSessionStartReply", reply);
            auto* model = LLIMModel::getInstance()->findIMSession(group);
            require(model && model->mSessionInitialized, "Group start reply did not initialize shared session");
            auto* floater = FSFloaterIM::findInstance(group);
            require(floater != nullptr, "Group skinned floater missing");
            floater->getChild<LLChatEntry>("chat_editor")->setText(LLStringExplicit("Native group Unicode \xCE\xA9"));
            floater->getChild<LLButton>("send_chat")->onCommit();
            incoming(owner, IM_SESSION_SEND, group, "Native incoming group");
            LLSD participants; participants["session_id"] = group;
            participants["agent_updates"][peer.asString()]["transition"] = "ENTER";
            participants["agent_updates"][peer.asString()]["info"]["mutes"]["text"] = false;
            event(owner, "ChatterBoxSessionAgentListUpdates", participants);
            require(LLIMModel::getInstance()->getSpeakerManager(group)->findSpeaker(peer).notNull(), "Group participant update lost shared speaker");
            ++step; return false;
        }
        if (step == 2)
        {
            if (!groupSend || !incomingGroup) return false;
            if (!muteQueued)
            {
                LLMute muted(peer, "CPU Sender", LLMute::AGENT, LLMute::flagTextChat);
                LLMuteList::getInstance()->add(muted);
                incoming(owner, IM_NOTHING_SPECIAL, direct, "Native muted text");
                muteQueued = true; return false;
            }
            if (!mutedDelivered) return false;
            require(!contains(direct, "Native muted text"), "Muted direct IM reached visible history");
            LLMuteList::getInstance()->remove(LLMute(peer, "CPU Sender", LLMute::AGENT));
            evidence["im_mute"] = true;
            require(gIMMgr->leaveSession(group), "Group leave failed");
            LLFloaterReg::destroyInstance("fs_impanel", group);
            require(!gIMMgr->hasSession(group), "Group leave retained model");
            auto* contacts = FSFloaterContacts::getInstance();
            contacts->openTab("groups");
            contacts->getPanelByName("groups_panel")->getChild<LLButton>("chat_btn")->onCommit();
            require(gIMMgr->hasSession(group), "Group cannot reopen from real contacts");
            LLSD failed; failed["success"] = false; failed["temp_session_id"] = group;
            failed["session_id"] = group; failed["error"] = "session_initialization_timed_out_error";
            event(owner, "ChatterBoxSessionStartReply", failed);
            require(startError.notNull(), "Group start failure was not surfaced");
            respond(startError);
            require(!gIMMgr->hasSession(group), "Start failure confirmation retained session");
            // Text invitation creates the shared group model and posts the real
            // capability acceptance, including its participant-list response.
            LLSD invite; auto& params = invite["instantmessage"]["message_params"];
            params["from_id"] = peer; params["from_name"] = "CPU Sender"; params["id"] = group;
            params["message"] = "Native invitation text"; params["offline"] = IM_ONLINE;
            params["timestamp"] = 1; params["parent_estate_id"] = 0;
            params["region_id"] = LLUUID::null;
            LLSD::Binary bucket; const std::string groupName = "Native replay group";
            bucket.insert(bucket.end(), groupName.begin(), groupName.end()); bucket.push_back(0);
            params["data"]["binary_bucket"] = bucket;
            event(owner, "ChatterBoxInvitation", invite);
            require(gIMMgr->hasSession(group) && contains(group, "Native invitation text"), "Invitation did not restore shared group UI/model");
            ++step; return false;
        }
        if (step == 3)
        {
            if (!left) return false;
            LLSD failed; failed["session_id"] = group; failed["success"] = false;
            failed["event"] = "message"; failed["error"] = "session_initialization_timed_out_error";
            LLStringUtil::format_map_t eventArgs;
            eventArgs["RECIPIENT"] = LLIMModel::getInstance()->getName(group);
            expectedEventText = LLTrans::getString("message", eventArgs);
            event(owner, "ChatterBoxSessionEventReply", failed);
            require(eventError.notNull() && eventTipDelivered, "Group event error did not deliver the real translated notifytip");
            LLSD close; close["session_id"] = group; close["reason"] = "session_initialization_timed_out_error";
            event(owner, "ForceCloseChatterBoxSession", close);
            require(forceClose.notNull(), "Server force close was not surfaced"); respond(forceClose);
            require(!gIMMgr->hasSession(group), "Force-close confirmation retained shared session");
            evidence["im_contacts_ui"] = true; evidence["im_direct_encoded"] = directSend;
            evidence["im_direct_incoming_offline"] = incomingText; evidence["im_typing"] = incomingTyping && typingStart && typingStop;
            evidence["im_group_start"] = groupStart; evidence["im_group_encoded"] = groupSend;
            evidence["im_group_incoming"] = incomingGroup; evidence["im_group_participants"] = true;
            evidence["im_group_leave"] = left;
            evidence["im_group_reopen"] = true; evidence["im_group_invitation"] = true;
            evidence["im_group_start_error"] = true; evidence["im_group_event_error"] = true;
            evidence["im_group_force_close"] = true;
            ++step; return false;
        }
        if (step == 4)
        {
            if (!notificationSent)
            {
                incoming(owner, IM_FRIENDSHIP_OFFERED, LLUUID::generateNewID(), "");
                struct InviteBucket { S32 fee; LLUUID role; } bucket{};
                pack_instant_message(gMessageSystem, inviteGroup, true, LLUUID::null, gAgentID,
                    "CPU Sender", "Native group invitation", IM_ONLINE, IM_GROUP_INVITATION,
                    inviteTransaction, 0, LLUUID::null, LLVector3::zero, 1,
                    reinterpret_cast<const U8*>(&bucket), sizeof(bucket));
                finishServerIM();
                gMessageSystem->sendReliable(owner.host());
                struct NoticeBucket { U8 hasInventory; U8 type; LLUUID groupId; char itemName; } notice{};
                notice.groupId = group;
                pack_instant_message(gMessageSystem, peer, false, LLUUID::null, gAgentID,
                    "CPU Sender", "Native notice subject|Native notice body", IM_ONLINE, IM_GROUP_NOTICE,
                    LLUUID::generateNewID(), 0, LLUUID::null, LLVector3::zero, 1,
                    reinterpret_cast<const U8*>(&notice), sizeof(notice));
                finishServerIM();
                gMessageSystem->sendReliable(owner.host());
                notificationSent = true; return false;
            }
            if (friendshipOffer.isNull() || groupOffer.isNull() || groupNotice.isNull()) return false;
            auto friendship = LLNotifications::instance().find(friendshipOffer);
            auto groupInvite = LLNotifications::instance().find(groupOffer);
            auto notice = LLNotifications::instance().find(groupNotice);
            require(friendship && groupInvite && notice, "Connected notification UI discarded offer/notice");
            retiredNotification = friendship->getPayload();
            require(vs_native_im_notification_current(retiredNotification), "Connected notification was incorrectly expired");
            require(notice->getPayload()["subject"].asString() == "Native notice subject" &&
                notice->getPayload()["message"].asString() == "Native notice body", "Group notice text changed");
            LLSD decline; decline["Decline"] = true;
            friendship->respond(decline); groupInvite->respond(decline);
            LLNotifications::instance().cancel(notice);
            evidence["im_friendship_notification"] = true;
            evidence["im_group_notice"] = true;
            ++step; return false;
        }
        if (step == 5)
        {
            if (!groupDeclined) return false;
            if (!inventorySent)
            {
                gSavedSettings.setBOOL("AutoAcceptNewInventory", false);
                gSavedSettings.setBOOL("FSUseLegacyInventoryAcceptMessages", false);
                gSavedSettings.setBOOL("ShowNewInventory", true);
                std::vector<U8> bucket(1 + UUID_BYTES);
                bucket[0] = LLAssetType::AT_TEXTURE;
                std::copy(offeredItem.mData, offeredItem.mData + UUID_BYTES, bucket.begin() + 1);
                pack_instant_message(gMessageSystem, peer, false, LLUUID::null, gAgentID,
                    "CPU Sender", "Native offered texture", IM_ONLINE, IM_INVENTORY_OFFERED,
                    offerTransaction, 0, LLUUID::null, LLVector3::zero, 1, bucket.data(), static_cast<S32>(bucket.size()));
                finishServerIM();
                gMessageSystem->sendReliable(owner.host());
                std::vector<U8> attachment(2 + UUID_BYTES);
                attachment[0] = 1; attachment[1] = LLAssetType::AT_TEXTURE;
                std::copy(group.mData, group.mData + UUID_BYTES, attachment.begin() + 2);
                const std::string itemName = "Native group texture";
                attachment.insert(attachment.end(), itemName.begin(), itemName.end()); attachment.push_back(0);
                groupNotice.setNull();
                pack_instant_message(gMessageSystem, peer, false, LLUUID::null, gAgentID,
                    "CPU Sender", "Native attached subject|Native attached body", IM_ONLINE, IM_GROUP_NOTICE,
                    attachmentTransaction, 0, LLUUID::null, LLVector3::zero, 1, attachment.data(), static_cast<S32>(attachment.size()));
                finishServerIM();
                gMessageSystem->sendReliable(owner.host());
                inventorySent = true; return false;
            }
            if (!inventoryResponded)
            {
                if (inventoryOffer.isNull() || groupNotice.isNull()) return false;
                auto offer = LLNotifications::instance().find(inventoryOffer);
                auto notice = LLNotifications::instance().find(groupNotice);
                require(offer && notice && notice->getPayload()["inventory_offer"].isMap(), "Real inventory offer/attachment missing");
                LLSD show; show["Show"] = true; offer->respond(show);
                // Exercise the production skinned group attachment controller.
                LLToastGroupNotifyPanel panel(notice);
                auto* link = panel.getChild<LLTextBox>("attachment");
                const S32 x = link->getRect().getWidth() / 2, y = link->getRect().getHeight() / 2;
                require(link->handleMouseDown(x, y, MASK_NONE), "Attachment link did not receive click");
                require(link->handleMouseUp(x, y, MASK_NONE), "Attachment link did not invoke real controller");
                inventoryResponded = true; return false;
            }
            if (!inventoryAccepted || !attachmentAccepted || !gInventory.getItem(offeredItem)) return false;
            auto* texture = LLFloaterReg::findTypedInstance<LLPreviewTexture>("preview_texture", offeredItem);
            if (!texture || !vs_ui_image_ready(profileImage)) return false;
            require(texture->getAssetStatus() == LLPreview::PREVIEW_ASSET_LOADED,
                "Real accepted texture preview did not decode");
            evidence["im_inventory_offer"] = true;
            evidence["im_group_attachment"] = true;
            evidence["im_inventory_preview"] = true;
            evidence["im_group_invitation_decline_encoded"] = true;
            gMessageSystem->setHandlerFunc("ImprovedInstantMessage", vs_native_im_receive);
            notificationConnection.disconnect(); ++step;
            auto* ownerUI = FSFloaterContacts::getInstance();
            auto* preview = new LLFloaterProfileTexture(ownerUI);
            ownerUI->addDependentFloater(preview);
            imageFloater = preview->getHandle();
            preview->loadAsset(profileImage);
            preview->openFloater();
            return false;
        }
        if (step == 6)
        {
            if (!vs_ui_image_ready(profileImage)) return false;
            auto image = LLUI::getUIImageByID(profileImage);
            require(image && image->getTextureWidth() > 0 && image->getTextureHeight() > 0 && imageFloater.get(),
                "Connected profile image was not published to real preview UI");
            evidence["connected_ui_image_decoded"] = true;
            evidence["connected_ui_image_width"] = image->getTextureWidth();
            evidence["connected_ui_image_height"] = image->getTextureHeight();
            ++step;
            // Keep the actual profile preview visible for the parent replay's
            // native packet/readback oracle, then retire it after logout.
            return false;
        }
        if (step == 7)
        {
            auto* prefs = LLFloaterReg::showTypedInstance<LLFloaterPreference>("preferences");
            require(prefs && prefs->getVisible(), "Production Preferences did not open");
            preferencesFloater = prefs->getHandle();
            originalItalic = gSavedSettings.getBOOL("EmotesUseItalic");
            auto* control = prefs->getChild<LLCheckBoxCtrl>("EmotesUseItalic");
            prefs->getChild<LLTabContainer>("pref core")->selectTabByName("chat");
            prefs->getChild<LLPanel>("chat")->getChild<LLTabContainer>("tabs")->selectTabByName("ChatVisuals");
            clickCheckbox(control);
            require(gSavedSettings.getBOOL("EmotesUseItalic") == !originalItalic, "Preferences checkbox did not edit setting");
            ++step; return false; // Draw the entire production panel graph before applying.
        }
        if (step == 8)
        {
            auto* prefs = dynamic_cast<LLFloaterPreference*>(preferencesFloater.get());
            require(prefs != nullptr, "Preferences disappeared before apply");
            prefs->getChild<LLButton>("OK")->onCommit();
            require(!prefs->getVisible() && gSavedSettings.getBOOL("EmotesUseItalic") == !originalItalic,
                "Preferences OK did not apply edited value");
            prefs->openFloater();
            auto* control = prefs->getChild<LLCheckBoxCtrl>("EmotesUseItalic");
            clickCheckbox(control);
            ++step; return false;
        }
        if (step == 9)
        {
            auto* prefs = dynamic_cast<LLFloaterPreference*>(preferencesFloater.get());
            require(prefs != nullptr, "Preferences disappeared before cancel");
            prefs->getChild<LLButton>("Cancel")->onCommit();
            require(!prefs->getVisible() && gSavedSettings.getBOOL("EmotesUseItalic") == !originalItalic,
                "Preferences Cancel did not restore applied setting");
            gSavedSettings.setBOOL("EmotesUseItalic", originalItalic);
            evidence["connected_preferences_apply_cancel"] = true;

            auto& sets = LGGContactSets::instance();
            sets.addSet(contactSetName); // Fixture account setup; editing uses real controls.
            auto* config = LLFloaterReg::showInstance("fs_contact_set_config", contactSetName);
            require(config && config->getVisible(), "Contact-set production controls unavailable");
            auto* notify = config->getChild<LLCheckBoxCtrl>("show_set_notifications");
            const bool originalNotify = sets.getNotifyForSet(contactSetName);
            clickCheckbox(notify);
            require(sets.getNotifyForSet(contactSetName) == !originalNotify,
                "Contact-set notification checkbox did not update account model");
            evidence["connected_contact_set_edit"] = true;

            const LLUUID blocked("73000000-0000-0000-0000-000000000003");
            require(LLMuteList::instance().add(LLMute(blocked, "Native blocked resident", LLMute::AGENT)),
                "Fixture mute setup failed");
            auto* blockUI = LLFloaterReg::showInstance("fs_blocklist");
            require(blockUI && blockUI->getVisible(), "Production blocklist unavailable");
            auto* list = blockUI->getChild<FSScrollListCtrl>("block_list");
            // Blocklist rows have independent IDs to also support name-only mutes.
            // Select the real row by its protocol identity column.
            bool selected = false;
            const S32 identityColumn = list->getColumn("item_mute_uuid")->mIndex;
            for (auto* row : list->getAllData())
                if (row->getColumn(identityColumn)->getValue().asUUID() == blocked)
                {
                    list->deselectAllItems();
                    selected = list->selectByValue(row->getValue());
                    break;
                }
            require(selected, "Real blocklist did not display added mute");
            list->onCommit(); blockUI->getChild<LLButton>("unblock_btn")->onCommit();
            require(!LLMuteList::instance().isMuted(blocked), "Real blocklist Unblock failed");
            evidence["connected_blocklist_edit"] = true;

            auto* session = LLIMModel::instance().findIMSession(direct);
            require(session != nullptr, "Direct conversation disappeared before transcript preview");
            LLSD key; key["user_name"] = session->mHistoryFileName; key["complete_name"] = session->mName;
            auto* transcript = LLFloaterReg::showTypedInstance<LLFloaterConversationPreview>("preview_conversation", key);
            require(transcript && transcript->getVisible(), "Production transcript preview unavailable");
            transcriptFloater = transcript->getHandle();
            ++step; return false;
        }
        if (step == 10)
        {
            auto* transcript = transcriptFloater.get();
            require(transcript != nullptr, "Transcript preview retired before asynchronous history load");
            if (transcript->getChild<FSChatHistory>("chat_history")->getText().find("Native incoming offline") == std::string::npos)
                return false;
            evidence["connected_transcript_preview"] = true;
            ++step; return false;
        }
        if (step == 11)
        {
            LLAvatarActions::showProfile(peer);
            auto* resident = LLFloaterReg::findTypedInstance<LLFloaterProfile>("profile", LLSD().with("id", peer));
            require(resident && resident->getVisible(), "Real resident Profile did not open");
            residentFloater = resident->getHandle();
            ++step; return false;
        }
        if (step == 12)
        {
            auto* resident = residentFloater.get();
            require(resident != nullptr, "Resident Profile disappeared before data arrival");
            auto* notes = resident->getChild<LLTextEditor>("notes_edit");
            if (notes->getText() != "Native profile notes" || !notes->getEnabled()) return false;
            resident->getChild<LLTabContainer>("panel_profile_tabs")->selectTabByName("panel_profile_notes");
            notes->setFocus(true); notes->setCursorAndScrollToEnd();
            require(notes->handleUnicodeCharHere('x'), "Real profile notes editor rejected typing");
            auto* discard = resident->getChild<LLButton>("notes_discard_changes");
            require(discard->getEnabled(), "Profile typing did not enable Discard");
            discard->onCommit();
            require(notes->getText() == "Native profile notes" && !discard->getEnabled(),
                "Profile Discard did not restore server notes");
            evidence["connected_resident_profile_notes"] = true;
            for (const char* name : {"GroupMembersRequest", "GroupRoleDataRequest", "GroupRoleMembersRequest"})
                gMessageSystem->setHandlerFunc(name, groupRequest, reinterpret_cast<void**>(this));
            const bool standalonePreference = gSavedSettings.getBOOL("FSUseStandaloneGroupFloater");
            gSavedSettings.setBOOL("FSUseStandaloneGroupFloater", false);
            LLGroupActions::show(group);
            auto* groupUI = FSFloaterGroup::findInstance(group);
            gSavedSettings.setBOOL("FSUseStandaloneGroupFloater", standalonePreference);
            require(groupUI && groupUI->getVisible(), "Native group action did not open real standalone information");
            groupFloater = groupUI->getHandle();
            LLMessageSystem* msg = gMessageSystem;
            msg->newMessage("GroupProfileReply"); msg->nextBlock("AgentData"); msg->addUUID("AgentID", gAgentID);
            msg->nextBlock("GroupData"); msg->addUUID("GroupID", group);
            msg->addString("Name", "Native replay group"); msg->addString("Charter", "Native group charter");
            msg->addBOOL("ShowInList", true); msg->addString("MemberTitle", "Resident");
            msg->addU64("PowersMask", GP_SESSION_JOIN); msg->addUUID("InsigniaID", profileImage);
            msg->addUUID("FounderID", peer); msg->addS32("MembershipFee", 0); msg->addBOOL("OpenEnrollment", false);
            msg->addS32("Money", 0); msg->addS32("GroupMembershipCount", 2); msg->addS32("GroupRolesCount", 0);
            msg->addBOOL("AllowPublish", false); msg->addBOOL("MaturePublish", false); msg->addUUID("OwnerRole", LLUUID::null);
            msg->sendReliable(owner.host());
            ++step; return false;
        }
        if (step == 13)
        {
            auto* data = LLGroupMgr::instance().getGroupData(group);
            if (!data || !data->isGroupPropertiesDataComplete()) return false;
            require(data->mCharter == "Native group charter", "Decoded group charter differs");
            if (!groupRequestsSent)
            {
                LLGroupMgr::instance().sendGroupMembersRequest(group);
                LLGroupMgr::instance().sendGroupRoleDataRequest(group);
                groupRequestsSent = true; return false;
            }
            if (groupMemberRequest.isNull() || groupRoleRequest.isNull()) return false;
            LLMessageSystem* msg = gMessageSystem;
            msg->newMessage("GroupMembersReply"); msg->nextBlock("AgentData"); msg->addUUID("AgentID", gAgentID);
            msg->nextBlock("GroupData"); msg->addUUID("GroupID", group); msg->addUUID("RequestID", groupMemberRequest);
            msg->addS32("MemberCount", 2);
            for (const auto& member : { gAgentID, peer })
            {
                msg->nextBlock("MemberData"); msg->addUUID("AgentID", member); msg->addS32("Contribution", 0);
                msg->addString("OnlineStatus", "Online"); msg->addU64("AgentPowers", GP_SESSION_JOIN);
                msg->addString("Title", "Resident"); msg->addBOOL("IsOwner", false);
            }
            msg->sendReliable(owner.host());
            msg->newMessage("GroupRoleDataReply"); msg->nextBlock("AgentData"); msg->addUUID("AgentID", gAgentID);
            msg->nextBlock("GroupData"); msg->addUUID("GroupID", group); msg->addUUID("RequestID", groupRoleRequest);
            msg->addS32("RoleCount", 1); msg->nextBlock("RoleData"); msg->addUUID("RoleID", LLUUID::null);
            msg->addString("Name", "Everyone"); msg->addString("Title", "Resident"); msg->addString("Description", "All residents");
            msg->addU64("Powers", GP_SESSION_JOIN); msg->addU32("Members", 2); msg->sendReliable(owner.host());
            ++step; return false;
        }
        if (step == 14)
        {
            auto* data = LLGroupMgr::instance().getGroupData(group);
            if (!data || !data->isMemberDataComplete() || !data->isRoleDataComplete()) return false;
            if (!groupRolesMembersSent)
            {
                if (groupRoleMemberRequest.isNull())
                {
                    LLGroupMgr::instance().sendGroupRoleMembersRequest(group);
                    return false;
                }
                LLMessageSystem* msg = gMessageSystem;
                msg->newMessage("GroupRoleMembersReply"); msg->nextBlock("AgentData");
                msg->addUUID("AgentID", gAgentID); msg->addUUID("GroupID", group);
                msg->addUUID("RequestID", groupRoleMemberRequest); msg->addU32("TotalPairs", 0);
                msg->nextBlock("MemberData"); msg->addUUID("RoleID", LLUUID::null); msg->addUUID("MemberID", LLUUID::null);
                msg->sendReliable(owner.host()); groupRolesMembersSent = true;
                return false;
            }
            if (!data->isRoleMemberDataComplete()) return false;
            auto* groupUI = groupFloater.get();
            require(groupUI != nullptr, "Group information disappeared before decoded members");
            auto* general = groupUI->getChild<LLPanel>("group_general_tab_panel");
            if (!general->getChild<LLNameListCtrl>("visible_members")->getItem(peer)) return false;
            require(general->getChild<LLTextEditor>("charter")->getText() == "Native group charter",
                "Real group information did not display decoded charter");
            if (auto* tabs = groupUI->findChild<LLTabContainer>("groups_accordion"))
                require(tabs->selectTabByName("group_roles_tab_panel"), "Real group Roles tab did not select");
            else
            {
                auto* generalAccordion = groupUI->getChild<LLAccordionCtrlTab>("group_general_tab");
                if (generalAccordion->getDisplayChildren())
                    require(generalAccordion->handleMouseDown(generalAccordion->getRect().getWidth() / 2,
                        generalAccordion->getRect().getHeight() - 11, MASK_NONE), "Real group General accordion rejected close click");
                auto* accordion = groupUI->getChild<LLAccordionCtrlTab>("group_roles_tab");
                if (!accordion->getDisplayChildren())
                    require(accordion->handleMouseDown(accordion->getRect().getWidth() / 2,
                        accordion->getRect().getHeight() - 11, MASK_NONE), "Real group Roles accordion rejected click");
                require(accordion->getDisplayChildren(), "Real group Roles accordion did not expand");
            }
            auto* memberTabs = groupUI->getChild<LLTabContainer>("roles_tab_container");
            require(memberTabs->selectTabByName("members_sub_tab"), "Real group Members tab did not select");
            ++step; return false;
        }
        if (step == 15)
        {
            auto* groupUI = groupFloater.get();
            require(groupUI != nullptr, "Group information disappeared before member selection");
            auto* roles = groupUI->getChild<LLPanel>("group_roles_tab_panel");
            auto* memberList = roles->getChild<LLNameListCtrl>("member_list");
            if (!memberList->selectByID(peer)) return false;
            memberList->onCommit();
            evidence["connected_group_profile_members_roles"] = true;
            for (const char* name : {"GroupMembersRequest", "GroupRoleDataRequest", "GroupRoleMembersRequest"})
                gMessageSystem->setHandlerFunc(name, null_message_callback);
            ++step; return false;
        }
        if (step == 16)
        {
            gMessageSystem->setHandlerFunc("GroupTitlesRequest", accountRequest, reinterpret_cast<void**>(this));
            auto* titles = LLFloaterReg::showInstance("fs_group_titles");
            require(titles && titles->getVisible(), "Production group Titles did not open");
            titleFloater = titles->getHandle();
            ++step; return false;
        }
        if (step == 17)
        {
            if (titleRequest.isNull()) return false;
            LLMessageSystem* msg = gMessageSystem;
            msg->newMessage("GroupTitlesReply"); msg->nextBlock("AgentData");
            msg->addUUID("AgentID", gAgentID); msg->addUUID("GroupID", group);
            msg->addUUID("RequestID", titleRequest); msg->nextBlock("GroupData");
            msg->addString("Title", "Native resident title"); msg->addUUID("RoleID", LLUUID::null);
            msg->addBOOL("Selected", true); msg->sendReliable(owner.host());
            ++step; return false;
        }
        if (step == 18)
        {
            auto* titles = titleFloater.get();
            require(titles != nullptr, "Group Titles retired before decoded role data");
            auto* list = titles->getChild<LLScrollListCtrl>("title_list");
            if (!list->selectByValue(group.asString() + LLUUID::null.asString())) return false;
            list->onCommit();
            require(list->getFirstSelected()->getColumn(0)->getValue().asString() == "Native resident title",
                "Group Titles failed to display decoded server title");
            auto* info = titles->getChild<LLButton>("btnInfo");
            require(info->getEnabled(), "Real group title selection disabled Info"); info->onCommit();
            require(FSFloaterGroup::findInstance(group)->getVisible(), "Group title Info failed to open production profile");
            evidence["connected_group_titles"] = true;
            gMessageSystem->setHandlerFunc("GroupTitlesRequest", null_message_callback);
            gMessageSystem->setHandlerFunc("DirFindQuery", accountRequest, reinterpret_cast<void**>(this));
            LLGroupActions::search();
            auto* search = LLFloaterReg::findInstance("vs_group_search");
            require(search && search->getVisible(), "Production group Search did not open");
            searchFloater = search->getHandle();
            auto* query = search->getChild<LLSearchComboBox>("name");
            query->setTextEntry(LLStringExplicit("Native replay group")); query->onCommit();
            ++step; return false;
        }
        if (step == 19)
        {
            if (searchRequest.isNull()) return false;
            LLMessageSystem* msg = gMessageSystem;
            msg->newMessage("DirGroupsReply"); msg->nextBlock("AgentData"); msg->addUUID("AgentID", gAgentID);
            msg->nextBlock("QueryData"); msg->addUUID("QueryID", searchRequest);
            msg->nextBlock("QueryReplies"); msg->addUUID("GroupID", group);
            msg->addString("GroupName", "Native replay group"); msg->addS32("Members", 2);
            msg->addF32("SearchOrder", 1.f); msg->sendReliable(owner.host());
            ++step; return false;
        }
        if (step == 20)
        {
            auto* search = searchFloater.get();
            require(search != nullptr, "Group Search retired before decoded results");
            auto* list = search->getChild<LLScrollListCtrl>("results");
            if (!list->selectByID(group)) return false;
            list->onCommit(); auto* profile = search->getChild<LLButton>("open_profile_btn");
            require(profile->getEnabled(), "Decoded group search selection disabled Profile"); profile->onCommit();
            require(FSFloaterGroup::findInstance(group)->getVisible(), "Group Search Profile failed to open production profile");
            evidence["connected_group_directory_search"] = true;
            gMessageSystem->setHandlerFunc("DirFindQuery", null_message_callback);
            ++step; return false;
        }
        if (step == 21)
        {
            LLAvatarPropertiesProcessor::instance().addObserver(gAgentID, this);
            LLAvatarActions::showProfile(gAgentID);
            auto* profile = LLFloaterReg::findTypedInstance<LLFloaterProfile>("profile", LLSD().with("id", gAgentID));
            require(profile && profile->getVisible(), "Real self Profile did not open");
            selfProfileFloater = profile->getHandle();
            ++step; return false;
        }
        if (step == 22 || step == 25)
        {
            auto* profile = selfProfileFloater.get();
            require(profile != nullptr, "Self Profile disappeared before image selection");
            auto* tabs = profile->getChild<LLTabContainer>("panel_profile_tabs");
            if (step == 22)
            {
                auto* panel = profile->findChild<LLPanelProfileSecondLife>("panel_profile_secondlife");
                require(panel != nullptr, "Real self profile tab controller is missing");
                if (!panel->getIsLoaded()) return false;
                require(tabs->selectTabByName("panel_profile_secondlife"), "Self Second Life tab did not select");
                auto* menuButton = panel->getChild<LLMenuButton>("image_action_btn");
                require(menuButton->getVisible() && menuButton->getEnabled(), "Real self profile image action is unavailable");
                require(menuButton->handleMouseDown(menuButton->getRect().getWidth() / 2,
                    menuButton->getRect().getHeight() / 2, MASK_NONE), "Real profile image menu rejected mouse down");
                menuButton->handleMouseUp(menuButton->getRect().getWidth() / 2,
                    menuButton->getRect().getHeight() / 2, MASK_NONE);
                auto* item = menuButton->getMenu()->getChild<LLMenuItemCallGL>("change_photo");
                item->buildDrawLabel();
                require(item->getEnabled(), "Real Change Photo menu item is disabled");
                item->onCommit();
            }
            else
            {
                auto* panel = profile->findChild<LLPanelProfileFirstLife>("panel_profile_firstlife");
                require(panel != nullptr, "Real self profile tab controller is missing");
                if (!panel->getIsLoaded()) return false;
                require(tabs->selectTabByName("panel_profile_firstlife"), "Self First Life tab did not select");
                auto* change = panel->getChild<LLButton>("fl_change_image");
                require(change->getVisible() && change->getEnabled(), "Real First Life Change Photo is unavailable");
                change->onCommit();
            }
            auto* picker = findProfilePicker(profile);
            require(picker != nullptr, "Real profile image action did not construct its inventory picker");
            profilePicker = picker->getHandle();
            pickerWaitTicks = 0;
            picker->getChild<LLInventoryPanel>("inventory panel")->setSelection(offeredItem, true);
            ++step; return false;
        }
        if (step == 23 || step == 26)
        {
            auto* picker = dynamic_cast<LLFloaterTexturePicker*>(profilePicker.get());
            require(picker && picker->getVisible(), "Production image picker disappeared before inventory selection");
            auto* inventory = picker->getChild<LLInventoryPanel>("inventory panel");
            auto* row = inventory->getItemByID(offeredItem);
            const auto selected = inventory->getSelectedItems();
            if (row && row->passedFilter() && (selected.size() != 1 || *selected.begin() != row))
                inventory->setSelection(offeredItem, true);
            auto* select = picker->getChild<LLButton>("Select");
            if (++pickerWaitTicks == 1 || pickerWaitTicks % 120 == 0)
            {
                LL_INFOS("NativeIMReplay") << "Profile picker stage " << step
                    << " asset=" << picker->getAssetID() << " row=" << (row != nullptr)
                    << " filtered=" << (row && row->passedFilter()) << " selected=" << selected.size()
                    << " ready=" << vs_ui_image_ready(profileImage) << " select=" << select->getEnabled()
                    << " inventory_visible=" << inventory->isInVisibleChain() << LL_ENDL;
            }
            if (picker->getAssetID() != profileImage || !vs_ui_image_ready(profileImage) || !select->getEnabled())
                return false;
            require(select->isInVisibleChain(), "Real inventory image Select is not visible");
            select->onCommit();
            ++step; return false;
        }
        if (step == 24 || step == 27)
        {
            auto* profile = selfProfileFloater.get();
            require(profile != nullptr, "Self Profile disappeared before server-confirmed image update");
            const bool secondLife = step == 24;
            if (secondLife ? !selfSecondLifeSaved : !selfFirstLifeSaved) return false;
            auto* picture = profile->getChild<LLTextureCtrl>(secondLife ? "2nd_life_pic" : "real_world_pic");
            if (picture->getImageAssetID() != profileImage) return false;
            require(vs_ui_image_ready(profileImage), "Saved profile image lacks a current decoded native resource");
            evidence[secondLife ? "connected_profile_second_life_image_save" : "connected_profile_first_life_image_save"] = true;
            if (!secondLife) LLAvatarPropertiesProcessor::instance().removeObserver(gAgentID, this);
            ++step; return false;
        }
        if (step == 28)
        {
            LLAvatarName name;
            require(LLAvatarNameCache::get(peer, &name), "Production peer name unavailable before display-name event");
            LLSD body;
            body["agent_id"] = peer;
            body["old_display_name"] = name.getDisplayName();
            body["agent"] = name.asLLSD();
            body["agent"]["display_name"] = "Native renamed resident";
            body["agent"]["is_display_name_default"] = false;
            event(owner, "DisplayNameUpdate", body);
            require(LLAvatarNameCache::get(peer, &name) && name.getDisplayName() == "Native renamed resident",
                "Decoded display-name event did not update production name cache");
            require(!FSRadar::instanceExists(), "Connected text UI constructed deferred world radar service");
            ++step; return false;
        }
        if (step == 29)
        {
            auto* profile = residentFloater.get();
            require(profile != nullptr, "Resident profile retired before name-change UI refresh");
            if (profile->getChild<LLTextEditor>("complete_name")->getText().find("Native renamed resident") == std::string::npos)
                return false;
            require(!FSRadar::instanceExists(), "Display-name UI refresh constructed deferred world radar");
            evidence["connected_display_name_update"] = true;
            ++step; return false;
        }
        if (step == 30)
        {
            gMessageSystem->setHandlerFunc("ImprovedInstantMessage", packet, reinterpret_cast<void**>(this));
            gMessageSystem->setHandlerFunc("MoneyTransferRequest", moneyPacket, reinterpret_cast<void**>(this));
            accountNotifications = LLNotifications::instance().getChannel("System")->connectChanged([this](const LLSD& value)
            {
                auto n = LLNotifications::instance().find(value["id"].asUUID());
                if (!n) return false;
                if (n->getName() == "ShareItemsConfirmation") shareConfirmation = n->getID();
                if (n->getName() == "PayConfirmation") payConfirmation = n->getID();
                if (n->getName() == "NativePaymentBalanceUnavailable") unknownBalanceAlert = n->getID();
                if (n->getName() == "NativePaymentInsufficientFunds") insufficientBalanceAlert = n->getID();
                return false;
            });
            auto* profile = residentFloater.get();
            require(profile != nullptr, "Real resident profile unavailable for Share"); profile->setVisible(true);
            require(profile->getChild<LLTabContainer>("panel_profile_tabs")->selectTabByName("panel_profile_secondlife"),
                "Resident Share profile tab unavailable");
            auto* menu = profile->getChild<LLMenuButton>("overflow_btn");
            require(menu->getEnabled() && menu->isInVisibleChain(), "Actual resident overflow control unavailable");
            menu->handleMouseDown(menu->getRect().getWidth()/2, menu->getRect().getHeight()/2, MASK_NONE);
            menu->handleMouseUp(menu->getRect().getWidth()/2, menu->getRect().getHeight()/2, MASK_NONE);
            auto* share = menu->getMenu()->getChild<LLMenuItemCallGL>("share"); share->buildDrawLabel();
            require(share->getEnabled(), "Actual Share menu is disabled"); share->onCommit();
            auto* panel = LLInventoryPanel::getActiveInventoryPanel(false, true);
            require(panel && panel->isInVisibleChain(), "Real Share did not expose the production account inventory tree");
            shareInventory = panel->getParentByType<LLFloater>()->getHandle();
            panel->setSelection(offeredItem, true);
            ++step; return false;
        }
        if (step == 31)
        {
            auto* panel = LLInventoryPanel::getActiveInventoryPanel(false, true);
            require(panel && shareInventory.get(), "Share inventory browser retired before selection");
            if (!panel->getItemByID(offeredItem)) return false;
            panel->setSelection(offeredItem, true);
            const auto selected = panel->getSelectedItems();
            if (selected.size() != 1 || *selected.begin() != panel->getItemByID(offeredItem)) return false;
            auto* item = gInventory.getItem(offeredItem);
            require(item != nullptr, "Selected Share item missing from authenticated inventory");
            auto* conversation = FSFloaterIM::show(direct);
            require(conversation && conversation->getVisible(), "Real direct conversation unavailable for inventory drop");
            EAcceptance accept = ACCEPT_NO; std::string tooltip;
            require(conversation->handleDragAndDrop(1, 1, MASK_NONE, true, DAD_TEXTURE, item, &accept, tooltip) &&
                accept >= ACCEPT_YES_COPY_SINGLE, "Actual conversation rejected selected inventory item");
            require(shareConfirmation.notNull(), "Real Share drop did not ask for recipient confirmation");
            auto n = LLNotifications::instance().find(shareConfirmation);
            require(n != nullptr, "Share confirmation retired before user response");
            LLSD response = n->getResponseTemplate(LLNotification::WITHOUT_DEFAULT_BUTTON);
            response["OK_okcancelignore"] = true;
            n->respond(response);
            ++step; return false;
        }
        if (step == 32)
        {
            if (!outgoingSharedItem) return false;
            evidence["connected_inventory_share"] = true;
            gMessageSystem->setHandlerFunc("ImprovedInstantMessage", vs_native_im_receive);
            savedPaymentConfirm = gSavedSettings.getBOOL("FSConfirmPayments");
            savedPaymentThreshold = gSavedSettings.getS32("FSPaymentConfirmationThreshold");
            gSavedSettings.setBOOL("FSConfirmPayments", true); gSavedSettings.setS32("FSPaymentConfirmationThreshold", 0);
            sendBalance(owner, 100); ++step; return false;
        }
        if (step == 33)
        {
            if (!owner.balanceKnown() || owner.balance() != 100) return false;
            openResidentPay(); payFloater.get()->getChild<LLButton>("fastpay 1")->onCommit();
            require(payConfirmation.notNull(), "Real resident Pay skipped requested confirmation");
            auto n = LLNotifications::instance().find(payConfirmation);
            require(n != nullptr, "Real payment confirmation missing");
            LLSD response = n->getResponseTemplate(LLNotification::WITHOUT_DEFAULT_BUTTON);
            response["OK_okcancelbuttons"] = true;
            const S32 option = LLNotificationsUtil::getSelectedOption(n->asLLSD(), response);
            if (option != 0)
                LL_WARNS("NativeReplay") << "Payment confirmation option=" << option
                    << " form=" << ll_pretty_print_sd(n->getForm()->asLLSD())
                    << " response=" << ll_pretty_print_sd(response) << LL_ENDL;
            require(option == 0, "Real payment confirmation response does not select Pay");
            n->respond(response);
            ++step; return false;
        }
        if (step == 34)
        {
            if (outgoingPayments != 1) return false;
            evidence["connected_resident_pay"] = true;
            owner.clearBalance(); openResidentPay(); payFloater.get()->getChild<LLButton>("fastpay 1")->onCommit();
            require(unknownBalanceAlert.notNull(), "Unknown account balance did not gate actual Pay control");
            if (auto n = LLNotifications::instance().find(unknownBalanceAlert))
                n->respond(n->getResponseTemplate(LLNotification::WITH_DEFAULT_BUTTON));
            sendBalance(owner, 0); ++step; return false;
        }
        if (step == 35)
        {
            if (!owner.balanceKnown() || owner.balance() != 0) return false;
            openResidentPay(); payFloater.get()->getChild<LLButton>("fastpay 1")->onCommit();
            require(insufficientBalanceAlert.notNull(), "Insufficient account funds did not gate actual Pay control");
            if (auto n = LLNotifications::instance().find(insufficientBalanceAlert))
                n->respond(n->getResponseTemplate(LLNotification::WITH_DEFAULT_BUTTON));
            sendBalance(owner, 100); ++step; return false;
        }
        if (step == 36)
        {
            if (!owner.balanceKnown() || owner.balance() != 100) return false;
            payConfirmation.setNull(); openResidentPay(); payFloater.get()->getChild<LLButton>("fastpay 1")->onCommit();
            auto n = LLNotifications::instance().find(payConfirmation);
            require(n != nullptr, "Payment lifetime fixture lacks real confirmation");
            LLFloaterReg::destroyInstance("pay_resident", peer);
            LLSD response = n->getResponseTemplate(LLNotification::WITHOUT_DEFAULT_BUTTON);
            response["OK_okcancelbuttons"] = true;
            const S32 option = LLNotificationsUtil::getSelectedOption(n->asLLSD(), response);
            if (option != 0)
                LL_WARNS("NativeReplay") << "Payment confirmation option=" << option
                    << " form=" << ll_pretty_print_sd(n->getForm()->asLLSD())
                    << " response=" << ll_pretty_print_sd(response) << LL_ENDL;
            require(option == 0, "Real payment confirmation response does not select Pay");
            n->respond(response);
            gSavedSettings.setBOOL("FSConfirmPayments", savedPaymentConfirm);
            gSavedSettings.setS32("FSPaymentConfirmationThreshold", savedPaymentThreshold);
            ++step; return false;
        }
        if (step == 37)
        {
            require(outgoingPayments == 1, "Unknown/insufficient/retired payment produced an encoded transfer");
            evidence["connected_payment_balance_gate"] = true;
            gMessageSystem->setHandlerFunc("MoneyTransferRequest", null_message_callback);
            accountNotifications.disconnect(); ++step; return false;
        }
        if (step == 38)
        {
            const auto filename = gDirUtilp->findSkinnedFilename("textures", "Blank.png");
            require(!filename.empty(), "Packaged CPU texture upload fixture is missing");
            LLSD key; key["filename"] = filename; key["dest"] = textureFolder;
            upload_single_file({filename}, LLFilePicker::FFLOAD_IMAGE, textureFolder);
            auto* dialog = LLFloaterReg::findTypedInstance<LLFloaterImagePreview>("upload_image", key);
            require(dialog && dialog->isInVisibleChain(), "Real texture upload dialog did not open");
            textureUpload = dialog->getHandle();
            dialog->getChild<LLLineEditor>("name_form")->setText(LLStringExplicit("Native uploaded texture"));
            dialog->getChild<LLLineEditor>("description_form")->setText(LLStringExplicit("Connected native upload qualification"));
            auto* upload = dialog->getChild<LLButton>("ok_btn");
            require(upload->getEnabled() && upload->isInVisibleChain(), "Real texture Upload control is unavailable");
            ++step; return false; // Render the decoded native preview before committing.
        }
        if (step == 39)
        {
            auto* dialog = textureUpload.get();
            require(dialog != nullptr, "Texture upload dialog retired before Upload");
            dialog->getChild<LLButton>("ok_btn")->onCommit(); ++step; return false;
        }
        if (step == 40)
        {
            auto* item = gInventory.getItem(LLUUID("71000000-0000-0000-0000-000000000002"));
            if (!item) return false;
            require(item->getParentUUID() == textureFolder && item->getType() == LLAssetType::AT_TEXTURE &&
                item->getAssetUUID() == LLUUID("60000000-0000-0000-0000-000000000007") &&
                item->getName() == "Native uploaded texture", "Actual upload response produced incorrect texture inventory metadata");
            evidence["connected_inventory_texture_upload"] = true;
            waveFilename = gDirUtilp->getTempFilename() + ".wav";
            // One second of valid 44.1 kHz mono PCM; the ordinary upload path encodes it to Vorbis.
            std::ofstream wav(waveFilename, std::ios::binary);
            require(wav.good(), "Unable to create bounded sound upload fixture");
            auto u16 = [&wav](U16 n) { const char b[] = {static_cast<char>(n), static_cast<char>(n >> 8)}; wav.write(b, 2); };
            auto u32 = [&wav](U32 n) { const char b[] = {static_cast<char>(n), static_cast<char>(n >> 8),
                static_cast<char>(n >> 16), static_cast<char>(n >> 24)}; wav.write(b, 4); };
            wav.write("RIFF", 4); u32(36 + 88200); wav.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
            u32(44100); u32(88200); u16(2); u16(16); wav.write("data", 4); u32(88200);
            for (unsigned i = 0; i != 44100; ++i) u16(0);
            wav.close();
            LLSD key; key["filename"] = waveFilename; key["dest"] = textureFolder;
            upload_single_file({waveFilename}, LLFilePicker::FFLOAD_WAV, textureFolder);
            auto* dialog = LLFloaterReg::findInstance("upload_sound", key);
            require(dialog && dialog->isInVisibleChain(), "Real sound upload dialog did not open");
            soundUpload = dialog->getHandle();
            dialog->getChild<LLLineEditor>("name_form")->setText(LLStringExplicit("Native uploaded sound"));
            dialog->getChild<LLLineEditor>("description_form")->setText(LLStringExplicit("Connected native upload qualification"));
            auto* upload = dialog->getChild<LLButton>("ok_btn");
            require(upload->getEnabled() && upload->isInVisibleChain(), "Real sound Upload control is unavailable");
            ++step; return false;
        }
        if (step == 41)
        {
            auto* dialog = soundUpload.get();
            require(dialog != nullptr, "Sound upload dialog retired before Upload");
            dialog->getChild<LLButton>("ok_btn")->onCommit(); ++step; return false;
        }
        if (step == 42)
        {
            auto* item = gInventory.getItem(LLUUID("71000000-0000-0000-0000-000000000003"));
            if (!item) return false;
            require(item->getParentUUID() == textureFolder && item->getType() == LLAssetType::AT_SOUND &&
                item->getAssetUUID() == LLUUID("60000000-0000-0000-0000-000000000008") &&
                item->getName() == "Native uploaded sound", "Actual upload response produced incorrect sound inventory metadata");
            evidence["connected_inventory_sound_upload"] = true;
            LLFile::remove(waveFilename); waveFilename.clear();
            ++step; return false;
        }
        if (step == 43)
        {
            savedLegacyProperties = gSavedSettings.getBOOL("FSUseLegacyObjectProperties");
            gSavedSettings.setBOOL("FSUseLegacyObjectProperties", true);
            gMessageSystem->setHandlerFunc("UpdateInventoryItem", inventoryPacket, reinterpret_cast<void**>(this));
            const LLUUID uploaded("71000000-0000-0000-0000-000000000002");
            show_item_profile(uploaded);
            auto* properties = LLFloaterReg::findTypedInstance<LLFloaterProperties>("properties", LLSD().with("item_id", uploaded));
            require(properties && properties->isInVisibleChain(), "Real legacy inventory Properties did not open");
            legacyProperties = properties->getHandle();
            require(properties->getChild<LLLineEditor>("LabelItemName")->getText() == "Native uploaded texture" &&
                properties->getChild<LLCheckBoxCtrl>("CheckOwnerCopy")->get(), "Legacy Properties lost actual inventory metadata/permissions");
            auto* description = properties->getChild<LLLineEditor>("LabelItemDesc");
            require(description->getEnabled(), "Actual owned legacy description cannot be edited");
            description->setText(LLStringExplicit("Native legacy metadata edit")); description->onCommit();
            ++step; return false;
        }
        if (step == 44)
        {
            const LLUUID uploaded("71000000-0000-0000-0000-000000000002");
            if (propertyWrites < 1 || gInventory.getItem(uploaded)->getDescription() != "Native legacy metadata edit") return false;
            gSavedSettings.setBOOL("FSUseLegacyObjectProperties", false); show_item_profile(uploaded);
            auto* properties = LLFloaterReg::findInstance("item_properties", LLSD().with("id", uploaded));
            require(properties && properties->isInVisibleChain(), "Real modern inventory Properties did not open");
            modernProperties = properties->getHandle();
            ++step; return false;
        }
        if (step == 45)
        {
            auto* properties = modernProperties.get();
            require(properties != nullptr, "Modern Properties retired before editing");
            auto* info = properties->findChild<LLSidepanelItemInfo>("sidepanel");
            require(info != nullptr, "Modern inventory Properties lost actual item controller");
            require(info->getChild<LLLineEditor>("LabelItemName")->getText() == "Native uploaded texture" &&
                info->getChild<LLCheckBoxCtrl>("CheckOwnerCopy")->get(), "Modern Properties lost actual inventory metadata/permissions");
            auto* description = info->getChild<LLTextEditor>("LabelItemDesc");
            require(description->getEnabled(), "Actual owned modern description cannot be edited");
            description->setText(LLStringExplicit("Native modern metadata edit")); description->onCommit();
            ++step; return false;
        }
        if (step == 46)
        {
            const LLUUID uploaded("71000000-0000-0000-0000-000000000002");
            if (propertyWrites < 2 || gInventory.getItem(uploaded)->getDescription() != "Native modern metadata edit") return false;
            gSavedSettings.setBOOL("FSUseLegacyObjectProperties", savedLegacyProperties);
            evidence["connected_inventory_properties"] = true;
            gMessageSystem->setHandlerFunc("UpdateInventoryItem", null_message_callback);
            auto* info = modernProperties.get()->findChild<LLSidepanelItemInfo>("sidepanel");
            auto* thumbnailButton = info->getChild<LLButton>("change_thumbnail_btn");
            require(thumbnailButton->getEnabled() && thumbnailButton->isInVisibleChain(), "Actual metadata thumbnail control unavailable");
            thumbnailButton->onCommit();
            auto* thumbnail = LLFloaterReg::findInstance("change_item_thumbnail", LLSD().with("task_id", LLUUID::null).with("item_id", uploaded));
            require(thumbnail && thumbnail->isInVisibleChain(), "Real inventory metadata thumbnail editor did not open");
            thumbnailEditor = thumbnail->getHandle();
            auto* sound = LLFloaterReg::showInstance("preview_sound", LLUUID("71000000-0000-0000-0000-000000000003"));
            require(sound && sound->isInVisibleChain(), "Real uploaded inventory Sound preview did not open");
            soundPreview = sound->getHandle();
            require(sound->getChild<LLLineEditor>("desc")->getText() == "Connected native upload qualification",
                "Real Sound preview lost uploaded description");
            ++step; return false; // Draw both real native metadata graphs.
        }
        if (step == 47)
        {
            require(thumbnailEditor.get() && soundPreview.get(), "Native metadata editor retired before drawing");
            evidence["connected_inventory_sound_preview"] = true;
            ++step; return false;
        }
        if (step == 48)
        {
            const LLUUID script("71000000-0000-0000-0000-000000000004");
            if (!gInventory.getItem(script))
            {
                if (!scriptRequested)
                {
                    LLInventoryFetchItemsObserver fetch(script);
                    fetch.startFetch(); scriptRequested = true;
                }
                return false;
            }
            savedPreprocessor = gSavedSettings.getBOOL("_NACL_LSLPreprocessor");
            gSavedSettings.setBOOL("_NACL_LSLPreprocessor", false);
            auto* preview = LLFloaterReg::showInstance("preview_script", script);
            require(preview && preview->isInVisibleChain(), "Real inventory Script editor did not open");
            scriptPreview = preview->getHandle();
            ++step; return false;
        }
        if (step == 49)
        {
            auto* preview = scriptPreview.get();
            require(preview != nullptr, "Script editor retired before asset download");
            auto* editor = preview->findChild<LLScriptEditor>("Script Editor");
            require(editor != nullptr, "Actual shared Script text control is missing");
            if (editor->getText().find("Native initial script") == std::string::npos) return false;
            require(editor->getEnabled() && editor->isInVisibleChain(), "Actual owned Script text cannot be edited");
            editor->setFocus(true); editor->selectAll();
            const std::string text = "default { state_entry() { llOwnerSay(\"Native edited script\"); } }";
            for (const auto c : text)
                require(editor->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(c))),
                    "Actual Script editor rejected typed text");
            ++step; return false; // Let real script controller draw/update Save enablement.
        }
        if (step == 50)
        {
            auto* preview = scriptPreview.get();
            require(preview != nullptr, "Script editor retired before Save");
            auto* save = preview->getChild<LLButton>("save_btn");
            if (!save->getEnabled()) return false;
            require(save->isInVisibleChain(), "Actual Script Save button is not visible");
            save->onCommit(); ++step; return false;
        }
        if (step == 51)
        {
            auto* item = gInventory.getItem(LLUUID("71000000-0000-0000-0000-000000000004"));
            if (!item || item->getAssetUUID() != LLUUID("60000000-0000-0000-0000-000000000010")) return false;
            auto* editor = scriptPreview.get()->findChild<LLScriptEditor>("Script Editor");
            require(editor && editor->getText().find("Native edited script") != std::string::npos && editor->isPristine(),
                "Actual uploaded Script editor did not confirm its saved text");
            gSavedSettings.setBOOL("_NACL_LSLPreprocessor", savedPreprocessor);
            evidence["connected_inventory_script_edit_save"] = true;
            ++step; return false;
        }
        return true;
    }
};
Replay replay;
U64 economyFixtureGeneration = 0;
U64 economyRequestedGeneration = 0;
}
void vs_native_im_replay_economy_request(LLMessageSystem* message, void**)
{
    const auto owner = VSNativeSession::active();
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected || message->getSender() != owner->host()) return;
    economyFixtureGeneration = owner->generation();
    LLMessageSystem* reply = gMessageSystem;
    reply->newMessage("EconomyData"); reply->nextBlock("Info");
    for (const char* field : {"ObjectCapacity", "ObjectCount", "PriceObjectClaim", "PricePublicObjectDecay",
         "PricePublicObjectDelete", "PriceEnergyUnit", "PriceUpload", "PriceRentLight", "TeleportMinPrice", "PriceGroupCreate",
         "PriceParcelClaim", "PriceParcelRent"})
        reply->addS32(field, 0);
    for (const char* field : {"TeleportPriceExponent", "PriceParcelClaimFactor", "EnergyEfficiency", "PriceObjectRent", "PriceObjectScaleFactor"})
        reply->addF32(field, 0.f);
    reply->sendReliable(owner->host());
}
bool vs_native_im_replay_tick(VSNativeSession& session, LLSD& evidence)
{
    if (LLGridManager::instance().isInSecondLife())
    {
        require(LLAgentBenefitsMgr::current().getTextureUploadCost() == 0 &&
            LLAgentBenefitsMgr::current().getSoundUploadCost() == 0,
            "Actual SL login benefits did not initialize account upload costs");
        evidence["connected_account_benefits"] = true;
    }
    else
    {
        if (economyFixtureGeneration != session.generation() || LLAgentBenefitsMgr::current().getTextureUploadCost() < 0)
        {
            if (economyRequestedGeneration != session.generation())
            {
                economyRequestedGeneration = session.generation();
                // Reissue through the actual shared native sender after fixture admission is installed.
                gMessageSystem->newMessage("EconomyDataRequest");
                gAgent.sendReliableMessage();
            }
            return false;
        }
        require(LLAgentBenefitsMgr::current().getTextureUploadCost() == 0 &&
            LLAgentBenefitsMgr::current().getSoundUploadCost() == 0,
            "Encoded OpenSim economy reply lost fixture costs");
        evidence["connected_account_benefits"] = true;
    }
    return replay.tick(session, evidence);
}
bool vs_native_im_replay_expired()
{
    const bool expired = replay.retiredGuard && !replay.retiredGuard() &&
        !vs_native_im_notification_current(replay.retiredNotification) &&
        !vs_ui_image_ready(replay.profileImage);
    if (auto* floater = replay.imageFloater.get()) delete floater;
    return expired;
}
