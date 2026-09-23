# Initial WGL loader patch runtime validation (2026-09-22)

Branch viewer fcaf5d5101, RelWithDebInfo 7.2.5.81932, built successfully with
OpenJPEG, SoLoud, Zink, LTO, no installer and crash reporting disabled.
Viewer SHA-256: 20CAFA7E5A32FBD8990D44FE5BDE9CB97EF640513F00B584722E6D1C9BBDC9B1.
Patched Mesa 00e42c51b1 debugoptimized DLL module paths and hashes were verified
in process 30080. Startup and manual login succeeded. Normal settings.xml was
not used for test writes; a separately named settings copy was loaded.

Evidence directory:
`%LOCALAPPDATA%/VulkanStorm/measurements/2026-09-22-wglinit/`.
It contains artifact identity, test manifest, PresentMon CSV and summary,
shader profile, validation-status.json, and a prepared elevated trace script.

The stationary 120-second PresentMon sample returned 2433 valid intervals,
zero invalid intervals, all Composed Flip: mean present rate 20.285 Hz,
median 49.0626 ms, p95 52.3444 ms, p99 54.7521 ms, max 60.8566 ms.
633 intervals exceeded 50 ms. The collector emitted its process-query privilege
warning but completed successfully. No event-loss warning was reported.
Collector overhead and continuous foreground state are not qualified.

UI checks: Ultra, 256 m, Sun/Moon + Projectors, Static & Dynamic reflections,
Full scene coverage, SMAA Ultra, anisotropic filtering off, mirrors and SSR off.
Local Midday was visibly checked. VSync and frame limiting are overridden off by
launch arguments. Window 2273 x 1064; camera and window dimensions unchanged.
GPU RX 9070 XT, driver 32.0.31041.3013. Avatar idle animation/network remain live.

No obvious visual artifact appeared in the inspected screenshots. This does
not establish absence of intermittent flicker, motion parity, or hero-probe
smoothness. Mirrors remained off; hero-probe validation is outstanding.

A separate one-frame shader profile summed to 16.1273 ms, 517 binds,
80,257,661 samples and 1,897,883 triangles. Largest shader times:
tree shadow 4.080 ms, shadow 3.119 ms, alpha-mask shadow 1.844 ms.
These sum to approximately 9.04 ms. Query timings are diagnostic, not a complete
frame decomposition or evidence of a patch performance benefit.

WPR CPU/GPU/DesktopComposition recording failed with 0xc5585011:
"Failed to enable the policy to profile system performance."
Subsequent status confirmed WPR was not recording. The prepared
capture-elevated.ps1 passed PowerShell syntax parsing; elevated execution is
pending. It refuses an existing trace/session, allows 15 seconds for foreground
settling, records a 30-second stable interval, and stops its own recording.

An unpatched Mesa baseline at the same revision, compiler and debugoptimized
configuration has built but has not run yet. Do not compare this viewer against
the older installed Release executable to attribute a gain or regression to
the patch. Matching baseline, repeatability and visual qualification remain open.

## Elevated trace analysis

The operator subsequently saved `patched-cpu-gpu.etl` (3,240,099,840 bytes).
Trace start 2026-09-22 01:33:17.5578603 UTC, end 01:34:25.6006811 UTC.
Header reports zero lost events and buffers. WPR is no longer recording.
Analysis uses the operator script's stable interval 01:33:33.6480072 through
01:34:03.6666107 UTC (relative 16.090147 through 46.108750 seconds).

Xperf CPU samples resolve viewer and Mesa functions using the matching PDBs.
OS and proprietary AMD function names remain unresolved. Sample weights summed
by module for viewer PID 30080:

| Module | Sampled CPU seconds |
|---|---:|
| libgallium_wgl.dll | 13.014 |
| vulkanstorm-bin.exe | 3.875 |
| ntoskrnl.exe | 3.480 |
| amdvlk64.dll | 2.422 |
| ntdll.dll | 0.853 |

