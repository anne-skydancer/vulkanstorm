/**
 * @file lltexturefetch_parity_stubs.cpp
 * @brief The viewer environment of LLTextureFetch, stubbed for the parity test.
 *
 * lltexturefetch.cpp and lltexturecache.cpp read a handful of viewer
 * globals (the agent's region and its capability URLs, settings, the decode
 * thread, the HTTP policy classes, statistics sinks, the asset blacklist).
 * This file supplies them without the viewer: a region object holding the
 * mock server's URL, an agent whose storage is never constructed (only its
 * region lookup is called), an application object with the HTTP policy
 * table, and no-op statistics.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "../llappviewer.h"
#include "../llappcorehttp.h"
#include "../llviewerregion.h"
#include "../llagent.h"
#include "../llworld.h"
#include "../llviewerstatsrecorder.h"
#include "../fsassetblacklist.h"
#include "../llviewertexture.h"
#include "../llviewerthrottle.h"
#include "../llstartup.h"
#include "../llviewerstats.h"
#include "../llviewernetwork.h"
#include "../lltextureinfo.h"
#include "../llviewerassetstats.h"
#include "../llvoavatar.h"

#include "lltexturefetch_parity_stubs.h"

#include <cstring>
#include <new>

// Access to the private members the stubs must set (the application's
// statics and core-http member, the region's capability URLs): an explicit
// template instantiation may name a private member, and the friend it
// defines hands the pointer out.
namespace
{
    template<typename Tag, typename Tag::type M>
    struct Steal
    {
        friend typename Tag::type steal(Tag) { return M; }
    };
    struct AppInstanceTag { typedef LLAppViewer** type; friend type steal(AppInstanceTag); };
    struct AppDecodeTag { typedef LLImageDecodeThread** type; friend type steal(AppDecodeTag); };
    struct AppCacheTag { typedef LLTextureCache** type; friend type steal(AppCacheTag); };
    struct AppFetchTag { typedef LLTextureFetch** type; friend type steal(AppFetchTag); };
    struct AppCoreHttpTag { typedef LLAppCoreHttp LLAppViewer::*type; friend type steal(AppCoreHttpTag); };
    struct RegionAssetUrlTag { typedef std::string LLViewerRegion::*type; friend type steal(RegionAssetUrlTag); };
    struct RegionHttpUrlTag { typedef std::string LLViewerRegion::*type; friend type steal(RegionHttpUrlTag); };
}
template struct Steal<AppInstanceTag, &LLAppViewer::sInstance>;
template struct Steal<AppDecodeTag, &LLAppViewer::sImageDecodeThread>;
template struct Steal<AppCacheTag, &LLAppViewer::sTextureCache>;
template struct Steal<AppFetchTag, &LLAppViewer::sTextureFetch>;
template struct Steal<AppCoreHttpTag, &LLAppViewer::mAppCoreHttp>;
template struct Steal<RegionAssetUrlTag, &LLViewerRegion::mViewerAssetUrl>;
template struct Steal<RegionHttpUrlTag, &LLViewerRegion::mHttpUrl>;

// ---- settings and application ---------------------------------------------

LLControlGroup gSavedSettings("Global");
// gDisconnected is defined in llcommon (llapp.cpp) since upstream #5241 (147e864246).
EStartupState LLStartUp::gStartupState = STATE_FIRST;

LLAppViewer* LLAppViewer::sInstance = NULL;
LLTextureCache* LLAppViewer::sTextureCache = NULL;
LLImageDecodeThread* LLAppViewer::sImageDecodeThread = NULL;
LLTextureFetch* LLAppViewer::sTextureFetch = NULL;

void LLAppViewer::pauseMainloopTimeout() {}
void LLAppViewer::resumeMainloopTimeout(std::string_view) {}
void LLAppViewer::outOfMemorySoftQuit() {}

LLAppCoreHttp::HttpClass::HttpClass()
    : mPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID),
      mConnLimit(0U),
      mPipelined(false)
{}

LLAppCoreHttp::LLAppCoreHttp()
    : mRequest(NULL),
      mStopHandle(LLCORE_HTTP_HANDLE_INVALID),
      mStopRequested(0.0),
      mStopped(false),
      mPipelined(false)
{}

LLAppCoreHttp::~LLAppCoreHttp() {}
void LLAppCoreHttp::onCompleted(LLCore::HttpHandle, LLCore::HttpResponse*) {}

namespace
{
    alignas(64) unsigned char g_app_storage[sizeof(LLAppViewer)];
    bool g_app_made = false;
}

void vks_test_install_app(LLImageDecodeThread* decode_thread, LLTextureCache* cache, LLTextureFetch* fetch)
{
    if (!g_app_made)
    {
        memset(g_app_storage, 0, sizeof(g_app_storage));
        LLAppViewer* app = reinterpret_cast<LLAppViewer*>(g_app_storage);
        new (&(app->*steal(AppCoreHttpTag()))) LLAppCoreHttp();
        *steal(AppInstanceTag()) = app;
        g_app_made = true;
    }
    *steal(AppDecodeTag()) = decode_thread;
    *steal(AppCacheTag()) = cache;
    *steal(AppFetchTag()) = fetch;
}

// ---- the region and the agent ----------------------------------------------

namespace
{
    alignas(64) unsigned char g_region_storage[sizeof(LLViewerRegion)];
    LLViewerRegion* g_region = NULL;
    LLUUID g_region_id;
}

void vks_test_set_region(const std::string& viewer_asset_url, const std::string& http_url, const LLUUID& region_id)
{
    if (!g_region)
    {
        memset(g_region_storage, 0, sizeof(g_region_storage));
        g_region = reinterpret_cast<LLViewerRegion*>(g_region_storage);
        new (&(g_region->*steal(RegionAssetUrlTag()))) std::string();
        new (&(g_region->*steal(RegionHttpUrlTag()))) std::string();
    }
    g_region->*steal(RegionAssetUrlTag()) = viewer_asset_url;
    g_region->*steal(RegionHttpUrlTag()) = http_url;
    g_region_id = region_id;
}

void vks_test_clear_region()
{
    if (g_region)
    {
        (g_region->*steal(RegionAssetUrlTag())).clear();
        (g_region->*steal(RegionHttpUrlTag())).clear();
    }
    g_region = NULL;
}

LLViewerRegion* vks_test_region()
{
    return g_region;
}

const LLUUID& LLViewerRegion::getRegionID() const { return g_region_id; }
bool LLViewerRegion::capabilitiesReceived() const { return true; }
boost::signals2::connection LLViewerRegion::setCapabilitiesReceivedCallback(const caps_received_signal_t::slot_type&)
{
    return boost::signals2::connection();
}

// The agent: lltexturefetch.cpp refers to the gAgent object but calls only
// its region accessors, which are stubbed here; the object itself is raw
// storage that is never constructed. On Windows the linker aliases the
// object's symbol to the storage; on the Itanium ABI a global object's
// symbol is its plain name, which the storage takes.
#if LL_WINDOWS
extern "C" unsigned char gAgentStorage[1 << 16];
extern "C" unsigned char gAgentStorage[1 << 16] = { 0 };
#pragma comment(linker, "/alternatename:?gAgent@@3VLLAgent@@A=gAgentStorage")
#else
alignas(64) unsigned char gAgentStorage[1 << 16] __asm__("gAgent") = { 0 };
#endif

LLViewerRegion* LLAgent::getRegion() const { return g_region; }
LLHost LLAgent::getRegionHost() const { return LLHost(); }

LLViewerRegion* LLWorld::getRegion(const LLHost&) { return g_region; }

// ---- statistics, throttle, blacklist, grid ----------------------------------

LLViewerStatsRecorder::LLViewerStatsRecorder()
    : mStatsFile(NULL),
      mTimer(),
      mFileOpenTime(0.0),
      mLastSnapshotTime(0.0),
      mInterval(0.2f),
      mMaxDuration(300.f),
      mEnableStatsRecording(false),
      mEnableStatsLogging(false),
      mSkipSaveIfZeros(false)
{
}
LLViewerStatsRecorder::~LLViewerStatsRecorder() {}

bool FSAssetBlacklist::isBlacklisted(const LLUUID&, LLAssetType::EType, eBlacklistFlag) const { return false; }

void LLViewerTextureManager::findTextures(const LLUUID&, std::vector<LLViewerTexture*>&) {}
U32Bytes gTotalTextureBytesPerBoostLevel[LLViewerTexture::MAX_GL_IMAGE_CATEGORY];

F32 LLViewerThrottle::getMaxBandwidthKbps() { return 1500.f; }

namespace LLStatViewer
{
    LLTrace::CountStatHandle<F64Kilobytes> TEXTURE_NETWORK_DATA_RECEIVED("texturedatareceived", "Texture network data received");
    LLTrace::SampleStatHandle<> FPS_SAMPLE("fpssample");
}

bool use_http_textures() { return true; }

const std::string& fttype_to_string(const FTType& fttype)
{
    static const std::string names[] = { "FTT_UNKNOWN", "FTT_DEFAULT", "FTT_SERVER_BAKE", "FTT_HOST_BAKE", "FTT_MAP_TILE", "FTT_LOCAL_FILE" };
    S32 i = (S32)fttype + 1;
    return names[(i >= 0 && i < 6) ? i : 0];
}

void dump_sequential_xml(const std::string, const LLSD&) {}

LLGridManager::LLGridManager() {}
LLGridManager::~LLGridManager() {}
bool LLGridManager::isInProductionGrid() const { return true; }
bool LLGridManager::isSystemGrid(const std::string&) const { return true; }

// ---- LLTextureInfo (download logging): no-ops --------------------------------

LLTextureInfo::LLTextureInfo(bool) : mLoggingEnabled(false) {}
LLTextureInfo::~LLTextureInfo() {}
void LLTextureInfo::setLogging(bool) {}
bool LLTextureInfo::has(const LLUUID&) { return false; }
void LLTextureInfo::setRequestStartTime(const LLUUID&, U64) {}
void LLTextureInfo::setRequestSize(const LLUUID&, U32) {}
void LLTextureInfo::setRequestOffset(const LLUUID&, U32) {}
void LLTextureInfo::setRequestType(const LLUUID&, LLTextureInfoDetails::LLRequestType) {}
void LLTextureInfo::setRequestCompleteTimeAndLog(const LLUUID&, U64Microseconds) {}
void LLTextureInfo::startRecording() {}
void LLTextureInfo::stopRecording() {}

// ---- what llviewerassetstats.cpp needs ---------------------------------------

void LLVOAvatar::getNearbyRezzedStats(std::vector<S32>& counts, F32& avg_cloud_time, S32& cloud_avatars, S32& pending_meshes, S32& control_avatars)
{
    counts.resize(3);
    counts[0] = 0;
    counts[1] = 0;
    counts[2] = 0;
    avg_cloud_time = 0.f;
    cloud_avatars = 0;
    pending_meshes = 0;
    control_avatars = 0;
}

std::string LLVOAvatar::rezStatusToString(S32) { return "unknown"; }

LLViewerStats::StatsAccumulator& LLViewerStats::PhaseMap::getPhaseStats(const std::string&)
{
    static LLViewerStats::StatsAccumulator junk;
    return junk;
}
