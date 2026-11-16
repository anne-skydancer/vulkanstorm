// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// See lldirectionalshadowreadout.h.

#include "llviewerprecompiledheaders.h"
#include "lldirectionalshadowreadout.h"

#include "lldirectionalshadowgate.h"
#include "lleventapi.h"
#include "llsd.h"
#include "llsdutil.h"

#include <cmath>

namespace
{
    struct Last
    {
        bool known = false;
        bool skipped = false;
        float light = 0.f;
        float fade = 1.f;
        float logged_light = -1.f;
        U64 frames_skipped = 0;
        U64 frames_rendered = 0;
        U64 probe_calls = 0;
        U64 changes = 0;
    } sLast;

    void api_stats(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        r["known"] = sLast.known;                     // false until the Stochastic method has run a frame
        r["skipped"] = sLast.skipped;                 // the cascade pass is skipped (dark gate closed)
        r["decision_light"] = (F64)sLast.light;       // E = 3 x lum(linear(primary light colour)) x sky sunlight scale
        r["threshold"] = LLDirectionalShadowGate::THRESHOLD;
        r["resume"] = LLDirectionalShadowGate::RESUME;
        r["full"] = LLDirectionalShadowGate::FULL;    // the shadow is at full strength from here
        r["strength_factor"] = (F64)sLast.fade;       // multiplies primary_shadow_strength
        r["frames_skipped"] = (F64)sLast.frames_skipped;
        r["frames_rendered"] = (F64)sLast.frames_rendered;
        r["probe_calls"] = (F64)sLast.probe_calls;
        r["changes"] = (F64)sLast.changes;
    }

    class DirectionalShadowsListener : public LLEventAPI
    {
    public:
        DirectionalShadowsListener()
            : LLEventAPI("DirectionalShadows", "Directional shadow dark gate (development builds): decision light and state")
        {
            add("stats", "The dark gate's decision light, threshold and state, on [\"reply\"].", &api_stats, llsd::map("reply", LLSD()));
        }
    };

    DirectionalShadowsListener sListener;
}

void DirectionalShadowReadout::note(bool skipped, float light, float fade, bool probe)
{
    if (probe)
    {
        ++sLast.probe_calls;
        return; // the main view's call carries the state; the probes take the same decision
    }
    const bool changed = !sLast.known || sLast.skipped != skipped;
    const bool moved = sLast.logged_light >= 0.f
        && (light > 2.f * sLast.logged_light || light < 0.5f * sLast.logged_light) && !(light == 0.f && sLast.logged_light == 0.f);
    sLast.known = true;
    sLast.skipped = skipped;
    sLast.light = light;
    sLast.fade = fade;
    ++(skipped ? sLast.frames_skipped : sLast.frames_rendered);
    if (changed || moved)
    {
        sLast.changes += changed ? 1 : 0;
        sLast.logged_light = light;
        LL_INFOS("DirectionalShadows") << "directional shadows: " << (skipped ? "skipped" : "rendered") << " (light L=" << light
                                       << ", threshold " << LLDirectionalShadowGate::THRESHOLD << ", strength factor " << fade << ")" << LL_ENDL;
    }
}
