// Development-only focus/resize diagnostics. Never wait for a GPU query.
#pragma once
#include "llgl.h"
#include "llerror.h"
#include <chrono>
#include <vector>
#include <algorithm>

namespace FocusRenderProbe
{
using Clock = std::chrono::steady_clock;
struct Probe;
inline std::vector<Probe*>& registry() { static std::vector<Probe*> probes; return probes; }
struct Probe
{
    const char* name;
    bool every_call;
    U64 calls = 0;
    double total_ms = 0., max_ms = 0.;
    GLuint queries[2] = {};
    bool pending = false;
    Clock::time_point report = Clock::now(), submitted = Clock::now() - std::chrono::seconds(2);
    explicit Probe(const char* label, bool event = false) : name(label), every_call(event) { registry().push_back(this); }
    void clear()
    {
        if (queries[0]) glDeleteQueries(2, queries);
        queries[0] = queries[1] = 0; pending = false;
    }
};
inline void cleanup() { for (auto* probe : registry()) probe->clear(); }
struct Scope
{
    Probe& probe;
    Clock::time_point start = Clock::now();
    bool gpu_started = false;
    explicit Scope(Probe& p, bool gpu = false) : probe(p)
    {
        if (probe.pending)
        {
            GLint ready = 0;
            glGetQueryObjectiv(probe.queries[1], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready)
            {
                GLuint64 begin = 0, end = 0;
                glGetQueryObjectui64v(probe.queries[0], GL_QUERY_RESULT, &begin);
                glGetQueryObjectui64v(probe.queries[1], GL_QUERY_RESULT, &end);
                LL_INFOS("FocusRender") << "phase=" << probe.name
                    << " gpu_ms=" << double(end-begin)/1.e6
                    << " sample_age_ms=" << std::chrono::duration<double,std::milli>(start-probe.submitted).count() << LL_ENDL;
                probe.clear();
            }
        }
        // At most one outstanding pair per phase; no forced flush or wait.
        if (gpu && gGLManager.mGLVersion >= 3.3f && !probe.pending &&
            start-probe.submitted >= std::chrono::seconds(5))
        {
            glGenQueries(2, probe.queries);
            glQueryCounter(probe.queries[0], GL_TIMESTAMP);
            probe.submitted = start; gpu_started = true;
        }
    }
    ~Scope()
    {
        if (gpu_started) { glQueryCounter(probe.queries[1], GL_TIMESTAMP); probe.pending = true; }
        const auto now = Clock::now();
        const double ms = std::chrono::duration<double,std::milli>(now-start).count();
        ++probe.calls; probe.total_ms += ms; probe.max_ms = std::max(probe.max_ms, ms);
        if (probe.every_call || now-probe.report >= std::chrono::seconds(5))
        {
            LL_INFOS("FocusRender") << "phase=" << probe.name << " calls=" << probe.calls
                << " cpu_mean_ms=" << probe.total_ms/probe.calls << " cpu_max_ms=" << probe.max_ms
                << " gpu_pending=" << probe.pending << LL_ENDL;
            probe.report = now; probe.calls = 0; probe.total_ms = probe.max_ms = 0.;
        }
    }
};
}
