# Initial in-world measurement observations, 2026-09-22

Status: preliminary diagnostic evidence, not an optimization result or visual
qualification. No rendering source changes. Both PPLL corrections remain intact.

## Configuration

- Installed Vulkanstorm 7.2.5.81932, revision fcaf5d5101. This executable differs
  from the local build; do not mix their results.
- Actual backend verified from startup log: Mesa/Zink, Mesa 26.3.0-devel
  00e42c51b1, AMD Radeon RX 9070 XT, driver 32.0.31041.3013.
- Stationary indoor camera; live environment and avatar idle animation active.
  Cache was warm and not cleared. No camera movement or window resize introduced.
- VSync was initially enabled and was disabled through Preferences for testing.
  Frame limiter was already disabled. Driver-level caps have not been verified.
- Mirrors disabled: these captures cannot evaluate hero-probe smoothness.
- Ultra graphics, 256 m draw distance, Sun/Moon + Projectors shadows,
  Static & Dynamic / Full Scene reflections, SMAA Ultra, max texture 2048.
- Window screenshots measured 2273 x 1064. Diagnostic overlays closed for timing.
- PresentMon 2.6.0, 15-second delay followed by 120-second capture, PID 10800.

## Timing results

These are application present intervals, not displayed FPS or GPU pass times.
Nearest-rank percentiles; one swapchain; zero invalid intervals in either run.

| Run | Valid intervals | Mean presents/s | p50 ms | p95 ms | p99 ms | Max ms | Intervals >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| zink-01 | 2512 | 20.94 | 46.01 | 51.73 | 91.57 | 111.14 | 199 |
| zink-02 | 3060 | 25.51 | 39.02 | 42.36 | 45.22 | 52.06 | 1 |

First run: 2042 Hardware Independent Flip and 470 Composed Flip samples.
Second run: all 3060 Composed Flip. The cause and performance impact of this
presentation difference are unestablished. Do not attribute the difference to
a code improvement, or average the runs into a supposedly stable baseline.
Live scene variation and collector overhead remain uncontrolled/unqualified.
Collector logs contain a process-query privilege warning, but successful samples
for the selected PID and no reported event-loss warning.

## Separate viewer diagnostics

One Frame Profile, taken between clean timing runs, reported 15.66 ms summed
shader time, 505 binds, 81.59 million samples and 1.437 million triangles.
The JSON time unit is seconds (LLGLSLShader::dumpStats()). Largest shader entries:

| Shader | Time ms |
|---|---:|
| Deferred Tree Shadow Shader | 3.738 |
| Deferred Shadow Alpha Mask Shader | 2.072 |
| Deferred Shadow Shader | 1.815 |
| Deferred Terrain Shader | 1.476 |
| Deferred Soften Shader | 0.573 |

This makes shadow work worth further measurement; it does not establish the
frame bottleneck. Do not subtract this separately profiled shader sum from
PresentMon intervals to manufacture a CPU cost. Frame Profile itself perturbs
execution through timer queries.

Texture Console observation: HTP, DEC and CRE all zero, no pending fetch at
that instant. This is a snapshot, not proof that streaming was inactive for
the entire capture. Expanded Fast Timers showed mostly small general viewer
scopes and a large Frame remainder; it did not provide useful rendering-pass
CPU attribution in this observation.

## Artifacts and next steps

Raw CSV, collector logs, manifests and summaries remain outside Git under
`%LOCALAPPDATA%/VulkanStorm/measurements/2026-09-22-inworld/`.
`zink-profile-01.json` preserves the separate shader profile. Raw context can
contain machine, region and account-related identifiers; it is not committed.

Next: verify native OpenGL event coverage using the same installed executable;
control lighting, camera, foreground/presentation state and warmup for repeated
matched comparisons; qualify collector overhead. Then establish a separate
mirror/hero-probe scene and visual recording protocol. No visual regression
pass can be inferred from these CSV files or occasional screenshots.

For comparison preparation, the renderer was changed to OpenGL through the UI.
VSync remains disabled for uncapped testing and must be restored to its original
enabled state when testing ends. Authentication is handed back to the operator.

Restart verified native AMD OpenGL in the log (ATI Technologies Inc.; Radeon
RX 9070 XT; OpenGL 4.6.0 Core Profile Context 26.9.1.260826). The viewer displayed
a graphics-card-change notice and reset graphics settings to recommended levels.
All effective settings must therefore be rechecked before a native measurement;
backend selection alone does not preserve an equivalent workload. User input
was detected during handoff and Preferences was subsequently observed open.

## Native OpenGL follow-up

After the operator logged in, PID 21152 used the same installed executable and
camera/location. Preferences verified the recorded General, Hardware and
Rendering 1 settings. Anisotropic filtering had become enabled; it was disabled
to match the original Zink observation. VSync and frame limiting were off.
Mirrors remained off. This was a recorded-setting check, not a complete dump of
all effective renderer controls. Live environment continued to advance.

| Run | Valid intervals | Mean presents/s | p50 ms | p95 ms | p99 ms | Max ms | Intervals >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| native-01 | 4545 | 37.88 | 26.16 | 28.80 | 31.62 | 38.27 | 0 |
| native-02 (rejected) | 3563 | 29.71 | 26.94 | 69.31 | 73.12 | 85.67 | 568 |

