// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// OpenGL submission instrumentation (development builds only, -DVKS_GL_STATS=ON).
//
// Counts, per view and pass and per frame, what the renderer asks of the GL driver: draws, buffer and
// attribute rebinding, texture binds (and how many were redundant), uniform uploads (skin palettes
// included), buffer uploads, matrix syncs, state toggles, occlusion queries, and how many consecutive draws
// differ only in their vertex buffer (the "mergeable run" length). GPU time per pass comes from timestamp
// query pairs in a four-frame ring, read without waiting. See doc/gl/instrumentation.md.
//
// Off (the default, and always on release): every macro below expands to nothing. On but not collecting:
// one pointer test per hook site. Counters are exclusive (attributed to the innermost scope); CPU and GPU
// times are inclusive of nested scopes.
#pragma once

#include <cstdint>
#include <cstring>

#ifndef VKS_GL_STATS
#define VKS_GL_STATS 0
#endif

// ---------------------------------------------------------------------------------------------------
// Pure image comparison (no GL). Also compiled when the instrumentation is off so a test can use it.
// ---------------------------------------------------------------------------------------------------
namespace LLGLSubmitImage
{
    struct Diff
    {
        uint64_t pixels = 0;            // pixels compared
        uint64_t mismatchedPixels = 0;  // pixels with at least one differing channel
        uint32_t maxDiff[4] = {0, 0, 0, 0}; // per channel: |a-b| for 8 bit, ulp distance for float32
        int32_t  firstX = -1;
        int32_t  firstY = -1;
        bool identical() const { return mismatchedPixels == 0; }
    };

    // Maps a float32 bit pattern to an integer that increases with the float value, so the difference of
    // two mapped values is the distance in representable floats (+0 and -0 are one apart).
    inline uint32_t orderedFloatBits(uint32_t bits)
    {
        return (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
    }

    // Bitwise compare of two images of 'channels' (1..4) channels each, stored interleaved row after row.
    // float32: 32 bit channels compared as bit patterns (NaNs with different payloads differ); otherwise 8 bit.
    inline Diff compare(const void* a, const void* b, uint32_t width, uint32_t height, uint32_t channels, bool float32)
    {
        Diff d;
        d.pixels = (uint64_t)width * height;
        if (channels == 0 || channels > 4)
        {
            d.mismatchedPixels = d.pixels;
            return d;
        }
        const size_t stride = float32 ? 4 : 1;
        const uint8_t* pa = static_cast<const uint8_t*>(a);
        const uint8_t* pb = static_cast<const uint8_t*>(b);
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const size_t off = ((size_t)y * width + x) * channels * stride;
                bool differs = false;
                for (uint32_t c = 0; c < channels; ++c)
                {
                    uint32_t da = 0;
                    if (float32)
                    {
                        uint32_t ba, bb;
                        std::memcpy(&ba, pa + off + c * 4, 4);
                        std::memcpy(&bb, pb + off + c * 4, 4);
                        if (ba == bb)
                        {
                            continue;
                        }
                        const uint32_t oa = orderedFloatBits(ba), ob = orderedFloatBits(bb);
                        da = oa > ob ? oa - ob : ob - oa;
                    }
                    else
                    {
                        const uint8_t ba = pa[off + c], bb = pb[off + c];
                        if (ba == bb)
                        {
                            continue;
                        }
                        da = ba > bb ? ba - bb : bb - ba;
                    }
                    differs = true;
                    if (da > d.maxDiff[c])
                    {
                        d.maxDiff[c] = da;
                    }
                }
                if (differs)
                {
                    if (d.mismatchedPixels == 0)
                    {
                        d.firstX = (int32_t)x;
                        d.firstY = (int32_t)y;
                    }
                    ++d.mismatchedPixels;
                }
            }
        }
        return d;
    }
}

#if VKS_GL_STATS

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/json.hpp>

