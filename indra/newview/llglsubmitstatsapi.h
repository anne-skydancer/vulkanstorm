// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Viewer side of the OpenGL submission instrumentation (development builds only, -DVKS_GL_STATS=ON):
// the per-frame glue (settings), the Frame Profile "submit" section, the in-frame identical-image check
// ("render the pass twice and compare"), the copy census of the faces the draws carry, and the LEAP API "GLSubmitStats".
// See doc/gl/instrumentation.md.
#pragma once

#include "llglsubmitstats.h"

#if VKS_GL_STATS

#include <boost/json.hpp>
#include <memory>
#include <string>
#include <vector>
#include "stdtypes.h"

class LLRenderTarget;
class LLDrawInfo;
class LLFace;
class LLUUID;

namespace LLGLSubmitStatsApi
{
    // The frame glue: starts the stats frame of display() / display_startup() and ends it with the scope (or, inside the
    // main loop's frame, hands the end over to it).
    struct DisplayFrame
    {
        explicit DisplayFrame(bool profile_frame);
        ~DisplayFrame();
        bool mOwnsStats = false;    // this call started the statistics frame
        bool mOwnsCompare = false;  // this call armed the identical-image check
    };

    // The main loop's frame (llappviewer.cpp): the statistics frame and the compare arming that display() begins stay open
    // until end(), after the reflection probe update that follows display(), so windows count the probe face and its
    // cascades and compare 13 reaches a reflection face. The snapshot floaters' display() calls come after end() and get
    // statistics frames of their own; only the main loop's display() begins or carries a compare arming
    // (LLGLSubmitMath::CompareFrames). When display() began no frame (not rendering), end() has nothing to end.
    struct MainLoopFrame
    {
        MainLoopFrame();
        ~MainLoopFrame() { end(); }
        MainLoopFrame(const MainLoopFrame&) = delete;
        MainLoopFrame& operator=(const MainLoopFrame&) = delete;
        void end();
        bool mOpen = false;
    };

    // Called from display() when a Frame Profile is written: adds the "submit" section.
    void addToProfile(boost::json::object& stats);

    // The copy census (G5b-0, X1-0). While it records (LEAP "censusRecord", which also rebuilds every draw item so that
    // each has its record), registerFace records what each face of a draw item would be as a copy (its geometry cache
    // key, why it stays baked, its size, bytes and last bake time); the draws credit it in census windows; the LEAP
    // operation "census" sums the resident faces. Off (the default), registerFace and the bake pay one test each. Main
    // thread only (registerFace, the bake and the draws are).
    extern bool sCensusRecording;
    inline bool censusRecording() { return sCensusRecording; }
    void censusFace(LLDrawInfo& info, LLFace* face, U32 pass_type);
    // One face's record as censusFace makes it (selfBench times it on faces of the scene).
    LLGLSubmitMath::CensusFace makeCensusFace(LLFace* face, const LLUUID& material_id, U32 pass_type);
    // Drops the bake time waiting for the next record (when the census stops, and after selfBench's timer).
    void forgetCensusBake();

