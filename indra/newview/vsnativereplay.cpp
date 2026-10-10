// Replay ordinary login and native session dispatch against a loopback fixture. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsnativereplay.h"
#include "vsnativeimreplay.h"
#include "vsnativesession.h"
#include "vsplainchat.h"
#include "fsnearbychatvoicemonitor.h"
#include "fsfloaternearbychat.h"
#include "llappviewer.h"
#include "llviewerwindow.h"
#include "llrootview.h"
#include "llviewercontrol.h"
#include "llstartup.h"
#include "lllogininstance.h"
#include "lluri.h"
#include "lltexteditor.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llbutton.h"
#include "lltoolbarview.h"
#include "lltoolbar.h"
#include "lllocationinputctrl.h"
#include "llfavoritesbar.h"
#include "llstatusbar.h"
#include "llwindow.h"
#include "llui.h"
#include "lluictrlfactory.h"
#include "llfocusmgr.h"
#include "llclipboard.h"
#include "llviewermenu.h"
#include "llmenugl.h"
#include "llpersistentnotificationstorage.h"
#include "lldonotdisturbnotificationstorage.h"
#include "llworld.h"
#include "vsvulkancontext.h"
#include "llsdserialize.h"
#include "llsdutil_math.h"
#include "llchat.h"
#include "llagent.h"
#include "lllandmarkactions.h"
#include "lllandmark.h"
#include "llregionhandle.h"
#include "llinventorymodel.h"
#include "llworldmap.h"
#include "llnotifications.h"
#include "lltoast.h"
#include "lltoastalertpanel.h"
#include "llcommunicationchannel.h"
#include "llfloaterreg.h"
#include "llfloater.h"
#include "llurldispatcher.h"
#include "llfloatersidepanelcontainer.h"
#include "llpanelpeople.h"
#include "lltabcontainer.h"
#include "llfiltereditor.h"
#include "fsfloatercontacts.h"
#include "llmutelist.h"
#include "llxfermanager.h"
#include "llxfer.h"
#include "workqueue.h"
#include "message.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

