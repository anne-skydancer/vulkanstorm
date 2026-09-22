# Noninvasive Mesa/Zink measurement harness

Status: external collector/analyzer implemented; an 8-second Zink login-screen
plumbing capture succeeded on 2026-09-22 (PresentMon 2.6.0, RX 9070 XT). It yielded
1,002 valid intervals in one stream. Its legacy CSV uses `msBetweenPresents`;
the analyzer accepts header capitalization variants, covered by a regression test.
Two preliminary 120-second in-world Zink captures also succeeded; see
[initial observations](mesa-zink-inworld-observations.md). Native OpenGL event
coverage also succeeded; one repeat was rejected after foreground focus was lost.
Controlled comparison and overhead qualification remain pending.
No renderer changes. The installed 7.2.5.81932 executable was used;
it differs from the local executable, so do not mix their results.

## Boundary

Start with the viewer's existing developer metadata. PresentMon is a complementary
timing channel, not a replacement for renderer-specific evidence.

## Existing viewer facilities to use first

| Facility | Evidence | Harness use |
|---|---|---|
| About / viewer log | Viewer, GL renderer/provider and driver identity | Establish actual backend; preserve relevant metadata with each run |
| Develop > Render Tests > Frame Profile | `profile.*.json`: context, shader time, binds, samples and triangles | Separate diagnostic captures; use existing `scripts/perf/profile_csv.py`, `profile_cmp.py` and `profile_pretty.py` |
| Develop > Consoles > Texture Console | Fetch/decode/create queue and texture information | Diagnose streaming stage pressure in dedicated observation runs |
| Develop > Consoles > Fast Timers | Instrumented CPU scopes available in this build | Inspect hot scopes; confirm coverage instead of assuming every Tracy scope exists here |
| Scene Statistics / Show Render Info / Statistics Bar | Scene complexity, rendering and aggregate frame information | Establish comparable workload and detect scene changes |

Frame Profile already writes viewer context through `getProfileStatsContext()`
and shader totals through `LLGLSLShader::finishProfile()`. Its shader query path
reads `GL_QUERY_RESULT`; profiling can affect scheduling and must not be treated
as a zero-overhead continuous FPS baseline. Profile JSON aggregates shaders, not
necessarily individual hero-probe faces or upload operations. Preserve original
JSON; supply explicit file paths to existing analysis scripts rather than relying
on whichever profile happens to be newest. Their default comparison tolerance is
not an automatically accepted performance threshold for this project.

Developer overlays themselves draw work. Record which are open and use identical
states for comparisons; keep diagnostic overlay/profile runs distinct from clean
timing runs. No new in-viewer logging is justified until this inventory is tested.

## External complementary timing

The first layer runs PresentMon externally through Windows ETW. It does not inject
code, hook GL, change settings/environment/driver options, move the camera, launch
or stop the viewer, alter synchronization, enable validation, or perform GPU
readback. The collector has nonzero CPU/I/O overhead; noninvasive does not mean
zero overhead. A fresh output directory and unique ETW session prevent accidental
overwrite or takeover of another collection session. No automatic elevation.

Existing viewer frame statistics aggregate timing over windows. Existing perfbot
and autopilot controls change scene execution; they are not automatically enabled.
Release builds have profiling configuration constraints. This first layer needs
no rebuild and keeps baseline and candidate instrumentation identical.

## Capture protocol

1. Notify the operator before any viewer control. Start the viewer manually, verify
   the actual backend from its log/About information (not merely its setting),
   executable identity, PID, GPU and driver. Record viewer and Mesa DLL hashes,
   revision, graphics settings, hero-probe settings, resolution, monitor refresh,
   VSync/driver caps, focus state, scene/camera and cache state in the manifest.
   Backend identity is operator supplied, not inferred from PresentMon's runtime.
2. Use matched uncapped settings. Disable adaptive quality through the agreed test
   configuration, not through this collector. Keep scene/time/weather/avatar load
   controlled as far as possible; document network and scene variability.
3. Warm up separately. Use a settled stationary scene, a repeatable hero-probe
   motion scene, and a separate texture-streaming trial. Do not mix these results.
