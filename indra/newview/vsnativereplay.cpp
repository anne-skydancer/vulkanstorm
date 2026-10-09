// Replay production native session dispatch; never authenticates to a grid. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsnativereplay.h"
#include "vsnativesession.h"
#include "vsplainchat.h"
#include "llappviewer.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llstartup.h"
#include "lltexteditor.h"
#include "llworld.h"
#include "vsvulkancontext.h"
#include "llsdserialize.h"
#include "llsdutil_math.h"
#include "llchat.h"
#include "llnotifications.h"
#include "llfloaterreg.h"
#include "llurldispatcher.h"
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
    U64 oldGeneration = 0;
    bool queuedExpired = false;
    std::shared_ptr<bool> readback = std::make_shared<bool>(false);
    bool captureRequested = false;
    bool submitted = false;
    unsigned encodedChannels = 0;
    bool muteRequest = false, muteReplyQueued = false;
    bool encodedChat = false, encodedTypingStart = false, encodedTypingStop = false;
    LLSD report;
    unsigned deniedSettings = 0, allowedSettings = 0;
    static void outgoing(LLMessageSystem* msg, void** data)
    {
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
        { require(text == "V4 outgoing Unicode \xCE\xA9", "Encoded chat changed UTF-8 payload"); replay.encodedChat = true; }
        else if (type == CHAT_TYPE_START) { require(text.empty(), "Typing carried chat text"); replay.encodedTypingStart = true; }
        else if (type == CHAT_TYPE_STOP) { require(text.empty(), "Typing carried chat text"); replay.encodedTypingStop = true; }
        else require(false, "Unexpected encoded chat type");
    }
    static void outgoingMute(LLMessageSystem* msg, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data); LLUUID agent, session; U32 crc;
        msg->getUUID("AgentData", "AgentID", agent); msg->getUUID("AgentData", "SessionID", session);
        msg->getU32("MuteData", "MuteCRC", crc);
        require(agent == LLUUID(response()["agent_id"].asString()) && session == LLUUID(response()["session_id"].asString()), "Mute request lost identity");
        replay.muteRequest = true;
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
            return owner && (((name == "ChatFromViewer" || name == "MuteListRequest") && sender == owner->host()) || owner->admit(name, sender));
        });
        gMessageSystem->setHandlerFunc("ChatFromViewer", outgoing, reinterpret_cast<void**>(this));
        gMessageSystem->setHandlerFunc("MuteListRequest", outgoingMute, reinterpret_cast<void**>(this));
    }
    void tick(VSNativeSession& owner)
    {
        require(!LLWorld::instanceExists(), "Replay instantiated a scene world");
        if (step == 0)
        {
            if (LLStartUp::getStartupState() != STATE_LOGIN_WAIT) return;
            gSavedPerAccountSettings.setBOOL("LogNearbyChat", false);
            gSavedPerAccountSettings.setBOOL("LogShowHistory", false);
            require(!owner.acceptLogin(LLSD::emptyMap()), "Malformed login was admitted");
            require(owner.acceptLogin(response()), "Synthetic CPU login identity rejected");
            owner.begin(); oldGeneration = owner.generation();
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
            require(chat && chat->getVisible(), "Connected production chat panel is hidden");
            if (chat->transcript()->getText().find("Native nearby:") == std::string::npos || chat->transcript()->getText().find("Native event queue chat") == std::string::npos) return;
            if (!captureRequested)
            {
                const std::string path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
                std::filesystem::create_directories(path);
                vs_native_replay_capture(*gViewerWindow->nativeContext(), path, readback);
                captureRequested = true; return;
            }
            if (!*readback) return;
            if (!submitted)
            {
                chat->input()->setText(LLStringExplicit("V4 outgoing Unicode \xCE\xA9"));
                require(chat->submit() && chat->input()->getText().empty(), "Production chat submit failed");
                owner.typing(true); owner.typing(false);
                require(owner.sendChat("/123 channel message") && owner.sendChat("//repeat channel") && owner.sendChat("/whisper quiet message") && owner.sendChat("/shout loud message"), "Chat channel/type parsing failed");
                require(!owner.sendChat("/999999999999 overflow") && !owner.sendChat("/123"), "Invalid channel input admitted");
                submitted = true; return;
            }
            if (!encodedChat || !encodedTypingStart || !encodedTypingStop || encodedChannels != 4) return;
            const std::weak_ptr<VSNativeSession> weak = owner.shared_from_this();
            LL::WorkQueue::getInstance("mainloop")->post([this, weak, generation = oldGeneration]()
            {
                if (auto current = weak.lock()) queuedExpired = !current->deliver("ChatFromSimulator", LLSD::emptyMap(), current->host(), generation);
            });
            owner.requestLogout(false);
            LLSD reply; reply["AgentData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
            reply["AgentData"][0]["SessionID"] = LLUUID(response()["session_id"].asString());
            http("LogoutReply", reply, owner.host());
            require(owner.phase() == VSNativeSession::Phase::Login && !chat->getVisible(), "Logout did not detach session/UI");
            ++step;
        }
        else if (step == 3)
        {
            if (!queuedExpired || LLStartUp::getStartupState() != STATE_LOGIN_WAIT) return;
            require(owner.acceptLogin(response()), "Relogin identity rejected");
            owner.begin(); handshake(owner.host()); ++step;
        }
        else if (step == 4)
        {
            if (owner.phase() != VSNativeSession::Phase::Connected) return;
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
            require(!LLFloaterReg::getInstance("preferences"), "Optional UI was constructed during native session");
            report = owner.evidence(); report["schema"] = 1; report["mode"] = "viewer-native-session-replay";
            report["malformed"] = true; report["unknown"] = true; report["wrong_host"] = true;
            report["udp_chat"] = true; report["http_gate"] = true; report["udp_gate"] = rejected > 0;
            report["queued_expired"] = queuedExpired; report["relogin"] = true;
            report["readback"] = *readback; report["chat_submit"] = true; report["typing"] = true; report["settings_gate"] = true;
            report["encoded_chat"] = encodedChat; report["encoded_typing"] = encodedTypingStart && encodedTypingStop;
            require(LLURLDispatcher::dispatchFromTextEditor("secondlife:///app/teleport/Native/10/20/30", false), "Deferred app URL was not consumed");
            require(LLURLDispatcher::dispatchFromTextEditor("secondlife://Native/10/20/30", false), "Deferred region URL was not consumed");
            require(!LLWorld::instanceExists(), "Deferred URL instantiated scene resources");
            report["encoded_channels"] = encodedChannels == 4;
            report["queued_http_chat"] = true;
            report["ui_gate"] = true;
            LLSD forged; forged["agent"] = LLUUID::generateNewID(); forged["session"] = LLUUID(response()["session_id"].asString());
            require(!owner.deliver("KickUser", forged, owner.host(), owner.generation()), "Forged kick identity admitted");
            LLSD transfer; transfer["MuteData"][0]["AgentID"] = LLUUID(response()["agent_id"].asString());
            transfer["MuteData"][0]["Filename"] = "native-v4-replay-muted.txt";
            http("MuteListUpdate", transfer, owner.host());
            require(!gXferManager->mReceiveList.empty(), "Mute transfer was not queued");
            owner.requestLogout(false); owner.expireDeadlineForReplay(); owner.tick();
            require(gXferManager->mReceiveList.empty(), "Logout retained mute transfer callbacks");
            report["mute_request"] = muteRequest; report["mute_transfer_cleanup"] = true;
            require(owner.phase() == VSNativeSession::Phase::Login, "Logout deadline did not release session");
            report["logout_timeout"] = true;
            require(owner.acceptLogin(response()), "Crossing identity rejected"); owner.begin();
            require(owner.deliver("CrossedRegion", LLSD::emptyMap(), owner.host(), owner.generation()) && owner.phase() == VSNativeSession::Phase::Disconnected, "Unsupported crossing did not disconnect");
            report["crossing_disconnect"] = true;
            report["live_server_qualified"] = false;
            // Partial-init cancellation invalidates the suspended seed request and
            // reliable callback before the same owner accepts another identity.
            owner.reset(); require(owner.acceptLogin(response()), "Partial-init identity rejected");
            owner.begin(); owner.reset();
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
