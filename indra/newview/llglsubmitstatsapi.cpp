// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Viewer side of the OpenGL submission instrumentation. Compiled to nothing unless VKS_GL_STATS is set.

#include "llviewerprecompiledheaders.h"
#include "llglsubmitstatsapi.h"

#if VKS_GL_STATS

#include <atomic>
#include <chrono>
#include <fstream>

#include "lleventapi.h"
#include "llfile.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llsd.h"
#include "llsdjson.h"
#include "llsdutil.h"
#include "llviewercontrol.h"
#include "llappviewer.h"
#include "lldir.h"
#include "lldate.h"

extern bool gShaderProfileFrame; // llviewerdisplay.cpp

namespace
{
    // ---- the compare check -------------------------------------------------------------------------------

    S32 sArmed = LLGLSubmitCompare::PASS_OFF;   // the pass(es) to compare this frame
    bool sInRun = false;                        // a compared pass is being rendered (no nesting)
    bool sReplay = false;                       // ... for the second time
    S32 sInject = 0;
    U64 sFramesSeen = 0;                        // display() and display_startup() frames, collected or not

    struct Capture
    {
        std::string name;
        U32 texture = 0;
        S32 width = 0;
        S32 height = 0;
        U32 channels = 4;
        bool depth = false;
        bool f32 = false;
        std::vector<uint8_t> data;
    };

    struct AttachmentResult
    {
        std::string name;
        LLGLSubmitImage::Diff diff;
        bool injected = false;
    };

    struct PassResult
    {
        S32 pass = 0;
        bool ran = false;
        std::string note;
        std::vector<AttachmentResult> attachments;
        bool identical() const
        {
            for (const auto& a : attachments)
            {
                if (!a.diff.identical())
                {
                    return false;
                }
            }
            return ran;
        }
    };

    std::vector<PassResult> sResults;
    U64 sResultsFrame = 0;

    const char* passLabel(S32 pass)
    {
        static const char* const labels[LLGLSubmitCompare::PASS_COUNT] = {
            "off", "G-buffer", "sun cascade 0", "sun cascade 1", "sun cascade 2", "sun cascade 3",
            "spot shadow 0", "spot shadow 1", "post-deferred"};
        return pass >= 0 && pass < LLGLSubmitCompare::PASS_COUNT ? labels[pass] : "?";
    }

    bool armedFor(S32 pass)
    {
        return sArmed != LLGLSubmitCompare::PASS_OFF && !sInRun && (sArmed == LLGLSubmitCompare::PASS_ALL || sArmed == pass);
    }

    struct PixelStore
    {
        GLint pack = 4, unpack = 4;
        PixelStore()
        {
            glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }
        ~PixelStore()
        {
            glPixelStorei(GL_PACK_ALIGNMENT, pack);
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
        }
    };

    // Reads one 2D texture (level 0) as 8 bit RGBA, float RGBA or float depth: lossless for the formats the
    // renderer uses, so equal bits mean equal stored values.
    bool readTexture(Capture& c)
    {
        while (glGetError() != GL_NO_ERROR) {}
        gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, c.texture);
        GLint w = 0, h = 0, rs = 0, gs = 0, bs = 0, as = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        if (!c.depth)
        {
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &rs);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &gs);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &bs);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &as);
        }
        c.width = w;
        c.height = h;
        bool ok = w > 0 && h > 0 && glGetError() == GL_NO_ERROR;
        if (ok)
        {
            c.f32 = c.depth || rs > 8 || gs > 8 || bs > 8 || as > 8;
            c.channels = c.depth ? 1 : 4;
            c.data.assign((size_t)w * h * c.channels * (c.f32 ? 4 : 1), 0);
            glGetTexImage(GL_TEXTURE_2D, 0, c.depth ? GL_DEPTH_COMPONENT : GL_RGBA, c.f32 ? GL_FLOAT : GL_UNSIGNED_BYTE, c.data.data());
            ok = glGetError() == GL_NO_ERROR;
        }
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        if (!ok)
        {
            c.data.clear();
        }
        return ok;
    }

    bool writeTexture(const Capture& c)
    {
        while (glGetError() != GL_NO_ERROR) {}
        gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, c.texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, c.width, c.height, c.depth ? GL_DEPTH_COMPONENT : GL_RGBA,
                        c.f32 ? GL_FLOAT : GL_UNSIGNED_BYTE, c.data.data());
        const bool ok = glGetError() == GL_NO_ERROR;
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        return ok;
    }

    std::vector<Capture> captureTarget(LLRenderTarget& rt)
    {
        std::vector<Capture> out;
        PixelStore store;
        for (U32 i = 0; i < rt.getNumTextures(); ++i)
        {
            Capture c;
            c.name = "color" + std::to_string(i);
            c.texture = rt.getTexture(i);
            if (readTexture(c))
            {
                out.push_back(std::move(c));
            }
            else
            {
                LL_WARNS("GLSubmitCompare") << "attachment " << c.name << " could not be read back" << LL_ENDL;
            }
        }
        if (rt.getDepth())
        {
            Capture c;
            c.name = "depth";
            c.texture = rt.getDepth();
            c.depth = true;
            if (readTexture(c))
            {
                out.push_back(std::move(c));
            }
            else
            {
                LL_WARNS("GLSubmitCompare") << "depth could not be read back" << LL_ENDL;
            }
        }
        return out;
    }

    void flipOneBit(Capture& c)
    {
        // the first channel of the centre pixel, lowest bit: one unit in the last place
        if (c.data.empty())
        {
            return;
        }
        const size_t pixel = ((size_t)(c.height / 2) * c.width + (c.width / 2)) * c.channels;
        const size_t off = pixel * (c.f32 ? 4 : 1);
        c.data[off] ^= 1;
    }

    const char* kTag = "GLSubmitCompare";
}

