# Mesa/Zink performance baseline

## Verified starting point

The consolidated RelWithDebInfo build completed successfully on 2026-09-22,
based on release 7.2.5.81939 (a392b4137b) plus the profiling changes here.
Tracy, Mesa/Zink, AVX2, and LTO are enabled; Tracy GPU queries are disabled.
The executable and matching PDB are under build-vc170-64/newview/RelWithDebInfo.
The local build log is build-vc170-64/compile-81939.log. Both Mesa DLL hashes
match the released package. The viewer has not been launched for runtime
qualification of this consolidated build.

The 12 apitrace harness tests and four PresentMon harness tests pass. The
optional notification replay patch passes git apply --check and is not active.
This checkpoint establishes a diagnostic baseline, not a performance fix.

## Consolidated investigation (2026-09-22)

This is the sole active worktree for the Mesa/Zink performance investigation.
The former mesa-zink-performance capture scripts now live in scripts/perf;
its eight measurement and analysis notes are alongside this document. Their
dates, old paths, package identities, and observations are historical evidence,
not current launch instructions. Current source is a392b4137b (release 81939).

The notification-gl-timing changes are preserved as the optional
scripts/perf/notification-replay-clock.patch. This is deterministic notification
replay, not passive timing instrumentation: it can override frame time and wait
for an external controller. It is deliberately NOT applied to the ordinary
Tracy profiling source. Its includes now resolve to this worktree's existing
tools/vulkan/diagnostic_replay_clock.h. Apply only for a separate replay
experiment; do not use controlled-clock runs as throughput measurements.

Local originals, SHA-256 copy records, old configuration, and package rollback
DLLs are preserved under logs/consolidation-20260922. Raw measurement captures
referenced in the imported notes remain in their original external measurement
directories. Superseded generated build output is not part of the consolidation.
The old manifest delta is already included in this baseline and was not reapplied.

Capture harness checks, from the worktree root:

```powershell
python -m unittest discover -s scripts/perf -p test_apitrace_harness.py
python -m unittest discover -s scripts/perf -p test_zink_capture.py
git apply --check scripts/perf/notification-replay-clock.patch
```

The older setup narrative below is retained for provenance. Instructions about
keeping the two former worktrees separate are superseded by this consolidation.

## Current layout after correction (2026-09-22)

The profiling build was stopped before moving this worktree with git worktree move.
The source is now C:/Dev/vulkanstorm/worktrees/zink-performance-baseline.
The active build directory is its own build-vc170-64 subdirectory, regenerated
with autobuild configure -A64 -c RelWithDebInfo --id 81939 and Tracy ON,
GPU queries OFF, on-demand ON, local-only ON, Mesa/Zink ON.
No compilation is requested as part of this relocation.

WebRTC's package contains headers longer than the traditional Windows path
limit even at this location. The package was installed through autobuild using
the extended-path spelling of the same packages directory (no local package
override). For a fresh dependency installation, run from the worktree root:

```powershell
$extended = [string]::Concat([char]92,[char]92,'?',[char]92,(Resolve-Path 'build-vc170-64/packages').Path)
autobuild install -A64 --install-dir $extended webrtc
```

If retrying an interrupted extraction, its read-only headers may need their
read-only attribute cleared before reinstalling. Do not change source files.

The former short-path build C:/Dev/builds/zink-tracy-rwdi is retained only as
historical output and evidence. Do not resume it: its cache points to the old
source location. The previous partial in-worktree build directory is retained
as build-vc170-64-before-relocation and must not be reused either.

Earlier build commands and paths below document the previous setup; this
section supersedes them. Run future configuration and builds from this worktree
using build-vc170-64, without a separate -B override.

From the worktree root, the full regeneration command is:

```powershell
autobuild configure -A64 -c RelWithDebInfo --id 81939 -- '-DINSTALL_PROPRIETARY:BOOL=OFF' '-DROOT_PROJECT_NAME:STRING=Firestorm' '-DVIEWER_CHANNEL:STRING=Vulkanstorm-RelWithDebInfo' '-DUSE_AVX2_OPTIMIZATION:BOOL=ON' '-DUSE_LTO:BOOL=ON' '-DUSE_KDU:BOOL=OFF' '-DUSE_MESAZINK:BOOL=ON' '-DUSE_TRACY:BOOL=ON' '-DUSE_TRACY_GPU:BOOL=OFF' '-DUSE_TRACY_ON_DEMAND:BOOL=ON' '-DUSE_TRACY_LOCAL_ONLY:BOOL=ON'
```

## Resumption against release 81939 (2026-09-22)

The profiling worktree has now been fast-forwarded to `a392b4137b`, the source
of release 7.2.5.81939 with PR #71 merged. The existing profiling changes below
remain local. The old package override has been replaced by the merged normal
download manifest; package naming and the master checkout are unchanged.
Earlier references below to `fcaf5d5101` describe the original capture baseline.