void setting_setup_signal_listener(LLControlGroup&, const std::string&, std::function<void(const LLSD&)>);
void setting_setup_signal_listener(LLControlGroup&, const std::string&, std::function<void()>);
void vs_native_replay_capture(VSVulkanContext&, const std::string&, std::shared_ptr<bool>);
namespace
{
void require(bool condition, const char* reason) { if (!condition) { LL_WARNS("NativeSessionReplay") << reason << LL_ENDL; throw std::runtime_error(reason); } }
void qualifyConnectedSkinGeometry(LLSD& report)
{
    auto* root = gViewerWindow->getRootView();
    auto* chat = gViewerWindow->nativeChat();
    auto* navigation = root ? root->findChild<LLPanel>("navigation_bar") : nullptr;
    require(chat && navigation && gToolBarView && gStatusBar && gMenuBarView &&
            chat->getVisible() && navigation->getVisible() && gToolBarView->getVisible() &&
            gStatusBar->getVisible(), "Required real connected chrome owners are missing or hidden");
    auto contained = [&report](LLView* parent, const char* name, bool visible = true, S32 bottom_overhang = 0)
    {
        auto* child = parent->findChild<LLView>(name);
        require(child && (!visible || child->isInVisibleChain()), "Required connected skin child is hidden or missing");
        const LLRect bounds = parent->calcScreenRect();
        LLRect r = child->calcScreenRect();
        LL_INFOS("NativeSessionReplay") << "Skin geometry " << parent->getName() << "/" << name
            << " rect=" << r.mLeft << "," << r.mBottom << "," << r.mRight << "," << r.mTop
            << " owner=" << bounds.mLeft << "," << bounds.mBottom << "," << bounds.mRight << "," << bounds.mTop << LL_ENDL;
        require(r.getWidth() > 0 && r.getHeight() > 0 && r.mLeft >= bounds.mLeft &&
                r.mRight <= bounds.mRight && r.mBottom >= bounds.mBottom - bottom_overhang && r.mTop <= bounds.mTop,
                "Connected skin child lies outside its owner");
        r.translate(-bounds.mLeft, -bounds.mBottom);
        LLSD rect; rect.append(r.mLeft); rect.append(r.mBottom); rect.append(r.mRight); rect.append(r.mTop);
        report["connected_skin_rects"][name] = rect;
        return r;
    };
    const auto header = contained(chat, "native_region");
    const auto logout = contained(chat, "native_logout");
    const auto transcript = contained(chat, "native_plain_transcript");
    const auto input = contained(chat, "native_chat_input");
    require(input.mTop <= transcript.mBottom && transcript.mTop <= header.mBottom &&
            transcript.mTop <= logout.mBottom && header.mRight <= logout.mLeft,
            "Connected nearby skin controls overlap");
    require(navigation->findChild<LLLocationInputCtrl>("location_combo") &&
            navigation->findChild<LLFavoritesBarCtrl>("favorite"), "Real location/favorites controllers are missing");
    auto* location = navigation->findChild<LLLocationInputCtrl>("location_combo");
    if (location->isInVisibleChain()) contained(navigation, "location_combo");
    else require(!gSavedSettings.getBOOL("ShowNavbarNavigationPanel"), "Saved navigation preference did not match real visibility");
    // Favorites visibility is a saved account preference, not a reason to change user configuration.
    if (auto* favorite = navigation->findChild<LLFavoritesBarCtrl>("favorite"); favorite->isInVisibleChain())
        contained(navigation, "favorite");
    for (const char* name : {"toolbar_left", "toolbar_right", "toolbar_bottom"})
    {
        auto* toolbar = gToolBarView->findChild<LLToolBar>(name);
        require(toolbar, "Real configured toolbar owner is missing");
        if (toolbar->hasButtons()) contained(gToolBarView, name);
    }
    contained(root, "status");
    // These two shared status text controls deliberately extend below their
    // background panels. Witness the selected layered XUI's exact top/height,
    // rather than permitting a general geometry tolerance or changing the skin.
    LLXMLNodePtr status_xml;
    require(LLUICtrlFactory::getLayeredXMLNode("panel_status_bar.xml", status_xml),
        "Status geometry XUI witness is missing");
    auto find_node = [](auto&& self, LLXMLNodePtr node, const char* target) -> LLXMLNodePtr
    {
        if (!node) return nullptr;
        std::string name;
        if (node->getAttributeString("name", name) && name == target) return node;
        for (auto child = node->getFirstChild(); child; child = child->getNextSibling())
            if (auto found = self(self, child, target)) return found;
        return nullptr;
    };
    auto status_text = [&](const char* background, const char* name)
    {
        auto text_xml = find_node(find_node, status_xml, name);
        auto bg_xml = find_node(find_node, status_xml, background);
        S32 text_top = 0, text_height = 0, bg_top = 0;
        require(text_xml && bg_xml && text_xml->getAttributeS32("top", text_top) &&
            text_xml->getAttributeS32("height", text_height) && bg_xml->getAttributeS32("top", bg_top),
            "Status text geometry has no exact selected-XUI witness");
        auto* text = gStatusBar->findChild<LLView>(name);
        auto* bg = gStatusBar->findChild<LLView>(background);
        require(text && bg && text->getParent() == bg, "Status text XUI ownership changed");
        const LLRect owner = gStatusBar->calcScreenRect();
        const LLRect actual = text->calcScreenRect();
        require(bg->calcScreenRect().mTop == owner.mTop - bg_top &&
            actual.mTop == owner.mTop - bg_top - text_top && actual.getHeight() == text_height,
            "Status text differs from its selected-XUI top/height witness");
        contained(gStatusBar, name, true, llmax(0, bg_top + text_top + text_height - owner.getHeight()));
    };
    status_text("time_and_media_bg", "TimeText");
    status_text("balance_bg", "balance");
    // Building every instantiated menu label evaluates the real enabled/check/visible routes,
    // including nested menus, without committing destructive or server actions.
    auto labels = [&](auto&& self, LLView* view) -> void
    {
        if (auto* item = dynamic_cast<LLMenuItemGL*>(view)) item->buildDrawLabel();
        for (auto* child : *view->getChildList()) self(self, child);
    };
    labels(labels, gMenuBarView);
    unsigned openedMenus = 0;
    for (auto* child : *gMenuBarView->getChildList())
        if (auto* branch = dynamic_cast<LLMenuItemBranchGL*>(child); branch && branch->getEnabled())
        {
            branch->onCommit();
            require(branch->getBranch() && branch->getBranch()->getVisible(), "Real top-level menu did not open");
            labels(labels, branch->getBranch());
            gViewerWindow->drawNativeUI();
            branch->getBranch()->setVisible(false);
            ++openedMenus;
        }
    require(openedMenus > 0, "No real connected top-level menus were exercised");
    const bool rollover = gSavedSettings.getBOOL("FSStatusBarMenuButtonPopupOnRollover");
    gSavedSettings.setBOOL("FSStatusBarMenuButtonPopupOnRollover", false);
    auto* volume = gStatusBar->findChild<LLButton>("volume_btn");
    require(volume && volume->getEnabled(), "Real volume control is unavailable");
    volume->onCommit();
    auto* popup = root->findChild<LLPanel>("volumepulldown_floater");
    require(popup && popup->isInVisibleChain(), "Real volume click did not open its skinned popup");
    gViewerWindow->drawNativeUI();
    popup->setVisible(false);
    gSavedSettings.setBOOL("FSStatusBarMenuButtonPopupOnRollover", rollover);
    const LLRect chatBounds = chat->calcScreenRect();
    auto noChatOverlap = [&](auto&& self, LLView* view) -> void
    {
        if ((dynamic_cast<LLToolBarButton*>(view) || view->getName() == "default_chat_bar") && view->isInVisibleChain())
            require(!chatBounds.overlaps(view->calcScreenRect()), "Nearby chat overlaps an actual toolbar control");
        for (auto* child : *view->getChildList()) self(self, child);
    };
    noChatOverlap(noChatOverlap, gToolBarView);
    auto* window = gViewerWindow->getWindow();
    require(window && !gFocusMgr.getMouseCapture(), "Cursor qualification requires an uncaptured real window");
    auto hoverAt = [&](S32 x, S32 y)
    {
        LLCoordGL point;
        LLUI::getInstance()->screenPointToGL(x, y, &point.mX, &point.mY);
        gViewerWindow->handleMouseMove(window, point, MASK_NONE);
        window->updateCursor();
    };
    auto* editor = chat->findChild<LLLineEditor>("native_chat_input");
    require(editor != nullptr, "Actual nearby input editor is missing");
    const LLRect editorBounds = editor->calcScreenRect();
    hoverAt(editorBounds.getCenterX(), editorBounds.getCenterY());
    require(window->getCursor() == UI_CURSOR_IBEAM, "Actual editor hover did not select the text cursor");
    window->hideCursorUntilMouseMove();
    const LLRect rootBounds = root->calcScreenRect();
    hoverAt(rootBounds.getCenterX(), rootBounds.mBottom + rootBounds.getHeight() * 3 / 4);
    require(!window->isCursorHidden() && window->getCursor() == UI_CURSOR_ARROW,
            "Native mouse movement did not restore the default visible cursor");
    report["connected_native_cursor"] = true;
    const std::string draft = editor->getText();
    editor->setText(LLStringExplicit("native menu editing proof"));
    editor->handleRightMouseDown(editor->getRect().getWidth() / 2, editor->getRect().getHeight() / 2, MASK_NONE);
    LLMenuGL* editPopup = nullptr;
    for (auto* view : *gMenuHolder->getChildList())
        if (auto* menu = dynamic_cast<LLMenuGL*>(view); menu && menu->getVisible() && menu->getName() == "Text editor context menu")
            editPopup = menu;
    require(editPopup != nullptr, "Real text editor context menu did not open");
    auto commitEdit = [&](const char* name)
    {
        // Committing a menu item hides its popup. Reopen through the actual
        // editor for each action, preserving its current text/selection.
        editor->handleRightMouseDown(editor->getRect().getWidth() / 2, editor->getRect().getHeight() / 2, MASK_NONE);
        require(editPopup->getVisible(), "Actual editing context popup did not reopen");
        labels(labels, editPopup);
        auto* item = editPopup->findChild<LLMenuItemGL>(name);
        require(item && item->getEnabled(), "Actual editing context action is unavailable");
        item->onCommit();
    };
    commitEdit("Select All"); commitEdit("Copy");
    LLWString copied;
    require(window->pasteTextFromClipboard(copied) && wstring_to_utf8str(copied) == "native menu editing proof",
            "Context Copy did not use the active editor selection");
    commitEdit("Delete");
    require(editor->getText().empty(), "Context Delete did not edit the active selection");
    commitEdit("Paste");
    require(editor->getText() == "native menu editing proof", "Context Paste did not restore clipboard text");
    editPopup->setVisible(false);
    editor->setText(LLStringExplicit(draft));
    report["connected_text_context_menu"] = true;
    if (auto* favorites = navigation->findChild<LLFavoritesBarCtrl>("favorite"); favorites->isInVisibleChain())
        for (auto* child : *favorites->getChildList())
            if (auto* favorite = dynamic_cast<LLButton*>(child); favorite && favorite->isInVisibleChain())
            {
                favorite->handleRightMouseDown(favorite->getRect().getWidth() / 2,
                                              favorite->getRect().getHeight() / 2, MASK_NONE);
                for (auto* menuView : *gMenuHolder->getChildList())
                    if (auto* menu = dynamic_cast<LLMenuGL*>(menuView); menu && menu->getVisible() &&
                        menu->findChild<LLMenuItemGL>("Show On Map"))
                    {
                        labels(labels, menu);
                        auto* map = menu->findChild<LLMenuItemGL>("Show On Map");
                        auto* pick = menu->findChild<LLMenuItemGL>("create_pick");
                        auto* copy = menu->findChild<LLMenuItemGL>("Copy slurl");
                        require(map && pick && copy && copy->getEnabled(), "Actual favorite context controls are missing or disabled");
                        require(!map->getEnabled() && !pick->getEnabled(),
                                "Favorites context enabled an unowned world action");
                        gViewerWindow->drawNativeUI();
                        window->copyTextToClipboard(utf8str_to_wstring("before favorite copy"));
                        copy->onCommit();
                        LLWString clipboard;
                        require(window->pasteTextFromClipboard(clipboard) &&
                                wstring_to_utf8str(clipboard).find("Native%20replay%20region") != std::string::npos,
                                "Real favorite Copy SLURL did not publish the selected landmark location");
                        // Copy SLURL opens the baseline modal acknowledgement. Complete
                        // its real input cycle before exercising other connected controls.
                        auto* copiedToast = dynamic_cast<LLNotificationsUI::LLToast*>(gFocusMgr.getMouseCapture());
                        auto copiedNotification = copiedToast ? copiedToast->getNotification() : LLNotificationPtr();
                        require(copiedNotification && copiedNotification->getName() == "CopySLURL" &&
                                copiedToast->isInVisibleChain(), "Favorite Copy SLURL did not open its actual modal acknowledgement");
                        auto* copiedPanel = dynamic_cast<LLToastAlertPanel*>(copiedToast->getPanel());
                        auto* acknowledge = copiedPanel ? copiedPanel->findChild<LLButton>("close") : nullptr;
                        require(acknowledge && acknowledge->isInVisibleChain() && acknowledge->getEnabled(),
                                "Favorite Copy SLURL modal acknowledgement control is unavailable");
                        const LLRect acknowledgementBounds = acknowledge->calcScreenRect();
                        LLCoordGL acknowledgementPoint;
                        LLUI::getInstance()->screenPointToGL(acknowledgementBounds.getCenterX(),
                            acknowledgementBounds.getCenterY(), &acknowledgementPoint.mX, &acknowledgementPoint.mY);
                        require(gViewerWindow->handleMouseDown(window, acknowledgementPoint, MASK_NONE) &&
                                acknowledge->hasMouseCapture(), "Favorite acknowledgement did not receive native mouse down");
                        require(gViewerWindow->handleMouseUp(window, acknowledgementPoint, MASK_NONE) &&
                                copiedNotification->isRespondedTo() && !gFocusMgr.getMouseCapture(),
                                "Favorite acknowledgement did not complete its modal response and release capture");
                        report["connected_favorites_modal_acknowledgement"] = true;
                        auto* about = menu->findChild<LLMenuItemGL>("Landmark Open");
                        auto* itemCopy = menu->findChild<LLMenuItemGL>("Landmark Copy");
                        require(about && itemCopy && about->getEnabled() && itemCopy->getEnabled(),
                                "Favorite CPU metadata/copy actions were not wired");
                        about->onCommit();
                        LLFloater* properties = nullptr;
                        for (auto* candidate : LLFloaterReg::getFloaterList("properties"))
                            if (candidate->getVisible()) properties = candidate;
                        const LLUUID expectedFavorite("71000000-0000-0000-0000-000000000020");
                        require(properties && properties->getKey()["item_id"].asUUID() == expectedFavorite,
                                "Favorite View/Edit did not open the selected favorite inventory properties");
                        const LLUUID selectedFavorite = properties->getKey()["item_id"].asUUID();
                        properties->closeFloater();
                        itemCopy->onCommit();
                        require(LLClipboard::instance().isOnClipboard(selectedFavorite),
                                "Favorite Copy did not retain selected inventory identity");
                        menu->setVisible(false);
                        report["connected_favorites_context"] = true;
                    }
                break;
            }
    require(report["connected_favorites_context"].asBoolean(), "Populated real favorite context was not exercised");
    report["connected_skin_geometry"] = true;
    report["connected_native_chrome"] = true;
}

LLSD response()
{
    LLSD result;
    result["agent_id"] = "10000000-0000-0000-0000-000000000001";
    result["session_id"] = "20000000-0000-0000-0000-000000000002";
    result["sim_ip"] = "127.0.0.1";
    result["sim_port"] = LLSD::Integer(gMessageSystem->getListenPort());
    result["region_x"] = 256; result["region_y"] = 512;
    result["circuit_code"] = 123456;
    result["seed_capability"] = std::getenv("VS_VULKAN_REPLAY_SEED");
    return result;
}
void http(const std::string& name, const LLSD& body, const LLHost& host)
{
    LLSD message; message["sender"] = host.getIPandPort(); message["body"] = body;
    LLMessageSystem::dispatch(name, message);
}
void handshake(const LLHost& host)
{
    LLSD body;
    body["RegionInfo"][0]["RegionFlags"] = ll_sd_from_U32(0);
    body["RegionInfo"][0]["SimAccess"] = 13;
    body["RegionInfo"][0]["SimName"] = "Native replay region";
    body["RegionInfo"][0]["SimOwner"] = LLUUID("30000000-0000-0000-0000-000000000003");
    body["RegionInfo2"][0]["RegionID"] = LLUUID("40000000-0000-0000-0000-000000000004");
    http("RegionHandshake", body, host);
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessage("AgentMovementComplete"); msg->nextBlock("AgentData");
    msg->addUUID("AgentID", LLUUID(response()["agent_id"].asString())); msg->addUUID("SessionID", LLUUID(response()["session_id"].asString()));
    msg->nextBlock("Data"); msg->addVector3("Position", LLVector3(10, 20, 30)); msg->addVector3("LookAt", LLVector3::x_axis);
    msg->addU64("RegionHandle", (U64(256) << 32) | 512); msg->addU32("Timestamp", 1);
    msg->nextBlock("SimData"); msg->addString("ChannelVersion", "V4 protocol replay"); msg->sendReliable(host);
}
void udpChat(const LLHost& host)
{
    LLMessageSystem* msg = gMessageSystem; msg->newMessage("ChatFromSimulator"); msg->nextBlock("ChatData");
    msg->addString("FromName", "CPU sender");
    msg->addUUID("SourceID", LLUUID("50000000-0000-0000-0000-000000000005"));
    msg->addUUID("OwnerID", LLUUID("50000000-0000-0000-0000-000000000005"));
    msg->addU8("SourceType", CHAT_SOURCE_AGENT); msg->addU8("ChatType", CHAT_TYPE_NORMAL);
    msg->addU8("Audible", CHAT_AUDIBLE_FULLY); msg->addVector3("Position", LLVector3(10, 20, 30));
    msg->addString("Message", "Native nearby: \xCE\xA9 \xE6\x97\xA5\xE6\x9C\xAC"); msg->sendReliable(host);
}
struct Replay
{
    unsigned step = 0;
    unsigned loggedStep = ~0u;
    bool accountSetupStarted = false;
    bool challengeAcknowledged = false;
    bool reloginSubmitted = false;
    bool reloginHandshake = false;
    unsigned accountSetups = 0;
    U64 oldGeneration = 0;
    bool queuedExpired = false;
    std::shared_ptr<bool> readback = std::make_shared<bool>(false);
    bool captureRequested = false;
    bool imCaptureRequested = false;
    std::shared_ptr<bool> imReadback = std::make_shared<bool>(false);
    bool submitted = false;
    unsigned encodedChannels = 0;
    bool encodedToolbarChat = false, encodedNegativeChat = false;
    bool muteRequest = false, muteReplyQueued = false;
    bool encodedChat = false, encodedTypingStart = false, encodedTypingStop = false;
    unsigned navigationRequests = 0;
    bool navigationStarted = false;
    bool encodedRegionLookup = false, encodedLocationTeleport = false, encodedHomeTeleport = false;
    F64 navigationDeadline = 0;
    LLSD report, imEvidence;
    unsigned deniedSettings = 0, allowedSettings = 0;
    static void outgoing(LLMessageSystem* msg, void** data)
    {
        if (vs_native_im_replay_nearby_request(msg)) return;
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID agent, session; U8 type; S32 channel; std::string text;
        msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", session);
        msg->getString("ChatData", "Message", text); msg->getU8("ChatData", "Type", type); msg->getS32("ChatData", "Channel", channel);
        require(agent == LLUUID(response()["agent_id"].asString()) && session == LLUUID(response()["session_id"].asString()), "Encoded chat changed agent/session identity");
        if (channel != 0)
        {
            require(channel == 123 && type == CHAT_TYPE_NORMAL && (text == "channel message" || text == "repeat channel"), "Encoded channel/payload changed");
            ++replay.encodedChannels; return;
        }
        if (type == CHAT_TYPE_WHISPER || type == CHAT_TYPE_SHOUT)
        {
            require(text == "quiet message" || text == "loud message", "Encoded whisper/shout changed payload");
            ++replay.encodedChannels; return;
        }
        if (type == CHAT_TYPE_NORMAL)
        {
            if (text == "V4 toolbar Unicode \xCE\xA9") replay.encodedToolbarChat = true;
            else { require(text == "V4 outgoing Unicode \xCE\xA9", "Encoded chat changed UTF-8 payload"); replay.encodedChat = true; }
        }
        else if (type == CHAT_TYPE_START) { require(text.empty(), "Typing carried chat text"); replay.encodedTypingStart = true; }
        else if (type == CHAT_TYPE_STOP) { require(text.empty(), "Typing carried chat text"); replay.encodedTypingStop = true; }
        else require(false, "Unexpected encoded chat type");
    }
    static void outgoingDialog(LLMessageSystem* msg, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        LLUUID agent, session, object; S32 channel; std::string text;
        msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", session);
        msg->getUUID("Data", "ObjectID", object); msg->getS32("Data", "ChatChannel", channel);
        msg->getString("Data", "ButtonLabel", text);
        require(agent == gAgentID && session == gAgentSessionID && object == gAgentID &&
            channel == -42 && text == "toolbar script dialog", "Actual chat bar changed negative-channel packet");
        replay.encodedNegativeChat = true;
    }
    static void outgoingMute(LLMessageSystem* msg, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data); LLUUID agent, session; U32 crc;
        msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", session);
        msg->getU32("MuteData", "MuteCRC", crc);
        require(agent == LLUUID(response()["agent_id"].asString()) && session == LLUUID(response()["session_id"].asString()), "Mute request lost identity");
        replay.muteRequest = true;
    }
    static void outgoingNavigation(LLMessageSystem* msg, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        const std::string name = msg->getMessageName();
        if (name == "RegionHandleRequest")
        {
            LLUUID id; msg->getUUID("RequestBlock", "RegionID", id);
            require(id == LLUUID("40000000-0000-0000-0000-000000000099"), "Landmark handle request changed region UUID");
            return;
        }
        LLUUID agent, identity; const char* block = name == "TeleportLandmarkRequest" ? "Info" : "AgentData";
        msg->getUUID(block, "AgentID", agent); msg->getUUID(block, "SessionID", identity);
        require(agent == LLUUID(response()["agent_id"].asString()) && identity == LLUUID(response()["session_id"].asString()), "Navigation request lost connected identity");
        if (name == "MapBlockRequest")
        {
            U16 minX, minY, maxX, maxY; msg->getU16("PositionData", "MinX", minX); msg->getU16("PositionData", "MinY", minY); msg->getU16("PositionData", "MaxX", maxX); msg->getU16("PositionData", "MaxY", maxY);
            require(minX == maxX && minY == maxY, "CPU location lookup requested an unbounded map rectangle"); return;
        }
        if (name == "MapNameRequest")
        {
            std::string region; msg->getString("NameData", "Name", region);
            if (region == "Native") return;
            require(region == "Native destination", "Navigation changed destination name");
            replay.encodedRegionLookup = true;
        }
        if (name == "TeleportLocationRequest")
        {
            LLVector3 pos; U64 handle; msg->getVector3("Info", "Position", pos); msg->getU64("Info", "RegionHandle", handle);
            require(pos == LLVector3(12, 24, 36) && handle == to_region_handle(256, 512), "Navigation encoded wrong destination coordinates");
            replay.encodedLocationTeleport = true;
        }
        if (name == "TeleportLandmarkRequest")
        {
            LLUUID landmark; msg->getUUID("Info", "LandmarkID", landmark);
            require(landmark.isNull(), "Home navigation changed its null landmark ID");
            replay.encodedHomeTeleport = true;
        }
        ++replay.navigationRequests;
    }
    void inspectOutgoing(VSNativeSession& owner)
    {
        require(!owner.admit("ChatFromViewer", owner.host()), "Production policy admitted a client-only message");
        const std::weak_ptr<VSNativeSession> weak = owner.shared_from_this();
        // Only the loopback fixture receives the client's encoded send. Every
        // server semantic message still passes the unchanged production policy.
        gMessageSystem->setMessageAdmission([weak](const std::string& name, const LLHost& sender)
        {
            auto owner = weak.lock();
            const bool clientFixture = name == "RegionHandleRequest" || name == "AgentPause" || name == "AgentResume" || name == "ScriptDialogReply" || name == "MapBlockRequest" || name == "MapNameRequest" || name == "TeleportLocationRequest" || name == "TeleportLandmarkRequest" || name == "ChatFromViewer" || name == "MuteListRequest" ||
                name == "GroupMembersRequest" || name == "GroupRoleDataRequest" || name == "GroupRoleMembersRequest" ||
                name == "GroupTitlesRequest" || name == "GroupTitleUpdate" || name == "ActivateGroup" || name == "DirFindQuery" || name == "MoneyTransferRequest" || name == "UpdateInventoryItem" || name == "EconomyDataRequest";
            return owner && ((clientFixture && sender == owner->host()) || owner->admit(name, sender));
        });
        for (const char* name : {"RegionHandleRequest", "MapBlockRequest", "MapNameRequest", "TeleportLocationRequest", "TeleportLandmarkRequest"})
            gMessageSystem->setHandlerFunc(name, outgoingNavigation, reinterpret_cast<void**>(this));
        gMessageSystem->setHandlerFunc("EconomyDataRequest", vs_native_im_replay_economy_request);
        gMessageSystem->setHandlerFunc("ChatFromViewer", outgoing, reinterpret_cast<void**>(this));
        gMessageSystem->setHandlerFunc("ScriptDialogReply", outgoingDialog, reinterpret_cast<void**>(this));
        gMessageSystem->setHandlerFunc("MuteListRequest", outgoingMute, reinterpret_cast<void**>(this));
    }
    void tick(VSNativeSession& owner)
    {
        if (loggedStep != step)
        {
            loggedStep = step;
            LL_INFOS("NativeSessionReplay") << "Connected replay step " << step << LL_ENDL;
        }
        require(!LLWorld::instanceExists(), "Replay instantiated a scene world");
        if (step == 0)
        {
            if (!accountSetupStarted)
            {
                if (LLStartUp::getStartupState() != STATE_LOGIN_WAIT) return;
                auto* root = gViewerWindow->getRootView();
                auto* username = root->findChild<LLComboBox>("username_combo");
                auto* password = root->findChild<LLLineEditor>("password_edit");
                auto* connect = root->findChild<LLButton>("connect_btn");
                require(username && password && connect, "Replay login controls missing");
                gSavedSettings.setBOOL("FSLoginDontSavePassword", true);
                gSavedSettings.setBOOL("ShowNavbarFavoritesPanel", true);
                username->setTextEntry(LLStringExplicit("Native Replay"));
                username->onCommit();
                password->setText(LLStringExplicit("offline-only"));
                connect->onCommit();
                require(LLStartUp::getStartupState() == STATE_LOGIN_CLEANUP, "Replay login callback did not submit");
                accountSetupStarted = true;
                return;
            }
            if (LLStartUp::getStartupState() == STATE_AGENTS_WAIT && accountSetups < 2)
            {
                require(LLPersistentNotificationStorage::instanceExists() &&
                        LLDoNotDisturbNotificationStorage::instanceExists() &&
                        dynamic_cast<LLCommunicationChannel*>(LLNotifications::instance().getChannel("Communication").get()),
                        "Native login notification channel/storage was not initialized");
                LLDoNotDisturbNotificationStorage::getInstance()->saveNotifications();
                LLDoNotDisturbNotificationStorage::getInstance()->loadNotifications();
                LLPersistentNotificationStorage::getInstance()->saveNotifications();
                LLPersistentNotificationStorage::getInstance()->loadNotifications();
                if (++accountSetups < 2) LLStartUp::setStartupState(STATE_LOGIN_CLEANUP);
                return;
            }
            // Let the ordinary startup state machine construct the request,
            // authenticate through XML-RPC, process its response and begin the
            // session. Only the endpoint is replaced by the loopback fixture.
            if (!challengeAcknowledged)
            {
                for (auto* challenge : LLFloaterReg::getFloaterList("message_critical"))
                {
                    if (!challenge || !challenge->getVisible()) continue;
                    auto* button = challenge->getChild<LLButton>("Continue");
                    require(button->getEnabled(), "Authentication policy acknowledgement was disabled");
                    button->onCommit(); challengeAcknowledged = true; return;
                }
            }
            if (owner.phase() != VSNativeSession::Phase::Connecting) return;
            require(LLLoginInstance::getInstance()->authSuccess(), "Session bypassed authentication");
            gSavedPerAccountSettings.setBOOL("LogNearbyChat", false);
            gSavedPerAccountSettings.setBOOL("LogShowHistory", false);
            oldGeneration = owner.generation();
            inspectOutgoing(owner);
            handshake(owner.host()); ++step;
        }
        else if (step == 1)
        {
            if (owner.phase() != VSNativeSession::Phase::Connected) return;
            if (!muteRequest) return;
            if (!muteReplyQueued)
            {
                LLSD mute; mute["MuteData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString()); mute["MuteData"][0]["Filename"] = "native-v4-replay-empty.txt";
                http("MuteListUpdate", mute, owner.host());
                require(!gXferManager->mReceiveList.empty(), "Mute download was not queued");
                LLMessageSystem* packet = gMessageSystem;
                packet->newMessage("SendXferPacket"); packet->nextBlock("XferID");
                packet->addU64("ID", gXferManager->mReceiveList.front()->mID); packet->addU32("Packet", 0x80000000u);
                packet->nextBlock("DataPacket"); const U8 emptyFile[4] = {0, 0, 0, 0};
                packet->addBinaryData("Data", emptyFile, sizeof(emptyFile)); packet->sendReliable(owner.host());
                muteReplyQueued = true; return;
            }
            if (!LLMuteList::getInstance()->isLoadedFromServer()) return;
            require(owner.evidence()["circuit_ack"].asBoolean(), "Circuit was not acknowledged");
            require(!owner.deliver("AgentMovementComplete", LLSD::emptyArray(), owner.host(), owner.generation()), "Malformed semantic message admitted");
            require(!owner.admit("ObjectUpdate", owner.host()), "Scene message admitted");
            require(!owner.admit("NewUnknownMessage", owner.host()), "Unknown message admitted");
            require(!owner.admit("ChatFromSimulator", LLHost("127.0.0.2", owner.host().getPort())), "Wrong simulator admitted");
            http("ObjectUpdate", LLSD::emptyMap(), owner.host()); // HTTP gate before node/scene execution.
            LLMessageSystem* msg = gMessageSystem;
            msg->newMessage("EnableSimulator"); msg->nextBlock("SimulatorInfo");
            msg->addU64("Handle", 1); msg->addIPAddr("IP", owner.host().getAddress()); msg->addIPPort("Port", owner.host().getPort());
            msg->sendReliable(owner.host()); // UDP gate before allocating a child region.
            udpChat(owner.host()); ++step;
        }
        else if (step == 2)
        {
            auto* chat = gViewerWindow->nativeChat();
            auto* nearby = FSFloaterNearbyChat::findInstance();
            require(chat && (chat->usesSharedFrontend()
                    ? nearby && nearby->isInVisibleChain() && !chat->getVisible()
                    : chat->isInVisibleChain()),
                "Active connected nearby frontend is missing or hidden");
            if (chat->transcript()->getText().find("Native nearby:") == std::string::npos || chat->transcript()->getText().find("Native event queue chat") == std::string::npos) return;
            if (!captureRequested)
            {
                const LLUUID favoriteID("71000000-0000-0000-0000-000000000020");
                LLVector3d favoritePosition;
                if (!gInventory.getItem(favoriteID) || !LLLandmarkActions::getLandmarkGlobalPos(favoriteID, favoritePosition)) return;
                require(favoritePosition == LLVector3d(384, 640, 25), "Real favorite landmark asset has wrong global position");
                std::string favoriteURL;
                LLLandmarkActions::getSLURLfromPosGlobal(favoritePosition, [&favoriteURL](std::string& url) { favoriteURL = url; });
                require(!favoriteURL.empty() && !LLWorldMap::instanceExists(), "Current favorite SLURL helper created world map or lost location");
                qualifyConnectedSkinGeometry(imEvidence);
                const std::string path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
                std::filesystem::create_directories(path);
                vs_native_replay_capture(*gViewerWindow->nativeContext(), path, readback);
                captureRequested = true; return;
            }
            if (!*readback) return;
            if (!vs_native_im_replay_tick(owner, imEvidence)) return;
            if (!imCaptureRequested)
            {
                const auto path = std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY")) / "im";
                std::filesystem::create_directories(path);
                vs_native_replay_capture(*gViewerWindow->nativeContext(), path.string(), imReadback);
                imCaptureRequested = true; return;
            }
            if (!*imReadback) return;
            imEvidence["im_readback"] = true;
            if (!submitted)
            {
                chat->input()->setFocus(true);
                chat->input()->setText(LLStringExplicit("V4 outgoing Unicode \xCE\xA9"));
                require(chat->submit() && chat->input()->getText().empty(), "Production chat submit failed");
                require(chat->input()->handleKeyHere(KEY_UP, MASK_CONTROL) && chat->input()->getText() == "V4 outgoing Unicode \xCE\xA9", "Submitted input history was not recalled");
                require(chat->input()->handleKeyHere(KEY_DOWN, MASK_CONTROL) && chat->input()->getText().empty(), "Input history did not restore the empty draft");
                auto* bottom = gViewerWindow->getRootView()->findChild<FSNearbyChatVoiceControl>("default_chat_bar");
                require(bottom && bottom->isInVisibleChain() && bottom->getEnabled(), "Actual skinned toolbar chat bar is unavailable");
                bottom->setFocus(true); bottom->setText(LLStringExplicit("V4 toolbar Unicode \xCE\xA9"));
                require(bottom->handleKeyHere(KEY_RETURN, MASK_NONE) && bottom->getText().empty(), "Actual toolbar Enter did not submit chat");
                bottom->setText(LLStringExplicit("/-42 toolbar script dialog"));
                require(bottom->handleKeyHere(KEY_RETURN, MASK_NONE) && bottom->getText().empty(), "Actual toolbar negative-channel submit failed");
                owner.typing(true); owner.typing(false);
                require(owner.sendChat("/123 channel message") && owner.sendChat("//repeat channel") && owner.sendChat("/whisper quiet message") && owner.sendChat("/shout loud message"), "Chat channel/type parsing failed");
                require(!owner.sendChat("/999999999999 overflow") && !owner.sendChat("/123"), "Invalid channel input admitted");
                submitted = true; return;
            }
            if (!encodedChat || !encodedToolbarChat || !encodedNegativeChat || !encodedTypingStart || !encodedTypingStop || encodedChannels != 4) return;
            imEvidence["connected_toolbar_chat"] = true;
            const std::weak_ptr<VSNativeSession> weak = owner.shared_from_this();
            LL::WorkQueue::getInstance("mainloop")->post([this, weak, generation = oldGeneration]()
            {
                if (auto current = weak.lock()) queuedExpired = !current->deliver("ChatFromSimulator", LLSD::emptyMap(), current->host(), generation);
            });
            owner.requestLogout(false);
            LLSD reply; reply["AgentData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
            reply["AgentData"][0]["SessionID"] = LLUUID(response()["session_id"].asString());
            http("LogoutReply", reply, owner.host());
            require(owner.phase() == VSNativeSession::Phase::Login && !chat->getVisible() &&
                    (!FSFloaterNearbyChat::findInstance() || !FSFloaterNearbyChat::findInstance()->isInVisibleChain()),
                "Logout did not detach both shared and diagnostic nearby frontends");
            require(vs_native_im_replay_expired(), "Logout retained a live IM callback generation");
            imEvidence["im_callback_expired"] = true;
            chat->input()->handleKeyHere(KEY_UP, MASK_CONTROL);
            require(chat->input()->getText().empty(), "Logout retained input history from the previous identity");
            ++step;
        }
        else if (step == 3)
        {
            if (!queuedExpired) return;
            if (!reloginSubmitted)
            {
                if (LLStartUp::getStartupState() != STATE_LOGIN_WAIT) return;
                auto* root = gViewerWindow->getRootView();
                auto* username = root->findChild<LLComboBox>("username_combo");
                auto* password = root->findChild<LLLineEditor>("password_edit");
                auto* connect = root->findChild<LLButton>("connect_btn");
                require(username && password && connect, "Relogin controls missing");
                username->setTextEntry(LLStringExplicit("Native Replay")); username->onCommit();
                password->setText(LLStringExplicit("offline-only")); connect->onCommit();
                reloginSubmitted = true; return;
            }
            for (auto* challenge : LLFloaterReg::getFloaterList("message_critical"))
                if (challenge && challenge->getVisible())
                {
                    // Continue destroys the floater and its registry-list node.
                    challenge->getChild<LLButton>("Continue")->onCommit();
                    return;
                }
            if (owner.phase() != VSNativeSession::Phase::Connecting) return;
            require(LLLoginInstance::getInstance()->authSuccess(), "Relogin bypassed authentication");
            handshake(owner.host()); reloginHandshake = true; ++step;
        }
        else if (step == 4)
        {
            if (owner.phase() != VSNativeSession::Phase::Connected)
            {
                require(!navigationStarted, "Navigation lost its connected account while awaiting encoded replies");
                return;
            }
            if (!navigationStarted)
            {
                // Relogin installs production handlers again. Reattach only the
                // authenticated loopback observers before this account sends.
                inspectOutgoing(owner);
                navigationRequests = 0;
                encodedRegionLookup = encodedLocationTeleport = encodedHomeTeleport = false;
                require(owner.generation() != oldGeneration, "Relogin reused callback generation");
                require(!owner.deliver("ChatFromSimulator", LLSD::emptyMap(), owner.host(), oldGeneration), "Late old-session callback admitted");
                setting_setup_signal_listener(gSavedSettings, "RenderGlow", std::function<void(const LLSD&)>([this](const LLSD&) { ++deniedSettings; gSavedSettings.setBOOL("RenderDepthOfField", true); }));
                setting_setup_signal_listener(gSavedSettings, "RenderDepthOfField", std::function<void()>([this]() { ++deniedSettings; }));
                setting_setup_signal_listener(gSavedSettings, "FSEnableLogThrottle", std::function<void()>([this]() { ++allowedSettings; }));
                gSavedSettings.setBOOL("FSEnableLogThrottle", !gSavedSettings.getBOOL("FSEnableLogThrottle"));
                const auto rejected = owner.evidence()["rejected"].asInteger();
                gSavedSettings.setBOOL("RenderGlow", !gSavedSettings.getBOOL("RenderGlow"));
                gSavedSettings.setBOOL("RenderDepthOfField", !gSavedSettings.getBOOL("RenderDepthOfField"));
                require(!LLWorld::instanceExists() && deniedSettings == 0 && allowedSettings == 1, "Settings admission/cascade policy failed");
                bool locationResolved = false;
                LLLandmarkActions::getRegionNameAndCoordsFromPosGlobal(LLVector3d(1400, 700, 25),
                    [&locationResolved](const std::string& name, S32 x, S32 y, S32 z)
                    { require(name == "Native variable destination" && x == 376 && y == 188 && z == 25, "CPU variable-region coordinate lookup changed location"); locationResolved = true; });
                LLSD locationReply; locationReply["AgentData"][0]["AgentID"] = LLUUID::generateNewID();
                locationReply["AgentData"][0]["Flags"] = 0x10000;
                locationReply["Data"][0]["Name"] = "Native variable destination";
                locationReply["Data"][0]["X"] = 4; locationReply["Data"][0]["Y"] = 2; locationReply["Data"][0]["Access"] = 13;
                locationReply["Size"][0]["SizeX"] = 512; locationReply["Size"][0]["SizeY"] = 512;
                http("MapBlockReply", locationReply, owner.host()); require(!locationResolved, "Forged location lookup identity admitted");
                locationReply["AgentData"][0]["AgentID"] = gAgentID; http("MapBlockReply", locationReply, owner.host());
                require(locationResolved && !LLWorldMap::instanceExists(), "Native location lookup did not resolve without world map");
                // A real v2 landmark resolves its local position through the same
                // authenticated RegionIDAndHandleReply adapter as ordinary favorites.
                const std::string v2 = "Landmark version 2\nregion_id 40000000-0000-0000-0000-000000000099\nlocal_pos 17 29 31\n";
                std::unique_ptr<LLLandmark> remoteLandmark(LLLandmark::constructFromString(v2.c_str(), S32(v2.size())));
                require(remoteLandmark != nullptr, "Version-2 landmark asset was not parsed");
                LLVector3d remotePosition;
                require(!remoteLandmark->getGlobalPos(remotePosition), "Foreign v2 landmark unexpectedly had a cached handle");
                bool handleResolved = false;
                const LLUUID remoteID("40000000-0000-0000-0000-000000000099");
                LLLandmark::requestRegionHandle(gMessageSystem, owner.host(), remoteID,
                    [&handleResolved](const LLUUID&, const U64&) { handleResolved = true; });
                LLSD handleReply; handleReply["ReplyBlock"][0]["RegionID"] = remoteID;
                handleReply["ReplyBlock"][0]["RegionHandle"] = ll_sd_from_U64(to_region_handle(1024, 512));
                http("RegionIDAndHandleReply", handleReply, LLHost("127.0.0.2", owner.host().getPort()));
                require(!handleResolved, "Foreign circuit supplied a landmark region handle");
                http("RegionIDAndHandleReply", handleReply, owner.host());
                require(handleResolved && remoteLandmark->getGlobalPos(remotePosition) &&
                    remotePosition == LLVector3d(1041, 541, 31), "Decoded CPU handle reply did not resolve a v2 landmark");
                LLSD unsolicited; unsolicited["id"] = remoteID; unsolicited["handle"] = ll_sd_from_U64(to_region_handle(2048, 512));
                require(!owner.deliver("RegionIDAndHandleReply", unsolicited, owner.host(), owner.generation()),
                    "Native landmark adapter accepted an unsolicited/retired handle reply");
                auto* preferences = LLFloaterReg::getInstance("preferences");
                require(preferences != nullptr, "Connected Preferences UI construction failed");
                preferences->closeFloater();
                const bool legacyPeople = gSavedSettings.getString("FSInternalSkinCurrent") == "Vintage" || !gSavedSettings.getBOOL("FSUseV2Friends");
                if (legacyPeople)
                {
                    auto* contacts = FSFloaterContacts::getInstance(); require(contacts != nullptr, "Legacy Contacts construction failed");
                    for (const char* tab : {"friends", "groups", "contact_sets"}) contacts->openTab(tab);
                    require(LLFloaterReg::getInstance("fs_blocklist") != nullptr, "Legacy blocked list construction failed");
                    LLFloaterReg::hideInstance("imcontacts"); LLFloaterReg::hideInstance("fs_blocklist");
                }
                else
                {
                LLFloaterSidePanelContainer::showPanel("people", "panel_people", LLSD().with("people_panel_tab_name", "friends_panel"));
                auto* people = LLFloaterSidePanelContainer::getPanel<LLPanelPeople>("people", "panel_people");
                require(people != nullptr && !people->findChild<LLPanel>("nearby_panel"), "People account panel retained scene widgets or failed construction");
                auto* peopleTabs = people->findChild<LLTabContainer>("tabs"); require(peopleTabs != nullptr, "People tab container missing");
                for (const char* tab : {"friends_panel", "groups_panel", "recent_panel", "contact_sets_panel", "blocked_panel"})
                {
                    people->onOpen(LLSD().with("people_panel_tab_name", tab));
                    require(peopleTabs->getCurrentPanel() && peopleTabs->getCurrentPanel()->getName() == tab, "People account tab failed to open");
                }
                people->onOpen(LLSD().with("people_panel_tab_name", "nearby_panel"));
                require(peopleTabs->getCurrentPanel()->getName() == "friends_panel", "Scene-only nearby request did not keep People on the account tab");
                for (const char* filter : {"friends_filter_input", "groups_filter_input", "recent_filter_input", "contact_sets_filter_input"})
                    if (auto* editor = people->findChild<LLFilterEditor>(filter))
                    { editor->setText(LLStringExplicit("Replay")); editor->onCommit(); editor->setText(LLStringUtil::null); editor->onCommit(); }
                if (auto* sets = people->findChild<LLComboBox>("combo_sets")) sets->onCommit();
                require(!LLWorld::instanceExists(), "People account UI created a scene world");
                LLFloaterReg::hideInstance("people");
                }

                report = owner.evidence(); report["schema"] = 1; report["mode"] = "viewer-native-session-replay";
                // Snapshot transport evidence before appending verified fixture witnesses.
                report["native_location_lookup"] = locationResolved;
                report["native_landmark_v2"] = handleResolved;
                for (auto item = imEvidence.beginMap(); item != imEvidence.endMap(); ++item) report[item->first] = item->second;
                report["login_authentication"] = true;
                report["native_people_account_ui"] = true;
                report["native_people_frontend"] = legacyPeople ? "legacy_contacts" : "people";
                report["login_challenge"] = challengeAcknowledged;
                report["login_account_setup"] = accountSetups == 2;
                report["notification_storage"] = true;
                report["malformed"] = true; report["unknown"] = true; report["wrong_host"] = true;
                report["udp_chat"] = true; report["http_gate"] = true; report["udp_gate"] = rejected > 0;
                report["queued_expired"] = queuedExpired; report["relogin"] = reloginSubmitted && reloginHandshake;
                report["readback"] = *readback; report["chat_submit"] = true; report["typing"] = true; report["settings_gate"] = true;
                report["encoded_chat"] = encodedChat; report["encoded_typing"] = encodedTypingStart && encodedTypingStop;
                require(LLURLDispatcher::dispatchFromTextEditor("secondlife:///app/teleport/Native/10/20/30", false), "Deferred app URL was not consumed");
                require(LLURLDispatcher::dispatchFromTextEditor("secondlife://Native/10/20/30", false), "Deferred region URL was not consumed");
                require(!LLWorld::instanceExists(), "Deferred URL instantiated scene resources");
                report["encoded_channels"] = encodedChannels == 4;
                LLSD frozen; frozen["FrozenData"][0]["Data"] = true;
                http("ViewerFrozenMessage", frozen, owner.host()); require(owner.evidence()["frozen"].asBoolean(), "Frozen status was not decoded");
                frozen["FrozenData"][0]["Data"] = false;
                http("ViewerFrozenMessage", frozen, owner.host()); require(!owner.evidence()["frozen"].asBoolean(), "Unfrozen status was not decoded");
                LLSD feature; feature["FailureInfo"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
                feature["FailureInfo"][0]["TransactionID"] = LLUUID::generateNewID(); feature["FailureInfo"][0]["ErrorMessage"] = "V4 replay feature status";
                const auto received = owner.evidence()["received"].asInteger(); http("FeatureDisabled", feature, owner.host());
                require(owner.evidence()["received"].asInteger() == received + 1, "Feature status was not decoded");
                report["status_messages"] = true;
                report["input_history"] = true;
                report["queued_http_chat"] = true;
                report["ui_gate"] = true;
                LLSD forged; forged["agent"] = LLUUID::generateNewID(); forged["session"] = LLUUID(response()["session_id"].asString());
                require(!owner.deliver("KickUser", forged, owner.host(), owner.generation()), "Forged kick identity admitted");
                LLSD transfer; transfer["MuteData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
                transfer["MuteData"][0]["Filename"] = "native-v4-replay-muted.txt";
                http("MuteListUpdate", transfer, owner.host());
                require(!gXferManager->mReceiveList.empty(), "Mute transfer was not queued");
                // Drive real CPU navigation sends and decoded simulator replies while
                // the ordinary connected account/IM owners remain alive.
                require(owner.teleportToRegion("Native destination", LLVector3(12, 24, 36)), "Address navigation did not send a region lookup");
                LLSD map; map["AgentData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
                map["Data"][0]["Name"] = "Native destination"; map["Data"][0]["X"] = 1; map["Data"][0]["Y"] = 2; map["Data"][0]["Access"] = 13;
                http("MapBlockReply", map, owner.host());
                require(owner.teleportHome(), "Home navigation did not send a landmark request");
                navigationStarted = true;
                navigationDeadline = LLFrameTimer::getTotalSeconds() + 10;
                return;
            }
            // Drain through ordinary application frames so UDP readiness and
            // queued callbacks can advance. Never resend the one-time setup.
            if (navigationRequests < 3 || !encodedRegionLookup || !encodedLocationTeleport || !encodedHomeTeleport)
            {
                require(LLFrameTimer::getTotalSeconds() < navigationDeadline,
                    "Encoded navigation requests were not decoded within the bounded replay deadline");
                return;
            }
            require(navigationRequests >= 3, "Encoded navigation requests were not decoded by loopback fixture");
            report["native_navigation_request"] = true;
            LLSD local; local["Info"][0]["AgentID"] = LLUUID(response()["agent_id"].asString()); local["Info"][0]["Position"] = ll_sd_from_vector3(LLVector3(12, 24, 36));
            http("TeleportLocal", local, owner.host());
            require(owner.position() == LLVector3(12, 24, 36) && owner.phase() == VSNativeSession::Phase::Connected, "Local teleport did not retain the connected UI");
            report["native_navigation_local"] = true;
            owner.requestLogout(false); owner.expireDeadlineForReplay(); owner.tick();
            require(gXferManager->mReceiveList.empty(), "Logout retained mute transfer callbacks");
            report["mute_request"] = muteRequest; report["mute_transfer_cleanup"] = true;
            require(owner.phase() == VSNativeSession::Phase::Login, "Logout deadline did not release session");
            report["logout_timeout"] = true;
            require(owner.acceptLogin(response()), "Crossing identity rejected"); owner.begin();
            handshake(owner.host());
            std::string navigationEvents(std::getenv("VS_VULKAN_REPLAY_SEED")); navigationEvents.replace(navigationEvents.size() - 4, 4, "events");
            LLSD navigationCaps; navigationCaps["EventQueueGet"] = navigationEvents;
            owner.capabilities(navigationCaps, owner.generation()); owner.circuitResult(owner.generation(), 0);
            LLSD initialMovement; initialMovement["agent"] = LLUUID(response()["agent_id"].asString()); initialMovement["session"] = LLUUID(response()["session_id"].asString());
            initialMovement["handle"] = ll_sd_from_U64((U64(256) << 32) | 512); initialMovement["position"] = ll_sd_from_vector3(LLVector3(10, 20, 30));
            require(owner.deliver("AgentMovementComplete", initialMovement, owner.host(), owner.generation()), "Crossing fixture readiness failed");
            require(owner.phase() == VSNativeSession::Phase::Connected, "Crossing fixture did not connect");
            const U64 accountEpoch = owner.generation(), regionEpoch = static_cast<U64>(owner.evidence()["region_epoch"].asInteger());
            const LLHost previousHost = owner.host();
            LLSD finish; finish["Info"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
            finish["Info"][0]["SimIP"] = ll_sd_from_ipaddr(previousHost.getAddress()); finish["Info"][0]["SimPort"] = LLSD::Integer(previousHost.getPort());
            finish["Info"][0]["RegionHandle"] = ll_sd_from_U64((U64(768) << 32) | 1024); finish["Info"][0]["SeedCapability"] = std::getenv("VS_VULKAN_REPLAY_SEED");
            const auto balance = owner.balance();
            http("TeleportFinish", finish, previousHost);
            require(owner.evidence()["changing_region"].asBoolean() && owner.phase() == VSNativeSession::Phase::Connected && owner.generation() == accountEpoch && owner.balance() == balance, "TeleportFinish retired connected account ownership");
            owner.capabilities(navigationCaps, accountEpoch, regionEpoch);
            owner.circuitResult(accountEpoch, 0, regionEpoch);
            require(!owner.evidence()["capabilities"].asBoolean() && !owner.evidence()["circuit_ack"].asBoolean(), "Retired region completions were accepted");
            owner.capabilities(navigationCaps, accountEpoch); owner.circuitResult(accountEpoch, 0);
            LLSD regionHandshake; regionHandshake["name"] = "Native destination"; regionHandshake["id"] = LLUUID::generateNewID(); regionHandshake["access"] = 13;
            require(owner.deliver("RegionHandshake", regionHandshake, owner.host(), accountEpoch), "Destination handshake rejected");
            initialMovement["handle"] = ll_sd_from_U64((U64(768) << 32) | 1024);
            require(owner.deliver("AgentMovementComplete", initialMovement, owner.host(), accountEpoch), "Destination movement rejected");
            require(!owner.evidence()["changing_region"].asBoolean() && owner.regionName() == "Native destination" && owner.generation() == accountEpoch, "Destination readiness did not retain the account");
            report["native_navigation_transition"] = true; report["native_navigation_epoch"] = true;
            LLSD crossing; crossing["AgentData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString()); crossing["AgentData"][0]["SessionID"] = LLUUID(response()["session_id"].asString());
            crossing["RegionData"][0]["SimIP"] = ll_sd_from_ipaddr(previousHost.getAddress()); crossing["RegionData"][0]["SimPort"] = LLSD::Integer(previousHost.getPort() + 1);
            crossing["RegionData"][0]["RegionHandle"] = ll_sd_from_U64((U64(1280) << 32) | 1536); crossing["RegionData"][0]["SeedCapability"] = std::getenv("VS_VULKAN_REPLAY_SEED");
            crossing["AgentData"][0]["SessionID"] = LLUUID::generateNewID(); http("CrossedRegion", crossing, owner.host());
            require(owner.host() == previousHost && !owner.evidence()["changing_region"].asBoolean(), "Forged crossing changed the authenticated host");
            crossing["AgentData"][0]["SessionID"] = LLUUID(response()["session_id"].asString()); http("CrossedRegion", crossing, owner.host());
            require(owner.host() != previousHost && owner.generation() == accountEpoch && !owner.admit("ChatFromSimulator", previousHost), "Authenticated crossing did not reject the retired host");
            report["native_navigation_identity"] = true;
            report["live_server_qualified"] = false;
            // Use the real circuit state seen by the connected tick, without
            // waiting for the message library's platform-dependent expiry timer.
            require(owner.acceptLogin(response()), "Dead-circuit identity rejected"); owner.begin();
            handshake(owner.host());
            std::string eventURL(std::getenv("VS_VULKAN_REPLAY_SEED")); eventURL.replace(eventURL.size() - 4, 4, "events");
            LLSD readyCaps; readyCaps["EventQueueGet"] = eventURL;
            owner.capabilities(readyCaps, owner.generation());
            owner.circuitResult(owner.generation(), 0);
            LLSD movement; movement["agent"] = LLUUID(response()["agent_id"].asString()); movement["session"] = LLUUID(response()["session_id"].asString());
            movement["handle"] = ll_sd_from_U64((U64(256) << 32) | 512); movement["position"] = ll_sd_from_vector3(LLVector3(10, 20, 30));
            require(owner.deliver("AgentMovementComplete", movement, owner.host(), owner.generation()) && owner.phase() == VSNativeSession::Phase::Connected, "Dead-circuit fixture did not establish CPU readiness");
            owner.tick(); // Drain the readiness packets before injecting circuit expiry.
            gMessageSystem->mCircuitInfo.removeCircuitData(owner.host()); owner.tick();
            require(owner.phase() == VSNativeSession::Phase::Disconnected, "Dead simulator circuit remained connected");
            report["connected_timeout"] = true;
            require(owner.acceptLogin(response()), "Reliable-failure identity rejected"); owner.begin();
            // Circuit retirement invokes the actual pending reliable callback.
            // It must return before its queued failure can reset the session.
            gMessageSystem->mCircuitInfo.removeCircuitData(owner.host());
            require(owner.phase() == VSNativeSession::Phase::Connecting, "Reliable callback reset transport during dispatch");
            LL::WorkQueue::getInstance("mainloop")->runPending();
            require(owner.phase() == VSNativeSession::Phase::Disconnected, "Reliable failure did not disconnect after dispatch");
            report["reliable_failure"] = true;
            // Partial-init cancellation invalidates the suspended seed request and
            // reliable callback before the same owner accepts another identity.
            owner.reset(); require(owner.acceptLogin(response()), "Partial-init identity rejected");
            owner.begin(); const LLHost retired = owner.host(); owner.reset();
            LL::WorkQueue::getInstance("mainloop")->runPending();
            require(!gMessageSystem->mCircuitInfo.findCircuit(retired), "Partial reset retained its transport circuit");
            report["partial_init_cleanup"] = owner.phase() == VSNativeSession::Phase::Login;
            require(owner.acceptLogin(response()), "Timeout identity rejected");
            owner.begin(); owner.expireDeadlineForReplay(); owner.tick();
            report["connection_timeout"] = owner.phase() == VSNativeSession::Phase::Disconnected;
            require(report["connection_timeout"].asBoolean(), "Connection deadline did not disconnect");
            report["passed"] = true;
            std::filesystem::path path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
            std::filesystem::create_directories(path);
            std::ofstream output(path / "session-replay.xml"); LLSDSerialize::toPrettyXML(report, output);
            require(output.good(), "Cannot write session replay evidence");
            owner.requestLogout(true); ++step;
        }
    }
};
}
bool vs_native_replay_tick()
{
    if (!std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY")) return false;
    static Replay replay;
    if (auto owner = VSNativeSession::active()) replay.tick(*owner);
    return true;
}

std::string vs_native_replay_login_uri(const std::string& selected)
{
    if (!std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY") || !VSNativeSession::active()) return selected;
    const char* endpoint = std::getenv("VS_VULKAN_REPLAY_LOGIN");
    if (!endpoint || !gMessageSystem) throw std::runtime_error("Missing loopback authentication fixture");
    LLURI uri(endpoint);
    if (uri.scheme() != "http" || uri.hostName() != "127.0.0.1" || uri.path() != "/login")
        throw std::runtime_error("Authentication replay requires an explicit loopback endpoint");
    return std::string(endpoint) + "?sim_port=" + std::to_string(gMessageSystem->getListenPort());
}