struct LLGLSubmitCompare::Run::Impl
{
    S32 pass = 0;
    LLRenderTarget* target = nullptr;
    bool restore = false;
    bool active = false;
    int step = 0;
    std::vector<Capture> inputs;
    std::vector<Capture> first;
};

LLGLSubmitCompare::Run::Run(S32 pass, LLRenderTarget* target, bool restore_inputs)
    : mImpl(new Impl)
{
    mImpl->pass = pass;
    mImpl->target = target;
    mImpl->restore = restore_inputs;
    if (!armedFor(pass))
    {
        return;
    }
    PassResult skipped;
    skipped.pass = pass;
    if (!target || target->getUsage() != LLTexUnit::TT_TEXTURE)
    {
        skipped.note = "not run: no plain 2D target this frame (missing, multisampled, or stochastic transparency in use)";
        LL_INFOS(kTag) << passLabel(pass) << ": " << skipped.note << LL_ENDL;
        sResults.push_back(std::move(skipped));
        return;
    }
    mImpl->active = true;
    sInRun = true;
}

LLGLSubmitCompare::Run::~Run()
{
    if (mImpl->active)
    {
        sInRun = false;
        sReplay = false;
    }
    delete mImpl;
}

bool LLGLSubmitCompare::Run::next()
{
    Impl& m = *mImpl;
    if (!m.active)
    {
        return m.step++ == 0;
    }
    switch (m.step++)
    {
    case 0:
        if (m.restore)
        {
            m.inputs = captureTarget(*m.target);
        }
        return true;
    case 1:
        m.first = captureTarget(*m.target);
        sReplay = true;
        if (m.restore)
        {
            for (const Capture& c : m.inputs)
            {
                if (!writeTexture(c))
                {
                    LL_WARNS(kTag) << "could not restore " << c.name << " for the second render" << LL_ENDL;
                }
            }
        }
        return true;
    default:
        break;
    }

    // after the second render
    sReplay = false;
    PassResult res;
    res.pass = m.pass;
    res.ran = true;
    std::vector<Capture> second = captureTarget(*m.target);
    if (second.size() != m.first.size() || m.first.empty())
    {
        res.ran = false;
        res.note = "attachments could not be read back";
    }
    else
    {
        for (size_t i = 0; i < second.size(); ++i)
        {
            AttachmentResult ar;
            ar.name = second[i].name;
            if (sInject && i == 0)
            {
                flipOneBit(second[i]);
                ar.injected = true;
            }
            ar.diff = LLGLSubmitImage::compare(m.first[i].data.data(), second[i].data.data(), (uint32_t)second[i].width,
                                               (uint32_t)second[i].height, second[i].channels, second[i].f32);
            res.attachments.push_back(std::move(ar));
        }
    }
    LL_INFOS(kTag) << passLabel(m.pass) << ": " << (res.ran ? (res.identical() ? "IDENTICAL" : "DIFFERENT") : res.note) << LL_ENDL;
    for (const auto& a : res.attachments)
    {
        LL_INFOS(kTag) << "  " << a.name << ": " << a.diff.mismatchedPixels << " of " << a.diff.pixels << " pixels differ"
                       << (a.diff.identical() ? "" : " (max " + std::to_string(a.diff.maxDiff[0]) + "/" + std::to_string(a.diff.maxDiff[1]) +
                                                   "/" + std::to_string(a.diff.maxDiff[2]) + "/" + std::to_string(a.diff.maxDiff[3]) +
                                                   ", first at " + std::to_string(a.diff.firstX) + "," + std::to_string(a.diff.firstY) + ")")
                       << (a.injected ? " [one bit injected into the second readback]" : "") << LL_ENDL;
    }
    sResults.push_back(std::move(res));
    sInRun = false;
    m.active = false;
    return false;
}