Both runs had one swapchain, zero invalid intervals, all samples reporting
Hardware Independent Flip, and no event-loss warning in collector logs.
Native-01 had a group notification visible immediately before capture, which
was gone at the end. Native-02 ended with VS Code covering the inactive viewer;
the viewer's visible FPS counter was about 14.6. Reject native-02 for foreground
comparison: focus loss contaminated it, although its exact onset was not logged.
Independent Flip alone does not establish continuous foreground focus.
No automated input was sent during either timing capture.

The separate native Frame Profile (`native-profile-01.json`, source timestamp
2026-09-21T23:40:26Z) reported 22.43 ms summed shader time, 517 binds,
77,360,178 samples and 1,956,786 triangles. Largest entries: Deferred Shadow
6.017 ms, Tree Shadow 5.008 ms, Star Program 2.883 ms, Shadow Alpha Mask 1.529 ms.
The different triangle workload and star pass reinforce that the native and
Zink single-frame profiles are not a matched shader benchmark. The native
present rate is higher in native-01, but the full difference cannot yet be
attributed to backend overhead.

Raw native capture artifacts share the directory above. The repeat remains
preserved, with rejection recorded in this note; the generic analyzer summary
continues to label all captures unqualified. No renderer source was changed.
At handback the viewer remains logged in on native OpenGL, VSync and frame
limiting off, anisotropic filtering off. Restore original VSync when testing
ends. Next controlled measurements require continuous foreground focus, fixed
environment, consistent settings, and separate hero-probe/visual qualification.

## Fixed Midday series

After another operator relog, PID 34520 was verified as native AMD OpenGL from
the startup log. General, Hardware and Rendering 1 preferences were checked:
same recorded settings, anisotropic filtering off, VSync/frame cap off, mirrors
off. World > Environment initially showed Use Shared Environment checked.
Selected Midday (not Midday Legacy) as a local override and allowed 60 seconds
to settle before the next 120-second capture. Camera and window were unchanged;
avatar animation, clouds and network activity were not frozen. An existing group
notice was dismissed before capture. No automated input or screenshots occurred
during the measurement itself.

| Run | Valid intervals | Mean presents/s | p50 ms | p95 ms | p99 ms | Max ms | Intervals >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| native-midday-01 | 4083 | 34.04 | 29.12 | 31.94 | 34.35 | 39.56 | 0 |

One swapchain, zero invalid intervals, all Hardware Independent Flip. No event
loss warning; same process-query privilege warning as previous captures.
Viewer foreground observed before and after, with operator asked to leave input
idle; continuous focus was not instrumented. No foreground-loss evidence was
observed for this run. Capture remains unqualified for collector overhead and
visual acceptance. Artifacts: `native-midday-manifest.json` and
`native-midday-01/` in the existing external measurement directory.

This starts a new fixed-lighting series, not a matched repeat of the earlier
shared-environment workloads. A Zink capture with this same Midday selection is
needed before a backend comparison. At handback native OpenGL remains logged
in, Midday remains selected, VSync/frame limit/anisotropic filtering remain off.
Original environment was shared; restore it when measurement work ends.

### Matching Zink capture

PID 30024 was verified from startup logs as Mesa/Zink on RX 9070 XT,
Mesa 26.3.0-devel 00e42c51b1, using the same installed executable. General,
Hardware and Rendering 1 preferences matched the recorded native Midday setup.
Chair textures were initially incomplete and visibly finished loading during
settings inspection. Reapplied Midday using the observed Ctrl+Shift+Y shortcut,
then allowed 60 seconds before the 120-second capture. No input or screenshots
were automated during collection. Foreground was observed before and after;
continuous focus and texture readiness were not instrumented.

| Run | Valid intervals | Mean presents/s | p50 ms | p95 ms | p99 ms | Max ms | Intervals >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| zink-midday-01 | 2410 | 20.09 | 49.59 | 53.16 | 55.68 | 63.20 | 930 |

One swapchain, zero invalid intervals, all Composed Flip. Collector reported no
event-loss warning, with the same process-query privilege warning as before.
Artifacts are `zink-midday-manifest.json` and `zink-midday-01/` under the external
measurement directory. An object-hover tooltip was visible after capture.

For this single pair, Zink's mean present rate was about 41% lower than native
OpenGL and its median interval about 70% longer. This is a measured pair, not a
general backend penalty: native used Independent Flip while Zink used Composed
Flip. Live scene/animation/network variation, residual streaming, unmeasured
collector overhead and lack of repeated matched samples remain limitations.
No visual-acceptance claim follows from timing data. Mirrors were off.

Next attribution priority: determine why the presentation paths differ, then
separate CPU submission/wait costs from GPU pass costs with diagnostic evidence.
Do not alter synchronization or lower graphics quality merely to improve these
numbers. Existing PPLL fixes remain unchanged. At handback the viewer remains
logged in on Zink with Midday selected and VSync/frame cap/anisotropic filtering
off for the measurement setup.
