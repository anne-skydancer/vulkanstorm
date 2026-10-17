// Diagnostic-only execution of production texture pressure/restoration. LGPL-2.1.
#pragma once
class LLSD;
void vs_r3_pressure_replay_pump();
bool vs_r3_pressure_replay_tick(LLSD&);
void vs_r3_pressure_replay_cleanup();
