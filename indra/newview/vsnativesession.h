// CPU-only native session ownership. LGPL-2.1, like the viewer.
#pragma once
#include "llhost.h"
#include "llsd.h"
#include "lluuid.h"
#include "llhandle.h"
#include "v3math.h"
#include "v3dmath.h"
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
class LLFloater;
class LLViewerMediaImpl;

// Deliberately does not own LLWorld, LLViewerRegion or scene resources.
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
    bool sendChatProcessed(const std::string&, U8 type, S32 channel);
    void typing(bool active);
    void receive(LLMessageSystem*);
    bool deliver(const std::string&, const LLSD&, const LLHost&, U64 generation);
    void capabilities(const LLSD&, U64 generation, U64 regionEpoch = 0);
    void circuitResult(U64 generation, S32 result, U64 regionEpoch = 0);
    U64 generation() const { return mGeneration; }
    Phase phase() const { return mPhase; }
    const LLHost& host() const { return mHost; }
    std::string capability(const std::string&) const;
    LLSD evidence() const;
    const std::string& regionName() const { return mName; }
    const LLVector3& position() const { return mPosition; }
    U64 handle() const { return mHandle; }
    U8 access() const { return mAccess; }
    std::string locationURL() const;
    using LocationCallback = std::function<void(const std::string&, const LLVector3&)>;
    bool resolveLocation(const LLVector3d&, LocationCallback);
    bool teleportToRegion(const std::string&, const LLVector3&);
    bool teleportToLocation(const LLVector3d&);
    bool teleportToLandmark(const LLUUID&);
    bool teleportHome() { return teleportToLandmark(LLUUID::null); }
    bool teleportToLure(const LLUUID&, bool godlike);

    // Current simulator's parcel metadata and ordinary 2D playback controls.
    const std::string& musicURL() const { return mMusicURL; }
    const std::string& mediaURL() const { return mMediaURL; }
    const std::string& parcelName() const { return mParcelName; }
    bool musicEnabled() const { return mMusicRequested; }
    bool mediaEnabled() const;
    bool setMusicEnabled(bool);
    bool setMediaEnabled(bool);
    void setMediaPaused(bool);
    LLViewerMediaImpl* parcelMedia() const;

    // Unknown balance stays unknown until an authenticated simulator reply.
    bool balanceKnown() const { return mBalanceKnown; }
    S32 balance() const { return mBalance; }
    S32 squareMetersLeft() const { return mLandCredit - mLandCommitted; }
    void updateBalance(S32 value) { mBalance = value; mBalanceKnown = true; }
    void clearBalance() { mBalance = 0; mBalanceKnown = false; mLandCredit = mLandCommitted = 0; }

#if VS_VULKAN_DIAGNOSTICS
    void expireDeadlineForReplay() { mDeadline = 0; }
#endif
private:
    bool send(const std::string&, const LLSD&);
    void seedCoro(std::string, U64, U64);
    bool changeRegion(const LLSD&);
    bool teleportRequest(U64, const LLVector3&);
    void connected();
    void requestParcel();
    void clearParcelPlayback();
    void updateParcelPlayback();
    bool requestParcelPlayback(bool music);
    bool playbackCurrent(bool music, U64 generation, U64 epoch, U64 revision, U64 request, const std::string& url) const;
    void playParcelAllowed(bool music);
    void rejectParcelPlayback(bool music);
    void parcelMediaCommand(U32 flags, U32 command, F32 time);
    bool updateParcelMediaMetadata(const LLSD& body);
    void applyParcelMediaState();
    void appendChat(LLSD, U64);
    void publishChat(const LLSD&, const std::string&, U64);
    void finishLogout();
    void unbind();
    Phase mPhase = Phase::Login;
    bool mShutdown = false;
    U64 mGeneration = 1;
    U64 mRegionEpoch = 1;
    bool mChangingRegion = false;
    LLHost mPreviousHost;
    std::string mRequestedRegion;
    LLVector3 mRequestedPosition;
    double mNavigationDeadline = 0;
    struct LocationRequest { LLVector3d position; LocationCallback callback; double deadline; };
    std::vector<LocationRequest> mLocationRequests;
    S32 mParcelSequence = 0;
    S32 mParcelID = -1;
    U64 mParcelRevision = 1, mMusicRequest = 0, mMediaRequest = 0;
    bool mMusicRequested = false, mMediaRequested = false;
    bool mMusicUserStopped = false, mMediaUserStopped = false;
    bool mMediaPresented = false;
    bool mMediaLoop = false, mMediaAutoScale = true, mMediaPaused = false, mMediaStateDirty = false;
    S32 mMediaWidth = 0, mMediaHeight = 0;
    F32 mMediaPendingSeek = -1.f;
    std::string mParcelName, mMusicURL, mMediaURL, mMediaType;
    LLHandle<LLFloater> mParcelMediaWindow;
    LLHost mHost;
    LLUUID mAgent, mSession, mRegionID, mOwner;
    U64 mHandle = 0, mFlags = 0;
    U8 mAccess = 0;
    U32 mCircuit = 0;
    bool mBalanceKnown = false;
    S32 mBalance = 0;
    S32 mLandCredit = 0, mLandCommitted = 0;
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
    bool mFrozen = false;
    bool mHandshake = false, mMovement = false, mSeedReady = false, mCircuitAck = false, mQuit = false;
    U64 mReceived = 0, mSent = 0, mRejected = 0, mExpired = 0;
    std::vector<boost::signals2::scoped_connection> mSettingConnections;
    std::vector<boost::signals2::scoped_connection> mNameConnections;
    std::map<LLUUID, std::string> mNames;
    std::set<LLUUID> mPendingNames;
};
