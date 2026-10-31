/**
 * @file llinvfetch_parity_ais.cpp
 * @brief The real llaisapi.cpp with a scripted LLCoros::checkStop() and a
 *        scriptable timer expiry (recorder R3b), as recorder R3a scripts them.
 *
 * The source is compiled unchanged, through this translation unit: every
 * header llaisapi.cpp and llaisapi.h include is included first (llaisapi.h
 * itself is not, as in R3a), then LLCoros and LLTimer are renamed, so the
 * renames reach only the text of llaisapi.h and llaisapi.cpp.
 *
 * - Its one LLCoros call, LLCoros::checkStop() in AISUpdate::checkTimeout()
 *   (after the yield to the next frame), goes to vks_r3b_LLCoros::checkStop(),
 *   which calls the real one and, when a scenario arms it, throws
 *   LLCoros::Stopping at the n-th call from then on, as the real one does when
 *   the viewer shuts down mid-update.
 * - vks_r3b_LLTimer is LLTimer with no data of its own (the same layout);
 *   hasExpired() answers true for the next n calls a scenario forces, and is
 *   LLTimer::hasExpired() otherwise (n is 0 unless a scenario sets it).
 *   checkTimeout() asks the task timer first, so forcing one call makes an
 *   update yield at its first checkTimeout(): since bug 15 a new update no
 *   longer yields at once by itself, and s24 needs that yield to reach
 *   checkStop().
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"
// What llaisapi.h includes (llaisapi.h itself comes in with llaisapi.cpp,
// under the renames below).
#include "lluuid.h"
#include <map>
#include <set>
#include <string>
#include "../llviewerinventory.h"
#include "llcorehttputil.h"
#include "llcoproceduremanager.h"
#include "lltimer.h"
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
#include "../llaisreply.h"
#include "llinventory_vks.h"

namespace vks_fetch
{
    namespace ais_stop_state
    {
        S64 calls = 0;      // checkStop() calls since the last arm()
        S64 abort_at = 0;   // 0: never; n: throw at the n-th call
        S64 thrown = 0;
    }
    namespace ais_timer_state
    {
        S64 force_expired = 0;  // the next n hasExpired() calls answer true
    }
}

class vks_r3b_LLTimer : public LLTimer
{
public:
    bool hasExpired() const
    {
        if (vks_fetch::ais_timer_state::force_expired > 0)
        {
            --vks_fetch::ais_timer_state::force_expired;
            return true;
        }
        return LLTimer::hasExpired();
    }
};
static_assert(sizeof(vks_r3b_LLTimer) == sizeof(LLTimer), "vks_r3b_LLTimer must keep LLTimer's layout");

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
#define LLTimer vks_r3b_LLTimer
#include "../llaisapi.cpp"
#undef LLTimer
#undef LLCoros
