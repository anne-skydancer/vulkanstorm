/**
 * @file lltexturefetch_parity_stubs.h
 * @brief The test's handles on the stubbed viewer environment.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLTEXTUREFETCH_PARITY_STUBS_H
#define LL_LLTEXTUREFETCH_PARITY_STUBS_H

#include <string>

class LLImageDecodeThread;
class LLTextureCache;
class LLTextureFetch;
class LLViewerRegion;
class LLUUID;

// Installs the fake application object with its HTTP policy table and the
// decode thread, cache and fetcher the viewer would hold.
void vks_test_install_app(LLImageDecodeThread* decode_thread, LLTextureCache* cache, LLTextureFetch* fetch);
// The agent's region: its ViewerAsset and legacy HTTP capability URLs.
void vks_test_set_region(const std::string& viewer_asset_url, const std::string& http_url, const LLUUID& region_id);
// No region (the agent is not connected).
void vks_test_clear_region();
LLViewerRegion* vks_test_region();

#endif
