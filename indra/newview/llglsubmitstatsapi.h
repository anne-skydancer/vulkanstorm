// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Viewer side of the OpenGL submission instrumentation (development builds only, -DVKS_GL_STATS=ON):
// the per-frame glue (settings), the Frame Profile "submit" section, the in-frame identical-image check
// ("render the pass twice and compare") and the LEAP API "GLSubmitStats". See doc/gl/instrumentation.md.
#pragma once

#include "llglsubmitstats.h"

#if VKS_GL_STATS

#include <boost/json.hpp>
#include <string>
#include <vector>
#include "stdtypes.h"

class LLRenderTarget;

namespace LLGLSubmitStatsApi
{
    // The frame glue: starts the stats frame of display() / display_startup() and ends it with the scope.
    struct DisplayFrame
    {
        explicit DisplayFrame(bool profile_frame);
        ~DisplayFrame();
        bool mOwnsStats = false;    // this call started the statistics frame
        bool mOwnsCompare = false;  // this call armed the identical-image check
    };

    // Called from display() when a Frame Profile is written: adds the "submit" section.
    void addToProfile(boost::json::object& stats);
}

namespace LLGLSubmitCompare
{
    enum PassId : S32
    {
        PASS_ALL = -1,
        PASS_OFF = 0,
        PASS_GBUFFER = 1,       // the main view's G-buffer: every colour attachment and the depth
        PASS_SUN_CASCADE0 = 2,  // 2..5: sun cascade depth maps
        PASS_SPOT_SHADOW0 = 6,  // 6..7: spot shadow depth maps
        PASS_POST_DEFERRED = 8, // alpha / glow / fullbright geometry drawn over the lit scene (sorted alpha only)
        PASS_COUNT = 9
    };

    // True during the second render of a compared pass: code that must not repeat (culling, sorting,
    // occlusion queries) tests it.
    bool replaying();

    // The submission path in force: RenderGLSubmitPath for the pass being rendered normally and for the
    // second render of a compared pass, 0 (today's path) for the first one. Later stages read it.
    S32 activePath();

    // for (Run run(pass, target); run.next(); ) { ...the pass... }
    // Normally the body runs once. When the check is armed for the pass (RenderGLSubmitCompare), it runs twice:
    // the target's attachments are read back after each run and compared bit for bit; with 'restore_inputs'
    // the target's colour and depth are put back before the second run (passes that draw over existing content).
    class Run
    {
    public:
        Run(S32 pass, LLRenderTarget* target, bool restore_inputs = false);
        ~Run();
        bool next();
    private:
        struct Impl;
        Impl* mImpl;
    };
}

#define VKS_GLSTAT_DISPLAY_FRAME(profile) LLGLSubmitStatsApi::DisplayFrame vks_glstat_display_frame_(profile)
#define VKS_GLSTAT_PROFILE(stats) LLGLSubmitStatsApi::addToProfile(stats)
#define VKS_GLCMP_REPLAY() LLGLSubmitCompare::replaying()
#define VKS_GLCMP_BEGIN(pass, target, restore) for (LLGLSubmitCompare::Run vks_glcmp_run_((pass), (target), (restore)); vks_glcmp_run_.next(); ) {
#define VKS_GLCMP_END() }

#else // !VKS_GL_STATS

#define VKS_GLSTAT_DISPLAY_FRAME(profile) do { } while (0)
#define VKS_GLSTAT_PROFILE(stats) do { } while (0)
#define VKS_GLCMP_REPLAY() (false)
#define VKS_GLCMP_BEGIN(pass, target, restore) {
#define VKS_GLCMP_END() }

#endif // VKS_GL_STATS
