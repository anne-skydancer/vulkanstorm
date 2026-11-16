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
        // effective values (what runs: the stored method, sanitized), and the stored settings beside them
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
        pool["static_renders"] = (F64)ps.staticRenders;   // cumulative since the pool was allocated: the static schedule (the first fill and every change)
        pool["dynamic_renders"] = (F64)ps.dynamicRenders; // ... the dynamic schedule (a moving lamp redrawn whole)
        pool["layer_renders"] = (F64)ps.layerRenders;     // P4: avatar layers alone (S -> L for the tile, the avatars over it)
        pool["last_frame_layer_renders"] = (S32)ps.lastFrameLayer;
        pool["last_frame_avatar_draws"] = (S32)ps.lastFrameAvatarDraws;
        pool["last_frame_static_renders"] = (S32)ps.lastFrameStatic;
        pool["last_frame_dynamic_renders"] = (S32)ps.lastFrameDynamic;
        pool["pinned_objects"] = (S32)ps.pinnedObjects;   // prims and trees held at full detail by at least one tile (the LOD pin)
        pool["pinned_triangles"] = (F64)ps.pinnedTriangles; // their triangles at that detail: what the pin costs the main view
        pool["mover_objects"] = (S32)ps.moverObjects;     // objects flagged as movers since start (out of the fixed layer while active)
        pool["pending_tiles"] = (S32)ps.pendingTiles;     // drawn tiles a change touched and the scheduler has not drawn again
        pool["avatar_tiles"] = (S32)ps.avatarTiles;
        pool["dirty_boxes_last_frame"] = (S32)ps.dirtyBoxesLastFrame;
        pool["frame"] = (S32)ps.frame;
        pool["dirty_age_ms_max"] = (F64)ps.dirtyAgeMsMax; // the longest a changed tile waited for its render since the last "reset_ages"
        pool["dirty_renders"] = (F64)ps.dirtyRenders;
        pool["repacks"] = (F64)ps.repacks;
        pool["cells"] = (S32)ps.cells;
        pool["resizes"] = (F64)ps.resizes;                // tile size changes made in place (the camera position's sizing, the level)
        pool["resize_repacks"] = (F64)ps.resizeRepacks;   // size changes that found no room and repacked the whole layout
        pool["resize_deferred"] = (F64)ps.resizeDeferred; // size changes held back by the per-frame limit
        pool["lights_without_tile"] = (S32)ps.lightsWithoutTile; // O-4: 0 after the first fill
        pool["tile_cost_ms"] = (F64)ps.costEmaMs;
        pool["last_frame_renders"] = (S32)ps.lastFrameRenders;
        pool["max_stale_frames"] = (S32)ps.maxStaleFrames; // the oldest pending tile (a change touched it, not yet redrawn), in frames; 0 in a static scene (the scheduler bounds it)
        pool["last_frame_render_ms"] = (F64)ps.lastFrameRenderMs;
        LLSD tiles = LLSD::emptyArray();
        const bool with_crc = !req.has("crc") || req["crc"].asBoolean(); // on unless asked off: a read-back of each tile
        for (const LLProjectorShadows::TileInfo& t : gPipeline.mProjectorShadows.getTiles())
        {
            LLSD tile;
            tile["key"] = (S32)t.key;
            tile["lamp"] = (S32)t.lamp;           // the lamp's id; a wide lamp's cells share it (key = lamp << 4 | cell)
            tile["cell"] = (S32)t.cell;           // llprojectorpool::cellIndex: row * 4 + column on the lamp's image plane (0 for one tile)
            tile["size"] = (S32)t.size;
            tile["x"] = (S32)t.x;
            tile["y"] = (S32)t.y;
            tile["drawn_frame"] = (S32)t.drawnFrame;
            tile["cam_dist"] = (F64)t.camDist;
            tile["pending"] = t.pending;
            tile["avatar"] = t.avatar;            // an avatar is in its light, or its last draw had one: its layer is redrawn on its own schedule; the CRC reads S, which holds no avatar
            tile["pose_stale"] = t.poseStale;     // the lamp's pose differs from the one the tile was drawn with
            tile["lamp_moving"] = t.lampMoving;   // the lamp's pose changed since the last frame
            if (with_crc && t.size && t.drawnFrame)   // GL 4.5 core, in the OpenGL 4.6 floor
            {
                std::vector<F32> texels((size_t)t.size * t.size);
                glGetTextureSubImage(gPipeline.mProjectorShadows.atlasS(), 0, (GLint)t.x, (GLint)t.y, 0, (GLsizei)t.size, (GLsizei)t.size, 1,
                                     GL_DEPTH_COMPONENT, GL_FLOAT, (GLsizei)(texels.size() * sizeof(F32)), texels.data());
                tile["crc32"] = (S32)crc32_of(texels.data(), texels.size() * sizeof(F32));
            }
            tiles.append(tile);
        }
        pool["tiles"] = tiles;  // per tile: key, size, drawn_frame, pending, avatar, pose_stale, lamp_moving, and (unless "crc": false) the crc32 of its texels in S
        LLSD causes = LLSD::emptyMap();   // renders by the cause that made the tile stale (new, lod_up, add_remove, edit, mover, lamp, avatar, lowres, other)
        for (size_t c = 0; c < llprojectorpool::kCauseCount; ++c)
        {
            causes[llprojectorpool::causeName(llprojectorpool::Cause(c))] = (F64)ps.causes.n[c];
        }
        pool["rerender_causes"] = causes;
        r["pool"] = pool;

        if (req.has("reset_ages") && req["reset_ages"].asBoolean())
        {
            gPipeline.mProjectorShadows.resetAgeStats();
        }

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