Prominent individual symbols include zink_set_vertex_buffers_optimal (2.842 s),
_mesa_HashLockMutex (1.129 s), bind_samplerview_resource_stage (1.014 s),
zink_bind_vertex_addresses (0.849 s), st_get_texture_sampler_view_from_stobj
(0.693 s), and zink_set_sampler_views (0.691 s). These are sampled CPU costs,
not function wall-clock durations. The lock helper's name does not prove
contention. Optimized/inlined code may affect attribution.

Context-switch accounting totals 24.946 CPU seconds across viewer threads in
30.019 wall seconds. The busiest two thread IDs, 30268 and 24836, accumulate
11.673 and 9.176 running seconds respectively. These totals do not establish
which work is on the critical path.

Butterfly call stacks place zink_set_vertex_buffers_optimal below
tc_batch_execute. Both the combined-event report and a separate CSwitch-only
report contain the WGL flush path through stw_st_flush, st_context_flush,
zink_fence_finish and zink_screen_timeline_wait. This confirms that the source
path is exercised, not that the wait is avoidable. Butterfly hit counts are
not blocked-time measurements; inclusive SwapBuffers wall time is still unknown.

Offline PresentMon finds 693 present records in the stable interval, all
Composed Flip. Median interval 43.2503 ms; median GPU-active estimate 30.1306 ms;
median until-displayed 6.4607 ms; median Present API duration 0.1693 ms.
Effective HAGS state remains unknown, so GPU-active timing is indicative.
The ETW-derived run differs from the earlier untraced sample and cannot be used
as an overhead calculation or patch speedup. No subtraction of these medians
is a valid CPU/GPU frame decomposition.

Outputs retained beside the trace: trace-stats.txt, cpu-samples.txt,
cpu-threads.txt, viewer-stacks.txt (HTML), viewer-switch-stacks.html,
trace-processes.txt, trace-presents.csv and trace-analysis-summary.json.

Next: matching unpatched Mesa run with the same viewer/toolchain/settings and
scene. The evidence prioritizes vertex-buffer and texture/sampler state work
for a source audit. It does not yet justify an optimization, removing fence
waits, or changing presentation mode. No viewer input was taken during this
offline analysis.

## Unpatched baseline stationary captures

Baseline PID 31852 uses the same viewer SHA-256 and verified baseline Mesa DLL
hashes from artifact-manifest.json. The latest patched settings were copied to
settings-wglinit-baseline-20260922.xml; identical launch overrides disable VSync
and frame limiting. Computer-use was unavailable, so current scene, foreground
and visual checks rely on the operator. User reported logged in and settled.

`baseline-01` completed with zero invalid intervals, but is excluded from patch
comparison. Its median was about 88-89 ms for the first 90 seconds, then about
46 ms late in the run, with Composed Flip changing to Independent Flip. The user
was unsure of foreground/occlusion history. No resize was observed or inferred;
presentation-mode causality is not established. qualification.json records this.

After instructing the operator to bring the viewer fully forward and remain
idle, `baseline-02` captured 120 seconds following a 30-second settling delay:
2590 valid intervals, zero invalid; all Independent Flip. Mean present rate
21.5960 Hz, median 46.1014 ms, p95 49.0123 ms, p99 51.1124 ms, max 54.8936 ms.
The four successive 30-second medians were 46.12, 46.11, 46.07, 46.07 ms.
This is internally consistent, but not visual qualification or independently
logged foreground verification. The collector completed with only its known
process-query privilege warning.

Patched-01 was 20.2850 Hz and 49.0626 ms median, all Composed Flip, in a different
session. Do not attribute the difference to the patch. The prepared
capture-baseline-elevated.ps1 uses baseline-cpu-gpu.etl and
baseline-trace-window.json, preserving patched outputs. Baseline CPU/GPU trace
and matched shader profiling are pending.

## First baseline ETW trace: focus confounder confirmed

The operator saved baseline-cpu-gpu.etl (1,660,944,384 bytes), with zero lost
events/buffers. Marked interval: 2026-09-22 06:00:02.2080421 through
06:00:32.2190336 UTC; relative 15.573342 through 45.584334 seconds.
Offline PresentMon found 325 records, all Composed Flip, median interval
91.1759 ms. This trace is excluded from foreground performance comparison.

