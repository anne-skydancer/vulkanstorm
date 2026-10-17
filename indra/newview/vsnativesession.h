// CPU-only native session ownership. LGPL-2.1, like the viewer.
#pragma once
#include "llhost.h"
#include "vsnativecamera.h"
#include "vsworldscene.h"
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
class VSNativeAppearance;

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
    bool                  requestAnimations(const std::vector<LLUUID>&, bool start);
    bool                  resetAvatar(const LLUUID&, bool resetAnimations);
    VSNativeAppearance*   appearance() const { return mAppearance.get(); }
    void typing(bool active);
    void receive(LLMessageSystem*);
    bool deliver(const std::string&, const LLSD&, const LLHost&, U64 generation);
    void capabilities(const LLSD&, U64 generation, U64 regionEpoch = 0);
    void circuitResult(U64 generation, S32 result, U64 regionEpoch = 0);
    U64 generation() const { return mGeneration; }
    Phase phase() const { return mPhase; }
    std::function<bool()> accountGuard(bool allowConnecting = false) const;
    struct AccountHandle {LLUUID agent,session;U64 generation=0;};
    struct InventoryHandle {AccountHandle account;LLUUID item,asset;U64 revision=0;};
    AccountHandle account() const {return {mAgent,mSession,mGeneration};}
    bool current(const AccountHandle&) const;
    std::optional<InventoryHandle> inventory(const LLUUID&) const;
    bool current(const InventoryHandle&) const;
    std::function<bool()> interactionGuard(const VSWorldScene::FaceHandle&) const;
    bool requestTaskInventory(const VSWorldScene::ObjectHandle&);
    const LLHost& host() const { return mHost; }
    std::string capability(const std::string&) const;
    std::string capability(const LLHost&, const std::string&) const;
    LLSD evidence() const;
    const LLSD& simulatorFeatures() const { return mSimulatorFeatures; }
    bool simulatorFeaturesReceived() const { return mSimulatorFeaturesReceived; }
    bool applySimulatorFeatures(const LLSD&, U64 generation, U64 regionEpoch);
    U64 circuitGeneration(const LLHost&) const;
    bool regionEvent(const std::string&, const LLSD&, const LLHost&, U64 account, U64 circuit);
    const std::string& regionName() const { return mName; }
    const LLVector3& position() const { return mPosition; }
    U64 regionEpoch() const { return mRegionEpoch; }
    bool regionReady() const { return mPhase == Phase::Connected && !mChangingRegion && mSeedReady && mHandshake && mCircuitAck; }
    VSNativeCamera::State cameraState() const { return mCamera.state(mPosition); }
    bool movementAction(U32 key, const std::string& action, bool down, bool repeated = false);
    void releaseMovement();
    void                  setFlying(bool);
    void                  mouseButton(bool down);
    void cameraOrbit(F32 yaw, F32 pitch);
    void cameraZoom(S32 clicks);
    bool updateAgentPosition(const LLVector3&, const LLHost&, U64 generation, U64 regionEpoch);

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
    bool cancelTeleport();

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
    static void simulatorFeaturesCoro(std::weak_ptr<VSNativeSession>, std::string, U64, U64);
    void clearSimulatorFeatures();
    bool changeRegion(const LLSD&);
    bool enableNeighbour(const LLHost&, U64 handle, U32 width, U32 height);
    void retireNeighbour(const LLHost&);
    void neighbourCircuitResult(const LLHost&, U64 account, U64 circuit, S32 result);
    void neighbourSeedCoro(LLHost, std::string, U64 account, U64 circuit);
    bool teleportRequest(U64, const LLVector3&);
    void connected();
    void publishProgress();
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
    bool mAppearanceOwnerInitialized=false,mWearablesKnown=false;
    U32 mWearablesSerial=0;
    LLSD mWearablesMetadata;
    std::unique_ptr<VSNativeAppearance> mAppearance;
    LLHost mPreviousHost;
    struct RegionCircuit
    {
        U64 handle=0, incarnation=0, requestRevision=0;
        U32 width=256,height=256;
        bool acknowledged=false,handshake=false,seedFailed=false;
        double deadline=0;
        LLSD capabilities;
        std::function<void()> cancel;
        std::unique_ptr<LLEventPoll> poll;
    };
    std::map<LLHost,RegionCircuit> mCircuits;
    U64 mNextCircuitGeneration=1;
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
    F32                                             mWaterHeight = 20;
    bool                                            mWaterKnown  = false;
    VSNativeCamera mCamera;
    double mCameraTime = 0, mAgentUpdateTime = 0;
    std::string mSeed, mName;
    LLSD mCapabilities;
    std::function<void()> mCancelSeed;
    LLSD mSimulatorFeatures;
    bool mSimulatorFeaturesReceived = false;
    std::function<void()> mCancelSimulatorFeatures;
    std::unique_ptr<LLEventPoll> mPoll;
    VSPlainChat* mChat = nullptr; // Root owns the panel; detach before root teardown.
    double mDeadline = 0;
    double mTypingDeadline = 0;
    bool mTyping = false;
    bool mFrozen = false;
    bool                                            mWorldMouseDown = false, mWorldMouseMouselook = false;
    U32                                             mMouseControls   = 0;
    U64                                             mAttentionAvatar = 0;
    bool mHandshake = false, mMovement = false, mSeedReady = false, mCircuitAck = false, mQuit = false;
    U64 mReceived = 0, mSent = 0, mRejected = 0, mExpired = 0;
    std::vector<boost::signals2::scoped_connection> mSettingConnections;
    std::vector<boost::signals2::scoped_connection> mNameConnections;
    std::map<LLUUID, std::string> mNames;
    std::set<LLUUID> mPendingNames;
};