The reported busy-region session was preserved locally as
`C:/Dev/builds/zink-tracy-rwdi/resume-81939/reported-session.log`.
It confirms build 81939, Mesa/Zink 00e42c51b1, and Radeon RX 9070 XT.
At 15:43:01-15:45:23 UTC, most logged FPS samples are 3.9-4.8, with two
intervening samples of 6.0 and 6.5. These are periodic viewer statistics,
not individual frame measurements.

Memory and texture pressure are the leading diagnostic questions for this
episode. At 15:42:08 UTC the viewer reports 511 MB available physical memory;
at 15:43:08 it reports 914 MB available, 2,281 MB process working set and
21,277 MB process commit. The latter is Windows PagefileUsage accounting,
not a measurement of disk paging. Immediate post-load texture downscale
warnings reach occurrence 5,200 at 15:43:46. The code does perform scaleDown
on that warning path. These observations do not establish a leak, actual
hard page faults, or which component owns the allocations.

There are also background/foreground transitions elsewhere in the session.
The log cannot establish uninterrupted foreground focus for the slow interval.
Capture foreground state explicitly on the next reproduction.

Next capture: correlate Tracy texture creation/downscale and fence/present
zones with process commit, working set, system available RAM, hard faults,
and GPU dedicated/shared memory. Compare native OpenGL using the same
instrumented executable and equivalent scene/settings. Preserve visual
correctness. Redundant texture binds remain a separate measured-call-count
candidate, not an established explanation of this episode.

Reconfigure with `autobuild configure -A64 -c RelWithDebInfo --id 81939`,
passing `-B C:/Dev/builds/zink-tracy-rwdi` and the absolute worktree `indra`
path via `-S`, with Tracy ON, GPU queries OFF, and on-demand/local-only ON.
Build and configure logs for this resumption are in the `resume-81939`
directory above. Do not interpret this preparation as a runtime capture.

## Worktree isolation

Profiling changes belong only to
`C:/Dev/vulkanstorm/worktrees/zink-performance-baseline`.
Do not configure or build these changes in `C:/Dev/vulkanstorm`, which is the
unmodified master baseline. The first profiling build incorrectly reused that
root build directory; its runtime and metadata have been preserved under this
worktree's ignored `build-vc170-profile-preserved` directory. That preserved
runtime is diagnostic only. Its archived CMake cache records the old absolute
paths and must not be reused to configure this worktree. The fresh branch build
uses `C:/Dev/builds/zink-tracy-rwdi`, sourced exclusively from this worktree.
The long in-worktree build path failed while unpacking WebRTC headers; its
partially configured `build-vc170-64` directory is not a usable build.

## Scope and status

Baseline source: `fcaf5d5101` (2026-09-22), plus the profiling changes on
`codex/zink-performance-baseline`. No performance improvement is claimed until
matched runtime captures and visual checks have been completed.

This CPU-profiling build supplements existing measurements; it is not the first
baseline. The separate worktree `worktrees/branches/codex/mesa-zink-performance`
contains the current capture harness and September 22 findings. Preserve that
worktree's untracked scripts, notes, and runtime artifacts.

The recovered `2026-09-22-apitrace-viewer-02/findings-summary.json` under
`%LOCALAPPDATA%/VulkanStorm/measurements` records a selected 1,639-swap window:

| API | Calls requesting the same state | Fraction of observed calls |
| --- | ---: | ---: |
| Texture binding | 3,100,932 | 34.11% |
| Active texture unit | 3,830,276 | 42.87% |
| Vertex attribute pointer | 95,621 | 0.300% |

The trace ended abnormally with an unreadable tail. The analyzed interior window
precedes that tail, but its boundaries are approximate file-write frontiers.
These are prioritization counts, not FPS measurements or stability acceptance.
The existing unhooked PresentMon runs and their limitations are documented in
that worktree's `doc/mesa-zink-inworld-observations.md` and
`doc/mesa-zink-presentation-trace.md`. Do not replace them with traced timings.

The next optimization candidate is redundant texture-state suppression, ahead
of vertex-attribute caching. Current source audit confirms that `bindFast`
unconditionally activates and binds, while `GLTFSceneManager::bindTexture`
also changes active units and bindings directly, bypassing `LLTexUnit` state.
A cache change must account for that path. PPLL head-image allocation temporarily
binds a texture but restores the actual previous binding; retain that behavior.
`LLTexUnit::unbind` uses cached name zero while binding the white fallback for
2D textures, so the cached field is not always the literal GL binding. Preserve
missing-texture fallback and dirty sampler updates even when a bind is skipped.

The package pin is Mesa `26.3.0-devel-git.00e42c51b1`. Record hashes of the
actual executable and both staged Mesa DLLs for each comparison; the package
pin alone does not prove which runtime was loaded. Confirm the renderer from
the viewer log/About information, including any fallback to native OpenGL.

## Build and instrumentation

From this isolated branch worktree in PowerShell, using its separate short
Windows build directory. Branch builds use **RelWithDebInfo**, never the root
baseline's Release build. The short path avoids Windows path-length failures
in the WebRTC dependency headers:

