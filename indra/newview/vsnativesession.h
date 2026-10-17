// CPU-only native session ownership. LGPL-2.1, like the viewer.
#pragma once
#include "llhost.h"
#include "vscoarselocations.h"
#include "vsnativecamera.h"
#include "vsparcelpermissions.h"
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
class LLParcel;
class VSRegionLimits;
class LLEventPoll;
class VSPlainChat;
class LLFloater;
class LLViewerMediaImpl;
class VSNativeAppearance;
class VSTaskInventory;
class VSObjectSelection;

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
    void logoutFramePresented();
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
    void capabilities(const LLSD&, U64 generation, U64 regionEpoch = 0, const LLSD& requested = LLSD());
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
    VSTaskInventory* taskInventory();
    VSObjectSelection* objectSelection();
    const LLHost& host() const { return mHost; }
    std::string capability(const std::string&) const;
    VSRegionLimits& regionLimits();
    bool publishRegionOwner(const LLHost&,U64 circuit,const LLUUID&);
    std::string capability(const LLHost&, const std::string&) const;
    LLSD evidence() const;
    const LLSD& simulatorFeatures() const { return mSimulatorFeatures; }
    bool simulatorFeaturesReceived() const { return mSimulatorFeaturesReceived; }
    bool applySimulatorFeatures(const LLSD&, U64 generation, U64 regionEpoch);
    struct RegionMetadata
    {
        F32 billableFactor=1.f;
        bool billableKnown=false,protocolsKnown=false,productKnown=false;
        U64 protocols=0;
        S32 cpuClass=0,cpuRatio=0;
        std::string coloName,productSKU,productName="unknown";
    };
    struct RegionSnapshot
    {
        AccountHandle account;
        LLHost host;
        U64 handle=0,circuit=0,flags=0;
        U32 width=256,height=256;
        LLUUID id,owner;
        std::string name;
        U8 access=0;
        F32 waterHeight=0;
        bool waterKnown=false,current=false;
        bool estateManager=false,estateManagerKnown=false;
        RegionMetadata metadata;
        U32 objectCapacity=0;
        bool objectCapacityKnown=false;
        std::vector<VSCoarseSample<LLUUID>> coarse;
        VSParcelOverlay::Snapshot parcelOverlay;
    };
    // CPU-only descriptor queries can omit spatial vectors/bitmaps; exact-host lookup is logarithmic.
    VSParcelOverlay::Snapshot parcelOverlay(const LLHost&) const;
    std::optional<RegionSnapshot> regionSnapshot(const LLHost&,bool includeSpatial=true) const;
    std::vector<RegionSnapshot> regionSnapshots(bool includeSpatial=true) const;
    bool current(const RegionSnapshot&) const;
    std::vector<VSCoarseSample<LLUUID>> coarseAvatarLocations() const;
    bool receiveParcelOverlay(const LLHost&,U64 account,U64 circuit,S32 sequence,const std::vector<U8>&);
    bool applyRegionInfo(LLMessageSystem&);
    bool applyRegionStats(const LLHost&,U64 account,U64 circuit,U64 flags,U32 capacity);
    bool receiveCoarse(const LLSD&, const LLHost&, U64 account, U64 circuit, bool legacyUDP=false);
    U64 circuitGeneration(const LLHost&) const;
    bool regionEvent(const std::string&, const LLSD&, const LLHost&, U64 account, U64 circuit);
    const std::string& regionName() const { return mName; }
    const LLVector3& position() const { return mPosition; }
    U64 regionEpoch() const { return mRegionEpoch; }
    bool regionReady() const { return mPhase == Phase::Connected && !mChangingRegion && mSeedReady && mHandshake && mCircuitAck; }
    VSNativeCamera::State cameraState() const;
    bool publishCamera();
    bool movementAction(U32 key, const std::string& action, bool down, bool repeated = false);
    void releaseMovement();
    void                  setFlying(bool);
    void                  mouseButton(bool down);
    void cameraOrbit(F32 yaw, F32 pitch);
    void cameraZoom(S32 clicks);
    void cameraPan(const LLVector3&);
    void cameraRoll(F32);
    void cameraReset();
    bool cameraLookAt(const LLVector3d& eye, const LLVector3d& target);
    void cameraResetOrbit();
    void cameraResetPan();
    void cameraResetRoll();
    void cameraFirstPerson(bool);
    F32 cameraZoomFraction() const { return mCamera.zoomFraction(); }
    void cameraZoomFraction(F32 value) { mCamera.setZoomFraction(value); synchronizeCameraMode(); mAgentUpdateTime = 0; }
    void cameraControls(F32 dt);
    bool canFly() const;
    bool updateAgentPosition(const LLVector3&, const LLHost&, U64 generation, U64 regionEpoch);

    U64 handle() const { return mHandle; }
    U8 access() const { return mAccess; }
    U64 regionFlags() const { return mFlags; }
    void publishAgentParcel(const LLParcel&);
    VSParcelPermissions parcelPermissions() const
    { return regionReady() && mParcelPermissionsKnown ? VSParcelPermissions::from(mFlags, mParcelFlags, mParcelSeeAvatars) : VSParcelPermissions{}; }
    std::string locationURL() const;
    using LocationCallback = std::function<void(const std::string&, const LLVector3&)>;
    bool resolveLocation(const LLVector3d&, LocationCallback);
    bool teleportToRegion(const std::string&, const LLVector3&);
    bool teleportToLocation(const LLVector3d&, const LLVector3* lookAt = nullptr);
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
    void synchronizeCameraMode(bool reset_mode = false);
    bool send(const std::string&, const LLSD&);
    void seedCoro(std::string, U64, U64);
    static void simulatorFeaturesCoro(std::weak_ptr<VSNativeSession>, std::string, U64, U64);
    void clearSimulatorFeatures();
    bool changeRegion(const LLSD&);
    bool enableNeighbour(const LLHost&, U64 handle, U32 width, U32 height);
    void retireNeighbour(const LLHost&);
    void neighbourCircuitResult(const LLHost&, U64 account, U64 circuit, S32 result);
    void neighbourSeedCoro(LLHost, std::string, U64 account, U64 circuit);
    bool teleportRequest(U64, const LLVector3&, const LLVector3* lookAt = nullptr);
    void initializeEnvironment();
    void connected();
    void publishProgress();
    void advanceStartup();
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
    enum class Startup { Idle, Inventory, Misc, Precache, Wearables, Complete };
    Startup mStartup = Startup::Idle;
    double mStartupTime = 0;
    bool mStartupTimedOut = false;
    bool mLogoutPresented = false, mLogoutReply = false;
    U64 mGeneration = 1;
    U64 mRegionEpoch = 1;
    bool mChangingRegion = false;
    bool mInterestListArriving = true;
    bool mAppearanceOwnerInitialized=false,mWearablesKnown=false;
    U32 mWearablesSerial=0;
    LLSD mWearablesMetadata;
    std::unique_ptr<VSNativeAppearance> mAppearance;
    std::unique_ptr<VSTaskInventory> mTaskInventory;
    std::unique_ptr<VSObjectSelection> mObjectSelection;
    LLHost mPreviousHost;
    struct RegionCircuit
    {
        U64 handle=0, incarnation=0, requestRevision=0;
        U32 width=256,height=256;
        bool acknowledged=false,handshake=false,seedFailed=false;
        double deadline=0;
        LLUUID id,owner;
        std::string name;
        U64 flags=0;
        U8 access=0;
        F32 waterHeight=0;
        bool waterKnown=false;
        bool estateManager=false,estateManagerKnown=false;
        RegionMetadata metadata;
        U32 objectCapacity=0;
        bool objectCapacityKnown=false;
        VSCoarseSnapshot<LLUUID> coarse;
        VSParcelOverlay::Snapshot parcelOverlay;
        LLSD capabilities;
        std::function<void()> cancel;
        std::unique_ptr<LLEventPoll> poll;
    };
    std::map<LLHost,RegionCircuit> mCircuits;
    U64 mNextCircuitGeneration=1;
    std::string mRequestedRegion;
    LLVector3 mRequestedPosition;
    double mNavigationDeadline = 0;
    U32 mTeleportFlags = 0;
    bool mTeleportArrival = false;
    struct LocationRequest { LLVector3d position; LocationCallback callback; double deadline; };
    std::vector<LocationRequest> mLocationRequests;
    S32 mParcelSequence = 0;
    S32 mParcelID = -1;
    S32 mParcelCellX = -1, mParcelCellY = -1;
    double mParcelQueryTime = 0;
    LLUUID mParcelOwner;
    U32 mParcelFlags = 0;
    bool mParcelPermissionsKnown = false, mParcelSeeAvatars = true;
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
    std::unique_ptr<VSRegionLimits> mRegionLimits;
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
