// Execute actual terrain dispatch, publication and Vulkan readback. LGPL-2.1.
#pragma once
class VSNativeSession;
class LLSD;
bool vs_terrain_replay_tick(VSNativeSession&,LLSD&);
