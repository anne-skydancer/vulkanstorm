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
    // CRC-32 (IEEE), for the tile checksums of the orbit check
    U32 crc32_of(const void* data, size_t bytes)
    {
        static U32 table[256];
        static bool ready = false;
        if (!ready)
        {
            for (U32 i = 0; i < 256; ++i)
            {
                U32 c = i;
                for (int k = 0; k < 8; ++k)
                {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            ready = true;
        }
        U32 crc = 0xFFFFFFFFu;
        const U8* p = static_cast<const U8*>(data);
        for (size_t i = 0; i < bytes; ++i)
        {
            crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

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
        const LLProjectorShadows::Stats ps = gPipeline.mProjectorShadows.getStats();
        LLSD pool;
        pool["pool_present"] = ps.present;
        pool["level"] = ps.level;
        pool["atlas_size"] = (S32)ps.atlasSize;
        pool["lights"] = (S32)ps.lights;
        pool["static_renders"] = (F64)ps.tileRenders;   // cumulative since the pool was allocated (P2: every render is a full one)
        pool["dynamic_renders"] = 0;
        pool["repacks"] = (F64)ps.repacks;
        pool["lights_without_tile"] = (S32)ps.lightsWithoutTile; // O-4: 0 after the first fill
        pool["tile_cost_ms"] = (F64)ps.costEmaMs;
        pool["last_frame_renders"] = (S32)ps.lastFrameRenders;
        pool["last_frame_render_ms"] = (F64)ps.lastFrameRenderMs;
        LLSD tiles = LLSD::emptyArray();
        const bool with_crc = !req.has("crc") || req["crc"].asBoolean(); // on unless asked off: a read-back of each tile
        for (const LLProjectorShadows::TileInfo& t : gPipeline.mProjectorShadows.getTiles())
        {
            LLSD tile;
            tile["key"] = (S32)t.key;
            tile["size"] = (S32)t.size;
            tile["x"] = (S32)t.x;
            tile["y"] = (S32)t.y;
            tile["drawn_frame"] = (S32)t.drawnFrame;
            tile["cam_dist"] = (F64)t.camDist;
            if (with_crc && t.size && t.drawnFrame && gGLManager.mGLVersion >= 4.49f)   // GL 4.5 core: fetched only from 4.5 up (llgl.cpp); the version, not the address (-Werror=address)
            {
                std::vector<F32> texels((size_t)t.size * t.size);
                glGetTextureSubImage(gPipeline.mProjectorShadows.atlasS(), 0, (GLint)t.x, (GLint)t.y, 0, (GLsizei)t.size, (GLsizei)t.size, 1,
                                     GL_DEPTH_COMPONENT, GL_FLOAT, (GLsizei)(texels.size() * sizeof(F32)), texels.data());
                tile["crc32"] = (S32)crc32_of(texels.data(), texels.size() * sizeof(F32));
            }
            tiles.append(tile);
        }
        pool["tiles"] = tiles;  // per tile: key, size, drawn_frame, and (unless "crc": false) the crc32 of its texels in S
        pool["rerender_causes"] = LLSD::emptyMap(); // P3 (the invalidation hooks)
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
