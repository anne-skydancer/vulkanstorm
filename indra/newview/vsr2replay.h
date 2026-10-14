// Production geometry/appearance replay. Diagnostic build only. LGPL-2.1.
#pragma once
class VSNativeSession;
class LLSD;
bool vs_r2_replay_tick(VSNativeSession&, LLSD&);
void vs_r2_replay_cleanup();
void vs_r2_replay_retired(VSNativeSession&, LLSD&);
