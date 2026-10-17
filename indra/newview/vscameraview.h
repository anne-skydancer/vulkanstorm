// Shared CPU camera draw-distance policy for renderer peers. LGPL-2.1.
#pragma once
#include "stdtypes.h"
F32 vs_camera_far_clip(bool cubeSnapshot=false);
F32 vs_agent_draw_distance(bool prioritizeNearby);
