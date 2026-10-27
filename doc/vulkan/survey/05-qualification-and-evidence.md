# 05 — Qualification and evidence

The `vkstorm-vulkan` line carries a large qualification system: a two-job GitHub
workflow, a standalone DiligentCore test, a staged-viewer harness, in-viewer
replay drivers (R1, R2, R3), a source-insertion catalog pinned by file hashes,
a parity ledger with 3,131 entries, and 342 Python test files. At the surveyed
commit `51d1f729a6` (27 Oct 2026) the CPU-only `source-evidence` job passes all
233 checks for the first time since 16 Oct. The `graphics` job fails on all three
software-Vulkan configurations at its first build step: the standalone Diligent
test no longer compiles, because two production renderer files now include
viewer headers. The last fully green graphics run on `vkstorm-vulkan` was
[38978167413](https://github.com/anne-skydancer/vulkanstorm/actions/runs/38978167413),
on 16 Oct 2026 at `93da7c824a`. That run had no R3 steps. The R3 texture-cache steps
have never passed on all three configurations in one run. Most of the machinery proves
source accounting and compile-level contracts. It does not show runtime parity with
the OpenGL renderer: the parity ledger has one runtime-qualified entry and none
qualified against an authenticated grid.

All paths are relative to the repository root. Counts were taken from the
worktree at `51d1f729a6`. CI facts come from `gh run list` / `gh run view` /
`gh api .../actions/jobs/<id>/logs` on 27 Oct 2026, around 22:45 UTC.

## Scope and size

| Component | Location | Size (at `51d1f729a6`) |
|---|---|---|
| Workflow | `.github/workflows/software_vulkan.yml` | 1,021 lines; 2 jobs; 233 `run:` checks in `source-evidence`; 3-entry matrix with 44 steps per graphics job |
| Standalone Diligent test | `tests/software_vulkan/` | `CMakeLists.txt` 48, `diligent_render_test.cpp` 1,126, `viewer_geometry_test.cpp` 235, `geometry_dependencies.cpp` 21 lines |
| Runners/harnesses | `scripts/run_software_vulkan_tests.py` (255), `scripts/vulkan_viewer_process.py` (307), `scripts/run_vulkan_viewer_diagnostic.py` (362), `scripts/run_vulkan_session_replay.py` (1,821), `scripts/run_vulkan_texture_cache_tests.py` (439), `scripts/run_vulkan_texture_cache_instances.py` (156), `scripts/run_vulkan_parity_baseline.py` (346), `scripts/check_vulkan_parity_r0.py` (130), `scripts/package_vulkan_evidence.py` (513), `scripts/run_vulkan_geometry_tests.py` (64) | 4,393 lines |
| In-viewer replay drivers | `indra/newview/vs*replay*.cpp` (R1, R2, R3 cache/glTF/media/pressure/terrain, HUD icon, tooltip, environment, IM, terrain, native) | `vsr2replay.cpp` 1,923 lines + `vsr2readback.cpp` 184; built only when `VS_VULKAN_DIAGNOSTICS=ON` (`indra/newview/CMakeLists.txt:38`, `:2390-2399`) |
| Insertion catalog | `doc/vulkan/diligent-insertion-*.{md,json,csv}`, `diligent-platform-review.json`, `check_diligent_insertions.py` (361), `carry_reviewed_ranges.py` (114) | records JSON 90,165 lines; sites CSV 30,130 lines; platform review 80,076 lines |
| Milestone-1 boundaries | `doc/vulkan/milestone1-boundaries.json` (5,505 lines), `check_milestone1_boundaries.py` (158), `milestone1-source-acceptance.md` (269) | 124 hashed sources, 12 gates, 5 registries |
| Parity ledger | `doc/vulkan/parity/acceptance-ledger.json` (97,893 lines), `scripts/vulkan_parity_inventory.py` (318), `doc/vulkan/parity/evidence/` (6 files, ~4.4 MB) | 3,131 entries |
| Python tests | `scripts/tests/` | 347 `.py` files = 342 `test_*.py` + 5 support modules; 751 `def test_` methods; 153 `.cpp` and 3 headers; `fixtures/` holds 119 entries |
| doc/vulkan unit tests | `doc/vulkan/test_*.py` | 4 files, 26 test methods |

Commands used for the counts (run from the worktree root):

```sh
grep -c "run: python" <(sed -n 24,509p .github/workflows/software_vulkan.yml)   # 233
ls scripts/tests/test_*.py | wc -l                                              # 342
ls scripts/tests/*.py | wc -l                                                   # 347
cat scripts/tests/test_*.py | grep -cE '^\s*def test_'                          # 751
```

## CI jobs

The workflow is named "Vulkan qualification" (`software_vulkan.yml:1`). It runs on
pushes to `vkstorm-vulkan`, `vkstorm_1.1.0` and `codex/vulkan-world-parity`, on
PRs into `vkstorm-vulkan`, `vkstorm-devel` and `vkstorm_1.1.0`, and on manual
dispatch with an optional `clean_dependencies` input (`:3-13`). Concurrency is
per PR/ref, with `cancel-in-progress: false` (`:18-21`), so queued runs on a busy
branch end up "cancelled" rather than replacing the active one. Of 181 runs on
`vkstorm-vulkan`, 69 are cancelled, 91 failed, 20 succeeded and 1 is in progress
(`gh run list --workflow software_vulkan.yml --branch vkstorm-vulkan --limit 1000`).

### Job 1: `source-evidence` (`software_vulkan.yml:24-509`)

- Runner `ubuntu-24.04`, Python 3.11, `libboost-dev` and `PyYAML==6.0.2` (`:34-39`).
  No GPU, no viewer build.
- Since PR #225 every check has `if: ${{ !cancelled() && steps.deps.outcome == 'success' }}`
  (`:40-41` comment). Each check runs even after an earlier failure, and any failure
  still fails the job. Before #225 the first failure stopped the job, which hid later failures.
- 233 checks:

| Kind | Count | Commands |
|---|---|---|
| Insertion-catalog acceptance | 1 | `python doc/vulkan/check_diligent_insertions.py --accept` (`:42-43`) |
| Milestone-1 boundary manifest | 1 | `python doc/vulkan/check_milestone1_boundaries.py` (`:44-45`) |
| doc/vulkan unit tests | 1 | `python -m unittest discover -s doc/vulkan -p 'test_*.py'` (26 tests) |
| Generated terrain shader drift | 1 | `python scripts/generate_vulkan_terrain_shader.py --check` |
| Parity ledger drift | 1 | `python scripts/vulkan_parity_inventory.py` (no `--write`) |
| `scripts/tests/test_*.py` files | 228 | one step per file; 535 test methods in total |

### Job 2: `graphics` (`software_vulkan.yml:510-1021`)

- `needs: source-evidence` (`:511`), with no `if:` override. A failing source job
  therefore skips all graphics work.
- `timeout-minutes: 240` (`:515`); `fail-fast: false`.
- Matrix (`:516-528`):

| `os` | `driver` | `viewer_directory` |
|---|---|---|
| `windows-2022` | `swiftshader` | `build-vc170-64` |
| `ubuntu-24.04` | `swiftshader` | `build-linux-x86_64` |
| `ubuntu-24.04` | `lavapipe` | `build-linux-x86_64` |

There is no Windows Lavapipe entry and no physical GPU, vendor driver or macOS
runner. `doc/vulkan/software-vulkan-ci.md:13-16` states that this is intended.

Graphics steps in order, with what each one shows when it passes:

| # | Step (line) | What it does | What a pass shows |
|---|---|---|---|
| 4 | Verify Windows UI isolation (`:545`) | `scripts/tests/test_vulkan_viewer_process.py` (Windows only) | The private window-station/desktop launcher works on the hosted runner |
| 5-6 | Install tools / Linux build and X11 deps (`:548-570`) | pip requirements; apt gcc-14, Mesa/LLVM 18 build deps, Xvfb, PulseAudio | Environment only |
| 8-9 | Identify runner image / restore cache (`:571-585`) | `actions/cache/restore` keyed on OS, driver, image version and hashes of the dependency scripts | Nothing (cache) |
| 10 | Build and install pinned Autobuild GHI packages (`:586-594`) | `scripts/build_vulkan_dependencies.py` | The pinned DiligentCore/Vulkan GHI packages build from source |
| 11 | Build pinned test ICD and validation layers (`:595-601`) | `scripts/build_software_vulkan.py --driver <driver>` | SwiftShader or Mesa Lavapipe, plus validation layers, build from pinned sources |
| 12 | **Build standalone staged Diligent tests** (`:602-615`) | CMake on `tests/software_vulkan`, build, `ctest` (only `device_selection`) | Three production renderer TUs (`vsworldrenderer.cpp`, `vsuipixeloracle.cpp`, `vsuirenderer.cpp`) compile against the GHI alone; the device-selection policy test passes |
| 13 | Native Vulkan tests on software device (`:616-626`) | `run_software_vulkan_tests.py` modes `offscreen world present invalid invalid-sync bad-pixels bad-orientation` (Xvfb on Linux) | Device creation, pixel oracles, world/UI renderer contracts, native swapchain lifecycle, and that the validation and pixel oracles catch injected faults |
| 14 | Software tests without a display (`:627-638`) | Same runner with `--headless` (no `present`) | The same, minus presentation; `presentation_qualified: false` is recorded |
| 15 | Retain qualified dependencies (`:639-649`) | `actions/cache/save` | Nothing |
| 16-17 | Install GHI into viewer; configure and stage viewer (`:650-684`) | Autobuild `ReleaseFS_open`, RelWithDebInfo, `USE_DILIGENTCORE=ON`, `VS_VULKAN_DIAGNOSTICS=ON`, `LL_TESTS=OFF`, LTO and AVX2 on; provenance begin/finish; `check_vulkan_ci_staging.py` | The full viewer with diagnostics compiles and stages on that OS. The executable hash is bound to the committed source |
| 18 | Qualify shared CPU camera projection (`:685-689`) | `scripts/tests/test_native_camera_projection.py` against the build dir | CPU camera projection matches the native projection (compile-level, using the build) |
| 19 | Qualify viewer CPU geometry producers (`:690-692`) | `run_vulkan_geometry_tests.py` runs `viewer_geometry_test`, built from `tests/software_vulkan/viewer_geometry_test.cpp` by `indra/newview/CMakeLists.txt:3366-3373` | Vegetation, flexible-path and terrain CPU producers give the expected results without a device |
| 20 | Provision headless audio (`:693-715`) | PulseAudio null sink (Linux) | WebRTC can start; environment only |
| 21 | Build separate pinned software OpenGL reference (`:716-720`) | `build_software_opengl.py` builds Mesa llvmpipe (Lavapipe matrix entry only) | The independent GL reference exists |
| 22 | Record independent production viewer baselines (`:721-734`) | `run_vulkan_parity_baseline.py` startup and `--scene world`, then `check_vulkan_parity_r0.py` (Lavapipe only) | R0: repeatable GL-vs-Vulkan captures of the login screen and a two-primitive static scene; known tint regions are present. This is not a parity comparison |
| 23 | Native Vulkan presentation through the staged viewer (`:735-749`) | `run_vulkan_viewer_diagnostic.py`: `positive`, 8 injected failures, 6 UI cases, 4 startup cases, plus per-skin cases | The real viewer executable creates a window, device and swapchain, survives resize/zero-extent/minimize/restore, tears down cleanly, renders its UI and startup screens, and reports the expected failure for each injected fault |
| 24 | Connected UI and chat (`:750-763`) | `run_vulkan_session_replay.py --all-skins` against the loopback `SimulatorHTTP` | The viewer runs login → connected UI → chat → logout against a local fake grid in every skin |
| 25 | Production R1 ownership and lifecycle replay (`:764-782`) | session replay `--headless --r1-contracts` | R1 owner/generation/retirement contracts hold across login, region crossing, teleport, relogin and shutdown in the fake session |
| 26 | Production asynchronous simulator console replay (`:783-801`) | `--console-async` | The async console HTTP path crosses the real controller |
| 27 | Production R2 geometry and appearance replay (`:802-818`) | `--r2-contracts --terrain-contracts` (see [R2 replay](#r2-replay)) | The 53-step R2 script completes and the runner's checks pass |
| 28-36 | Production R3 texture cache × 9 (`:819-971`) | `run_vulkan_texture_cache_tests.py --case {cold,warm,read-only,truncated,corrupt-header,clear,resize,cache-version,codec-version}` | The persistent texture cache behaves correctly in each lifecycle/corruption case |
| 37 | Production R3 concurrent texture-cache instances (`:972-987`) | `run_vulkan_texture_cache_instances.py` | Two viewer instances share or exclude the cache correctly |
| 38-40 | Record environment; package; upload (`:988-1021`, `if: always()`) | `package_vulkan_evidence.py`, adding `--require-complete` only when the job succeeded; artifact `software-vulkan-<OS>-<driver>` | Evidence is hashed and archived. A failed job uploads a partial package |

## Current results

### Runs on `vkstorm-vulkan` and the PR branch of #225

| Run | Created (UTC) | Head | Result |
|---|---|---|---|
| [40595455174](https://github.com/anne-skydancer/vulkanstorm/actions/runs/40595455174) | 2026-10-27 22:31 | `51d1f729a6` (merge of #225) | `source-evidence` **success**; 3 graphics jobs **in progress** when this was written |
| [40587007906](https://github.com/anne-skydancer/vulkanstorm/actions/runs/40587007906) | 2026-10-27 21:27 | `082f137fa4` (#225 head, branch `codex/vulkan-source-evidence-tests`) | `source-evidence` success (240 steps, 0 skipped); **all 3 graphics jobs failed at step 12** |
| [40548535142](https://github.com/anne-skydancer/vulkanstorm/actions/runs/40548535142) | 2026-10-27 17:13 | `8fda71152e` (#223) | `source-evidence` failed at `check_diligent_insertions.py --accept` (stale pin); graphics skipped |
| 40252017972, 40248218764, 40224131358 | 2026-10-25/26 | #216, #210, #205 | `source-evidence` failed at `test_vulkan_session_replay.py`; graphics skipped |
| 40223765193, 40195463693, 40188551982, 40176645533, 40159257490 | 2026-10-25 | #204, #179, … #189 | failed at `check_diligent_insertions.py --accept`; graphics skipped |
| 39098473819 | 2026-10-18 04:50 | `b0b3d53aa3` | failed at `test_native_parcel_playback.py`; graphics skipped |
| [39086372280](https://github.com/anne-skydancer/vulkanstorm/actions/runs/39086372280) | 2026-10-18 01:24 | `51c41124ca` | **Last graphics execution on `vkstorm-vulkan`.** Lavapipe: R2 replay failed. Windows and Linux SwiftShader: everything through the 9 R3 cache cases passed; concurrent cache instances failed |
| [38978167413](https://github.com/anne-skydancer/vulkanstorm/actions/runs/38978167413) | 2026-10-16 22:51 | `93da7c824a` | **Last fully green run.** All 4 jobs passed. The graphics jobs had 32 steps (no R3 steps yet). `source-evidence` had 80 steps |

The run at `51d1f729a6` should fail like 40587007906 (Inference): the
trees of `082f137fa4` and `51d1f729a6` are identical (`5924f43173`).

Between 2026-10-18 01:24 and 2026-10-27 21:35, no graphics job ran on any branch.
Every run in that window failed or was cancelled in `source-evidence`, or was
skipped. Run 40587007906 on the #225 PR branch was the first graphics run in more
than nine days.

### The graphics failure (runs 40587007906, jobs 122484486862 / 122484486978 / 122484487047)

All three jobs failed at step 12, "Build standalone staged Diligent tests". Steps 13-37
were skipped, and steps 38-40 ran because they use `always()`. From the job logs:

- Linux Lavapipe (job 122484487047, log line 3825) and Linux SwiftShader
  (job 122484486978, line 4261):
  `indra/newview/llperfstats.h:35:10: fatal error: lluuid.h: No such file or directory`,
  reached through `vsworldrenderer.cpp:3`. Ninja stopped after this first error,
  so the `vsuirenderer.cpp` error is not reported on Linux.
- Windows SwiftShader (job 122484486862, lines 2954 and 2958): `error C1083` for
  `lluuid.h` (via `llperfstats.h`, from `vsworldrenderer.cpp`) and for
  `llimagegl.h` (from `vsuirenderer.cpp(4,10)`).

Cause: `tests/software_vulkan/CMakeLists.txt:24-31` compiles three production
files into `diligent_render_test`. Its comment at `:28` reads "These production
renderer sources depend only on the GHI and standard library", and the target
only adds `indra/newview` as an include directory. `git blame` shows that
`#include "llperfstats.h"` (`vsworldrenderer.cpp:3`) and
`#include "llimagegl.h"` (`vsuirenderer.cpp:4`) both come from `116a410fc8`
("Implement connected UI and R3 service checkpoint [skip ci]", 18 Oct 2026). That
commit changed 828 files (+49,002/−7,759) and never ran this workflow. The next
graphics run came nine days later. The failure is a build-contract break in the
test harness, not a rendering failure. Until it is fixed, CI does not run the
viewer build, the runtime replays, R0, R1, R2 or R3.

### When graphics last passed, step by step

| Stage | Last pass on all 3 configurations | Notes |
|---|---|---|
| Standalone Diligent build and software-device tests | 39086372280 (18 Oct) | Failing since 116a410fc8 (first observed 27 Oct) |
| Staged viewer presentation, connected UI/chat, R1 | 39086372280 (18 Oct) | |
| R2 replay | 38978167413 (16 Oct) | Lavapipe failed R2 in 39073748457 and 39086372280 |
| R3 cache cases (9) | never in one run | Windows and Linux SwiftShader passed all 9 in 39086372280; Lavapipe never reached them |
| R3 concurrent cache instances | never on Linux | Windows passed in 39063890556 (17 Oct). Failed on both SwiftShader jobs in 39086372280 |

The repository's own records agree. `doc/vulkan/ui-integration-checkpoint-2026-10-18.md:57-61`
says that run 39086372280 "failed all three graphics jobs" and that "these failures
remain open".

## Runtime tests

### Standalone Diligent test (`tests/software_vulkan/`)

`diligent_render_test` is a single executable. Its first argument selects the
device (`auto|SwiftShader|llvmpipe|amd|nvidia`) and its second the mode
(`diligent_render_test.cpp:1065-1074`). It links DiligentCore and three production
TUs. Contents:

- `selectionTests()` (`:81-100`): checks device-selection policy against synthetic
  AMD, NVIDIA and CPU `VkPhysicalDeviceProperties`. It is the only `ctest` case
  (`CMakeLists.txt:34`).
- `preflight()` (`:102-200`): raw Vulkan enumeration. The `invalid` and
  `invalid-sync` modes deliberately trigger `VUID-VkBufferCreateInfo-size-00912`
  and `SYNC-HAZARD-WRITE-AFTER-WRITE`, to show that the validation layers are active.
- `presentation()` (`:297-356`): a native Win32 HWND or SDL2/X11 window with a
  swapchain at 128×128. It resizes to 160×96, takes 3 zero-extent skips and 2
  minimized skips, restores, and requires exactly 9 presents (`:351`).
- `draw()`/`compare()` (`:423-476`): renders two 2×2 images and compares each
  pixel within one UNORM unit. `bad-pixels` and `bad-orientation` corrupt the
  oracle to show that it can fail.
- `worldContract()` (`:479-1035`): drives the production `VSWorldRenderer` and
  `VSUIRenderer` on 8×8 targets. It covers depth order, alpha replacement while old
  submissions are still in flight, textured, material and normal-map packets,
  BC3 compression accounting, bump mip generation, sampler anisotropy and point
  filtering, UI batching across resource/scissor boundaries, font-atlas updates,
  mesh and texture budget refusal and retirement, and UI blend precision. There
  are 39 `capture("*.ppm")` readbacks. `run_software_vulkan_tests.py:15-` lists
  37 required material readbacks.
- Every run must end with `errors == 0 && validationErrors == 0` (`:1120`).

`scripts/run_software_vulkan_tests.py` runs each mode as a child process. It isolates
the pinned ICD, and on Windows registers manifests because elevated runners ignore
loader environment overrides (`:32-69`). `assess()` (`:71-85`) accepts a
negative mode only if it fails with exactly the expected VUID or "Pixel oracle
mismatch", and a positive mode only with exit 0, `PASS <mode>`, a device identity
and loaded-library hashes. Any `DILIGENT` message of severity 2 or higher, or any
unexpected VUID, fails the mode.

What this shows: the production world and UI renderer classes produce exact pixels
for small synthetic scenes on two CPU Vulkan implementations, under validation.
It does not show viewer integration. `qualification-roadmap.md` states that "standalone
GHI tests cannot close the viewer integration gap".

### Viewer-process harness

- `scripts/vulkan_viewer_process.py:14-71`: on Linux this is plain `subprocess`.
  On Windows, `_run_windows` (`:72-`) starts the viewer on a private window station
  and a desktop that is never activated, with no foreground fallback. The
  headless variants (`:39-70`) are explicit modes, never error fallbacks.
- `scripts/run_vulkan_viewer_diagnostic.py` starts the staged viewer with
  `RenderBackend=Vulkan` (`:69-73`) and the environment switches
  `VS_VULKAN_DIAGNOSTIC_UI` / `_STARTUP` (`:279-280`). The viewer writes a JSON
  record. `assess()` (`:158-202`) checks the stage list (`STAGES`, `:18-21`), 9
  presents, 3 zero-extent skips, 2 minimized skips, zero validation errors, and
  a set of `ui_*_verified` booleans. `assess_startup()` (`:36-59`) checks 17
  presents, 24 readbacks and about a dozen `*_verified` flags. Negative cases
  (`FAILURES`, `:22-23`; `UI_CASES`, `:25`) must fail with exact messages,
  including the `gl-trap` and `ui-gl-trap` cases ("GL presentation in native
  Vulkan window").
- Inference: most `*_verified` flags are booleans that the viewer's diagnostic code
  (`vsvkdiag.cpp`, which exists only under `VS_VULKAN_DIAGNOSTICS`) sets after
  running its own checks. The harness then checks that the flags are present and
  true. These are viewer self-reports, cross-checked by readback hashes and
  negative-case injection. They are not independent observation.

### Connected session replay and R2 replay

`scripts/run_vulkan_session_replay.py` (1,821 lines) runs the staged viewer
against `SimulatorHTTP` (`:906-927`), a loopback `ThreadingHTTPServer` on
`127.0.0.1` that answers login, seed capabilities, event queue, AIS inventory,
assets and uploads. It also generates simulator UDP traffic, such as
`circuit_code=123456` at `:1233`. No real grid or simulator is involved.

R2 replay has two sides:

- **Viewer side**: `indra/newview/vsr2replay.cpp`. `vs_r2_replay_tick()` steps through
  a 53-step script (`constexpr unsigned completedStep = 53;`, `:77`; step bodies
  `:827-1850`). It has 148 `require()` calls and fills 47 distinct `evidence["…"]`
  keys. In order, the steps cover: legacy uploads and initial appearance, body
  size and layer order, inventory actions, bake capability upload, avatar reset,
  body LOD publication, animated geometry and selection, 9 glTF topology cases,
  glTF TRS/skin animation and empty replacement, HUD attachment points 31-38,
  ordinary attachment rez/detach and fitted-detach restoration, legacy bake upload,
  11 bake GPU readbacks (`vs_r2_capture_bakes`, `vsr2readback.cpp`), gesture, AO
  and script-control transport, remote body parts, animation and visibility,
  37 specialized motions, physics weights and morphs, ground-sit hover, remote
  avatar retirement, logout retirement, and an empty retired scene.
- **Runner side**: `r2_contracts_passed()` (`run_vulkan_session_replay.py:325-478`).
  It requires 32 named contract booleans to be `true`. The fake server must have
  seen `r2-asset`, `r2-mesh`, `r2-bake-upload`, `r2-patch` and `r2-delete`
  requests, with AIS as the inventory transport and two exact metadata edits.
  `gltf_topology_cases == [0..8]`, HUD points `31..38` and body parts `0..7`
  must match. The motion catalog file must equal the reported cases (37 labels).
  The retired scene must have no owners, neighbours, slots, pending asset requests
  or resident bytes. The fixture data comes from `scripts/vulkan_r2_fixture.py`
  and `vulkan_r2_geometry_fixture.py`.

What R2 shows when it passes: the native avatar, appearance and geometry owners
execute a scripted lifecycle against synthetic assets and leave no residue. Bake
composites are read back from the GPU. What it does not show: that avatars,
attachments or glTF look the same as in the OpenGL viewer. There is no
cross-backend pixel comparison in R2, and the inputs are synthetic.

R1, async console and R3 use the same runner. The R3 cache runners corrupt,
truncate, version-bump or resize the on-disk texture cache between launches and
validate the viewer's resulting cache state (`run_vulkan_texture_cache_tests.py:19`
lists the nine cases).

### R0 baseline (Lavapipe only)

`run_vulkan_parity_baseline.py` captures 5 top-left RGB8 readbacks after 180
login-wait frames from the normal GL path (llvmpipe) and the normal Vulkan path
(Lavapipe) (`doc/vulkan/parity/README.md:29-37`). The static world scene sends two
primitives over loopback UDP. `check_vulkan_parity_r0.py` requires both peers,
identical inputs, repeatability and retained comparisons. The README is explicit:
"Cross-backend differences remain later-phase rendering gaps rather than being
masked or treated as parity" and "The initial scene has no avatars". GPU time is
reported as unavailable. Retained evidence: `doc/vulkan/parity/evidence/r0-summary.json`
and `r0-software-baseline.zip` (3.8 MB), from run 38211516817 (12 Oct).

### Evidence packages

`scripts/package_vulkan_evidence.py` collects 12 directories (`EVIDENCE_DIRECTORIES`,
`:17-20`) into `.ci/artifact-upload`. It re-validates them by importing the same
`assess`/`*_contracts_passed` functions as the runners (`:8-14`). It checks
that each report's `source_commit` and `viewer_executable_sha256` match the
provenance record, and re-hashes readbacks and archived members (`:39-67`,
`:314-348`). `--require-complete` is passed only when the job succeeded
(`software_vulkan.yml:1004-1013`). A failing job therefore produces a partial
package by design.

## Source evidence

### Diligent insertion catalog

`doc/vulkan/check_diligent_insertions.py` (361 lines):

1. Refuses to run if `git diff --name-only <source_commit> -- indra 3p autobuild.xml scripts/build_vulkan_dependencies.py`
   is non-empty (`:261-264`). Any source change after the pin fails CI.
2. Checks that every evidence symbol in each of the 27 records (`I01`–`I27`) still
   occurs in its file, and that every reviewed range's file still has the pinned SHA-256.
3. Discovers candidate sites lexically across all tracked files with regexes over
   GL/renderer identifiers (`TOKENS`, `VIRTUAL_ROOT`, `PLATFORM_GL`, `BOUNDARY_CALL`,
   `:22-34`).
4. Assigns each site a disposition in this order: a matching record's path or symbol
   pattern gives `needs-source-review`; then `boundary_proof()` (`:101-226`, mechanical
   rules); then a reviewed range overrides.
5. With `--accept`, fails if any site is `unmapped` or `needs-source-review`
   (`:356-358`). The emitted summary hard-codes `runtime_parity_qualified=False` (`:342`).

`doc/vulkan/diligent-insertion-summary.json` (pin `faa8f96bf1`):

| Disposition | Sites | Share |
|---|---:|---:|
| `mapped-boundary` | 22,801 | 75.7% |
| `interface-only` | 4,518 | 15.0% |
| `mapped` | 1,656 | 5.5% |
| `mapped-gate` | 496 | 1.6% |
| `test-only` | 416 | 1.4% |
| `mapped-module` | 225 | 0.7% |
| `cpu-only` | 13 | <0.1% |
| `conditional` | 4 | <0.1% |
| **Total** | **30,129** in 1,973 files (28,062 tracked paths scanned; 690 shader registration rows verified) | |

`diligent-insertion-records.json` holds 9,922 reviewed ranges (8,090 `mapped`,
1,572 `test-only`, 244 `mapped-gate`, 14 `cpu-only`, 2 `conditional`).
`diligent-platform-review.json` holds 8,896 ranges, with `"reviewer": "root"`
and the scope "Only listed source ranges reviewed; unlisted source is not
certified by this file". `diligent-insertion-catalog.md:8` states that acceptance
"does not mean that every algorithm has been ported, that every shader permutation
has been compiled, or that runtime parity is measured".

### Re-pinning and `carry_reviewed_ranges.py`

Step 1 means that every merge that changes `indra/` breaks `source-evidence` until
someone re-pins. `doc/vulkan/carry_reviewed_ranges.py` (114 lines) handles this.
A reviewed range in an unchanged file is kept. A range whose lines all fall in a
difflib "equal" block moves to its new line numbers and file hash. A range touching
an edited line is dropped, so the checker re-discovers that line. The tool "never
adds a review". Recent re-pins, from commit messages:

| Commit | Moved / dropped (catalog; platform) | Re-discovered sites resolved by |
|---|---|---|
| `1a7e0f77f4` (after #204) | 457/3; 352/3 | boundary proofs |
| `724f8d5d0b` (after #223) | 1/0; 1/0 | nothing reopened |
| PR #225 (to `d60e949144`) | 155 dropped | "the checker re-classified every re-discovered site" |
| `082f137fa4` (to `faa8f96bf1`) | 44/1; 44/1 | no site unmapped |

Inference: after this cycle, a reviewed range only proves that the reviewed file
bytes are unchanged, or that the review moved with unchanged lines. Dropped
ranges are usually re-closed by mechanical `boundary_proof` rules, not by new human
review. The pin also explains why `source-evidence` failed from #189 to #223: the
first failing check on 6 of the 9 `vkstorm-vulkan` runs from #189 to #223 was `check_diligent_insertions.py --accept`.

### Milestone-1 boundaries

`doc/vulkan/check_milestone1_boundaries.py` (docstring: "Check pinned source-analysis
evidence; does not execute the proposed gates"). It censuses message, settings,
floater, name and xfer registrations (`REGISTRIES`, `:16-22`) and checks them
against `milestone1-boundaries.json`. The manifest declares `"acceptance": "source-analysis-only"`
and `"gates_implemented": false`, and lists 12 gates (`G-BACKEND` … `G-LIFETIME`),
124 hashed sources, 12 routes, 27 families, 29 states and 13 lifecycles.
`milestone1-source-acceptance.md:8-15` says that item 1 "does not accept an
executable, a Vulkan build, rendered pixels, a live login, or hardware support".

## Parity ledger

`scripts/vulkan_parity_inventory.py` (318 lines) regenerates
`doc/vulkan/parity/acceptance-ledger.json` by scanning C++ and XUI sources. Without
`--write` it fails if the file differs ("R0 ledger is stale"). It always prints
`'source_inventory_current': True, 'runtime_acceptance': False` (`:314-315`).
`runtime_acceptance: false` is a **literal constant**. The tool never measures or
reads runtime results. It only means "this inventory is a discovery list and grants
nothing". The ledger's own scope string says the same: "Full parity acceptance
inventory; discovery only, no runtime acceptance."

Contents (3,131 entries; each has owner, phase, dependencies, implementation
state, three qualification flags, evidence and remaining gap):

| Category | Entries | | Phase | Entries |
|---|---:|---|---|---:|
| `UI` (XUI callbacks/controls) | 1,291 | | R1 | 1 |
| `UI_SETTING` (XUI `control_name` bindings) | 1,014 | | R2 | 9 |
| `SETTING` | 341 | | R3 | 246 |
| `FLOATER` | 236 | | R4 | 88 |
| `DRAW` (draw variants) | 87 | | R5–R8 | 12 |
| `COMMAND` | 69 | | R9 | 2,775 (88.6%) |
| `SURVEY` | 45 | | | |
| `APP_COMMAND` | 38 | | | |
| `FEATURE` | 7 | | | |
| `SETTING_FAMILY` | 3 | | | |

| `implementation` value | Entries |
|---|---:|
| `shared-source-present/native-effect-unqualified` | 1,250 |
| `unqualified` | 1,086 |
| `native-controller-pending` | 292 |
| `registered/native-construction-unqualified` | 236 |
| `native-reference-pass-integration-pending` | 87 |
| `declared/native-action-unqualified` | 69 |
| `native-reference-present/effect-unqualified` | 49 |
| `source-registration-unresolved` | 41 |
| `partial` | 11 |
| `not-integrated` | 5 |
| `placeholder` | 2 |
| `native-avatar-…`, `native-subscriber-…-pending` | 2 |
| `implemented` | **1** (`FEATURE.Startup`) |

Qualification flags: `source_qualified` true on 1 entry,
`software_runtime_qualified` true on 1 (both `FEATURE.Startup`), and
`authenticated_runtime_qualified` true on 0. One entry has a non-empty `evidence`
list. Five `SURVEY` entries (mesh LOD, rigged/animesh, avatars, primitives,
terrain) carry an R1 `qualification_subsets` record pointing to
`evidence/r1-owner-lifecycle-summary.json`. That covers ownership/lifecycle only.
All 87 `DRAW` entries are unqualified.

PR #225 regenerated the ledger because it "was stale against the source", and the
check passes again at `51d1f729a6`.

## Test inventory

### Counts

| Group | Files | Where it runs |
|---|---:|---|
| `scripts/tests/test_*.py` in `source-evidence` | 228 | `software_vulkan.yml` |
| Release/infra tests | 6 | `build.yaml:27-30,109-110` (`windows_symbol_archive`, `version_schema`, `ndof_cmake`, `inno_payload`, `velopack_version`); `check-pr.yaml:37` (`check_no_base_merges`) |
| Run by the graphics job | 1 | `test_native_camera_projection.py` (`:685-689`); also `test_vulkan_viewer_process.py` on Windows (also in source-evidence) |
| Imported by a CI test | 2 | `test_gl_probe_binding.py` (by `test_vulkan_parity_baseline.py`), `test_windows_dpi_client_extent.py` (referenced by `test_vulkan_viewer_diagnostic.py`) |
| **Not run by any workflow** | **105** | all added by `116a410fc8` (`git show --name-status 116a410fc8 -- 'scripts/tests/test_*.py'`: 105 `A`) |
| Support modules (not tests) | 5 | `native_source_fixture.py`, `native_statistics_fixture.py`, `native_driver_version_fixture.py`, `render_backend_selector_fixture.py`, `check_release_hooks.py` |

The 105 unwired files cover particles (8), sit (7), transform (7), grab (4),
face editing (7), pie menus (4), camera (4), selection, picking, pathfinding,
voice and other areas. PR #225 notes that one of them, `test_native_name_tags.py`,
already has a stale fixture. Whether the other 104 pass is unknown, because no CI
runs them.

### What the tests do

Classification by static inspection of each file. A test counts as "compile" if
it calls `shutil.which('g++'|'clang++')` directly or calls a shared compile helper
(`test_native_material_selection.run`, `test_world_scene_contracts.compile`):

| Class | Files | Description |
|---|---:|---|
| Compile-and-run C++ extracted from production | 309 (90.4%) | 293 call the host compiler directly and 16 through helpers. 292 read files under `indra/`. 278 cut function bodies or source spans out of production files (`native_source_fixture.method()` or `str.index` slices) and splice them into a fixture `.cpp` with stub types (for example `using LLUUID=int;`). The result is compiled with `-std=c++17` and run. 79 also splice in a **reference copy** of the original Firestorm logic from `scripts/tests/fixtures/` and compare the two (for example `test_native_sit_policy.py` with `fixtures/native_sit_original_policy.cpp`). 43 include a whole production header by path. 37 also make `assertIn`-style text assertions |
| Python harness and evidence-validator unit tests | 21 | Import `scripts/run_*`/`package_*` modules and feed them synthetic reports, e.g. `test_vulkan_evidence_package.py`, `test_software_vulkan_runner.py`, `test_vulkan_texture_cache_*`, `test_native_console_async_http.py` (drives `SimulatorHTTP`) |
| XUI/settings/data text checks | 6 | `test_progress_panels.py`, `test_connected_utility_xui.py`, `test_native_environment_xui.py`, `test_native_region_xui.py`, `test_native_environment_bindings.py`, `test_native_parcel_pixels.py` |
| Release/infra | 6 | listed above |

In the source-evidence job, 199 of the 228 wired files compile C++.

What "compiles production code" means here: a production **function body**,
copied out at test time, compiles and behaves as expected inside a stub harness
built with the host g++/clang++. The production translation unit, its real
dependencies, MSVC and the linked viewer are not involved. These are compile-level
and logic-level contract checks, mostly differential against a reference copy of
the original logic. They break whenever production code drifts from its stubs.
That is how 41 tests went stale silently while the job stopped at its first failure
(#205 → #225). They need no GPU and no viewer build. None of them execute Diligent
or Vulkan.

"Source-evidence fixtures" and "runtime tests" are different things. All 342
`scripts/tests` files are source/compile-level or harness-logic tests. The only
runtime tests are the graphics-job steps (standalone Diligent, viewer diagnostic,
session/R1/R2/R3 replays, R0 baseline, geometry producers). None of them ran
successfully after 18 Oct 2026.

## Roadmap vs reality

Two roadmap documents apply. `doc/vulkan/qualification-roadmap.md` (198 lines)
holds the historical gates Q0–Q8 and work packages WP0–WP7. It also records the
expanded mandate of 7 Oct 2026 to full supported rendering parity, with "no OpenGL
fallback". `doc/vulkan/full-parity-roadmap.md:43-53` defines the current stages R0–R10.

| Stage | Roadmap goal | Claimed status in repo | CI reality at `51d1f729a6` |
|---|---|---|---|
| R0 | Acceptance inventory + reproducible baseline | Passed 12 Oct, run 38211516817 (`parity/README.md`) | Ledger check passes; baseline capture has not run since 18 Oct (graphics blocked) |
| R1 | Owners, lifecycle, shared contracts | "Implemented … complete local software replay passes on pinned Windows SwiftShader" (`r1-owner-lifecycle.md:3-8`) | Last passed on all 3 configurations in 39086372280 (18 Oct) |
| R2 | Geometry, avatars, appearance | Contradictory: `r2-production-audit.md:3` says "in progress, not qualified", while `r3-production-audit.md:3` says "R2 software-device qualification is complete" | Passed on all 3 in 38978167413 (16 Oct); Lavapipe failed R2 on 17 and 18 Oct; not run since |
| R3 | Materials, texture streaming, settings | "R3 is in implementation; this record does not certify R3" (`r3-production-audit.md:3-4`) | Cache cases never green on Lavapipe; concurrent instances never green on Linux |
| R4–R8 | Render graph/HDR, lighting, shadows/probes, water/transparency, post | No qualification records | 0 of 87 `DRAW` ledger entries qualified; no runtime tests exist for these stages |
| R9 | Auxiliary rendering, connected-service closure | 2,775 ledger entries assigned; work paused (`ui-integration-checkpoint-2026-10-18.md:5-6`) | 105 new compile tests for this work are not wired into CI |
| R10 | Whole-viewer parity, stability, performance | Not started | No GPU timing (reported unavailable); no physical GPU; no authenticated grid |
| Q-gates/WP0–WP7 | Paired DiligentCore vs bgfx experiments, Q1–Q8 | Superseded: DiligentCore is "the user-selected GHI" | The bgfx comparison was not carried out; the Q6 (NVRHI) and Q7 (vendor utilities) gates are unexercised |

The roadmap's requirements for correct-lifecycle and reproducible delivery
(resize, minimize, swapchain recreation, zero validation errors, staged
RelWithDebInfo viewer) are the parts that the graphics job did exercise, up to
18 Oct. The roadmap's own parity matrix (materials, views, composition, lifetime)
and its measurement requirements (p50/p95/p99, GPU pass timings, memory) have
no implementation in CI.

## What the evidence does and does not prove

**Shows, when green:**

1. Every lexically discovered renderer-relevant site (30,129) has an assigned
   owner and disposition at a pinned commit. Reviewed ranges are bound to exact
   file hashes (`check_diligent_insertions.py`).
2. Pinned registration censuses and boundary sources for the UI/chat slice are
   unchanged (`check_milestone1_boundaries.py`).
3. A complete discovery list of 3,131 UI, settings, floater, command and draw
   obligations exists and matches source (`vulkan_parity_inventory.py`).
4. About 309 extracted production function bodies compile with the host compiler
   under stubs and behave like reference copies of the original logic.
5. When the graphics job last ran (16-18 Oct), the following held on SwiftShader and Lavapipe:
   the production `VSWorldRenderer`/`VSUIRenderer` gave exact pixels on synthetic
   8×8 scenes under Vulkan validation; the staged viewer created, resized, minimized
   and destroyed a native swapchain; it ran a loopback login, UI, chat and logout in
   every skin; and R1/R2 lifecycle scripts completed without leaked owners.
6. The validation and pixel oracles can fail: injected VUID, sync hazard,
   bad-pixel, orientation and GL-trap cases are required to fail.

**Does not show:**

1. **Runtime parity with OpenGL.** No CI step compares Vulkan and GL output for
   any world feature. R0 compares a login screen and a two-primitive scene and
   explicitly does not treat that as parity. The ledger has 0 qualified `DRAW` entries.
2. **Rendering correctness on real content.** All runtime inputs are synthetic:
   8×8 targets, fixture assets and a loopback "simulator". `authenticated_runtime_qualified`
   is false on all 3,131 ledger entries.
3. **Hardware or vendor-driver behaviour.** Only CPU Vulkan (SwiftShader, Lavapipe)
   is tested. Selection tests use synthetic AMD/NVIDIA descriptors.
4. **Performance.** GPU time is "reported unavailable"; there is no sustained-FPS
   or frame-pacing measurement.
5. **Current runtime state.** No graphics step has passed since 18 Oct. The source
   has since gained 116a410fc8 (+49k lines, `[skip ci]`) and about 350 more commits
   (`git log 93da7c824a..51d1f729a6 | wc -l` → 353 commits).
6. **Semantic review of every site.** 75.7% of catalog sites are closed by
   mechanical `boundary_proof` rules. Re-pins carry reviews forward by line identity,
   and re-discovered sites are re-closed by the same rules.
7. **Production TU correctness.** Extracted-body tests do not compile the real
   translation units or link the viewer. The graphics job's viewer build does,
   but it is blocked.

## Risks and open questions

1. **Graphics is blocked by a harness build break.** `vsworldrenderer.cpp:3` and
   `vsuirenderer.cpp:4` violate `tests/software_vulkan/CMakeLists.txt:28`. The
   options are to remove the viewer-header dependencies from the renderer TUs or
   to change the standalone test's contract. Either way, all runtime qualification
   waits on this, and regressions since 18 Oct (R2 on Lavapipe, R3 concurrent instances)
   are still unconfirmed.
2. **`[skip ci]` on large commits.** 116a410fc8 (828 files) bypassed the only
   runtime gate. The break surfaced nine days later.
3. **105 tests outside CI.** These are mostly compile-level tests for the
   18 Oct connected-UI work; at least one is known stale. Wiring them into
   `source-evidence` would probably uncover more stale fixtures (Inference, based on
   the 41 found by #225).
4. **Pin churn.** Any `indra/` change fails `source-evidence` until a re-pin. Ten
   consecutive `vkstorm-vulkan` runs (18-27 Oct) failed in `source-evidence`, six of
   them on the pin. The effect is that `source-evidence` health tracks re-pin
   discipline more than code health, and it hides graphics.
5. **Stub-drift fragility.** Extracted-body tests break when production signatures
   change. That is useful as a tripwire but expensive to maintain, and a test can pass
   while the real TU fails to compile (as with the standalone test here).
6. **Self-reported diagnostics.** Viewer-diagnostic acceptance relies largely on
   `*_verified` booleans emitted by `VS_VULKAN_DIAGNOSTICS` code inside the viewer.
   Open question: which flags are backed by independent readback hashes, and which
   are bare self-attestation?
7. **Contradictory stage records.** The R2 status differs between
   `r2-production-audit.md:3` and `r3-production-audit.md:3`. The CI history
   supports "passed once on all three configurations (16 Oct), regressed on Lavapipe".
8. **Evidence archives in-tree.** `doc/vulkan/parity/evidence/` holds 4.4 MB of
   zips from 12-13 Oct. They describe sources that have since changed substantially.
   Open question: should retained evidence be regenerated or marked historical?
9. **Coverage gaps by design.** There is no Windows Lavapipe job, no physical GPU, no
   authenticated grid and no GPU timing. These are stated limits
   (`software-vulkan-ci.md:13-16`), but they mean CI cannot qualify R10 or a
   hardware support claim.
10. **Run 40595455174** (`51d1f729a6`) was still in progress at the time of writing.
    Its graphics jobs should fail at step 12 (identical tree to 40587007906).
    Check its final status before quoting this chapter.