bool LLGLSubmitCompare::replaying()
{
    return sReplay;
}

S32 LLGLSubmitCompare::activePath()
{
    if (sInRun && !sReplay)
    {
        return 0;
    }
    return gSavedSettings.getS32("RenderGLSubmitPath");
}

// ---- the frame glue, the Frame Profile section and the LEAP API ---------------------------------------------

namespace
{
    boost::json::array resultsToJson()
    {
        boost::json::array arr;
        for (const PassResult& r : sResults)
        {
            boost::json::object o;
            o.emplace("pass", (int64_t)r.pass);
            o.emplace("label", passLabel(r.pass));
            o.emplace("ran", r.ran);
            o.emplace("identical", r.identical());
            if (!r.note.empty())
            {
                o.emplace("note", r.note);
            }
            boost::json::array atts;
            for (const AttachmentResult& a : r.attachments)
            {
                boost::json::object ao;
                ao.emplace("name", a.name);
                ao.emplace("pixels", (uint64_t)a.diff.pixels);
                ao.emplace("mismatched_pixels", (uint64_t)a.diff.mismatchedPixels);
                if (!a.diff.identical())
                {
                    ao.emplace("max_diff", boost::json::array{a.diff.maxDiff[0], a.diff.maxDiff[1], a.diff.maxDiff[2], a.diff.maxDiff[3]});
                    ao.emplace("first_x", a.diff.firstX);
                    ao.emplace("first_y", a.diff.firstY);
                }
                if (a.injected)
                {
                    ao.emplace("injected", true);
                }
                atts.emplace_back(std::move(ao));
            }
            o.emplace("attachments", std::move(atts));
            arr.emplace_back(std::move(o));
        }
        return arr;
    }

    // only the headline numbers: the LEAP reply stays small (see scripts/inworld/README.md, "Big replies")
    LLSD headline(const boost::json::object& summary)
    {
        LLSD out = LLSD::emptyMap();
        static const char* const keys[] = {"draws", "drawTriangles", "vbChanges", "attribRespecs", "iboBinds", "shaderBinds",
                                           "texBindFast", "texBindFastRedundant", "texGLBind", "uniformGL", "uniformBytes",
                                           "paletteUploads", "paletteDistinct", "mdvSyncs", "bufSubData", "flushBuffers",
                                           "stateToggles", "occBlocking", "runs", "mergeableDraws", "cpu_ms", "gpu_ms"};
        if (const boost::json::value* totals = summary.if_contains("totals"))
        {
            const boost::json::object& t = totals->as_object();
            LLSD tl = LLSD::emptyMap();
            for (const char* k : keys)
            {
                if (const boost::json::value* v = t.if_contains(k))
                {
                    tl[k] = v->is_int64() ? LLSD((S32)v->as_int64()) : LLSD(v->to_number<double>());
                }
            }
            out["totals"] = tl;
        }
        if (const boost::json::value* views = summary.if_contains("views"))
        {
            LLSD vl = LLSD::emptyArray();
            for (const auto& v : views->as_array())
            {
                const boost::json::object& vo = v.as_object();
                LLSD e = LLSD::emptyMap();
                e["view"] = std::string(vo.at("view").as_string());
                for (const char* k : {"draws", "cpu_ms", "gpu_ms"})
                {
                    if (const boost::json::value* x = vo.if_contains(k))
                    {
                        e[k] = x->is_int64() ? LLSD((S32)x->as_int64()) : LLSD(x->to_number<double>());
                    }
                }
                vl.append(e);
            }
            out["views"] = vl;
        }
        return out;
    }

