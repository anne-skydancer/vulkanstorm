# Mesa/Zink binding capture findings - 2026-09-22

## Result

The observed repeated-state opportunity is substantially larger for texture
binding and active-unit selection than for vertex-attribute pointer definitions.
Prioritize the texture fast path for a narrowly scoped optimization experiment.
These counts establish repeated requests, not safe elimination or an FPS gain.

Selected main-context window (1,639 swap calls):

| API category | Observed calls | Same requested state | Fraction | Repeats per swap |
| --- | ---: | ---: | ---: | ---: |
| Texture bind | 9,090,707 | 3,100,932 | 34.11% | 1,892 |
| Active texture unit | 8,933,715 | 3,830,276 | 42.87% | 2,337 |
| Vertex attribute pointer | 31,824,250 | 95,621 | 0.300% | 58 |

Unknown prior state accounted for 180 texture binds and 187 attribute pointer
calls in that window. 95,300 of the 95,621 repeated attribute definitions followed
an observed shader change. Shader-transition setup is therefore reachable, but
accounts for a small fraction of all attribute definitions in this scene.

## Capture qualification

Artifacts are under
`%LOCALAPPDATA%/VulkanStorm/measurements/2026-09-22-apitrace-viewer-02`.
The prepared `-01` attempt did not launch: closing the previous viewer changed its
source settings hash. `-02` uses the original immutable settings snapshot.

- Viewer: performance-branch RelWithDebInfo 7.2.5.81932, PID 31500.
- Runtime: unpatched Mesa baseline, not native OpenGL. The apitrace-only loader
  patch selects the intended Mesa DLL; it is separate from the Mesa loader fix.
- Loaded modules include the patched apitrace wrapper and the expected bundled
  Mesa GL and Gallium DLLs. Paths are saved in `loaded-gl-modules.json`; hashes
  are in `capture-plan.json`.
- GL renderer: `zink Vulkan 1.4(AMD Radeon RX 9070 XT (Driver Unknown))`.
  GL version: Mesa 26.3.0-devel, git-00e42c51b1. Renderer identity is also present
  in the trace prefix, not merely the requested backend setting.
- The viewer reached STATE_STARTED at 07:29:03 UTC. The user reported it settled.
- Requested wait was 15 seconds; recorded marks were 07:30:43.3730895 through
  07:31:37.0066130 UTC, actually 53.6335 seconds apart. Tool/interaction elapsed
  time must not be represented as a precisely controlled 15-second capture.
- The trace is 9,993,355,266 bytes. Its readable prefix has 437,419,047 calls and
  58,721 frame-end flags, ending at call 437,419,046.
- Exit status was 3221225477 / 0xC0000005 (access violation). The log reaches
  shutdown cleanup and then repeats deleted-LLWinDebug singleton warnings.
  Apitrace reports unexpected EOF. The cause of the shutdown failure is unknown;
  do not attribute it to the loader patch, viewer, Mesa or apitrace without a
  separate investigation. This is not successful stability validation.

## Interval selection

There are no exact wall-clock frame markers embedded by this harness. The tool
observed 5,947,174,914 trace bytes near the start mark and 8,180,977,666 at the end.
`byte-frontier-mapping.json` records conversion of those compressed-file positions
to decompressed stream positions via apitrace's Snappy chunks. Each chunk has a
four-byte little-endian compressed size; its Snappy payload starts with an
uncompressed-length varint. Cumulative decompressed lengths can then be compared
with the frame sizes reported by `apitrace info --dump-frames`.

The two positions fall in global frame entries 55,768 and 57,408. Analysis uses
the interior complete frames: call 221,743,435 inclusive through 340,305,247
exclusive. It reconstructs binding state from trace start before counting this
window. This is an approximate file-write-frontier sample near the settled marks,
not an exact 53.6335-second segment. Do not derive FPS from it. Buffered writes
and outstanding calls are additional reasons not to assign exact timestamps.

The original `trace-info.json` contains an unescaped Windows filename from the
upstream tool; `trace-info-normalized.json` corrects only that JSON string.
Both the original output and parser stderr are retained. The selected window
ends well before the unreadable tail and before logged logout at 07:32:19 UTC.

## Analyzer verification and artifacts

- `native-binding-summary-v2.json`: direct-parser full-readable-history and
  selected-window counters, separated by GL context.
- `native-binding-errors-v2.txt`: retained unexpected-EOF warning.
- `findings-summary.json`: fractions/per-swap counts and analyzer source/binary
  SHA-256 values.
- `qualification.json`: capture limitations independent of numerical results.
- Python and direct-reader counts agree on the first 100,001 real trace calls;
  both also recover the intentionally repeated calls in the standalone probe.
- Twelve Python regression tests pass. Real capture required preserving bindings
  across nonbinding shader-linkage calls and creation of another context; both
  readers now do so. Resource invalidation and unsupported binding APIs remain
  conservative.
- A whole-text dump was stopped because of its size and formatting cost.
  `calls.txt` is partial and is NOT used as the complete-session result. Earlier
  intermediate native output was superseded by the explicit `-v2` result.

## Recommended next change

Start with suppressing proven redundant active-unit and texture binds in the
viewer fast path. Keep texture activation/use accounting, dirty sampler updates,
fallback loading, resource lifetime handling and required drawing flushes intact.
Account for raw GL binds that bypass the wrapper before trusting cached state.
Do not infer C++ call-site identity from the GL trace alone: these counts do not
separate bindFast from every other caller or identify the white fallback ID.

Test the candidate separately from vertex-state work. Use an unhooked matched
baseline/candidate timing comparison and a separate visual trial for alpha,
materials, UI, shadows and reflections/hero probes. Retain both PPLL fixes and
the established synchronization. This capture supports prioritization only;
no timing benefit or visual parity has been established.