Xperf focuschange provides direct evidence: Code.exe PID27500 held focus from
5.071520 to 46.736649 seconds, covering the entire marked baseline interval.
The viewer PID31852 had focus earlier from 2.221180 to 5.071520 seconds.
For comparison, the patched trace's viewer PID30080 gained focus at 6.432226
seconds and retained it through the end, including its marked interval.

llappviewer.cpp's background path sleeps for BackgroundYieldTime (default 40ms)
when the window is invisible or lacks focus. This provides a source explanation
consistent with the slower unfocused run; the exact setting/sleep duration was
not independently measured. No resize trigger is inferred.

Baseline sampled CPU totals (Mesa 6.330 s, viewer 1.978 s) reflect far fewer
rendered frames and must not be interpreted as an efficiency gain over the
patched trace. The same vertex-buffer/texture state symbols and WGL fence path
are present. Preserve these as path evidence, not a matched performance result.

baseline-focus-events.txt, patched-focus-events.txt, baseline CPU reports,
baseline-switch-stacks.html and baseline-trace-qualification.json retain the
evidence. capture-baseline-elevated-02.ps1 passed syntax validation and writes
baseline-cpu-gpu-02.etl plus baseline-trace-window-02.json without overwriting
either original trace. Foreground baseline repeat remains pending.

## Baseline ETW repeat: foreground verified

baseline-cpu-gpu-02.etl saved successfully (1,836,056,576 bytes), with zero lost
events/buffers. Start 2026-09-22 06:04:46.4339230 UTC. Marked interval
06:05:01.7211200 through 06:05:31.7342091 UTC, relative 15.287197 through
45.300286 seconds. Focus events show viewer PID31852 gained focus at 0.754990
seconds and retained it through the end. This is usable foreground diagnostic
evidence; the first baseline ETW run remains excluded.

| Marked trace interval | Patched | Baseline repeat |
|---|---:|---:|
| Present records | 693 | 629 |
| Mean present rate, Hz | 23.068 | 20.967 |
| Median interval, ms | 43.2503 | 47.5083 |
| Median GPU-active estimate, ms | 30.1306 | 34.6924 |
| Median DXGI Present API duration, ms | 0.1693 | 0.6679 |
| Median until-displayed, ms | 6.4607 | 0.8334 |
| Presentation mode | Composed Flip | Independent Flip |
| Summed viewer-process CPU running seconds | 24.946 | 21.999 |

Sampled module CPU time normalized by present count (aggregate work across
threads per present, NOT inclusive call duration or frame critical-path time):

| Module | Patched ms/present | Baseline ms/present |
|---|---:|---:|
| Mesa Gallium | 18.779 | 17.986 |
| Viewer | 5.592 | 5.287 |
| Windows kernel | 5.021 | 5.011 |
| AMD Vulkan driver | 3.495 | 3.394 |

Both traces show the same leading Mesa CPU symbols: vertex-buffer state,
vertex addresses, texture lookup and sampler/resource binding. Baseline sampled
zink_set_vertex_buffers_optimal time is 2.406 s, versus patched 2.842 s;
different frame counts matter. CSwitch-only stacks again contain stw_st_flush,
st_context_flush, zink_fence_finish and zink_screen_timeline_wait. These stack
counts do not measure wait duration or prove removable synchronization.

No consistent patch performance winner is established: the untraced samples
favored baseline, while these traced samples favor patched. Sessions, modes,
live scene/animation and tracing conditions differ. HAGS remains unverified.
Do not use the GPU-active estimates or normalized CPU samples as an exact
per-frame decomposition. There is still no matched baseline shader profile,
continuous visual qualification or hero-probe motion test.

Recommendation: retain the initialization as a correctness fix without a speed
claim. The repeated state-handling costs justify a focused source audit of
vertex-buffer and texture/sampler state changes before implementing any
performance change. Leave synchronization and present mode unchanged.

Evidence: baseline-02-trace-stats.txt, baseline-02-focus-events.txt,
baseline-02-cpu-samples.txt, baseline-02-cpu-threads.txt,
baseline-02-switch-stacks.html, baseline-02-trace-presents.csv,
baseline-02-trace-qualification.json and foreground-trace-cpu-comparison.json.
WPR is stopped; no viewer input was taken during this analysis.
