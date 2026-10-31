/**
 * @file llinvfetch_bench_frozen.h
 * @brief The frozen C++ background fetch scheduler of llinvfetch_parity's
 *        bench (llinvfetch_bench_frozen.cpp).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLINVFETCH_BENCH_FROZEN_H
#define LL_LLINVFETCH_BENCH_FROZEN_H

#include "lluuid.h"

namespace vks_fetch_bench
{
    // The frozen class's singleton: deleted (a fresh one on the next call).
    void frozen_reset();
    void frozen_start(const LLUUID& id, bool recursive);
    void frozen_schedule_folder(const LLUUID& id);
    // backgroundFetch(), the idle callback.
    void frozen_idle();
    // mBackgroundFetchActive
    bool frozen_active();
    bool frozen_everything_fetched();
    bool frozen_folder_fetch_active();
    bool frozen_bulk_complete();
    bool frozen_inventory_completed();
}

#endif
