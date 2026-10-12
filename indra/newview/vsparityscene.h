// Offline deterministic production scene fixture, development builds only. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
class LLVector3;
class LLSD;
LLSD vs_parity_scene_inputs();
LLVector3 vs_parity_scene_eye();
bool vs_parity_scene_frame();
bool vs_parity_scene_active();
VSWorldRenderer::View vs_parity_scene_view(unsigned width,unsigned height);
