// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// OpenGL submission instrumentation. Compiled to nothing unless VKS_GL_STATS is set (development builds).

#include "linden_common.h"
#include "llglsubmitstats.h"

#if VKS_GL_STATS

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include "llgl.h"
#include "llglslshader.h"
#include "llglheaders.h"
#include "llrender.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr size_t kMaxInstances = 4096;   // timestamp pairs per frame; the rest is counted without GPU time
    constexpr size_t kMaxPending = 4;        // frames whose GPU timers are still being read (the ring)

    struct StackEntry
    {
        LLGLSubmitStats::Record* rec = nullptr;
        std::string view;
        std::string path;
        Clock::time_point t0;
        uint32_t qBegin = 0;
        bool timed = false;
        uint32_t qSamples = 0;      // an open GL_SAMPLES_PASSED query (alpha passes only; one at a time)
    };

    bool sSamplesOpen = false;

    // The passes whose fragments are counted: the sorted-alpha pools. Samples queries cannot nest, so the first one open wins.
    bool isAlphaPass(const char* name)
    {
        return std::strcmp(name, "ALPHA") == 0 || std::strcmp(name, "ALPHA_PRE_WATER") == 0 || std::strcmp(name, "ALPHA_POST_WATER") == 0;
    }

    std::shared_ptr<LLGLSubmitStats::Frame> sFrame;                 // the frame being built
    std::vector<std::shared_ptr<LLGLSubmitStats::Frame>> sPending;  // finished frames awaiting GPU results
    std::vector<StackEntry> sStack;
    Clock::time_point sFrameT0;
    uint64_t sFrameNo = 0;
    bool sGpuThisFrame = false;

    std::vector<GLuint> sFreeQueries;       // timestamp queries (target GL_TIMESTAMP)
    std::vector<GLuint> sFreeSampleQueries; // GL_SAMPLES_PASSED queries: a query object keeps the target of its first use, so the two pools must never mix

    // The mergeable-run trackers: every change that reached GL, and the same without uniforms, palettes and model
    // matrices (the runs per-draw records would leave, G6r-0).
    LLGLSubmitMath::RunTracker sRun;
    LLGLSubmitMath::RunTracker sRunNoUniform;

    LLGLSubmitStats::Record* recordOf(const void* rec)
    {
        return const_cast<LLGLSubmitStats::Record*>(static_cast<const LLGLSubmitStats::Record*>(rec));
    }

    void creditRun(const LLGLSubmitMath::RunTracker::Closed& c)
    {
        if (c.rec && c.len)
        {
            LLGLSubmitStats::Record* r = recordOf(c.rec);
            r->c.runHist[LLGLSubmitMath::runBucket(c.len)]++;
            r->c.runs++;
            r->c.mergeableDraws += c.len - 1;
        }
    }

    void creditRunNoUniform(const LLGLSubmitMath::RunTracker::Closed& c)
    {
        if (c.rec && c.len)
        {
            LLGLSubmitStats::Record* r = recordOf(c.rec);
            r->c.runHistNoUniform[LLGLSubmitMath::runBucket(c.len)]++;
            r->c.runsNoUniform++;
            r->c.mergeableDrawsNoUniform += c.len - 1;
        }
    }

    // The vertex streams of the last buffer set: a buffer with other streams ends a run (G5's pages hold one format).
    uint32_t sLastFormat = 0;

    // The copy census's runs of one template, and the last face's policy-P0 status in its scope.
    LLGLSubmitMath::CensusRunTracker sCensusRun;
    const LLGLSubmitStats::Record* sCensusLastRec = nullptr;
    bool sCensusLastInstanced = false;

    void creditCensusRun(const LLGLSubmitMath::CensusRunTracker::Closed& c)
    {
        if (c.rec && c.len)
        {
            LLGLSubmitCensus& census = recordOf(c.rec)->census;
            const int size = LLGLSubmitMath::censusSizeClass(c.vertices);
            const int bucket = LLGLSubmitMath::censusRunBucket(c.len);
            census.runHist[size][bucket]++;
            census.runVertices[size][bucket] += (uint64_t)c.len * c.vertices; // N-SF1 reads them as a share of the pass's vertices
        }
    }

    // What a draw's state key holds besides what it reads at the draw: the last skin palette and the caps toggled
    // since the frame began (an XOR of each toggled cap, so equal sets give equal signatures in any order).
    uint64_t sPaletteKey = 0;
    uint64_t sCapSignature = 0;

    uint64_t mixMatrix(uint64_t h, const glm::mat4& m)
    {
        uint32_t bits[16];
        std::memcpy(bits, &m[0][0], sizeof(bits));
        for (uint32_t b : bits)
        {
            h = LLGLSubmitMath::mix64(h, b);
        }
        return h;
    }

    // The state a draw is issued with, as a by-state sort would bucket it: the program, the textures on its channels,
    // the transforms, the palette and the caps. Other uniform values (alpha cutoff, material factors) are not in it, so
    // the count of distinct keys is a lower bound of the buckets.
    uint64_t drawStateKey()
    {
        uint64_t h = LLGLSubmitMath::mix64(0x73746174656b6579ull, LLGLSLShader::sCurBoundShader);
        if (const LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr)
        {
            for (S32 i = 0; i < shader->mActiveTextureChannels; ++i)
            {
                LLTexUnit* unit = gGL.getTexUnit(i);
                h = LLGLSubmitMath::mix64(h, ((uint64_t)unit->getCurrType() << 32) | unit->getCurrTexture());
            }
        }
        h = mixMatrix(h, gGL.getModelviewMatrix());
        h = mixMatrix(h, gGL.getProjectionMatrix());
        h = LLGLSubmitMath::mix64(h, sPaletteKey);
        return LLGLSubmitMath::mix64(h, sCapSignature);
    }

    // The window aggregate: sums over the frames of a window, in order of first appearance.
    std::vector<std::string> sAggOrder;
    std::unordered_map<std::string, LLGLSubmitStats::Record> sAgg;
    uint64_t sAggFrames = 0;

    constexpr char kKeySep = '\x1f';

    uint32_t acquireQuery()
    {
        if (sFreeQueries.empty())
        {
            GLuint ids[256];
            glGenQueries(256, ids);
            for (GLuint id : ids)
            {
                sFreeQueries.push_back(id);
            }
        }
        GLuint id = sFreeQueries.back();
        sFreeQueries.pop_back();
        return id;
    }

    void releaseQuery(uint32_t id) { sFreeQueries.push_back(id); }

    uint32_t acquireSampleQuery()
    {
        if (sFreeSampleQueries.empty())
        {
            GLuint ids[16];
            glGenQueries(16, ids);
            for (GLuint id : ids)
            {
                sFreeSampleQueries.push_back(id);
            }
        }
        GLuint id = sFreeSampleQueries.back();
        sFreeSampleQueries.pop_back();
        return id;
    }

    void releaseSampleQuery(uint32_t id) { sFreeSampleQueries.push_back(id); }

    LLGLSubmitStats::Record* recordFor(LLGLSubmitStats::Frame& f, const std::string& view, const std::string& pass)
    {
        std::string key = view;
        key += kKeySep;
        key += pass;
        auto it = f.byKey.find(key);
        if (it != f.byKey.end())
        {
            return it->second;
        }
        auto rec = std::make_unique<LLGLSubmitStats::Record>();
        rec->view = view;
        rec->pass = pass;
        LLGLSubmitStats::Record* raw = rec.get();
        f.records.push_back(std::move(rec));
        f.byKey.emplace(std::move(key), raw);
        return raw;
    }

    std::string nameWithIndex(const char* name, int index)
    {
        std::string s(name);
        if (index >= 0)
        {
            s += ' ';
            s += std::to_string(index);
        }
        return s;
    }

    double msSince(Clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }

    void addRecord(LLGLSubmitStats::Record& dst, const LLGLSubmitStats::Record& src, bool cpu)
    {
        if (cpu)
        {
            dst.c.add(src.c);
            dst.census.add(src.census);
            dst.calls += src.calls;
            dst.cpuMs += src.cpuMs;
        }
        else
        {
            dst.gpuMs += src.gpuMs;
            dst.samples += src.samples;
        }
    }

    LLGLSubmitStats::Record& aggFor(const LLGLSubmitStats::Record& r)
    {
        std::string key = r.view;
        key += kKeySep;
        key += r.pass;
        auto it = sAgg.find(key);
        if (it == sAgg.end())
        {
            sAggOrder.push_back(key);
            it = sAgg.emplace(key, LLGLSubmitStats::Record()).first;
            it->second.view = r.view;
            it->second.pass = r.pass;
        }
        return it->second;
    }

    boost::json::value number(double v, bool integral)
    {
        // whole numbers as integers (counters, and means that came out whole), the rest rounded to a thousandth
        if (integral || (std::fabs(v) < 9.0e15 && std::fabs(v - std::round(v)) < 1.0e-9))
        {
            return boost::json::value((int64_t)std::llround(v));
        }
        return boost::json::value(std::round(v * 1000.0) / 1000.0);
    }

    // Counters as JSON. div scales to per-frame means (div > 1); only_nonzero drops zero entries; census: a census window
    // or frame (the draw-state keys are counted only there, so elsewhere they are left out rather than shown as 0).
    void countersJson(boost::json::object& out, const LLGLSubmitCounters& c, double div, bool only_nonzero, bool census)
    {
        const bool integral = div == 1.0;
#define VKS_GLSTAT_EMIT(name) \
        if (!only_nonzero || c.name) { out.emplace(#name, number(LLGLSubmitMath::perFrameMean((double)c.name, div), integral)); }
        VKS_GLSTAT_COUNTER_LIST(VKS_GLSTAT_EMIT)
#undef VKS_GLSTAT_EMIT
        if (!census)
        {
            out.erase("stateKeysDistinct");
        }
        auto hist = [&](const char* key, const uint64_t (&h)[6])
        {
            bool any = false;
            for (uint64_t v : h)
            {
                any = any || v;
            }
            if (any || !only_nonzero)
            {
                boost::json::array arr;
                for (uint64_t v : h)
                {
                    arr.emplace_back(number(LLGLSubmitMath::perFrameMean((double)v, div), integral));
                }
                out.emplace(key, std::move(arr));
            }
        };
        hist("run_hist", c.runHist);
        hist("run_hist_no_uniform", c.runHistNoUniform);
    }

    const char* const kCensusSizeNames[LLGLSubmitMath::kCensusSizeClasses] = {"under_64", "64_255", "256_575", "576_up"};

    // The copy census of a scope as JSON (nothing when the scope drew no face with a census record). div as countersJson.
    void censusJson(boost::json::object& out, const LLGLSubmitCensus& c, double div)
    {
        if (!c.faces)
        {
            return;
        }
        const bool integral = div == 1.0;
        auto n = [&](uint64_t v) { return number(LLGLSubmitMath::perFrameMean((double)v, div), integral); };
        auto ms = [&](uint64_t ns) { return number(LLGLSubmitMath::perFrameMean((double)ns, div) / 1.0e6, false); };
        auto hist = [&](const uint64_t (&h)[LLGLSubmitCensus::kRuns])
        {
            boost::json::array arr;
            for (uint64_t v : h)
            {
                arr.emplace_back(n(v));
            }
            return arr;
        };

        boost::json::object o;
        o.emplace("faces", n(c.faces));
        o.emplace("vertices", n(c.vertices));
        boost::json::object reasons;
        for (int r = 0; r < LLGLSubmitCensus::kReasons; ++r)
        {
            if (c.reasonFaces[r])
            {
                boost::json::object e;
                e.emplace("faces", n(c.reasonFaces[r]));
                e.emplace("vertices", n(c.reasonVertices[r]));
                reasons.emplace(LLGLSubmitMath::censusExclusionName(r), std::move(e));
            }
        }
        o.emplace("by_reason", std::move(reasons));
        boost::json::object sizes, runs, run_vertices, relaxed;
        for (int s = 0; s < LLGLSubmitCensus::kSizes; ++s)
        {
            if (c.sizeFaces[s])
            {
                boost::json::object e;
                e.emplace("faces", n(c.sizeFaces[s]));
                e.emplace("vertices", n(c.sizeVertices[s]));
                e.emplace("bytes", n(c.sizeBytes[s]));
                e.emplace("bake_ms", ms(c.sizeBakeNs[s]));
                sizes.emplace(kCensusSizeNames[s], std::move(e));
            }
            bool any_run = false, any_relaxed = false;
            for (int b = 0; b < LLGLSubmitCensus::kRuns; ++b)
            {
                any_run = any_run || c.runHist[s][b];
                any_relaxed = any_relaxed || c.relaxedHist[s][b];
            }
            if (any_run)
            {
                runs.emplace(kCensusSizeNames[s], hist(c.runHist[s]));
                run_vertices.emplace(kCensusSizeNames[s], hist(c.runVertices[s]));
            }
            if (any_relaxed)
            {
                relaxed.emplace(kCensusSizeNames[s], hist(c.relaxedHist[s]));
            }
        }
        o.emplace("eligible_by_size", std::move(sizes));
        o.emplace("items", n(c.items));
        o.emplace("instanced_items", n(c.instancedItems));
        o.emplace("mixed_items", n(c.mixedItems));
        o.emplace("sets", n(c.sets));
        o.emplace("p0_extra_calls", n(c.p0ExtraCalls));
        o.emplace("p0_switches", n(c.p0Switches));
        o.emplace("p1_lost_vertices", n(c.p1LostVertices));
        o.emplace("run_hist", std::move(runs));        // buckets 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+
        o.emplace("run_vertices", std::move(run_vertices)); // the vertices those runs draw, same buckets
        o.emplace("relaxed_hist", std::move(relaxed));
        out.emplace("census", std::move(o));
    }

    // records: in render order. div_cpu scales counters and CPU time, div_gpu GPU time (0: not available).
    boost::json::object recordsToJson(const std::vector<const LLGLSubmitStats::Record*>& recs, double div_cpu, double div_gpu,
                                      bool with_passes, bool census)
    {
        boost::json::object root;
        const bool gpu_ok = div_gpu > 0.0;

        LLGLSubmitCounters total;
        LLGLSubmitCensus total_census;
        double frame_cpu = 0.0, frame_gpu = 0.0;
        for (const auto* r : recs)
        {
            total.add(r->c);
            total_census.add(r->census);
            if (r->view == "frame" && r->pass.empty())
            {
                frame_cpu = r->cpuMs;
                frame_gpu = r->gpuMs;
            }
        }
        boost::json::object totals;
        countersJson(totals, total, div_cpu, false, census);
        censusJson(totals, total_census, div_cpu);
        totals.emplace("cpu_ms", number(frame_cpu / div_cpu, false));
        if (gpu_ok)
        {
            totals.emplace("gpu_ms", number(frame_gpu / div_gpu, false));
            uint64_t alpha_samples = 0;
            for (const auto* r : recs)
            {
                alpha_samples += r->samples;
            }
            totals.emplace("alpha_samples", number((double)alpha_samples / div_gpu, false));
        }
        root.emplace("totals", std::move(totals));

        // views: counters summed over their passes, times from the view scope itself
        std::vector<std::string> view_order;
        std::map<std::string, LLGLSubmitCounters> view_counters;
        std::map<std::string, LLGLSubmitCensus> view_census;
        std::map<std::string, const LLGLSubmitStats::Record*> view_scope;
        for (const auto* r : recs)
        {
            if (view_counters.find(r->view) == view_counters.end())
            {
                view_order.push_back(r->view);
            }
            view_counters[r->view].add(r->c);
            view_census[r->view].add(r->census);
            if (r->pass.empty())
            {
                view_scope[r->view] = r;
            }
        }
        boost::json::array views;
        for (const auto& name : view_order)
        {
            boost::json::object v;
            v.emplace("view", name);
            auto it = view_scope.find(name);
            if (it != view_scope.end())
            {
                v.emplace("calls", number((double)it->second->calls / div_cpu, div_cpu == 1.0));
                v.emplace("cpu_ms", number(it->second->cpuMs / div_cpu, false));
                if (gpu_ok)
                {
                    v.emplace("gpu_ms", number(it->second->gpuMs / div_gpu, false));
                }
            }
            countersJson(v, view_counters[name], div_cpu, true, census);
            censusJson(v, view_census[name], div_cpu);
            views.emplace_back(std::move(v));
        }
        root.emplace("views", std::move(views));

        if (with_passes)
        {
            boost::json::array passes;
            for (const auto* r : recs)
            {
                boost::json::object p;
                p.emplace("view", r->view);
                p.emplace("pass", r->pass);
                p.emplace("calls", number((double)r->calls / div_cpu, div_cpu == 1.0));
                p.emplace("cpu_ms", number(r->cpuMs / div_cpu, false));
                if (gpu_ok)
                {
                    p.emplace("gpu_ms", number(r->gpuMs / div_gpu, false));
                    if (r->samples)
                    {
                        p.emplace("samples", number((double)r->samples / div_gpu, false));
                    }
                }
                countersJson(p, r->c, div_cpu, true, census);
                censusJson(p, r->census, div_cpu);
                passes.emplace_back(std::move(p));
            }
            root.emplace("passes", std::move(passes));
        }
        return root;
    }
}

std::atomic<LLGLSubmitStats::Record*> LLGLSubmitStats::sCurRec{nullptr};
LLGLSubmitMath::RunEpochs LLGLSubmitStats::sEpochs;
std::shared_ptr<LLGLSubmitStats::Frame> LLGLSubmitStats::sLast;
uint32_t LLGLSubmitStats::sWindowTarget = 0;
uint32_t LLGLSubmitStats::sWindowCollected = 0;
uint32_t LLGLSubmitStats::sWindowGpuFrames = 0;
bool LLGLSubmitStats::sWindowGpu = true;
bool LLGLSubmitStats::sWindowCensus = false;

void LLGLSubmitCounters::add(const LLGLSubmitCounters& o)
{
#define VKS_GLSTAT_ADD(name) name += o.name;
    VKS_GLSTAT_COUNTER_LIST(VKS_GLSTAT_ADD)
#undef VKS_GLSTAT_ADD
    for (int i = 0; i < 6; ++i)
    {
        runHist[i] += o.runHist[i];
        runHistNoUniform[i] += o.runHistNoUniform[i];
    }
}

void LLGLSubmitCensus::add(const LLGLSubmitCensus& o)
{
    faces += o.faces;
    vertices += o.vertices;
    for (int r = 0; r < kReasons; ++r)
    {
        reasonFaces[r] += o.reasonFaces[r];
        reasonVertices[r] += o.reasonVertices[r];
    }
    for (int s = 0; s < kSizes; ++s)
    {
        sizeFaces[s] += o.sizeFaces[s];
        sizeVertices[s] += o.sizeVertices[s];
        sizeBytes[s] += o.sizeBytes[s];
        sizeBakeNs[s] += o.sizeBakeNs[s];
        for (int b = 0; b < kRuns; ++b)
        {
            runHist[s][b] += o.runHist[s][b];
            runVertices[s][b] += o.runVertices[s][b];
            relaxedHist[s][b] += o.relaxedHist[s][b];
        }
    }
    items += o.items;
    instancedItems += o.instancedItems;
    mixedItems += o.mixedItems;
    sets += o.sets;
    p0ExtraCalls += o.p0ExtraCalls;
    p0Switches += o.p0Switches;
    p1LostVertices += o.p1LostVertices;
}

void LLGLSubmitStats::closeRun()
{
    creditRun(sRun.close());
    creditRunNoUniform(sRunNoUniform.close());
    creditCensusRun(sCensusRun.close());
    sCensusLastRec = nullptr;
}

bool LLGLSubmitStats::beginFrame(bool collect, bool gpu)
{
    sIsMain = true;
    if (sFrame)
    {
        return false; // nested: the outer frame goes on
    }
    resolveGpu(false);
    if (!collect)
    {
        setCur(nullptr);
        return false;
    }

    sFrame = std::make_shared<Frame>();
    sFrame->number = ++sFrameNo;
    sFrame->census = sWindowCensus && windowActive(); // the frame is counted into a census window
    sGpuThisFrame = gpu && glQueryCounter && glGetQueryObjectui64v;
    sFrame->gpuTimed = sGpuThisFrame;
    sFrameT0 = Clock::now();
    sStack.clear();
    sRun = LLGLSubmitMath::RunTracker();
    sRunNoUniform = LLGLSubmitMath::RunTracker();
    sCensusRun = LLGLSubmitMath::CensusRunTracker();
    sCensusLastRec = nullptr;
    sEpochs = LLGLSubmitMath::RunEpochs();
    sLastFormat = 0;
    sPaletteKey = 0;
    sCapSignature = 0;
    enterView("frame", -1);
    return true;
}

void LLGLSubmitStats::endFrame()
{
    if (!sFrame)
    {
        return;
    }
    while (!sStack.empty())
    {
        leave();
    }
    closeRun();
    setCur(nullptr);

    std::shared_ptr<Frame> f = std::move(sFrame);
    sFrame.reset();
    f->cpuMs = msSince(sFrameT0);

    for (auto& r : f->records)
    {
        r->c.paletteDistinct = r->palettes.size();
        r->c.vbosDistinct = r->vbos.size();
        r->c.stateKeysDistinct = r->stateKeys.size();
        for (const auto& key : r->censusKeys)
        {
            // relaxed order: every copy of a key in the scope could be one set
            r->census.relaxedHist[LLGLSubmitMath::censusSizeClass(key.second.second)][LLGLSubmitMath::censusRunBucket(key.second.first)]++;
        }
        r->palettes = {};
        r->vbos = {};
        r->stateKeys = {};
        r->censusKeys = {};
    }
    f->palettesAll = {};

    if (sWindowTarget != 0 && sWindowCollected < sWindowTarget)
    {
        for (auto& r : f->records)
        {
            addRecord(aggFor(*r), *r, true);
        }
        f->inWindow = true;
        ++sAggFrames;
        ++sWindowCollected;
    }

    sLast = f;
    if (f->gpuTimed && !f->insts.empty())
    {
        sPending.push_back(f);
        if (sPending.size() > kMaxPending)
        {
            // The GPU is more than the ring behind: give up the oldest frame's GPU times (never wait for them).
            dropOldest();
        }
    }
    else
    {
        f->gpuResolved = false;
    }
}

void LLGLSubmitStats::dropOldest()
{
    if (sPending.empty())
    {
        return;
    }
    std::shared_ptr<Frame> f = sPending.front();
    sPending.erase(sPending.begin());
    for (const Frame::Inst& in : f->insts)
    {
        releaseQuery(in.begin);
        releaseQuery(in.end);
    }
    f->insts.clear();
    for (const Frame::SampleInst& si : f->sampleInsts)
    {
        releaseSampleQuery(si.query);
    }
    f->sampleInsts.clear();
    f->gpuResolved = false;
    f->gpuTruncated = true;
}

void LLGLSubmitStats::resetGL(bool contextValid)
{
    std::vector<GLuint> ids = sFreeQueries;
    ids.insert(ids.end(), sFreeSampleQueries.begin(), sFreeSampleQueries.end());
    for (const auto& f : sPending)
    {
        for (const Frame::Inst& in : f->insts)
        {
            ids.push_back(in.begin);
            ids.push_back(in.end);
        }
        f->insts.clear();
        for (const Frame::SampleInst& si : f->sampleInsts)
        {
            ids.push_back(si.query);
        }
        f->sampleInsts.clear();
        f->gpuResolved = false;
    }
    if (sFrame)
    {
        for (const Frame::Inst& in : sFrame->insts)
        {
            ids.push_back(in.begin);
            ids.push_back(in.end);
        }
        for (StackEntry& e : sStack)
        {
            if (e.timed)
            {
                ids.push_back(e.qBegin);
                e.timed = false;
            }
            if (e.qSamples)
            {
                ids.push_back(e.qSamples);
                e.qSamples = 0;
            }
        }
        for (const Frame::SampleInst& si : sFrame->sampleInsts)
        {
            ids.push_back(si.query);
        }
        sFrame->sampleInsts.clear();
        sFrame->insts.clear();
        sFrame->gpuTimed = false;
        sGpuThisFrame = false;
    }
    sSamplesOpen = false;   // a query left open in a lost context is gone with it
    if (contextValid && !ids.empty() && glDeleteQueries)
    {
        glDeleteQueries((GLsizei)ids.size(), ids.data());
    }
    sFreeQueries.clear();
    sFreeSampleQueries.clear();
    sPending.clear();
}

void LLGLSubmitStats::resolveGpu(bool blockForGpu)
{
    while (!sPending.empty())
    {
        std::shared_ptr<Frame> f = sPending.front();
        if (!blockForGpu)
        {
            GLuint64 avail = 0;
            glGetQueryObjectui64v(f->insts.back().end, GL_QUERY_RESULT_AVAILABLE, &avail);
            if (!avail)
            {
                return;
            }
        }
        for (const Frame::Inst& in : f->insts)
        {
            GLuint64 t0 = 0, t1 = 0;
            glGetQueryObjectui64v(in.begin, GL_QUERY_RESULT, &t0);
            glGetQueryObjectui64v(in.end, GL_QUERY_RESULT, &t1);
            in.rec->gpuMs += t1 >= t0 ? (double)(t1 - t0) / 1.0e6 : 0.0;
            releaseQuery(in.begin);
            releaseQuery(in.end);
        }
        for (const Frame::SampleInst& si : f->sampleInsts)
        {
            GLuint64 n = 0;
            glGetQueryObjectui64v(si.query, GL_QUERY_RESULT, &n);
            si.rec->samples += n;
            releaseSampleQuery(si.query);
        }
        f->sampleInsts.clear();
        f->insts.clear();
        f->gpuResolved = true;
        sPending.erase(sPending.begin());
        if (f->inWindow && sWindowGpu)
        {
            for (auto& r : f->records)
            {
                addRecord(aggFor(*r), *r, false);
            }
            ++sWindowGpuFrames;
        }
        if (blockForGpu && sPending.empty())
        {
            break;
        }
    }
}

void LLGLSubmitStats::enterView(const char* name, int index)
{
    if (!sFrame || !onMain())
    {
        return;
    }
    closeRun();
    StackEntry e;
    e.view = nameWithIndex(name, index);
    if (!sStack.empty() && sStack.back().view != "frame")
    {
        e.view = sStack.back().view + " > " + e.view; // a view inside another view: "probe face > sun cascade 0"
    }
    e.rec = recordFor(*sFrame, e.view, "");
    e.rec->calls++;
    e.t0 = Clock::now();
    if (sGpuThisFrame && sFrame->insts.size() < kMaxInstances)
    {
        e.qBegin = acquireQuery();
        glQueryCounter(e.qBegin, GL_TIMESTAMP);
        e.timed = true;
    }
    else if (sGpuThisFrame)
    {
        sFrame->gpuTruncated = true;
    }
    setCur(e.rec);
    sStack.push_back(std::move(e));
}

void LLGLSubmitStats::enterPass(const char* name, int index)
{
    if (!sFrame || !onMain() || sStack.empty())
    {
        return;
    }
    closeRun();
    const StackEntry& parent = sStack.back();
    StackEntry e;
    e.view = parent.view;
    e.path = parent.path;
    if (!e.path.empty())
    {
        e.path += '/';
    }
    e.path += nameWithIndex(name, index);
    e.rec = recordFor(*sFrame, e.view, e.path);
    e.rec->calls++;
    e.t0 = Clock::now();
    if (sGpuThisFrame && sFrame->insts.size() < kMaxInstances)
    {
        e.qBegin = acquireQuery();
        glQueryCounter(e.qBegin, GL_TIMESTAMP);
        e.timed = true;
    }
    else if (sGpuThisFrame)
    {
        sFrame->gpuTruncated = true;
    }
    // Frame Profile and per-shader profiling hold the GL_SAMPLES_PASSED target themselves (one query per target at a time)
    if (e.timed && !sSamplesOpen && !LLGLSLShader::sProfileEnabled && isAlphaPass(name))
    {
        e.qSamples = acquireSampleQuery();
        glBeginQuery(GL_SAMPLES_PASSED, e.qSamples);
        sSamplesOpen = true;
    }
    setCur(e.rec);
    sStack.push_back(std::move(e));
}

void LLGLSubmitStats::leave()
{
    if (!sFrame || !onMain() || sStack.empty())
    {
        return;
    }
    closeRun();
    StackEntry& e = sStack.back();
    e.rec->cpuMs += msSince(e.t0);
    if (e.qSamples)
    {
        glEndQuery(GL_SAMPLES_PASSED);
        sSamplesOpen = false;
        sFrame->sampleInsts.push_back({e.rec, e.qSamples});
    }
    if (e.timed)
    {
        uint32_t qe = acquireQuery();
        glQueryCounter(qe, GL_TIMESTAMP);
        sFrame->insts.push_back({e.rec, e.qBegin, qe});
    }
    sStack.pop_back();
    setCur(sStack.empty() ? nullptr : sStack.back().rec);
}

void LLGLSubmitStats::noteDraw(uint32_t indices)
{
    Record* const rec = cur();
    if (!rec || !onMain())
    {
        return;
    }
    rec->c.draws++;
    rec->c.drawIndices += indices;
    creditRun(sRun.draw(rec, sEpochs.all));
    creditRunNoUniform(sRunNoUniform.draw(rec, sEpochs.noUniform));
    if (sFrame && sFrame->census)
    {
        rec->stateKeys.insert(drawStateKey());
    }
}

void LLGLSubmitStats::notePalette(const void* avatar, uint64_t skinHash, uint32_t bytes)
{
    Record* const sCur = cur();
    if (!sCur || !onMain())
    {
        return;
    }
    sCur->c.paletteUploads++;
    sCur->c.paletteBytes += bytes;
    const uint64_t key = (uint64_t)(uintptr_t)avatar * 0x9E3779B97F4A7C15ull ^ skinHash;
    sCur->palettes.insert(key);
    if (sFrame)
    {
        sFrame->palettesAll.insert(key);
    }
    sPaletteKey = key;
    sEpochs.uniform(); // a palette is a uniform upload, which per-draw records would carry
}

void LLGLSubmitStats::noteVBO(uint32_t name, uint32_t format)
{
    Record* const sCur = cur();
    if (!sCur || !onMain())
    {
        return;
    }
    sCur->vbos.insert(name);
    if (format != sLastFormat)
    {
        if (sLastFormat != 0)
        {
            // other vertex streams: setupVertexBuffer re-points every attribute, and G5's pages hold one format each
            sCur->c.vbFormatChanges++;
            sEpochs.stateChange();
        }
        sLastFormat = format;
    }
}

void LLGLSubmitStats::noteCapToggle(uint32_t cap)
{
    if (!active())
    {
        return;
    }
    sEpochs.stateChange();
    sCapSignature ^= LLGLSubmitMath::mix64(0x636170ull, cap);
}

void LLGLSubmitStats::noteCensusDraw(const LLGLSubmitMath::CensusFace* faces, size_t count)
{
    using namespace LLGLSubmitMath;
    Record* const rec = cur();
    if (!rec || !onMain() || !count)
    {
        return;
    }
    LLGLSubmitCensus& c = rec->census;
    const CensusItem item = censusItem(faces, count);
    c.items++;
    c.p0ExtraCalls += item.segments ? item.segments - 1 : 0;
    if (item.instancedVertices)
    {
        c.instancedItems++;
        c.sets += item.sets;
    }
    if (item.mixed)
    {
        c.mixedItems++;
        c.p1LostVertices += item.instancedVertices;
    }
    for (size_t i = 0; i < count; ++i)
    {
        const CensusFace& f = faces[i];
        const uint8_t reason = f.exclusion < CENSUS_EXCLUSIONS ? f.exclusion : (uint8_t)CENSUS_NO_KEY;
        c.faces++;
        c.vertices += f.vertices;
        c.reasonFaces[reason]++;
        c.reasonVertices[reason] += f.vertices;
        if (reason == CENSUS_ELIGIBLE)
        {
            const int size = censusSizeClass(f.vertices);
            c.sizeFaces[size]++;
            c.sizeVertices[size] += f.vertices;
            c.sizeBytes[size] += (uint64_t)f.vertexBytes + f.indexBytes;
            c.sizeBakeNs[size] += f.bakeNs;
            auto& copies = rec->censusKeys[f.key];
            copies.first++;
            copies.second = f.vertices;
        }
        // P0 changes program and record binding wherever list order goes from baked to instanced faces or back
        const bool instanced = f.instanced();
        if (sCensusLastRec == rec && instanced != sCensusLastInstanced)
        {
            c.p0Switches++;
        }
        sCensusLastRec = rec;
        sCensusLastInstanced = instanced;
        creditCensusRun(sCensusRun.face(rec, f));
    }
}

void LLGLSubmitStats::noteUniform(uint32_t bytes, uint32_t count, bool matrix)
{
    Record* const sCur = cur();
    if (!sCur || !onMain())
    {
        return;
    }
    sCur->c.uniformGL++;
    sCur->c.uniformBytes += bytes;
    if (count > 1)
    {
        sCur->c.uniformArrayGL++;
    }
    if (matrix)
    {
        sCur->c.uniformMatrixGL++;
    }
    sEpochs.uniform(); // a uniform that reached GL separates draws that could otherwise be merged
}

void LLGLSubmitStats::windowBegin(uint32_t frames, bool gpu, bool census)
{
    sAgg.clear();
    sAggOrder.clear();
    for (auto& p : sPending)
    {
        p->inWindow = false; // frames in flight from before the window do not belong to it
    }
    sAggFrames = 0;
    sWindowCollected = 0;
    sWindowGpuFrames = 0;
    sWindowGpu = gpu;
    sWindowCensus = census;
    sWindowTarget = frames;
}

void LLGLSubmitStats::windowStop()
{
    sWindowTarget = sWindowCollected; // keeps what was collected, collects no more
}

boost::json::object LLGLSubmitStats::frameToJson(const Frame& f)
{
    std::vector<const Record*> recs;
    for (const auto& r : f.records)
    {
        recs.push_back(r.get());
    }
    boost::json::object o = recordsToJson(recs, 1.0, f.gpuResolved ? 1.0 : 0.0, true, f.census);
    o.emplace("schema", 1);
    o.emplace("kind", "frame");
    o.emplace("frame_number", f.number);
    o.emplace("frames", 1);
    o.emplace("gpu_timed", f.gpuTimed);
    o.emplace("gpu_resolved", f.gpuResolved);
    o.emplace("gpu_truncated", f.gpuTruncated);
    return o;
}

boost::json::object LLGLSubmitStats::windowToJson()
{
    std::vector<const Record*> recs;
    for (const auto& key : sAggOrder)
    {
        recs.push_back(&sAgg[key]);
    }
    const double frames = std::max<double>(1.0, (double)sWindowCollected);
    const double gpu_frames = (double)sWindowGpuFrames;
    boost::json::object o = recordsToJson(recs, frames, gpu_frames, true, sWindowCensus);
    o.emplace("schema", 1);
    o.emplace("kind", "window");
    o.emplace("scale", "per_frame_mean");
    o.emplace("frames", sWindowCollected);
    o.emplace("gpu_frames", sWindowGpuFrames);
    o.emplace("census_window", sWindowCensus); // the census and the draw-state keys were counted
    return o;
}

boost::json::object LLGLSubmitStats::summaryOfFrame(const Frame& f)
{
    std::vector<const Record*> recs;
    for (const auto& r : f.records)
    {
        recs.push_back(r.get());
    }
    boost::json::object o = recordsToJson(recs, 1.0, f.gpuResolved ? 1.0 : 0.0, false, f.census);
    o.emplace("frame_number", f.number);
    o.emplace("gpu_resolved", f.gpuResolved);
    return o;
}

boost::json::object LLGLSubmitStats::summaryOfWindow()
{
    std::vector<const Record*> recs;
    for (const auto& key : sAggOrder)
    {
        recs.push_back(&sAgg[key]);
    }
    const double frames = std::max<double>(1.0, (double)sWindowCollected);
    boost::json::object o = recordsToJson(recs, frames, (double)sWindowGpuFrames, false, sWindowCensus);
    o.emplace("frames", sWindowCollected);
    o.emplace("gpu_frames", sWindowGpuFrames);
    return o;
}

#endif // VKS_GL_STATS
