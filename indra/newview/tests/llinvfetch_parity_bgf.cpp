/**
 * @file llinvfetch_parity_bgf.cpp
 * @brief The real llinventorymodelbackgroundfetch.cpp with a scripted frame
 *        budget clock (recorder R3b; the seam V0-R3b proposed).
 *
 * The source is compiled unchanged, through this translation unit: every
 * header it includes is included first, then LLTimer is renamed for the
 * source's own text only, so its three LLTimer::getTotalSeconds() calls (the
 * 1 s / 6 ms dispatch budget of bulkFetchViaAis) read vks_fetch's scripted
 * clock. Every read returns the clock and then advances it by the step;
 * step 0 (the default) is the frozen clock every other scenario records.
 *
 * A port that moves the scheduler to Rust must keep an equivalent seam: an
 * injected clock read at the same three points (README, "Seams").
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#include "../llviewerprecompiledheaders.h"
#include "../llinventorymodelbackgroundfetch.h"
#include "../llaisapi.h"
#include "../llagent.h"
#include "../llappviewer.h"
#include "../llappearancemgr.h"
#include "llcallbacklist.h"
#include "../llinventorymodel.h"
#include "../llinventorypanel.h"
#include "llnotificationsutil.h"
#include "../llstartup.h"
#include "../llviewercontrol.h"
#include "../llviewerinventory.h"
#include "../llviewermessage.h"
#include "../llviewerregion.h"
#include "../llviewerwindow.h"
#include "llhttpconstants.h"
#include "bufferarray.h"
#include "bufferstream.h"
#include "llcorehttputil.h"
#include "../llviewermenu.h"
#include "../llviewernetwork.h"

namespace vks_fetch
{
    namespace bgf_clock_state
    {
        F64 now = 1000.0;   // seconds
        F64 step = 0.0;     // added after every read
        S64 reads = 0;
        F64 read()
        {
            ++reads;
            F64 t = now;
            now += step;
            return t;
        }
    }
}

struct vks_r3b_LLTimer
{
    static F64 getTotalSeconds() { return vks_fetch::bgf_clock_state::read(); }
};

#define LLTimer vks_r3b_LLTimer
#include "../llinventorymodelbackgroundfetch.cpp"
#undef LLTimer
