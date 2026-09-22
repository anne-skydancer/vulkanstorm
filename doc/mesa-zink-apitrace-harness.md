# External apitrace harness

Status, 2026-09-22: implemented and tested with synthetic cases and a real
hidden-window WGL capture on the RX 9070 XT. A viewer capture has also been run;
it exited abnormally during shutdown and has a truncated tail. See
`mesa-zink-apitrace-findings.md` for qualification and results.
No viewer or Mesa source, binary, synchronization, or rendering settings changed.
This is source-unmodified API interception, not passive or zero-overhead tracing.

## Components

- `scripts/perf/apitrace_harness.py`: prepare, explicitly launch, dump without
  replay, and analyze. Python standard library only.
- `scripts/perf/test_apitrace_harness.py`: state-tracking regression tests.
- `scripts/perf/apitrace_wgl_probe.py`: a hidden standalone Windows WGL window;
  exercises an absolute Mesa DLL load, GL function lookup, two repeated texture
  binds, and two repeated attribute definitions. Does not operate the viewer.
- `scripts/perf/apitrace-windows-mesa-loader.patch`: diagnostic apitrace-only
  loader change against tag 14.0, commit
  `18cfd388ac47a9e00978b34d6c20341f3e2cdb56`.

## Why the loader patch is required

The stock Windows wrapper in `wrappers/glproc_gl.cpp` explicitly loads system
`opengl32.dll`. Linux's `TRACE_LIBGL` override does not apply on Windows.
The patch adds `APITRACE_OPENGL_DLL`, requiring an absolute DLL path when supplied.
Load failure or selecting the wrapper itself aborts rather than silently changing
to native OpenGL. Without an override, system OpenGL remains the default.

The viewer already explicitly loads `mesa/opengl32.dll`. Apitrace 14.0's Windows
trace command injects its wrapper and hooks library/function lookup. The probe
successfully exercised explicit loading through those hooks. That is plumbing
qualification, not proof of complete capture coverage in the full viewer.

Upstream references:
- https://github.com/apitrace/apitrace/blob/14.0/wrappers/glproc_gl.cpp
- https://github.com/apitrace/apitrace/blob/14.0/cli/cli_trace.cpp
- https://github.com/apitrace/apitrace/blob/14.0/docs/USAGE.markdown

## Local tools and reproducibility

Official win64 14.0 archive and original extraction are retained in
`%LOCALAPPDATA%/VulkanStorm/tools/apitrace-14.0`.
`apitrace-14.0-zink` is a separate copy with only its GL wrapper DLL/PDB replaced.
`tool-hashes.json` records the archive, CLI and replacement wrapper SHA-256.
The viewer runtime is not overwritten.

Source checkout: `C:/Dev/vulkanstorm/worktrees/branches/codex/apitrace-zink-source`,
branch `codex/apitrace-zink-loader`. Build logs are `configure.log` and
`build-wrapper.log` there. Reproduction from the pinned source:

```powershell
git submodule update --init --depth 1 --recursive
git apply /path/to/apitrace-windows-mesa-loader.patch
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DENABLE_GUI=OFF -DENABLE_EGL=OFF -DBUILD_TESTING=OFF "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build --config RelWithDebInfo --target wgltrace --parallel 8
```

Use the official matching CLI distribution and replace its
`lib/wrappers/opengl32.dll` and PDB with `build/wrappers/RelWithDebInfo` outputs
in a separate tool directory. No setup packaging or viewer build is required.

## Prepared viewer capture

The initial plan is at:
`%LOCALAPPDATA%/VulkanStorm/measurements/2026-09-22-apitrace-viewer-01/capture-plan.json`.
It selects the existing performance-branch RelWithDebInfo executable and its
current baseline Mesa runtime. It uses a separate copy of the baseline settings
XML. Other viewer data/cache paths are not isolated by this harness.

Preparation hashes the executable, Mesa GL/Gallium libraries, apitrace CLI,
wrapper and source settings, and writes an isolated settings copy. Capture
rechecks those inputs and refuses existing trace/log destinations. It never
records login credentials or adds login arguments. Log in interactively.
The capture command explicitly launches the viewer; preparation and analysis do
not. Notify the operator before using capture. Do not run two test viewers.

From the performance worktree:

```powershell
$run = "$env:LOCALAPPDATA\VulkanStorm\measurements\2026-09-22-apitrace-viewer-01"
$tool = "$env:LOCALAPPDATA\VulkanStorm\tools\apitrace-14.0\apitrace-14.0-zink\bin\apitrace.exe"
python scripts/perf/apitrace_harness.py capture --plan "$run\capture-plan.json"
# Close the viewer normally to complete the trace, then:
python scripts/perf/apitrace_harness.py dump --apitrace $tool --trace "$run\viewer.trace" --output "$run\calls.txt"
python scripts/perf/apitrace_harness.py analyze --dump "$run\calls.txt" --output "$run\summary.json"
```