// One entry per counter: the struct field, the JSON key, and the sum over the records all use this list.
#define VKS_GLSTAT_COUNTER_LIST(X) \
    X(draws)              /* glDraw* calls (drawRange, drawRangeFast, drawArrays) */ \
    X(drawIndices)        /* indices (or array vertices) submitted */ \
    X(drawTriangles)      /* triangles submitted (GL_TRIANGLES draws only) */ \
    X(drawArraysCalls)    /* of draws: glDrawArrays */ \
    X(vbSetBuffer)        /* LLVertexBuffer::setBuffer calls */ \
    X(vbChanges)          /* ... of which rebound the array buffer */ \
    X(attribRespecs)      /* setupVertexBuffer calls (a full attribute respecification) */ \
    X(attribPointers)     /* glVertexAttrib(I)Pointer calls */ \
    X(iboBinds)           /* element array buffer binds in setBuffer */ \
    X(shaderBinds)        /* LLGLSLShader::bind that changed the program */ \
    X(attribToggles)      /* glEnable/DisableVertexAttribArray */ \
    X(texBindFast)        /* LLTexUnit::bindFast (unconditional glBindTexture) */ \
    X(texBindFastRedundant) /* ... of which the name was already on the unit */ \
    X(texBindOther)       /* bind/bindManual calls that reached glBindTexture */ \
    X(texBindOtherFiltered) /* bind/bindManual calls skipped by the unit cache */ \
    X(texGLBind)          /* glBindTexture calls issued by LLTexUnit */ \
    X(texGLActive)        /* glActiveTexture calls issued by LLTexUnit */ \
    X(uniformRequests)    /* LLGLSLShader::uniform* calls */ \
    X(uniformGL)          /* glUniform* calls that reached GL */ \
    X(uniformArrayGL)     /* ... with count > 1 */ \
    X(uniformMatrixGL)    /* ... matrices (incl. palettes) */ \
    X(uniformBytes)       /* bytes uploaded by glUniform* */ \
    X(matrixSyncs)        /* syncMatrices calls with a shader bound */ \
    X(mdvSyncs)           /* modelview branch taken (normal, MVP, inverse recomputed or uploaded) */ \
    X(projSyncs)          /* projection branch taken */ \
    X(inverseMatrices)    /* glm::inverse evaluations in syncMatrices */ \
    X(modelMatrixCalls)   /* LLRenderPass::applyModelMatrix calls */ \
    X(modelMatrixChanges) /* ... that changed the matrix pointer */ \
    X(paletteUploads)     /* skin palette uploads (glUniformMatrix3x4fv) */ \
    X(paletteBytes)       /* bytes in those uploads */ \
    X(paletteDistinct)    /* distinct (avatar, skin) pairs uploaded (derived) */ \
    X(vbosDistinct)       /* distinct array buffers set (derived) */ \
    X(bufAllocHit)        /* VBO pool allocation served from the pool */ \
    X(bufAllocMiss)       /* ... missed: glBufferData */ \
    X(bufSubData)         /* glBufferSubData calls */ \
    X(bufSubDataBytes)    /* bytes in those calls */ \
    X(bufAllocMissBytes)  /* bytes allocated by those glBufferData calls */ \
    X(flushBuffers)       /* LLVertexBuffer::flushBuffers calls */ \
    X(stateToggles)       /* glEnable/glDisable that reached GL through LLGLState */ \
    X(occIssued)          /* occlusion queries issued */ \
    X(occPolled)          /* occlusion results polled */ \
    X(occBlocking)        /* occlusion results read with a blocking GL_QUERY_RESULT */ \
    X(gltfBinds)          /* LLFetchedGLTFMaterial::bind calls */ \
    X(gltfSameAsLast)     /* ... for the same material as the previous call */ \
    X(runs)               /* mergeable runs: draws separated by no shader, texture, uniform or state change */ \
    X(mergeableDraws)     /* draws - runs: draws that could join the previous draw's run */

struct LLGLSubmitCounters
{
#define VKS_GLSTAT_FIELD(name) uint64_t name = 0;
    VKS_GLSTAT_COUNTER_LIST(VKS_GLSTAT_FIELD)
#undef VKS_GLSTAT_FIELD
    uint64_t runHist[6] = {0, 0, 0, 0, 0, 0}; // runs of length 1, 2, 3-4, 5-8, 9-16, 17+
    void add(const LLGLSubmitCounters& o);
};

class LLGLSubmitStats
{
public:
    struct Record
    {
        std::string view;               // "frame", "main", "sun cascade 2", ...
        std::string pass;               // "" for the view itself, else the path of pass scopes inside the view
        LLGLSubmitCounters c;
        uint64_t calls = 0;             // times the scope was entered
        double cpuMs = 0.0;             // inclusive
        double gpuMs = 0.0;             // inclusive; valid when the frame's GPU results were read
        std::unordered_set<uint64_t> palettes;
        std::unordered_set<uint32_t> vbos;
    };

    struct Frame
    {
        uint64_t number = 0;
        double cpuMs = 0.0;
        bool gpuTimed = false;
        bool gpuResolved = false;
        bool gpuTruncated = false;
        bool inWindow = false;      // counted into the current window
        std::vector<std::unique_ptr<Record>> records;
        std::unordered_map<std::string, Record*> byKey;
        struct Inst { Record* rec; uint32_t begin; uint32_t end; };
        std::vector<Inst> insts;
        std::unordered_set<uint64_t> palettesAll;
    };

    // The record that hooks add to; null when not collecting.
    static Record* sCur;
    static std::thread::id sMainThread;
    static bool onMain() { return std::this_thread::get_id() == sMainThread; }

    // A frame: collect == false makes every hook a no-op for this frame. Returns true when this call started the frame
    // (and so owns its end); false when a frame is already open (a nested display call), which it leaves alone.
    static bool beginFrame(bool collect, bool gpu);
    static void endFrame();   // idempotent
    static bool collecting() { return sCur != nullptr; }

    // Scopes. A view starts a new group of passes; a pass is a path element inside the current view.
    static void enterView(const char* name, int index);
    static void enterPass(const char* name, int index);
    static void leave();