    // Times one face's bake (getGeometryVolume) for the census record that registerFace makes next.
    struct CensusBake
    {
        explicit CensusBake(const LLFace* face) : mFace(face), mStart(censusRecording() ? start() : 0) {}
        ~CensusBake() { if (mStart) { finish(); } }
        CensusBake(const CensusBake&) = delete;
        CensusBake& operator=(const CensusBake&) = delete;
        static U64 start();     // 0 off the main thread
        void finish();
        const LLFace* mFace;
        U64 mStart;
    };
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
        // 9..12 are reserved for G0b: the UI tee, the stochastic targets, the projector tiles' two layers
        PASS_PROBE_FACE = 13,   // one reflection or hero probe face's G-buffer (every attachment and the depth)
        PASS_HUD = 14,          // the HUD's geometry over the back buffer (colour and depth)
        PASS_IMPOSTOR = 15,     // one avatar impostor: its G-buffer and both post-deferred renders, into its target
        PASS_COUNT = 16
    };

    // Whether a compare id has a site (the reserved ids have none yet).
    bool implemented(S32 pass);

    // Why a pass that ends with renderGeomPostDeferred's tail (highlights, debug displays) cannot be rendered twice
    // alike this frame, or nullptr: the tail clears the highlight faces, rebuilds and redraws selected faces, and draws
    // debug displays and update blips that age (b-28 F9). Passes 8, 14 and 15 are not compared then. The compare sites
    // pass it as a function (NotRun): it is asked only when the pass is armed.
    const char* tailNotRepeatable();
    using NotRun = const char* (*)();

    // True during the second render of a compared pass: code that must not repeat (culling, sorting,
    // occlusion queries) tests it.
    bool replaying();

    // The fade of a nearby light: setupHWLights advances it by the frame time on every call, so the second render
    // would see a different light. The first render records the fade it used, the second uses the same and does not
    // advance. 'advance' is set to whether the caller advances the fade.
    F32 lightFade(F32 fade, bool& advance);

    // The submission path in force: RenderGLSubmitPath for the pass being rendered normally and for the
    // second render of a compared pass, 0 (today's path) for the first one. Later stages read it.
    S32 activePath();

    // The default framebuffer (the back buffer, where the HUD is drawn) as the compared target.
    struct BackBuffer {};

    // for (Run run(pass, target); run.next(); ) { ...the pass... }
    // Normally the body runs once. When the check is armed for the pass (RenderGLSubmitCompare), it runs twice:
    // the target's attachments are read back after each run and compared bit for bit; with 'restore_inputs'
    // the target's colour and depth are put back before the second run (passes that draw over existing content).
    // 'not_run' (asked only when the pass is armed) returning non-null: the pass is not compared this frame, for that
    // reason. The back buffer is read with glReadPixels and restored from a copy made by a blit of the same formats.
    // A first readback that fails, or inputs that cannot be put back, end the run as not run without a second render:
    // the target keeps the first render, as it would normally.
    class Run
    {
    public:
        Run(S32 pass, LLRenderTarget* target, bool restore_inputs = false, NotRun not_run = nullptr);
        Run(S32 pass, BackBuffer, bool restore_inputs, NotRun not_run = nullptr);
        ~Run();
        Run(const Run&) = delete;
        Run& operator=(const Run&) = delete;
        bool next();
    private:
        struct Impl;
        bool arm(S32 pass, NotRun not_run);
        int mStep = 0;
        std::unique_ptr<Impl> mImpl;   // only when the pass is armed and has a target
    };
}

#define VKS_GLSTAT_DISPLAY_FRAME(profile) LLGLSubmitStatsApi::DisplayFrame vks_glstat_display_frame_(profile)
#define VKS_GLSTAT_MAIN_LOOP_FRAME() LLGLSubmitStatsApi::MainLoopFrame vks_glstat_main_loop_frame_
#define VKS_GLSTAT_MAIN_LOOP_FRAME_END() vks_glstat_main_loop_frame_.end()
#define VKS_GLSTAT_PROFILE(stats) LLGLSubmitStatsApi::addToProfile(stats)
#define VKS_GLSTAT_CENSUS_FACE(info, face, type) do { if (LLGLSubmitStatsApi::censusRecording()) { LLGLSubmitStatsApi::censusFace((info), (face), (type)); } } while (0)
#define VKS_GLSTAT_CENSUS_BAKE(face) LLGLSubmitStatsApi::CensusBake vks_glstat_census_bake_(face)
#define VKS_GLCMP_REPLAY() LLGLSubmitCompare::replaying()
#define VKS_GLCMP_LIGHT_FADE(fade, advance) ((fade) = LLGLSubmitCompare::lightFade((fade), (advance)))
#define VKS_GLCMP_BEGIN(pass, target, restore) for (LLGLSubmitCompare::Run vks_glcmp_run_((pass), (target), (restore)); vks_glcmp_run_.next(); ) {
#define VKS_GLCMP_BEGIN_IF(pass, target, restore, not_run) for (LLGLSubmitCompare::Run vks_glcmp_run_((pass), (target), (restore), (not_run)); vks_glcmp_run_.next(); ) {
#define VKS_GLCMP_BEGIN_BACK_BUFFER(pass, restore, not_run) for (LLGLSubmitCompare::Run vks_glcmp_run_((pass), LLGLSubmitCompare::BackBuffer{}, (restore), (not_run)); vks_glcmp_run_.next(); ) {
#define VKS_GLCMP_END() }

#else // !VKS_GL_STATS

#define VKS_GLSTAT_DISPLAY_FRAME(profile) do { } while (0)
#define VKS_GLSTAT_MAIN_LOOP_FRAME() do { } while (0)
#define VKS_GLSTAT_MAIN_LOOP_FRAME_END() do { } while (0)
#define VKS_GLSTAT_PROFILE(stats) do { } while (0)
#define VKS_GLSTAT_CENSUS_FACE(info, face, type) do { } while (0)
#define VKS_GLSTAT_CENSUS_BAKE(face) do { } while (0)
#define VKS_GLCMP_REPLAY() (false)
#define VKS_GLCMP_LIGHT_FADE(fade, advance) do { } while (0)
#define VKS_GLCMP_BEGIN(pass, target, restore) {
#define VKS_GLCMP_BEGIN_IF(pass, target, restore, not_run) {
#define VKS_GLCMP_BEGIN_BACK_BUFFER(pass, restore, not_run) {
#define VKS_GLCMP_END() }

#endif // VKS_GL_STATS
