// Production ownership/lifecycle replay. Diagnostic build only. LGPL-2.1.
#pragma once
class VSNativeSession;
class LLSD;
void vs_r1_replay_pump();
bool vs_r1_replay_tick(VSNativeSession&, LLSD&);
void vs_r1_replay_cleanup();
void vs_r1_replay_retired(VSNativeSession&, LLSD&);