    std::string defaultDumpPath(const std::string& kind)
    {
        std::string stamp = LLDate::now().toHTTPDateString("%Y%m%d-%H%M%S");
        return gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "gl_submit_" + kind + "_" + stamp + ".json");
    }

    void api_status(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        r["compiled"] = true;
        r["collecting_setting"] = gSavedSettings.getBOOL("RenderGLSubmitStats");
        r["window_target"] = (S32)LLGLSubmitStats::windowTarget();
        r["window_collected"] = (S32)LLGLSubmitStats::windowCollected();
        r["window_gpu_frames"] = (S32)LLGLSubmitStats::windowGpuResolved();
        r["window_active"] = LLGLSubmitStats::windowActive();
        r["compare_armed"] = (S32)sArmed;
        r["compare_results"] = (S32)sResults.size();
        r["frame_count"] = (F64)sFramesSeen;
        if (auto f = LLGLSubmitStats::lastFrame())
        {
            r["last_frame"] = (F64)f->number;
            r["last_frame_gpu_resolved"] = f->gpuResolved;
        }
    }

    void api_window_begin(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const S32 frames = req.has("frames") ? req["frames"].asInteger() : 60;
        if (frames < 1 || frames > 100000)
        {
            return r.error("frames must be 1..100000");
        }
        LLGLSubmitStats::windowBegin((uint32_t)frames, req.has("gpu") ? req["gpu"].asBoolean() : true);
        r["frames"] = frames;
    }

    void api_window_stop(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        LLGLSubmitStats::windowStop();
        r["collected"] = (S32)LLGLSubmitStats::windowCollected();
    }

    void api_summary(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const bool window = !req.has("kind") || req["kind"].asString() == "window";
        if (window)
        {
            if (!LLGLSubmitStats::windowCollected())
            {
                return r.error("no window collected: call windowBegin and wait for window_active to turn false");
            }
            r["summary"] = headline(LLGLSubmitStats::summaryOfWindow());
            r["frames"] = (S32)LLGLSubmitStats::windowCollected();
            r["gpu_frames"] = (S32)LLGLSubmitStats::windowGpuResolved();
        }
        else
        {
            auto f = LLGLSubmitStats::lastFrame();
            if (!f)
            {
                return r.error("no frame collected yet: set RenderGLSubmitStats or call windowBegin");
            }
            r["summary"] = headline(LLGLSubmitStats::summaryOfFrame(*f));
        }
    }

    void api_dump(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const bool window = !req.has("kind") || req["kind"].asString() == "window";
        boost::json::object doc;
        if (window)
        {
            if (!LLGLSubmitStats::windowCollected())
            {
                return r.error("no window collected");
            }
            doc = LLGLSubmitStats::windowToJson();
        }
        else
        {
            auto f = LLGLSubmitStats::lastFrame();
            if (!f)
            {
                return r.error("no frame collected yet");
            }
            doc = LLGLSubmitStats::frameToJson(*f);
        }
        doc.emplace("compare", resultsToJson());
        const std::string path = req.has("path") ? req["path"].asString() : defaultDumpPath(window ? "window" : "frame");
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return r.error("cannot write " + path);
        }
        out << doc;
        out.close();
        r["path"] = path;
    }

    void api_compare_arm(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const S32 pass = req.has("pass") ? req["pass"].asInteger() : LLGLSubmitCompare::PASS_ALL;
        if (pass < LLGLSubmitCompare::PASS_ALL || pass >= LLGLSubmitCompare::PASS_COUNT)
        {
            return r.error("pass must be -1 (all) or 1..8");
        }
        if (req.has("inject"))
        {
            gSavedSettings.setS32("RenderGLSubmitCompareInject", req["inject"].asInteger());
        }
        gSavedSettings.setS32("RenderGLSubmitCompare", pass == 0 ? 0 : pass);
        r["armed"] = pass;
    }

    void api_compare_results(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        r["armed"] = (S32)sArmed;
        r["pending"] = gSavedSettings.getS32("RenderGLSubmitCompare");
        r["frame"] = (F64)sResultsFrame;
        r["results"] = LlsdFromJson(resultsToJson());
    }

    // The cost of the hooks in this very binary, measured in place: the mix of hooks one draw runs (about 20 macro
    // sites), with the statistics not collecting (the cost every dev build pays: a pointer test per site), collecting,
    // and the cost of one pass scope with and without GPU timers.
    void api_self_bench(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        if (LLGLSubmitStats::windowActive() || LLGLSubmitStats::collecting() || gSavedSettings.getBOOL("RenderGLSubmitStats"))
        {
            return r.error("stop the collection first (the benchmark uses the frame machinery)");
        }
        using Clock = std::chrono::steady_clock;
        const S32 n = llclamp(req.has("iterations") ? req["iterations"].asInteger() : 1000000, 1000, 100000000);
        constexpr int kHooksPerDraw = 20;
        // The barrier after each hook makes the compiler reload the collection pointer at every site, as it must in the
        // renderer where GL calls sit between the hooks (without it a loop of hooks collapses to one test).
        auto draw_mix = [](S32 count)
        {
#define VKS_BENCH_HOOK(hook) do { hook; std::atomic_signal_fence(std::memory_order_seq_cst); } while (0)
            for (S32 i = 0; i < count; ++i)
            {
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(vbSetBuffer));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(vbChanges));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(attribRespecs));
                VKS_BENCH_HOOK(VKS_GLSTAT(attribPointers, 5));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(iboBinds));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(modelMatrixCalls));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(modelMatrixChanges));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(texBindFast));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(texGLBind));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(texGLActive));
                VKS_BENCH_HOOK(VKS_GLSTAT_STATE_CHANGE());
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(matrixSyncs));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(mdvSyncs));
                VKS_BENCH_HOOK(VKS_GLSTAT_UNIFORM(64, 1, true));
                VKS_BENCH_HOOK(VKS_GLSTAT_UNIFORM(36, 1, true));
                VKS_BENCH_HOOK(VKS_GLSTAT_UNIFORM(64, 1, true));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(uniformRequests));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(uniformRequests));
                VKS_BENCH_HOOK(VKS_GLSTAT_DRAW(300));
                VKS_BENCH_HOOK(VKS_GLSTAT(drawTriangles, 100));
            }
