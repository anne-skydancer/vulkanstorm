// Shared reference avatar-render-complexity cost arithmetic. LGPL-2.1.
#pragma once
#include "llpartdata.h"
#include "llmath.h"
inline F32 vs_volume_render_cost(U32 num_triangles, U32 planar, U32 animtex, U32 alpha, U32 invisi, U32 glow, U32 bump, U32 shiny,
                                 U32 weighted_mesh, U32 flexi, U32 particles, U32 produces_light, U32 media_faces,
                                 const LLPartSysData* particleData, bool animatedRoot)
{
    // per-prim costs
    static const U32 ARC_PARTICLE_COST   = 1;    // determined experimentally
    static const U32 ARC_PARTICLE_MAX    = 2048; // default values
    static const U32 ARC_LIGHT_COST      = 500;  // static cost for light-producing prims
    static const U32 ARC_MEDIA_FACE_COST = 1500; // static cost per media-enabled face

    // per-prim multipliers
    static const F32 ARC_GLOW_MULT     = 1.5f;  // tested based on performance
    static const F32 ARC_BUMP_MULT     = 1.25f; // tested based on performance
    static const F32 ARC_FLEXI_MULT    = 5;     // tested based on performance
    static const F32 ARC_SHINY_MULT    = 1.6f;  // tested based on performance
    static const F32 ARC_INVISI_COST   = 1.2f;  // tested based on performance
    static const F32 ARC_WEIGHTED_MESH = 1.2f;  // tested based on performance

    static const F32 ARC_PLANAR_COST   = 1.0f; // tested based on performance to have negligible impact
    static const F32 ARC_ANIM_TEX_COST = 4.f;  // tested based on performance
    static const F32 ARC_ALPHA_COST    = 4.f;  // 4x max - based on performance

    F32 shame = 0;
    // shame currently has the "base" cost of 1 point per 15 triangles, min 2.
    shame = num_triangles * 5.f;
    shame = shame < 2.f ? 2.f : shame;

    // multiply by per-face modifiers
    if (planar)
    {
        shame *= planar * ARC_PLANAR_COST;
    }

    if (animtex)
    {
        shame *= animtex * ARC_ANIM_TEX_COST;
    }

    if (alpha)
    {
        shame *= alpha * ARC_ALPHA_COST;
    }

    if (invisi)
    {
        shame *= invisi * ARC_INVISI_COST;
    }

    if (glow)
    {
        shame *= glow * ARC_GLOW_MULT;
    }

    if (bump)
    {
        shame *= bump * ARC_BUMP_MULT;
    }

    if (shiny)
    {
        shame *= shiny * ARC_SHINY_MULT;
    }

    // multiply shame by multipliers
    if (weighted_mesh)
    {
        shame *= weighted_mesh * ARC_WEIGHTED_MESH;
    }

    if (flexi)
    {
        shame *= flexi * ARC_FLEXI_MULT;
    }

    // add additional costs
    if (particles && particleData)
    {
        const LLPartSysData* part_sys_data = particleData;
        const LLPartData*    part_data     = &particleData->mPartData;
        U32                  num_particles = (U32)(part_sys_data->mBurstPartCount * llceil(part_data->mMaxAge / part_sys_data->mBurstRate));
        num_particles                      = num_particles > ARC_PARTICLE_MAX ? ARC_PARTICLE_MAX : num_particles;
        F32 part_size =
            (llmax(part_data->mStartScale[0], part_data->mEndScale[0]) + llmax(part_data->mStartScale[1], part_data->mEndScale[1])) / 2.f;
        shame += num_particles * part_size * ARC_PARTICLE_COST;
    }

    if (produces_light)
    {
        shame += ARC_LIGHT_COST;
    }

    if (media_faces)
    {
        shame += media_faces * ARC_MEDIA_FACE_COST;
    }

    // Streaming cost for animated objects includes a fixed cost
    // per linkset. Add a corresponding charge here translated into
    // triangles, but not weighted by any graphics properties.
    if (animatedRoot)
    {
        shame += (15.0f / 0.06) * 5.0f;
    }

    return shame;
}