4. Pilot capture on both native OpenGL and Zink: confirm usable event coverage,
   correct PID, main-window swapchain, duration and no ETW loss warnings. Missing
   events/metrics are unsupported evidence, not zero cost. The collector log is
   retained; event loss and requested duration are not automatically qualified.
5. Alternate A/B/B/A runs, at least three samples per backend, initially 120 seconds
   each after warmup. Keep raw CSV and per-run summaries; do not pool all frames
   across runs. Identify every swapchain; never silently combine media/plugin or
   secondary-window streams with the main viewer's stream.
6. Compare median and p95/p99 present intervals, rate, long intervals and variability
   between runs. Do not average reciprocal per-frame FPS. No automatic pass/fail
   performance thresholds until baseline noise and collector overhead are known.

## Usage

Use a reviewed PresentMon console executable supporting `--v1_metrics`; record
its version separately. The harness records its SHA-256. No downloader is included.

Create a JSON manifest with these nonempty fields (settings may be an object):
`backend` (`native-opengl` or `zink`), `backend_evidence`, `gpu`, `driver`,
`mesa_revision` (`not-applicable` for native), `viewer_revision`, `scene`,
`settings`, `cache_state`, `scenario`. Add the identity and controls listed above.
These are operator assertions, not automatically verified hardware facts.

```powershell
python scripts/perf/zink_capture.py capture --presentmon C:/Tools/PresentMon.exe --pid 1234 --manifest C:/Measurements/run.json --output C:/Measurements/native-01 --seconds 120 --delay 15
python scripts/perf/zink_capture.py analyze C:/Measurements/native-01/presents.csv --pid 1234
```

The delay gives time to return focus to the viewer. Capture never focuses it.
Artifacts: input manifest/command/tool hash in `capture.json`, raw `presents.csv`,
`collector.log`, and per-swapchain `summary.json`. Existing output directories
are rejected. Unsupported schema/no samples fail rather than report success.
Invalid intervals are counted, not silently made zero. All valid positive long
intervals remain in the distribution. Percentiles use nearest rank.

## What these measurements cannot establish

Present intervals are not displayed FPS, pure GPU time, CPU render-thread time,
or hero-probe update intervals. External timing alone cannot identify a slow GL
call or prove probe smoothness. Raw PresentMon fields must be interpreted according
to API/driver support, without subtracting unrelated timings to invent pass costs.

Separate visual-validation runs retain the same scene/settings and include video
for mesh flicker, probe stepping/seams, moving objects, transparency, water and
reopened UI. Video overhead is not mixed into the primary timing comparison.
The previous flashing is a separate correctness issue; both PPLL fixes remain.
The analyzer always reports visual acceptance as not evaluated.

## Overhead and deeper attribution gates

- Alternate collector-off/on/off trials with unchanged viewer settings, using an
  independent already-available frame counter or external display measurement.
  An off run cannot be measured by the collector being evaluated. If no independent
  reference is available, mark overhead unqualified rather than claim negligible.
- Record collector CPU/I/O and look for interaction with background workloads.
  Repeat with shorter/longer windows. Keep video/validation/sampling profilers out
  of primary runs. Default PresentMon GPU tracking may be more expensive; evaluate
  that cost separately before changing capture configuration for both backends.
- Only after the external baseline works, perform separate sampled CPU traces
  to locate submission/wait hotspots. Never compare their FPS directly with an
  unprofiled run as if overhead were equal.
- Per-hero-face/mip and upload attribution may require opt-in instrumentation.
  Design that separately: existing timing facilities where possible, bounded
  buffers, deferred export, no per-draw disk logging, no added GL waits/flushes or
  synchronous timer-query results. Validate instrumentation-off equivalence and
  instrumentation-on overhead before using it to select an optimization.
- Linux needs a separately qualified collector; the offline analyzer is portable
  but this Windows ETW capture is not claimed as Linux coverage.

## References

- [PresentMon console documentation](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md)
- [Archived failure review](mesa-zink-performance-archive-review.md)