#undef VKS_BENCH_HOOK
        };
        auto time_ns = [&](auto&& body)
        {
            const auto t0 = Clock::now();
            body();
            return std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
        };

        draw_mix(n / 10); // warm up
        const double off_ns = time_ns([&] { draw_mix(n); });
        LLGLSubmitStats::beginFrame(true, false);
        const double on_ns = time_ns([&] { draw_mix(n); });
        LLGLSubmitStats::endFrame();

        const S32 scopes = 2000;
        LLGLSubmitStats::beginFrame(true, false);
        const double scope_cpu_ns = time_ns([&] { for (S32 i = 0; i < scopes; ++i) { VKS_GLSTAT_PASS("bench pass", i % 8); } });
        LLGLSubmitStats::endFrame();
        LLGLSubmitStats::beginFrame(true, true);
        const double scope_gpu_ns = time_ns([&] { for (S32 i = 0; i < scopes; ++i) { VKS_GLSTAT_PASS("bench pass", i % 8); } });
        LLGLSubmitStats::endFrame();
        LLGLSubmitStats::resolveGpu(true);

        r["iterations"] = n;
        r["hooks_per_draw"] = kHooksPerDraw;
        r["ns_per_draw_not_collecting"] = off_ns / n;
        r["ns_per_draw_collecting"] = on_ns / n;
        r["ns_per_hook_not_collecting"] = off_ns / n / kHooksPerDraw;
        r["ns_per_hook_collecting"] = on_ns / n / kHooksPerDraw;
        r["ns_per_scope_cpu_only"] = scope_cpu_ns / scopes;
        r["ns_per_scope_with_gpu_timers"] = scope_gpu_ns / scopes;
        LL_INFOS(kTag) << "hook cost: " << off_ns / n << " ns per draw's hooks not collecting, " << on_ns / n << " collecting; scope "
                       << scope_cpu_ns / scopes << " ns, with GPU timers " << scope_gpu_ns / scopes << " ns" << LL_ENDL;
    }

    void api_profile_frame(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        gShaderProfileFrame = true; // the next frame writes the Frame Profile, with the "submit" section
        r["logs_dir"] = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "");
    }

    class GLSubmitStatsListener : public LLEventAPI
    {
    public:
        GLSubmitStatsListener()
            : LLEventAPI("GLSubmitStats", "OpenGL submission instrumentation (development builds): counters per view and pass, GPU timers, the identical-image check")
        {
            add("status", "State of the collection and of the compare check, on [\"reply\"].", &api_status, llsd::map("reply", LLSD()));
            add("windowBegin",
                "Collect [\"frames\"] frames (default 60), GPU timers on unless [\"gpu\"] is false; the window is averaged per frame.",
                &api_window_begin, llsd::map("reply", LLSD()));
            add("windowStop", "Stop collecting; what was collected stays readable.", &api_window_stop, llsd::map("reply", LLSD()));
            add("summary",
                "Headline numbers of the window ([\"kind\"] \"window\", default) or of the last frame (\"frame\"), on [\"reply\"]. Small.",
                &api_summary, llsd::map("reply", LLSD()));
            add("dump",
                "Write the full JSON of the window or last frame ([\"kind\"]) to [\"path\"] (default: the viewer log folder); the path is in the reply.",
                &api_dump, llsd::map("reply", LLSD()));
            add("compareArm",
                "Arm the identical-image check for the next frame: [\"pass\"] -1 all, 1 G-buffer, 2..5 sun cascades, 6..7 spot shadows, 8 post-deferred. "
                "[\"inject\"] 1 flips one bit of the second readback to prove the check catches it.",
                &api_compare_arm, llsd::map("reply", LLSD()));
            add("compareResults", "Results of the last compared frame, on [\"reply\"].", &api_compare_results, llsd::map("reply", LLSD()));
            add("selfBench",
                "Measure the cost of the instrumentation hooks in this binary: ns per draw's worth of hooks not collecting and collecting, "
                "ns per pass scope. [\"iterations\"] default 1000000. Refused while collecting.",
                &api_self_bench, llsd::map("reply", LLSD()));
            add("profileFrame",
                "Write a Frame Profile (per-shader GPU time, binds, samples, triangles, plus the \"submit\" section) for the next frame; "
                "the file is profile.v*.json in [\"logs_dir\"].",
                &api_profile_frame, llsd::map("reply", LLSD()));
        }
    };

    GLSubmitStatsListener sListener;
}