    // Hooks with logic beyond a counter bump.
    static void noteDraw(uint32_t indices);                 // after the state of the draw was set
    static void noteStateChange() { if (sCur && onMain()) { ++sEpoch; } }
    static void notePalette(const void* avatar, uint64_t skinHash, uint32_t bytes);
    static void noteVBO(uint32_t name);
    static void noteUniform(uint32_t bytes, uint32_t count, bool matrix);
    static uint32_t sEpoch;

    // Windows of N frames, averaged: what the LEAP client reads.
    static void windowBegin(uint32_t frames, bool gpu);
    static void windowStop();
    static bool windowActive() { return sWindowTarget != 0 && sWindowCollected < sWindowTarget; }
    static bool windowWantsGpu() { return sWindowGpu; }
    static uint32_t windowTarget() { return sWindowTarget; }
    static uint32_t windowCollected() { return sWindowCollected; }
    static uint32_t windowGpuResolved() { return sWindowGpuFrames; }

    // Results. 'blockForGpu' waits for the GPU timers of the frames in flight (profile frames, tests).
    static void resolveGpu(bool blockForGpu);
    static std::shared_ptr<Frame> lastFrame() { return sLast; }
    static boost::json::object frameToJson(const Frame& f);
    static boost::json::object windowToJson();      // per-frame means over the window
    static boost::json::object summaryOfFrame(const Frame& f);
    static boost::json::object summaryOfWindow();   // small: totals and views only

    // RAII for the hooks.
    struct FrameScope
    {
        FrameScope(bool collect, bool gpu) : mOwns(beginFrame(collect, gpu)) {}
        ~FrameScope() { if (mOwns) { endFrame(); } }
        bool mOwns;
    };
    struct ViewScope
    {
        explicit ViewScope(const char* name, int index = -1) { if (sCur) { enterView(name, index); mOn = true; } }
        ~ViewScope() { if (mOn) { leave(); } }
        bool mOn = false;
    };
    struct PassScope
    {
        explicit PassScope(const char* name, int index = -1) { if (sCur) { enterPass(name, index); mOn = true; } }
        ~PassScope() { if (mOn) { leave(); } }
        bool mOn = false;
    };

private:
    static std::shared_ptr<Frame> sLast;
    static uint32_t sWindowTarget, sWindowCollected, sWindowGpuFrames;
    static bool sWindowGpu;
    static void closeRun();
};

#define VKS_GLSTAT_CAT2(a, b) a##b
#define VKS_GLSTAT_CAT(a, b) VKS_GLSTAT_CAT2(a, b)
// Bump a counter of the current record.
#define VKS_GLSTAT(field, n) do { if (LLGLSubmitStats::sCur && LLGLSubmitStats::onMain()) { LLGLSubmitStats::sCur->c.field += (n); } } while (0)
#define VKS_GLSTAT_INC(field) VKS_GLSTAT(field, 1)
// A change of GL state between draws: ends a mergeable run.
#define VKS_GLSTAT_STATE_CHANGE() LLGLSubmitStats::noteStateChange()
#define VKS_GLSTAT_VIEW(...) LLGLSubmitStats::ViewScope VKS_GLSTAT_CAT(vks_glstat_view_, __LINE__)(__VA_ARGS__)
#define VKS_GLSTAT_PASS(...) LLGLSubmitStats::PassScope VKS_GLSTAT_CAT(vks_glstat_pass_, __LINE__)(__VA_ARGS__)
#define VKS_GLSTAT_DRAW(indices) do { if (LLGLSubmitStats::sCur) { LLGLSubmitStats::noteDraw(indices); } } while (0)
#define VKS_GLSTAT_PALETTE(avatar, skinhash, bytes) do { if (LLGLSubmitStats::sCur) { LLGLSubmitStats::notePalette(avatar, skinhash, bytes); } } while (0)
#define VKS_GLSTAT_VBO(name) do { if (LLGLSubmitStats::sCur) { LLGLSubmitStats::noteVBO(name); } } while (0)
#define VKS_GLSTAT_UNIFORM(bytes, count, matrix) do { if (LLGLSubmitStats::sCur) { LLGLSubmitStats::noteUniform((uint32_t)(bytes), (uint32_t)(count), (matrix)); } } while (0)
#define VKS_GLSTAT_ACTIVE() (LLGLSubmitStats::sCur != nullptr)

#else // !VKS_GL_STATS

#define VKS_GLSTAT(field, n) do { } while (0)
#define VKS_GLSTAT_INC(field) do { } while (0)
#define VKS_GLSTAT_STATE_CHANGE() do { } while (0)
#define VKS_GLSTAT_VIEW(...) do { } while (0)
#define VKS_GLSTAT_PASS(...) do { } while (0)
#define VKS_GLSTAT_DRAW(indices) do { } while (0)
#define VKS_GLSTAT_PALETTE(avatar, skinhash, bytes) do { } while (0)
#define VKS_GLSTAT_VBO(name) do { } while (0)
#define VKS_GLSTAT_UNIFORM(bytes, count, matrix) do { } while (0)
#define VKS_GLSTAT_ACTIVE() (false)

#endif // VKS_GL_STATS
