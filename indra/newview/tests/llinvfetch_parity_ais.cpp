/**
 * @file llinvfetch_parity_ais.cpp
 * @brief The real llaisapi.cpp with a scripted LLCoros::checkStop()
 *        (recorder R3b), as recorder R3a scripted it for its corpus.
 *
 * The source is compiled unchanged, through this translation unit: every
 * header it includes is included first, then LLCoros is renamed for the
 * source's own text only. Its one LLCoros call, LLCoros::checkStop() in
 * AISUpdate::checkTimeout() (after the yield to the next frame), goes to
 * vks_r3b_LLCoros::checkStop(), which calls the real one and, when a
 * scenario arms it, throws LLCoros::Stopping at the n-th call from then on,
 * as the real one does when the viewer shuts down mid-update.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"
#include "../llaisapi.h"
#include "../llagent.h"
#include "../llappviewer.h"
#include "llcallbacklist.h"
#include "../llinventorymodel.h"
#include "../llinventoryobserver.h"
#include "llnotificationsutil.h"
#include "llsdutil.h"
#include "../llviewerregion.h"
#include "../llvoavatar.h"
#include "../llvoavatarself.h"
#include "../llviewercontrol.h"
#include "../llviewernetwork.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "llexception.h"

namespace vks_fetch
{
    namespace ais_stop_state
    {
        S64 calls = 0;      // checkStop() calls since the last arm()
        S64 abort_at = 0;   // 0: never; n: throw at the n-th call
        S64 thrown = 0;
    }
}

struct vks_r3b_LLCoros
{
    static void checkStop()
    {
        LLCoros::checkStop();
        ++vks_fetch::ais_stop_state::calls;
        if (vks_fetch::ais_stop_state::abort_at && vks_fetch::ais_stop_state::calls == vks_fetch::ais_stop_state::abort_at)
        {
            vks_fetch::ais_stop_state::abort_at = 0;
            ++vks_fetch::ais_stop_state::thrown;
            LLTHROW(LLCoros::Stopping("scripted shutdown (R3b)"));
        }
    }
};

#define LLCoros vks_r3b_LLCoros
#include "../llaisapi.cpp"
#undef LLCoros
