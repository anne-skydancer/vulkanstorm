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
#include "llgl.h"
#include "llglheaders.h"

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
    };

    std::shared_ptr<LLGLSubmitStats::Frame> sFrame;                 // the frame being built
    std::vector<std::shared_ptr<LLGLSubmitStats::Frame>> sPending;  // finished frames awaiting GPU results
    std::vector<StackEntry> sStack;
    Clock::time_point sFrameT0;
    uint64_t sFrameNo = 0;
    bool sGpuThisFrame = false;

    std::vector<GLuint> sFreeQueries;

    // The mergeable-run tracker.
    LLGLSubmitMath::RunTracker sRun;

    void creditRun(const LLGLSubmitMath::RunTracker::Closed& c)
    {
        if (c.rec && c.len)
        {
            auto* r = const_cast<LLGLSubmitStats::Record*>(static_cast<const LLGLSubmitStats::Record*>(c.rec));
            r->c.runHist[LLGLSubmitMath::runBucket(c.len)]++;
            r->c.runs++;
            r->c.mergeableDraws += c.len - 1;
        }
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
            dst.calls += src.calls;
            dst.cpuMs += src.cpuMs;
        }
        else
        {
            dst.gpuMs += src.gpuMs;
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

    // Counters as JSON. div scales to per-frame means (div > 1); only_nonzero drops zero entries.
    void countersJson(boost::json::object& out, const LLGLSubmitCounters& c, double div, bool only_nonzero)
    {
        const bool integral = div == 1.0;
#define VKS_GLSTAT_EMIT(name) \
        if (!only_nonzero || c.name) { out.emplace(#name, number(LLGLSubmitMath::perFrameMean((double)c.name, div), integral)); }
        VKS_GLSTAT_COUNTER_LIST(VKS_GLSTAT_EMIT)
#undef VKS_GLSTAT_EMIT
        bool any = false;
        for (uint64_t h : c.runHist)
        {
            any = any || h;
        }
        if (any || !only_nonzero)
        {
            boost::json::array hist;
            for (uint64_t h : c.runHist)
            {
                hist.emplace_back(number(LLGLSubmitMath::perFrameMean((double)h, div), integral));
            }
            out.emplace("run_hist", std::move(hist));
        }
    }

    // records: in render order. div_cpu scales counters and CPU time, div_gpu GPU time (0: not available).
    boost::json::object recordsToJson(const std::vector<const LLGLSubmitStats::Record*>& recs, double div_cpu, double div_gpu,
                                      bool with_passes)
    {
        boost::json::object root;
        const bool gpu_ok = div_gpu > 0.0;

        LLGLSubmitCounters total;
        double frame_cpu = 0.0, frame_gpu = 0.0;
        for (const auto* r : recs)
        {
            total.add(r->c);
            if (r->view == "frame" && r->pass.empty())
            {
                frame_cpu = r->cpuMs;
                frame_gpu = r->gpuMs;
            }
        }
        boost::json::object totals;
        countersJson(totals, total, div_cpu, false);
        totals.emplace("cpu_ms", number(frame_cpu / div_cpu, false));
        if (gpu_ok)
        {
            totals.emplace("gpu_ms", number(frame_gpu / div_gpu, false));
        }
        root.emplace("totals", std::move(totals));

        // views: counters summed over their passes, times from the view scope itself
        std::vector<std::string> view_order;
        std::map<std::string, LLGLSubmitCounters> view_counters;
        std::map<std::string, const LLGLSubmitStats::Record*> view_scope;
        for (const auto* r : recs)
        {
            if (view_counters.find(r->view) == view_counters.end())
            {
                view_order.push_back(r->view);
            }
            view_counters[r->view].add(r->c);
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
            countersJson(v, view_counters[name], div_cpu, true);
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
                }
                countersJson(p, r->c, div_cpu, true);
                passes.emplace_back(std::move(p));
            }
            root.emplace("passes", std::move(passes));
        }
        return root;
    }
}

std::atomic<LLGLSubmitStats::Record*> LLGLSubmitStats::sCurRec{nullptr};
uint32_t LLGLSubmitStats::sEpoch = 0;
std::shared_ptr<LLGLSubmitStats::Frame> LLGLSubmitStats::sLast;
uint32_t LLGLSubmitStats::sWindowTarget = 0;
uint32_t LLGLSubmitStats::sWindowCollected = 0;
uint32_t LLGLSubmitStats::sWindowGpuFrames = 0;
bool LLGLSubmitStats::sWindowGpu = true;

void LLGLSubmitCounters::add(const LLGLSubmitCounters& o)
{
#define VKS_GLSTAT_ADD(name) name += o.name;
    VKS_GLSTAT_COUNTER_LIST(VKS_GLSTAT_ADD)
#undef VKS_GLSTAT_ADD
    for (int i = 0; i < 6; ++i)
    {
        runHist[i] += o.runHist[i];
    }
}

void LLGLSubmitStats::closeRun()
{
    creditRun(sRun.close());
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
    sGpuThisFrame = gpu && glQueryCounter && glGetQueryObjectui64v;
    sFrame->gpuTimed = sGpuThisFrame;
    sFrameT0 = Clock::now();
    sStack.clear();
    sRun = LLGLSubmitMath::RunTracker();
    sEpoch = 0;
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
        r->palettes = {};
        r->vbos = {};
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
    f->gpuResolved = false;
    f->gpuTruncated = true;
}

void LLGLSubmitStats::resetGL(bool contextValid)
{
    std::vector<GLuint> ids = sFreeQueries;
    for (const auto& f : sPending)
    {
        for (const Frame::Inst& in : f->insts)
        {
            ids.push_back(in.begin);
            ids.push_back(in.end);
        }
        f->insts.clear();
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
        }
        sFrame->insts.clear();
        sFrame->gpuTimed = false;
        sGpuThisFrame = false;
    }
    if (contextValid && !ids.empty() && glDeleteQueries)
    {
        glDeleteQueries((GLsizei)ids.size(), ids.data());
    }
    sFreeQueries.clear();
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
    creditRun(sRun.draw(rec, sEpoch));
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
    ++sEpoch;
}

void LLGLSubmitStats::noteVBO(uint32_t name)
{
    Record* const sCur = cur();
    if (!sCur || !onMain())
    {
        return;
    }
    sCur->vbos.insert(name);
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
    ++sEpoch; // a uniform that reached GL separates draws that could otherwise be merged
}

void LLGLSubmitStats::windowBegin(uint32_t frames, bool gpu)
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
    boost::json::object o = recordsToJson(recs, 1.0, f.gpuResolved ? 1.0 : 0.0, true);
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
    boost::json::object o = recordsToJson(recs, frames, gpu_frames, true);
    o.emplace("schema", 1);
    o.emplace("kind", "window");
    o.emplace("scale", "per_frame_mean");
    o.emplace("frames", sWindowCollected);
    o.emplace("gpu_frames", sWindowGpuFrames);
    return o;
}

boost::json::object LLGLSubmitStats::summaryOfFrame(const Frame& f)
{
    std::vector<const Record*> recs;
    for (const auto& r : f.records)
    {
        recs.push_back(r.get());
    }
    boost::json::object o = recordsToJson(recs, 1.0, f.gpuResolved ? 1.0 : 0.0, false);
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
    boost::json::object o = recordsToJson(recs, frames, (double)sWindowGpuFrames, false);
    o.emplace("frames", sWindowCollected);
    o.emplace("gpu_frames", sWindowGpuFrames);
    return o;
}

#endif // VKS_GL_STATS
