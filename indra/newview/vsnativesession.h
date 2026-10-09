// CPU-only native session ownership. LGPL-2.1, like the viewer.
#pragma once
#include "llhost.h"
#include "llsd.h"
#include "lluuid.h"
#include "v3math.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <set>
#include <boost/signals2/connection.hpp>

class LLMessageSystem;
class LLEventPoll;
class VSPlainChat;

// Deliberately does not own LLWorld, LLViewerRegion or scene/parcel resources.
// All asynchronous deliveries carry an owner and a session generation.
class VSNativeSession : public std::enable_shared_from_this<VSNativeSession>
{
public:
    enum class Phase { Login, Connecting, Connected, Logout, Disconnected };
    static std::shared_ptr<VSNativeSession> create();
    static std::shared_ptr<VSNativeSession> active();
    ~VSNativeSession();
    void install(LLMessageSystem&);
    bool acceptLogin(const LLSD&);
    void begin();
    void tick();
    void reset();
    void shutdown();
    void requestLogout(bool quit);
    void disconnect(const std::string& reason);
    bool admit(const std::string&, const LLHost&);
    bool sendChat(const std::string&, U8 type = 1, S32 channel = 0);
    void typing(bool active);
    void receive(LLMessageSystem*);
    bool deliver(const std::string&, const LLSD&, const LLHost&, U64 generation);
    void capabilities(const LLSD&, U64 generation);
    void circuitResult(U64 generation, S32 result);
    U64 generation() const { return mGeneration; }
    Phase phase() const { return mPhase; }
    const LLHost& host() const { return mHost; }
    std::string capability(const std::string&) const;
    LLSD evidence() const;
#if VS_VULKAN_DIAGNOSTICS
    void expireDeadlineForReplay() { mDeadline = 0; }
#endif
private:
    bool send(const std::string&, const LLSD&);
    void seedCoro(std::string, U64);
    void connected();
    void appendChat(LLSD, U64);
    void publishChat(const LLSD&, const std::string&, U64);
    void finishLogout();
    void unbind();
    Phase mPhase = Phase::Login;
    U64 mGeneration = 1;
    LLHost mHost;
    LLUUID mAgent, mSession, mRegionID, mOwner;
    U64 mHandle = 0, mFlags = 0;
    U8 mAccess = 0;
    U32 mCircuit = 0;
    S32 mLastChannel = 0;
    U32 mWidth = 256, mHeight = 256;
    LLVector3 mPosition;
    std::string mSeed, mName;
    LLSD mCapabilities;
    std::function<void()> mCancelSeed;
    std::unique_ptr<LLEventPoll> mPoll;
    VSPlainChat* mChat = nullptr; // Root owns the panel; detach before root teardown.
    double mDeadline = 0;
    double mTypingDeadline = 0;
    bool mTyping = false;
    bool mHandshake = false, mMovement = false, mSeedReady = false, mCircuitAck = false, mQuit = false;
    U64 mReceived = 0, mSent = 0, mRejected = 0, mExpired = 0;
    std::vector<boost::signals2::scoped_connection> mSettingConnections;
    std::vector<boost::signals2::scoped_connection> mNameConnections;
    std::map<LLUUID, std::string> mNames;
    std::set<LLUUID> mPendingNames;
};
