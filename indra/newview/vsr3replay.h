// Production material/resource replay. Diagnostic build only. LGPL-2.1.
#pragma once
class LLSD;
void vs_r3_local_images(LLSD&);

bool vs_r3_world_images(LLSD&);
void vs_r3_replay_retired(LLSD&);
void vs_r3_replay_cleanup();
