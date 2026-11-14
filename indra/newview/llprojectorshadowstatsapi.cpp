// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// LEAP API "ProjectorShadows" (development builds only, -DVKS_GL_STATS=ON): the projector shadow pool's counters, read by
// scripts/inworld/projector_orbit_check.py and perf_check.py. Compiled to nothing unless VKS_GL_STATS is set.
//
// Stage S0 (before the pool exists): the operation reports what today's two projector shadow slots and the light set look
// like (for the pool budget, doc/shadows/projector-pool-budget.md) and the pool fields the orbit check will read, as
// "pool_present": false with zero counters, so the script skeleton and the reply shape are fixed now. The pool stages
// (P2 and later) fill the pool fields: static renders, lights without a tile, per-tile CRC32 of the static atlas
// (glGetTextureSubImage), the cause of each re-render. Main thread only (LEAP operations run on it).

#include "llviewerprecompiledheaders.h"
#include "llprojectorshadowstatsapi.h"

#if VKS_GL_STATS

#include "lleventapi.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llviewercontrol.h"
#include "pipeline.h"

namespace
{
    void api_stats(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const LLPipeline::ProjectorShadowStats st = gPipeline.getProjectorShadowStats();

        r["compiled"] = true;
        // effective values (what runs: a stored 2 runs as Legacy below GL 4.0), and the stored settings beside them
        r["shadow_method"] = (S32)LLPipeline::RenderShadowMethod;
        r["shadow_detail"] = (S32)LLPipeline::RenderShadowDetail;
        r["shadow_method_setting"] = (S32)gSavedSettings.getU32("RenderShadowMethod");
        r["shadow_detail_setting"] = (S32)gSavedSettings.getS32("RenderShadowDetail");

        // today's two slots and the light set (S0: what the pool replaces)
        LLSD slots;
        slots["occupied"] = (S32)st.slotsOccupied;      // slot renders per frame
        slots["targeted"] = (S32)st.slotsTargeted;
        r["slots"] = slots;
        LLSD lights;
        lights["nearby"] = (S32)st.nearbyLights;
        lights["spotlights"] = (S32)st.spotlights;
        lights["eligible"] = (S32)st.eligible;
        lights["reach_m"] = (F64)st.reach;
        lights["eligible_within_16m"] = (S32)st.eligibleWithin[0];
        lights["eligible_within_32m"] = (S32)st.eligibleWithin[1];
        lights["eligible_within_64m"] = (S32)st.eligibleWithin[2];
        lights["eligible_within_reach"] = (S32)st.eligibleWithin[3];
        r["lights"] = lights;

        // the pool (P2 and later); fixed shape for the orbit check
        LLSD pool;
        pool["pool_present"] = false;
        pool["level"] = 0;
        pool["static_renders"] = 0;         // cumulative since the pool was allocated
        pool["dynamic_renders"] = 0;
        pool["lights_without_tile"] = 0;
        pool["tiles"] = LLSD::emptyArray(); // per tile: key, size, drawn_frame, crc32 of the static tile
        pool["rerender_causes"] = LLSD::emptyMap();
        r["pool"] = pool;

        LL_INFOS("ProjectorShadows") << "stats: slots " << st.slotsOccupied << ", eligible projectors within 16/32/64 m/reach("
                                     << st.reach << " m): " << st.eligibleWithin[0] << "/" << st.eligibleWithin[1] << "/"
                                     << st.eligibleWithin[2] << "/" << st.eligibleWithin[3] << " of " << st.eligible
                                     << " eligible, " << st.spotlights << " spotlights, " << st.nearbyLights << " nearby lights"
                                     << LL_ENDL;
    }

    class ProjectorShadowsListener : public LLEventAPI
    {
    public:
        ProjectorShadowsListener()
            : LLEventAPI("ProjectorShadows", "Projector shadow pool counters (development builds): slots, light set, pool state")
        {
            add("stats", "Counters of the projector shadows and of the light set, on [\"reply\"].", &api_stats, llsd::map("reply", LLSD()));
        }
    };

    ProjectorShadowsListener sListener;
}

#endif // VKS_GL_STATS