```powershell
cmake -S indra -B C:/Dev/builds/zink-tracy-rwdi -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DUSE_TRACY:BOOL=ON -DUSE_TRACY_ON_DEMAND:BOOL=ON -DUSE_TRACY_LOCAL_ONLY:BOOL=ON -DUSE_TRACY_GPU:BOOL=OFF
cmake --build C:/Dev/builds/zink-tracy-rwdi --config RelWithDebInfo --target vulkanstorm-bin -- /m:6
```

This changes the local build configuration. CPU capture uses the existing
Tracy 0.11.1 package. GPU queries are disabled: `llprofiler.h` now respects
the CMake option instead of unconditionally enabling them. GPU profiling can
be requested separately with `USE_TRACY_GPU=ON`; do not mix that configuration
into a CPU-only comparison.

The `SwapBuffers` CPU zone is available even though the broad Win32 category
is disabled. `glClientWaitSync` has a separate texture-category CPU zone.
Existing zones include `glBufferSubData`, `glWaitSync`, and texture creation.
These measure host call duration. In particular, `glWaitSync` queues a GPU
dependency: a short host call does not prove that the GPU did not wait.
Time inside `SwapBuffers` can include pacing, VSync, or queue backpressure;
it is not by itself evidence of driver inefficiency.

The existing Tracy allocation events and enabled categories still contribute
profiling overhead. Compare the same instrumented executable across backends,
then confirm any eventual improvement with an ordinary Release build.

## Capture procedure

Launch/login and scene selection are operator-controlled. No automated command
here launches the viewer, changes its settings, or clears its cache.

1. Record source revision and dirty diff, executable/Mesa hashes, GPU and driver,
   actual GL renderer/version, window resolution, graphics preset, draw distance,
   VSync, frame cap, threaded texture settings, and relevant Mesa/Zink environment
   overrides. Record region, camera, avatar count, and cache state.
2. Use one executable for native OpenGL and Zink, restarting for backend changes.
   Keep all other settings fixed. Use matched uncapped runs to measure throughput,
   or a matched cap to investigate latency; label which experiment is being run.
3. For settled-scene throughput, allow texture loading and shader compilation to
   settle before recording. Collect at least three 30-second captures per backend,
   alternating backend order where practical. A changing live region limits
   reproducibility; record those changes rather than treating it as a static test.
4. In Developer > Profiling/Telemetry, enable **Start when connected**. Connect
   the local collector when ready. Check that **Profiling** becomes active.
   If profiling was manually disabled during the session, re-enable it manually.
5. Save a representative visual capture for each run. Separately exercise camera
   movement, animated avatars, transparency, particles, texture transitions, and
   UI to look for flashing, stale textures, or other regressions.

Example collector command (use a distinct filename for every run):

```powershell
New-Item -ItemType Directory -Force logs/zink-baseline | Out-Null
& C:/Dev/builds/zink-tracy-rwdi/packages/bin/tracy-capture.exe -a 127.0.0.1 -s 30 -o logs/zink-baseline/zink-warm-01.tracy
& C:/Dev/builds/zink-tracy-rwdi/packages/bin/tracy-csvexport.exe logs/zink-baseline/zink-warm-01.tracy > logs/zink-baseline/zink-warm-01-zones.csv
```

The collector waits for a running instrumented viewer. Use the bundled
`tracy-profiler.exe` to inspect frame markers and thread timelines. Report
median/p95/p99 frame duration and the sample count, alongside CPU zones for
submission, uploads, and presentation. Zone totals overlap when nested; do not
sum inclusive times as though they were disjoint. Zone-summary CSV is not a
frame-time distribution.

For shader-focused investigation, the existing Develop > Render Tests > Frame
Profile command and `scripts/perf/profile_cmp.py` provide a separate diagnostic.
Run it separately from the normal throughput capture because it adds measurement
work and answers a different question.

## Crowded-region texture arrival

Keep this separate from the settled-scene comparison. For each backend, use
controlled cold- and warm-cache trials with the same region/camera and record
Texture Console (Ctrl+Shift+3) at approximately 0, 10, 30, and 60 seconds.
Document how cold-cache state was established; do not infer it from a restart.
Preserve the user's normal cache unless a separate cold-cache setup is agreed.

| Evidence | Candidate stage to investigate |
| --- | --- |
| High HTP/fetch latency, low decode backlog | Network/fetch prioritization |
| High cache-read latency | Cache/disk contention |
| Growing DEC | Decode throughput |
| Growing CRE while DEC drains | Texture creation/upload |
| Repeated blur/reload near the memory limit | Residency/discard pressure |

Capture queue trends, latency, visible texture completeness, and frame-time
spikes together. A queue snapshot alone does not establish the root cause.

## Decision gate

Choose one change only after the trace identifies a repeatable limiting stage.
Repeat the same baseline, then qualify visual correctness before accepting it.
Do not remove synchronization, increase texture pipeline limits, change codecs,
or change Mesa descriptor/presentation modes based solely on aggregate FPS.
Windows/Linux on AMD/NVIDIA remain the qualification scope; one local capture
qualifies only the tested backend/GPU/driver combination.