Capture begins at process launch, including login. Keep the diagnostic session
short: settle the scene, record the selected stationary/motion interval, and
close normally. There is no timeout that kills the viewer. Full API traces can
be large and contain scene/texture data; retain them locally for this analysis.
This harness does not claim bounded trace storage or delayed capture start.

Use `analyze --start-frame N --end-frame M` to select zero-based per-context
swap intervals, with M exclusive. The analyzer still consumes the full preceding
history to reconstruct state; do not feed it an arbitrary filtered/trimmed dump.
Identify the main context from capture evidence rather than combining contexts.
Frame selection is not equivalent to selecting a wall-clock interval.

## Analyzer interpretation and limits

- Stream the dump; do not load its entirety or texture blobs into memory.
- Track current WGL context by trace thread, including failed make-current calls.
- Track actual requested texture ID by active unit and target, not the viewer's
  ambiguous zero sentinel. Count repeated active-unit selections separately.
- Track vertex definitions by VAO and attribute index: source buffer, offset,
  format, stride and integer/normalized interpretation. ARRAY_BUFFER unbinding
  alone does not erase configured pointer state.
- Count attribute repeats across observed shader changes separately.
- Invalidate conservatively on resource generation/deletion, context lifecycle,
  sharing/copy, unhandled binding APIs and other recognized state-restoration
  paths. Unknown state is not a duplicate. This deliberately undercounts.
- Ignore apitrace's synthetic `// fake` calls as application work and invalidate
  reconstruction around them. Reject malformed/unsupported dump syntax.
- Retain at most 12 repeated-call examples per context plus aggregate counters.

The supported model targets this viewer's WGL/core attribute-pointer calls;
it is not a universal OpenGL state validator. GL errors and shared-resource
visibility are not established. A repeated requested binding is NOT automatically
safe to remove, especially after cross-context updates. No speedup is inferred
from counts. Conservative invalidation totals expose incomplete coverage.

The GL stream does not identify `bindFast` versus `unbindFast` C++ call sites;
Windows apitrace does not supply the documented Linux/Android backtrace feature.
Texture IDs, surrounding calls and source inspection can guide attribution, but
the report does not invent caller labels. White fallback IDs also need separate
identification before labeling them as fallback work.

The trace renderer strings and loaded DLL paths must confirm Zink plus the
expected GL/Gallium libraries. A viewer log saying it requested Zink is not enough.
Capture status remains unqualified until that review; no FPS or visual acceptance
claim is made. Use separate unhooked PresentMon/WPR runs for timing and separate
visual validation. Both PPLL correctness fixes remain in place.

## Validation performed

`python -m unittest discover -s scripts/perf -p test_apitrace_harness.py -v`
passes 12 tests covering target/unit separation, shader transition repeats, VAO
separation, failed context switches, thread/context separation, resource name
reuse, unsupported mutations, selected-frame warmup, parser handling, and
synthetic-call exclusion (some tests cover multiple cases).

Real probe artifacts:
`%LOCALAPPDATA%/VulkanStorm/measurements/2026-09-22-apitrace-smoke`.
The trace reports vendor Mesa, renderer
`zink Vulkan 1.4(AMD Radeon RX 9070 XT (Driver Unknown))`, and
`4.6 (Compatibility Profile) Mesa 26.3.0-devel (git-00e42c51b1)`.
`glGetError` returned GL_NO_ERROR. Offline analysis recovered exactly one repeated
texture bind and one repeated attribute definition, matching the probe, with one
swap. This does not test full viewer injection or visual parity.

## Direct reader for large captures

`scripts/perf/apitrace_state_counts.cpp` uses the pinned apitrace parser directly
without generating a full text dump. It is an offline executable with no GL
calls. Copy it into the apitrace source's `cli` directory, append the following
two lines to that directory's CMakeLists.txt, reconfigure using the options above,
and build target `apitrace_state_counts` in RelWithDebInfo:

```cmake
add_executable(apitrace_state_counts apitrace_state_counts.cpp)
target_link_libraries(apitrace_state_counts common PkgConfig::BROTLIDEC PkgConfig::BROTLIENC)
```

Usage: `apitrace_state_counts.exe viewer.trace START_CALL END_CALL_EXCLUSIVE`.
It reconstructs state from the beginning and reports both full readable-history
counts and separate counts for the selected call range. Preserve stderr: the
upstream parser may warn about unexpected EOF but still exit successfully.
The direct reader's counts were cross-checked against the Python analyzer on the
real trace prefix, and against the known texture/attribute repeats in the probe.
Neither reader replays rendering or judges visual output.

The first large viewer capture showed that shader linkage (including apitrace's
synthetic attribute-location calls) and worker context creation must not erase
the main context's unchanged bindings. Both analyzers now handle those cases,
with dedicated regression tests. Unsupported binding mutations still invalidate
conservatively. Whole-text export of this capture was stopped deliberately;
the partial `calls.txt` is not a complete-session artifact.