LLGLSubmitStatsApi::DisplayFrame::DisplayFrame(bool profile_frame)
{
    static LLCachedControl<bool> stats_on(gSavedSettings, "RenderGLSubmitStats", false);
    static LLCachedControl<bool> stats_gpu(gSavedSettings, "RenderGLSubmitStatsGPU", true);
    const bool window = LLGLSubmitStats::windowActive();
    const bool collect = stats_on || window || profile_frame;
    const bool gpu = profile_frame ? true : (window ? LLGLSubmitStats::windowWantsGpu() : (bool)stats_gpu);
    ++sFramesSeen;
    mOwnsStats = LLGLSubmitStats::beginFrame(collect, gpu);

    sInject = gSavedSettings.getS32("RenderGLSubmitCompareInject");
    const S32 want = gSavedSettings.getS32("RenderGLSubmitCompare");
    if (want != LLGLSubmitCompare::PASS_OFF && sArmed == LLGLSubmitCompare::PASS_OFF)
    {
        mOwnsCompare = true;
        // one frame, then the setting returns to 0
        sArmed = want;
        sResults.clear();
        sResultsFrame = gFrameCount;
        gSavedSettings.setS32("RenderGLSubmitCompare", 0);
        LL_INFOS(kTag) << "armed for " << (want == LLGLSubmitCompare::PASS_ALL ? "all passes" : passLabel(want))
                       << " in frame " << gFrameCount << LL_ENDL;
    }
}

LLGLSubmitStatsApi::DisplayFrame::~DisplayFrame()
{
    if (mOwnsStats)
    {
        LLGLSubmitStats::endFrame();
    }
    if (mOwnsCompare)
    {
        if (sResults.empty())
        {
            LL_INFOS(kTag) << "the armed pass did not run in this frame" << LL_ENDL;
        }
        sArmed = LLGLSubmitCompare::PASS_OFF;
        sInRun = false;
        sReplay = false;
    }
}

void LLGLSubmitStatsApi::addToProfile(boost::json::object& stats)
{
    LLGLSubmitStats::endFrame();
    LLGLSubmitStats::resolveGpu(true);
    if (auto f = LLGLSubmitStats::lastFrame())
    {
        boost::json::object submit = LLGLSubmitStats::frameToJson(*f);
        submit.emplace("compare", resultsToJson());
        stats.emplace("submit", std::move(submit));
    }
}

#endif // VKS_GL_STATS
