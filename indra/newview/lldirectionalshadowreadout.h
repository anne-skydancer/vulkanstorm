// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Development readout of the directional shadows' dark gate (lldirectionalshadowgate.h); not part of release
// (scripts/tests/check_release_hooks.py rejects the name there). LLPipeline::generateSunShadow calls note() once per call;
// it logs "directional shadows: skipped (light L=...)" / "rendered (light L=...)" when the state changes or the decision
// light moves by a factor of 2 since the last line (so a sky change shows its value), and the LEAP operation
// DirectionalShadows.stats returns the last values. Main thread only.
#pragma once

namespace DirectionalShadowReadout
{
    // 'skipped': the cascades are not drawn this call; 'light': the decision light E; 'fade': the strength factor;
    // 'probe': the call is a reflection probe / cube snapshot render
    void note(bool skipped, float light, float fade, bool probe);
}
