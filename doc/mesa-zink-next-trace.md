# Mesa/WGL correctness patch and next diagnostic capture

## State on 2026-09-22

The isolated `3p-mesazink` branch `codex/mesa-wgl-loader-init` lives at
`C:/Dev/vulkanstorm/worktrees/branches/codex/mesa-wgl-loader-init`.
It starts at recipe commit `68f184f`, retaining tested Mesa revision
`00e42c51b10d8e0769489156fa414f111897d515`. The newer original recipe/source
worktrees, including their pre-existing changes, are untouched. For later
integration, transplant the patch deliberately; do not accidentally roll a newer
package pin back by merging this historical recipe wholesale.

The added patch initializes the complete Kopper loader output, retains
`has_alpha=true`, defaults `present_opaque=false` and `compression=0`, and sets
`initial_swap_interval` using the existing framebuffer/device fallback. It does
not change fence waits, Windows present mode selection, or viewer/PPLL code.

Validation completed:

- All three package patches applied at the pinned Mesa revision.
- Full MSVC x64 Mesa/Zink WGL build succeeded with Meson `debugoptimized`
  (optimized code plus symbols), Meson 1.12.0, MSVC 19.51.36257.
- The actual patched helper passed 32 compiled poisoned-output cases: four
  storage patterns, four framebuffer intervals, two inherited device intervals.
  Test wrappers do not emulate a live WGL framebuffer or Vulkan driver.
- Reverse patch applicability and whitespace checks passed.
- DLLs assembled under the recipe's `diagnostic-build/bin/release`; that path is
  the recipe layout, not a claim of a Release viewer build. Matching PDBs remain
  under `mesa-wgl-loader-init-source/build-vulkanstorm/src/gallium/targets/`.
- Package identity is `26.3.0-devel-git.00e42c51b1-wglinit1`.

No DLLs were installed into the user's viewer; no viewer was launched. No new
RelWithDebInfo viewer executable has been built for this patch yet. Runtime
correctness, visual parity, and performance remain unverified.

## Before running

Use the performance branch's RelWithDebInfo viewer and a separate staged runtime,
not the installed viewer. Keep its PPLL corrections. Retain rollback copies and
record SHA-256 of the viewer, both Mesa DLLs and matching PDBs. Compare patched
and unpatched Mesa built with identical compiler/options: the diagnostic build
above must not be compared to the installed Release Mesa as a patch FPS test.
Notify the user before taking PC input, then obtain the stable logged-in scene.

Record backend from logs, GPU and driver, Mesa revision plus patch digest, window
size, VSync/frame-limit state, AA, shadows, reflection detail, mirrors, draw
distance, textures, camera, fixed Midday environment, and cache warmup. Read the
effective GPU scheduling setting from Windows; an absent registry override is
not evidence that scheduling is disabled. Do not change it between runs.

Retain the original untraced 120-second PresentMon measurements. Collect short
diagnostic traces separately, bracketed by untraced measurements to estimate
collector overhead. Never label a traced run the uncapped FPS acceptance result.

## CPU, wait, and GPU timeline

Local `wpr -profiles` confirms CPU, GPU, DesktopComposition profiles. CPU verbose
includes sampled stacks, context switches, ready-thread events and their stacks.
WPR reported no active recording when checked; check again before each capture.
Do not cancel someone else's recording.

In an elevated PowerShell, create a fresh output directory, then:

```powershell
wpr -status
wpr -start CPU -start GPU -start DesktopComposition -filemode
# Return focus to the viewer. Allow settling, then hold the same view ~30 s.
wpr -stop '<fresh-output-directory>/zink-cpu-gpu.etl'
```

Record the stable foreground interval separately and exclude the focus changes
at either end in analysis. Repeat for native OpenGL with the same scene/settings.
Keep local symbols with the trace and point WPA to the matching viewer and Mesa
PDB directories. If privileges or profile startup fail, report the failure;
do not substitute a partial trace silently.

In WPA, filter by the recorded viewer PID and stable time interval:

1. CPU Usage (Sampled): identify rendering-thread work in the viewer, Mesa state
   tracker, Zink, and driver. Treat samples as attribution, not exact call timers.
2. CPU Usage (Precise): distinguish running, ready, and blocked time. Inspect
   switch-out stacks through `SwapBuffers`, WGL `stw_st_flush`, Mesa `st_flush`,
   `fence_finish` and driver waits. A missing optimized frame is not proof that
   the wait did not occur; check symbol resolution and inlining first.
3. GPU activity and queue execution: correlate outstanding work with those waits
   and DWM presentation. Account for other processes and inspect event loss.
4. PresentMon: align the same interval by timestamps. Its DXGI Present duration
   does not measure the complete WGL SwapBuffers call.

ETW stack sampling/context switches can identify a dominant wait path but cannot
guarantee exact inclusive per-frame SwapBuffers duration. If ambiguity remains,
the next separate diagnostic patch should time the entire viewer SwapBuffers
call with buffered frame records, and validate its overhead before use. Do not
infer that duration by subtracting independently aggregated medians.

## GPU passes and swapchain details

In separate matched diagnostic runs, use the viewer's existing shader profiling
to capture multiple representative frames under identical lighting and geometry.
Report shadow, terrain, alpha/PPLL and reflection costs; do not compare the old
unmatched one-frame profiles as backend costs. ETW alone cannot name these passes.

To settle the surface-state question, a separate diagnostic build should log once
at successful swapchain creation: supported composite-alpha flags, chosen alpha,
format, extent, actual image count, present mode and effective swap interval.
Include recreations and their reasons. Do not infer these from flip-mode labels
or alter them to obtain a preferred result. This logging is not in the current
correctness patch.

## Acceptance and next optimization

Repeat matched captures in both backend orders. Require stable foreground state
and no event loss. Record ordinary scene and hero-probe motion separately, with
visual recordings checking meshes, alpha/glow, shadows and reflections. Mirrors
were off in the existing pair, so it does not qualify hero-probe smoothness.

Only then choose the first performance patch: viewer submission work if CPU
samples dominate; the measured GPU pass if GPU execution dominates. Any fence
change requires evidence of avoidable serialization and a correctness argument
for resource lifetime/reuse. The archived fence experiments remain excluded.
No FPS claim or visual acceptance follows from a successful compile or the
metadata initialization test.
