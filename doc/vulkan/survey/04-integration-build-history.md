# 04 — Integration with the shared viewer, build and history

The native Vulkan work on `vkstorm-vulkan` is not isolated in its own files. It
adds 290 new `vs*` C++ files, and it also changes 885 existing C++ files in
`indra/`. Only 233 of those 885 files contain a `VS_NATIVE_VULKAN` guard. Of
the 35,274 lines added to the existing files, about 12,200 sit inside a guard.
The other ~23,000 added lines, and all 15,911 deleted lines, are compiled into
every OpenGL-only build. The macro is a build-time option. It is defined only
for the viewer executable target, and only when `USE_DILIGENTCORE=ON`, which
defaults to OFF. As a result, the `ll*` libraries contain no compile-time guards
and use runtime checks such as `LLRender2D::isNativeUI()` and `mUseGL` instead.
CI builds this line both ways. `build.yaml` produces an OpenGL-only build, which
has passed since 25 October. `software_vulkan.yml` builds with
`USE_DILIGENTCORE=ON`. Its viewer-compiling `graphics` job last ran on
`vkstorm-vulkan` on 18 October, before the 828-file `[skip ci]` checkpoint
`116a410fc8`. Every later run either skipped that job or failed before the
viewer was compiled.

All counts below are taken at `51d1f729a6` against `origin/vkstorm-release`.
Statements about runtime behaviour come from reading source, commit messages
and CI job metadata. None comes from running the viewer.

## Scope and size

### Baseline

| Fact | Value | Command |
|---|---|---|
| Merge base | `c0ba3ac56d` (2026-10-02) | `git merge-base origin/vkstorm-release HEAD` |
| Commits on the line, not on release | 659 (632 non-merge, 27 merge) | `git rev-list --count origin/vkstorm-release..HEAD`; `--no-merges` / `--merges` |
| Commits on release, not on the line | 128 | `git rev-list --count HEAD..origin/vkstorm-release` |
| Whole-tree diff from merge base | 2,268 files, +558,942 / −24,699 | `git diff --shortstat origin/vkstorm-release...HEAD` |
| Added / modified files | 1,158 A / 1,110 M | `git diff --name-status origin/vkstorm-release...HEAD \| cut -c1 \| sort \| uniq -c` |

Top-level distribution of the 2,268 changed paths (`git diff --name-status … | cut -f2 | awk -F/ '{print $1"/"$2}' | sort | uniq -c`):

| Path | Files |
|---|---|
| `indra/newview` | 1,264 (C++, XUI/skins, settings, icons, installers) |
| `scripts/tests` | 713 (all added; 362 `.cpp`, 346 `.py`) |
| `doc/vulkan` | 70 (≈39 MB on disk; mostly JSON/CSV evidence) |
| `indra/llui` | 55 |
| `indra/llrender` | 27 |
| `indra/llmessage`, `indra/llappearance` | 12 each |
| `indra/llwindow`, `indra/cmake` | 9 each |
| `indra/llinventory`, `indra/llcommon` | 8 each |
| `indra/llcorehttp` | 5 |
| `3p/3p-diligentcore`, `.github/workflows` | 5 each |
| other | ≈30 |

Most of the churn is not C++. By category (`git log --no-merges --numstat`, summed over the 632 non-merge commits):

| Category | Churn (lines added + removed, all commits) |
|---|---|
| `doc/` (evidence catalogues, plans, audits) | 1,377,046 |
| C++ under `indra/` | 125,113 |
| `scripts/tests/` | 46,415 |
| skins/XUI | 16,247 |
| build/CI/3p/tooling | 12,383 |

### C++ split: new `vs*` files and changed shared files

```sh
git diff --name-status origin/vkstorm-release...HEAD | grep -P "^\w\tindra/" \
  | grep -P "\.(cpp|h|hpp|inl|mm|c)$"                 # 1,175 C++ paths
#   of which basename starts with "vs": 290  (all added)
#   of which basename does not:         885  (881 modified, 4 added)
git diff --shortstat origin/vkstorm-release...HEAD -- <the 885>   # +35,274 / -15,911
git diff --shortstat origin/vkstorm-release...HEAD -- <the 290>   # +64,616
```

The brief gave "884 non-`vs*` files". The command above counts 885. The four
added non-`vs` files are `indra/llcommon/tests/llsys_test.cpp`,
`indra/newview/llversionparser.h`,
`indra/newview/tests/viewer_version_parser_test.cpp` and
`indra/newview/third_party/stb/stb_dxt.h`. That probably explains the one-file
difference. Ignoring whitespace (`-w --ignore-blank-lines`) changes the
shared-file total only slightly, to +34,596 / −15,273. The shared-file diff is
therefore not dominated by reformatting.

New `vs*` files by location (`git ls-files 'indra/*' | grep -E "/vs[^/]*\.(cpp|h)$"`):

| Location | Files | Lines |
|---|---|---|
| `indra/newview/vs*` | 280 (100 `.cpp`) | 63,353 |
| `indra/llrender/vs*.h` | 6 headers | |
| `indra/llappearance/vs*` | 3 (`vstexlayercpu.cpp/.h`, `vstexlayerinfo.h`) | 1,263 together with llui |
| `indra/llui/vsuiadmission.h` | 1 | |

## Hooks into shared code

### How `VS_NATIVE_VULKAN` is defined

- `indra/cmake/DiligentCore.cmake:4`:
  `option(USE_DILIGENTCORE "Stage the DiligentCore Vulkan GHI and Vulkan loader" OFF)`.
  When the option is OFF, the module returns immediately (lines 6–8). When it
  is ON, it requires Windows or Linux x64 (lines 9–11). It checks the installed
  `vulkan` and `diligentcore` package metadata against
  `3p/vulkan-dependencies.json` and stops with a fatal error on any mismatch
  (lines 15–31). It defines the imported targets `ll::vulkan` and
  `ll::diligentcore`.
- `indra/newview/CMakeLists.txt:2763-2771`:
  `if(USE_DILIGENTCORE) target_link_libraries(... ll::diligentcore ll::vulkan ll::freetype)`
  and `target_compile_definitions(${VIEWER_BINARY_NAME} PRIVATE VS_NATIVE_VULKAN=1)`.
  The definition is **PRIVATE to the viewer executable**. No `ll*` library
  target receives it.
- `indra/newview/CMakeLists.txt:38-44`:
  `option(VS_VULKAN_DIAGNOSTICS … OFF)` for development-only replay and
  diagnostic sources. It requires `USE_DILIGENTCORE` and a `RelWithDebInfo`
  build.
- `VS_NATIVE_VULKAN` is therefore a **build-time option**. It is not always on
  for this line. An ordinary configure, including CI `build.yaml`, produces an
  OpenGL-only viewer with no Vulkan code paths compiled in.

The native renderer is chosen at runtime through the existing `RenderBackend`
setting. That setting was already present at the merge base; the line adds no
new settings (see below). Relevant code:

- `indra/newview/vsrenderbackend.h` (whole file, 19 lines): `canStartSession()`
  returns `true` under `VS_NATIVE_VULKAN`, and otherwise refuses `"Vulkan"`.
- `indra/newview/llappviewer.cpp:1040-1046`: an unavailable backend shows
  `MBVulkanRendererUnavailable` and stops startup. The string at
  `skins/default/xui/en/strings.xml:9` says there is "no OpenGL fallback".
- `indra/newview/llappviewer.cpp:1070-1076`: `#if VS_NATIVE_VULKAN` with
  `RenderBackend == "Vulkan"` diverts to `initNativeVulkanLogin()`.
- `indra/newview/llappviewer.cpp:4174-4182`: `#if !VS_NATIVE_VULKAN` refuses GL
  window initialisation when Vulkan is selected.
- `indra/newview/llviewerwindow.cpp:2248-2253`: when `native_vulkan` is set,
  the `LLViewerWindow` constructor takes a separate `initNativeWindow()` path
  and returns early.

### Where the guards are

```sh
grep -rn "VS_NATIVE_VULKAN" --include=*.cpp --include=*.h --include=*.mm indra | wc -l     # 1,505
grep -rl ... | awk -F/ '{print $2}' | sort | uniq -c                                       # 259 files, all in newview
```

| | Files | Guard lines |
|---|---|---|
| Existing ("shared") files | 233 | 1,299 |
| New `vs*` files | 26 | 206 |
| Any `ll*` library | **0** | **0** |

Guard forms (`grep -rh VS_NATIVE_VULKAN … | sort | uniq -c`): 1,500 `#if VS_NATIVE_VULKAN`,
3 `#if !VS_NATIVE_VULKAN` (`llappviewer.cpp:4177`, `llmodelpreview.cpp:4398`,
`llviewerwindow.cpp:46`), 1 `#if VS_VULKAN_DIAGNOSTICS && VS_NATIVE_VULKAN`.
`#ifdef` and `#if defined(...)` are never used, so the codebase uses a single
consistent form.

Guard blocks in shared files (scratch scan of every `#if …VS_NATIVE_VULKAN …
#else/#endif` block):

| Measure | Value |
|---|---|
| Guard sites | 1,298 |
| Include-only blocks | 197 |
| Code blocks | 1,101 |
| Lines inside guards | 9,589 |
| Median block body | 3 lines (688 blocks ≤ 3 lines) |
| 90th percentile | 16 lines |
| Largest | 313 lines |

Heaviest guarded shared files (`grep -c VS_NATIVE_VULKAN`): `llviewermenu.cpp`
48, `llagentcamera.cpp` 41, `lltoolgrab.cpp` 38, `llagent.cpp` 35,
`llselectmgr.cpp` 27, `fslslbridge.cpp` 25, `llviewerwindow.cpp` 24,
`lltoolpie.cpp` 24, `llappviewer.cpp` 21, `llviewermessage.cpp` 18.

A typical guard is a small native branch inside an existing function. One
example is `llviewermenu.cpp:672-…`, which re-registers `Floater.Show`/`Toggle`
callbacks so the Vintage skin's People button opens Contacts in a native session.

### Hook mechanisms

Reading the diffs shows five mechanisms. Only the first is invisible to an
OpenGL-only build.

1. **Compile-time guard** (`#if VS_NATIVE_VULKAN`). Used in `newview` only.
2. **Header stubs with an `#else` fallback.** `indra/newview/vsnativeim.h:11-24`
   declares the real `vs_native_im_guard()` and related functions under
   `VS_NATIVE_VULKAN`. Otherwise it provides inline stubs
   (`return [] { return true; }`). Shared code calls these helpers
   **unguarded**: 226 added unguarded lines in 136 files call
   `vs_native_im_guard`.
3. **Runtime dispatch on a renderer flag**, compiled into every build:
   - `LLViewerWindow::isNativeVulkan()` (`llviewerwindow.h:187`). There are
     159 unguarded added uses in 68 shared files.
   - `LLRender2D::isNativeUI()` (`llrender2dutils.cpp:81`, `bool(native_rectangle)`).
     There are 124 unguarded added uses in 43 files, mostly `llui`/`llrender`.
   - `LLWindowWin32::mUseGL` (`llwindowwin32.cpp:508-513`). Code paths for the
     non-GL case throw `std::logic_error` if GL entry points are reached.
4. **"Transposition" into renderer-neutral `vs*` helpers.** Logic is moved out
   of an `LL*` class into a header or `vs*.cpp` shared by both renderers, and
   the original call site now calls the helper. There are 24 `vs*.cpp` and
   71 `vs*.h` entries in the **unconditional** `viewer_SOURCE_FILES` /
   `viewer_HEADER_FILES` lists (`indra/newview/CMakeLists.txt` lines 100–101,
   329–333, 497, 563–566, 735–740, 869–873, 916 and the header block at
   980–1657). They are therefore compiled into OpenGL-only builds. Examples:
   `vscapabilities.cpp`, `vsflexiblepath.cpp`, `vsterraingeometry.cpp`,
   `vsvegetation.cpp`, `vsavatarmotions.cpp`, `vsobjectproperties.cpp`,
   `vsregioncontext.cpp`. The `ll*` libraries gain header-only helpers in the
   same way (`llrender/vsuiprimitive.h`, `vsimage3dgeometry.h`,
   `vsimagealphamask.h`, `vsrenderprimitivegeometry.h`,
   `vsselectionlightgeometry.h`, `vsreferencegeometry.h`; `llui/vsuiadmission.h`;
   `llappearance/vstexlayercpu.cpp`). Unguarded added lines that include a `vs`
   header or call a `vs_*`/`VS*` symbol number 2,391 in 343 files.
5. **Native-only source lists.** 55 `.cpp` files are compiled only under
   `if(USE_DILIGENTCORE)` (`indra/newview/CMakeLists.txt:2359-2388`). Another
   19 diagnostic/replay `.cpp` files are compiled only under
   `if(VS_VULKAN_DIAGNOSTICS)` (lines 2390–2406). When unguarded shared code
   references one of these, OpenGL-only builds fail to link. That happened
   after `116a410fc8` (see History).

### Classification of the 885 shared files

Method (scratch script, not committed): for each file, take
`git diff -U0 origin/vkstorm-release...HEAD`. Classify every added line as
*guarded* (inside the true branch of `#if VS_NATIVE_VULKAN`, or the `#else` of
`#if !VS_NATIVE_VULKAN`, including the directive lines), *else-branch*, or
*unguarded*, using the preprocessor nesting of the file at HEAD. Count deleted
lines separately. Deleted lines always affect both builds.

| Class | Files | Meaning |
|---|---|---|
| Fully guarded | 17 | All additions are inside guards; nothing deleted |
| Guarded + unguarded | 216 | Has guards, but also changes compiled into OpenGL builds |
| Unguarded only | 652 | No guards at all; every change reaches OpenGL builds |

Line totals across the 885 files: 12,182 added lines are guarded or are
directives, 61 sit in GL-only `#else` branches, about 22,000 added lines are
unguarded (including ≈1,200 blank lines), and all 15,911 deleted lines reach
both builds. **About 65 % of the changed lines in shared files are compiled
into the OpenGL viewer.**

By library (three-dot diff; U = unguarded added non-blank lines, G = guarded
added lines, R = deleted lines):

| Library | Files | Guarded files | G | U | R |
|---|---|---|---|---|---|
| `newview` | 751 | 233 | 12,182 | 19,069 | 14,586 |
| `llui` | 53 | 0 | 0 | 514 | 254 |
| `llrender` | 20 | 0 | 0 | 617 | 728 |
| `llmessage` | 12 | 0 | 0 | 119 | 22 |
| `llwindow` | 9 | 0 | 0 | 373 | 88 |
| `llappearance` | 8 | 0 | 0 | 225 | 44 |
| `llinventory` | 8 | 0 | 0 | 482 | 34 |
| `llcommon` | 7 | 0 | 0 | 244 | 17 |
| `llcorehttp` | 5 | 0 | 0 | 26 | 3 |
| `llcharacter` | 4 | 0 | 0 | 19 | 5 |
| `llwebrtc` | 3 | 0 | 0 | 90 | 123 |
| `llaudio`, `llplugin` | 2 each | 0 | 0 | 30 / 11 | 0 / 3 |
| `llprimitive` | 1 | 0 | 0 | 3 | 0 |

Some of the `llinventory`, `llcommon`, `llmessage` and `llwebrtc` lines are
upstream fixes carried on this line, such as the inventory fixes in PR #179
`90c45e70e6` and the FIRE-36022 voice mutex. They are not native work.
Diffing HEAD directly against the current release tip removes 29 files that
became identical to release. Their content is unchanged in this table.

`newview` by subsystem (filename-prefix buckets; same columns):

| Subsystem | Files | Guarded files | Guard lines | G | U | R |
|---|---|---|---|---|---|---|
| Floaters (`llfloater*`) | 178 | 52 | 229 | 1,983 | 3,112 | 1,599 |
| Other `ll*` | 173 | 45 | 172 | 1,726 | 2,743 | 1,470 |
| Panels (`llpanel*`, `llsidepanel*`) | 91 | 29 | 144 | 1,159 | 1,741 | 799 |
| Firestorm (`fs*`, `lf*`, `ao*`, quickprefs, animationexplorer) | 79 | 15 | 86 | 725 | 1,916 | 926 |
| Viewer core (`llviewer*`, `llappviewer`, `llstartup`, `llagent*`) | 66 | 38 | 351 | 3,468 | 3,712 | 2,277 |
| World objects (`llvo*` except voice, `llface`, `lldrawable`, `llflexible*`, `llsurface*`, `llworld*`) | 39 | 13 | 42 | 434 | 663 | 4,664 |
| Media/texture/mesh/material | 32 | 11 | 60 | 828 | 1,518 | 616 |
| Tools/selection (`lltool*`, `llselectmgr`, `llmanip*`) | 30 | 15 | 143 | 1,333 | 1,503 | 1,661 |
| Inventory/appearance | 25 | 6 | 21 | 194 | 498 | 192 |
| Voice/IM/chat | 23 | 7 | 45 | 268 | 664 | 247 |
| RLVa (`rlv*`) | 8 | 2 | 6 | 64 | 242 | 110 |
| Subdirectories (`gltf/`, `tests/`, `third_party/`) | 7 | 0 | 0 | 0 | 757 | 25 |

"World objects" has by far the most deletions. Geometry builders were moved
out of `llvotree.cpp` (−663), `llvosurfacepatch.cpp` (−502),
`llflexibleobject.cpp` (−390) and `llvoavatar.cpp` (−1,592) into `vs*` helpers
that both renderers call.

## Effects on the OpenGL viewer

### What the unguarded changes are

Tagging each file's unguarded added lines with marker patterns (scratch scan;
a file can carry several tags):

| Pattern in unguarded added lines | Files | Lines |
|---|---|---|
| `vs` include / `vs_*()` call / `VS*` type (transposition to shared helpers) | 343 | 2,391 |
| Account/session lifetime (`vs_native_im_*`, `mAccountCurrent`, `accountRevision`, `weak_ptr`, `mLifetime`, `isDiscarded`) | 219 | 945 |
| Runtime native dispatch (`isNativeVulkan()`, `isNativeUI()`, `nativeContext()`, `VSNativeSession`) | 112 | 289 |
| None of the above | 447 | (the rest) |

The "none" group includes upstream fixes, test files, reindentation, and
unmarked refactors. Representative cases follow. Each was read in the diff.

**A. Refactor into shared helpers (intended to be behaviour-preserving).**

- `indra/newview/llvotree.cpp:108` and `:363`: the tree LOD selection and
  `isTreeRenderingStopped()` now call `vs_tree_lod()` / `vs_vegetation_stopped()`
  (`indra/newview/vsvegetationlod.h:6-25`). The thresholds `{30,20,15,F_ALMOST_ZERO}`
  match release `llvotree.cpp:74`. The helper adds one difference:
  `llmax(distance…, .01f)` clamps the camera distance where the original
  divided by it directly. `LLVOTree::updateGeometry` (`:483`) has lost about
  650 lines to `vsvegetation.cpp`.
- `indra/newview/llviewerregion.cpp:3458-3461`: `LLViewerRegionImpl::buildCapabilityNames`
  is now one call to `vs_build_capability_names()` in `vscapabilities.cpp`.
  `llviewerregion.cpp` lost 134 lines. Every capability the OpenGL viewer
  requests now comes from a `vs*` file. **This is the path behind the
  FlickrConnect regression.** `116a410fc8` added `"FlickrConnect"` to this
  shared list, so OpenGL sessions requested a capability upstream had removed
  (SL-11964). It was fixed in `bcdfc2af48`, which moved it to the native-only
  list (`vscapabilities.cpp:144-153`).
- `indra/llrender/llrender2dutils.cpp`: +288/−578. `gl_arc_2d` (`:1008`),
  `gl_circle_2d` (`:1022`), washer, ring and similar functions now call
  templated `VSUIPrimitive::gl_*_2d(draw, …)` in `llrender/vsuiprimitive.h`,
  with `draw` being `gGL` or a `NativePrimitive`. The original
  `pushUIMatrix`/`translateUI`/`begin`/`end` sequences were deleted from this
  file. OpenGL output now depends on the helper reproducing them exactly.
- `indra/llrender/llimagegl.cpp`: −97. The alpha-mask histogram analysis moved
  to `llrender/vsimagealphamask.h`.
- `indra/llui/llview.cpp:1374-1444`: `drawDebugRect` changed from an explicit
  `gGL.begin(LINES)` rectangle to `gl_rect_2d(..., false)`. GL texture unbind
  and matrix-mode calls are skipped when `isNativeUI()` is true.
- `indra/newview/lfsimfeaturehandler.cpp`: +203/−198 lines. With `-w` this is
  13 lines. The OpenSim extras parsing was extracted into
  `setSupportedFeatures(info, update_scene_limits)` and reindented.

**B. Behaviour changes that also apply to OpenGL sessions.**

- `indra/llwindow/llwindowwin32.cpp:3234-3260` (`WM_DPICHANGED`):
  `handleDPIChanged` used to be posted before `SetWindowPos`, carrying the
  suggested window-rectangle size. It is now posted after resizing and carries
  the `GetClientRect` client extent. This path runs for both renderers
  (commit `e538c000e2`).
- `indra/llwindow/llwindowwin32.cpp:3560-…`: new `openClipboardWithRetry`
  makes up to 8 attempts, a bounded ≈35 ms wait on the UI thread, for
  clipboard reads and writes in all builds.
- `indra/llwindow/llwindowwin32.cpp:3091-3111`: the previously commented-out
  `WM_MOUSELEAVE` case is now active and returns 0 without calling
  `DefWindowProc`. The native-only body is gated by `!mUseGL`. Inference: the
  GL path does not call `TrackMouseEvent`, so GL windows should rarely receive
  this message. The handler is nonetheless new code on the GL path.
- `indra/newview/llvoicevivox.cpp`: about 30 new `if (sShuttingDown) return`
  exits (78 occurrences against 48 in release) and `checkNativeAccountRevision()`
  calls that throw `AccountRetired` (`llvoicevivox.h:930-935`). The VAD
  settings signal connections are now stored and deduplicated in
  `mVADSettingsConnections` instead of being reconnected. The Vivox protocol
  parser receives an owner-liveness predicate. All of this is unguarded.
  Inference: `mAccountRevision` changes only in `retireNativeAccount()`
  (`:8229-8231`), so the revision checks should never fire in an OpenGL
  session. The shutdown exits and the signal deduplication do change GL
  control flow.
- `indra/newview/llmeshrepository.cpp:2702-2762, 3176`: the physics
  decomposition completion was rewritten. The raw `mThread` pointer is
  replaced by a shared `Lifetime` owner. Completion requires all
  `mPendingHulls`, not only the last request constructed. A request without
  exactly one hull now sets `mPhysicsFailed` where it used to `llassert`.
  These changes apply to OpenGL uploads too, and arguably fix a latent race.
- `indra/newview/skins/default/xui/en/panel_preferences_graphics1.xml:984-988`:
  the renderer combo now offers **"Vulkan (in development)"** in every build.
  In an OpenGL-only build, choosing it and restarting stops startup with
  `MBVulkanRendererUnavailable` ("no OpenGL fallback"). The user has to edit
  `RenderBackend` to recover. The About strings also changed: `AboutOGL` was
  replaced by `AboutRenderer` and `AboutGraphicsDriver`, and the
  `Rendering I/II` tab labels became `Rendering 1/2`.

**C. Additions that are inert unless a native session is active.**
Most of the 945 account-lifetime lines wrap callbacks in
`vs_native_im_account_callback` or capture `vs_native_im_guard()`. In OpenGL
builds the guard is an always-true lambda (`vsnativeim.h:21`), so the
remaining cost is an extra `std::function` per callback. Examples are
`llflickrconnect.cpp:453-465` and `llappearancemgr.cpp:580`.
`isNativeVulkan()` branches are dead at runtime in OpenGL sessions, but they
still have to compile and link there.

### Known regressions, and how they were caught

| Commit | Regression | Root cause | Caught by |
|---|---|---|---|
| `bcdfc2af48` (2026-10-27) | OpenGL viewer requested `FlickrConnect` capability | Added to the shared `vs_build_capability_names` in `116a410fc8` | `test_native_capability_catalogue` (source-evidence test) |
| `e77e965d88` (2026-10-27) | `RenderAvatarFriendsOnly` no longer hid non-friends (native path; message: "since 116a410fc8") | `overallAppearance()` in `vsworldavatar.cpp:436-441` | `test_avatar_visibility` |
| `a6cec18a04` (2026-10-24) | Windows OpenGL-only build failed to compile | Unguarded `nativeContext()` uses, type changes in `animationexplorer.cpp`, missing include in `llmodelpreview.cpp` | Build workflow |
| `e1b2c30f0a` (2026-10-24) | Windows OpenGL-only build failed to link | Unguarded calls into `VSNativeSession`, `VSObjectSelection`, `VSTrackerDraw`, which exist only with `USE_DILIGENTCORE` | Build workflow |
| `aae45e93da` (2026-10-25) | Linux OpenGL-only build failed (`-Werror=unused-function`) | Helpers used only from guarded code were left unguarded in `llpaneleditwearable.cpp` | Build run 40134807280 |
| `064e87af7b`, `43b5f72f4a`, `b035a78388`, `fd657d0219` | GCC/X11 build errors | `DepthCompare::Always` collided with an X11 macro; braced-list typing; misleading-indentation warnings | Build workflow |

The commit messages for `a6cec18a04`, `e1b2c30f0a` and `aae45e93da` state "no
behaviour change in either build mode". That statement is the authors'
reasoning about guard placement. No test was run to check it.

### Risk list (unguarded changes most likely to alter OpenGL behaviour)

Ranked by judgement from size, centrality and what was read:

1. **Capability list moved to `vscapabilities.cpp`.** Any edit to the "shared"
   catalogue changes what the OpenGL viewer requests from simulators. This has
   already caused one regression.
2. **2D UI primitives (`llrender2dutils.cpp`, `vsuiprimitive.h`).** Every
   floater draw goes through code that was rewritten as templates. Pixel
   parity between OpenGL and the release build is not tested in CI (the
   parity baselines compare native output with an OpenGL reference, not two
   OpenGL builds).
3. **Geometry builders moved out of `llvoavatar`, `llvotree`,
   `llvosurfacepatch`, `llflexibleobject`, `llvograss` and `llvopartgroup`.**
   About 4,700 deleted GL-path lines in the world-objects bucket, of which
   `llvoavatar.cpp` accounts for 1,592. The OpenGL renderer now depends on
   shared `vs*` producers.
4. **Win32 window procedure changes:** DPI publication order, mouse-leave
   handling, clipboard retry, and `mNativeMouseMovePending` in `gatherInput`.
5. **Voice (Vivox/WebRTC), mesh upload and texture fetch lifetime rewrites**
   (`llvoicevivox.cpp`, `llmeshrepository.cpp`, `lltexturefetch.cpp` +150/−21,
   `llwebrtc.cpp` +84/−123). These are concurrency-sensitive code paths.
6. **Selection, tools and camera** (`llselectmgr.cpp` +136/−879,
   `lltoolpie.cpp` +265/−436, `llagentcamera.cpp` +367/−310). Policy code was
   moved into `vsselectionflow.h`, `vspieclickpolicy.h`, `vscameraview.cpp`
   and similar helpers.
7. **Preferences offer "Vulkan (in development)" in OpenGL-only builds**, and
   choosing it stops startup.
8. **Localized XUI reformatted** (see next section). Inference: if any of
   those strings relied on surrounding whitespace, display would change.

## Other divergence from vkstorm-release

### Two different "differences"

- **Three-dot** (`origin/vkstorm-release...HEAD`) shows what the line changed
  since `c0ba3ac56d`.
- **Two-dot** (`origin/vkstorm-release HEAD`) also shows, reversed, what
  release gained and the line lacks. For example, the SLua/IDE work
  (`c2b161aa8f` and others) appears as deletions of
  `floater_scripting_settings.xml`, `Inv_Script_Luau.png`,
  `LLInventoryObject::mRuntime`, `ExternalWebsocket*` settings and
  `keywords_lsl_default.xml` (+19,161 lines). None of these were deleted by
  this line.

### Skins and XUI

`git diff --name-status origin/vkstorm-release...HEAD -- indra/newview/skins`
lists 173 files, +5,532/−8,547. Of those, 116 still differ from the release tip
(`git diff origin/vkstorm-release HEAD -- <those 173>`); the rest were
early-October branding and defaults changes that release also carries.

| Area | Files | Nature |
|---|---|---|
| `default/xui/en` | 24 M + 4 A | New native frontends `floater_vs_group_search.xml`, `panel_vs_connected_controls.xml`, `panel_vs_nearby_chat.xml`, `widgets/vs_chat_input.xml`; renderer selector, About strings, native-session strings in `strings.xml` |
| `default/xui/<locale>` (es 16, pt 14, tr 13, de 10, ru 9, da 9, fr 8, zh 7, pl 6, az 6, ja 5, it 5) | 108 | Mostly from `e8c0dd5635` (2026-10-18). `floater_about_land.xml` in 7 locales (−478 lines each with `-w`), `floater_edit_ext_day_cycle.xml`, `panel_region_environment.xml`, `panel_settings_sky_*`. Multi-line `<floater.string>` and `<panel.string>` values collapsed to one line with whitespace trimmed, and `&quot;` replaced by `"` (e.g. `es/floater_about_land.xml:3-12`); also `panel_fs_nui_login.xml` per locale |
| `ansastorm_modern`, `vintage`, `starlight*`, `metaharper` | ≈20 | Early-October colour/default restoration (shared with release); `ansastorm_modern/xui/en/panel_progress.xml` +200 |
| `default/colors.xml`, `ansastorm_modern/colors.xml` | 2 | Kokua palette (`6bba1f60ca`); ≈1,900 lines each, shared with release |

Inference: the locale rewrite in `e8c0dd5635` serves native XUI loading
("Wire shared land and nested environment UI with owned lifetimes"), but it
reaches both renderers because XUI is shared.

### Libraries

| Library | Notable changes (three-dot) |
|---|---|
| `llui` (53 files) | `isNativeUI()` branches in `llview`, `llviewborder`, `llstatbar` (+54/−19), `llstatgraph`, `llvirtualtrackball`, `llxyvector`; `vsuiadmission.h` hooks in `llfloaterreg.h`, `llmenugl.cpp`, `llpanel.h`, `lltoolbar.cpp`; `llmenugl.cpp` +58/−28 (part FIRE-37070); `lltextbase.cpp` +44/−46 |
| `llrender` (20) | 2D primitive transposition (above); `llfontgl.cpp` +53/−28; `lluiimage.*` +116; `llgltexture.cpp` +30; `llvertexbuffer.cpp` +22; `llrender.cpp` +39; 6 new `vs*.h` headers |
| `llwindow` (9) | `use_gl` parameter and `mUseGL` gating in Win32 (+151/−50) and SDL2 (+145/−9); `llwindowheadless.h` rewritten (+30/−26); `llwindow.cpp` +42 |
| `llappearance` (8) | `llavatarappearance.cpp` +208; `lltexlayer.cpp` −37 into `vstexlayercpu.cpp` |
| `llmessage` (12) | `llexperiencecache.cpp` +42/−12, `llcoproceduremanager.cpp` +16, `message.h` +7; plus `llxfermanager`/test fixes from release |
| `llinventory` (8) | Mainly upstream inventory fixes (#179) and their tests; `lllandmark.cpp` +23 |
| `llcommon` (7) | `llbase64.cpp` +17, `llstringtable.h` +16/−12, `llsys`/`lltracerecording` fixes |
| `llcorehttp` (5), `llcharacter` (4), `llaudio` (2), `llplugin` (2), `llprimitive` (1) | Small hooks (≤30 lines per file) |

### Settings

```sh
python keys.py   # extracts <key>X</key><map><key>Comment from each revision
```

| File | Keys at HEAD | Added vs merge base | Removed vs merge base | vs release tip |
|---|---|---|---|---|
| `settings.xml` | 2,385 | 0 | 0 | release has 6 more (`ExternalEditorTightIntegration`, `ExternalWebsocket*` ×4, `ScriptEditorDisableSyntaxHighlight`; SLua) |
| `settings_per_account.xml` | 134 | 0 | 0 | identical key set |
| `settings_firestorm.xml` | 24 | 0 | 2 (`FSCloudColorStart/End`, shared with release) | identical key set |

**The native renderer adds no settings.** It reads existing ones
(`RenderBackend`, `RenderAvatarFriendsOnly` and others). Changes to default
values since the merge base, for example the camera offsets in
`settings.xml:3014-3028` (`8fba878ae9`, "Honor shared rear-view offsets") and
the chat hyperlink default `3201a00c7a`, apply to OpenGL users too. Release
has since moved to a different rear-camera default (`3b4df0c589`, Penny
Patton), which this line does not carry.

### Upstream fixes and drift

- `git cherry origin/vkstorm-release HEAD`: 48 of the line's 632 non-merge
  commits are patch-identical to release commits. Of those, 30 date from
  2026-10-02 to 2026-10-04 (branding, defaults, installers, OpenJPEG) and 17
  from 2026-10-24 to 2026-10-27 (FIRE-37058, RLVa picks, FIRE-37070 follow-up,
  one-way-merge hooks, LTO/AVX2, FIRE-36022 retry).
- 58 commit messages contain "cherry picked from".
- `git cherry HEAD origin/vkstorm-release`: **57 of release's 102 non-merge
  commits since the merge base have no patch-identical copy on this line.**
  Some are present in adapted form: the AIS/inventory/gunzip set is squashed
  into `90c45e70e6` (#179), FIRE-36022 is in `76bd1b9e13`/`6917945184`, and
  FIRE-37070 is in `a76e210c29` plus `11349cd2b7` ("Native Vulkan: adapt to the
  upstream fixes"). Others are missing at HEAD. Examples checked: the selected
  Viewer 26.4 SLua port and its IDE bridge (`c2b161aa8f`, `dfd42a720b`,
  `a550105f2c`), the Penny Patton camera default (`3b4df0c589`), the FreeBSD
  runner workflow (`7bdc05cca9`), the reflection-probe and statistics fixes
  (`afac4a54a9`, `ab851a653d`, `1d70569af6`), and the hover-capture fix
  (`0890b4b419`).
- Two-dot `git diff --stat HEAD origin/vkstorm-release`: 2,174 files,
  +34,477/−567,058.

## Build and CI

### CMake options introduced or used

| Option | Default | Where | Effect |
|---|---|---|---|
| `USE_DILIGENTCORE` | OFF | `indra/cmake/DiligentCore.cmake:4` | Imports `ll::diligentcore`, `ll::vulkan`; defines `VS_NATIVE_VULKAN=1` on the viewer target; adds 55 native `.cpp` files; stages runtime libraries (`Copy3rdPartyLibs.cmake:342-344`); Linux links `X11-xcb` |
| `VS_VULKAN_DIAGNOSTICS` | OFF | `indra/newview/CMakeLists.txt:38` | Requires `USE_DILIGENTCORE` and `RelWithDebInfo`; adds 19 replay/diagnostic `.cpp` files and `VS_VULKAN_DIAGNOSTICS=1`; enables Autobuild download retries in `Prebuilt.cmake` under GitHub Actions |
| `--diligentcore=${USE_DILIGENTCORE}` | n/a | `newview/CMakeLists.txt:2549, 2663, 2919, 2951` | Passed to the viewer manifests so packaging includes or omits GHI runtime files |

Other build-system changes since the merge base: `indra/cmake/00-Common.cmake`
strips `/MD`/`/MT` from `LL_BUILD` on Windows. `CompilerCacheHeaders.cmake` is
new; it force-includes the PCH header textually when PCH is disabled.
`Prebuilt.cmake` captures Autobuild output and retries HTTP 5xx errors under
diagnostics CI. The capture applies to all builds, so output is printed after
the step finishes instead of streaming. AVX2/LTO defaults are shared with the
other lines.

### Diligent and Vulkan packages

- `3p/vulkan-dependencies.json` pins `vulkan` `1.4.365-ghi1`, built from
  Vulkan-Headers `c46850864f` and Vulkan-Loader `f866657ff6`. It also pins
  `diligentcore` `2.5.6-bcb8b11e-acquire1-wsi1-ghi1`, built from DiligentCore
  `bcb8b11eec` (recursive) with two hashed overlay patches:
  - `3p/3p-diligentcore/acquire-layout-transition.patch`, a
    `SwapChainVkImpl.cpp` acquire-semaphore wait at `ALL_COMMANDS`, written
    after a SwiftShader sync-validation hazard;
  - `available-linux-surfaces.patch`, touching `Instance.cpp` and
    `SwapChainVkImpl.cpp`.
- **`autobuild.xml` has no Diligent or Vulkan entries.** Its only change is an
  OpenJPEG URL/hash swap to the CPU-AVX2 build. The packages are built locally
  or in CI by `scripts/build_vulkan_dependencies.py` and installed into the
  build's `packages/` directory. `DiligentCore.cmake:14-31` checks them
  against the lock file. `3p/README.md:54-56` says publishing them and adding
  their hashes to `autobuild.xml` "is a later step".
- `3p/README.md` is stale. Lines 3–6 say "The viewer still renders with its
  existing OpenGL pipeline. The disabled Vulkan entry in Preferences
  identifies the planned backend". Line 51 says "The default remains OFF until
  the Vulkan renderer can use them". Native rendering code and an enabled
  combo entry now exist.
- `3p/tests/` holds a standalone package smoke test (`factory.cpp`).
  `tests/software_vulkan/` holds standalone Diligent and viewer-geometry tests
  built by CI.

### CI workflows that run on this line

| Workflow | File | Triggers | What it builds | Platforms |
|---|---|---|---|---|
| Build | `.github/workflows/build.yaml` | push to `vkstorm-release`, `-devel`, `-canary`, **`vkstorm-vulkan`**, `codex/**`; PRs (`:5-9`) | Viewer **without** `USE_DILIGENTCORE` (the cmake call at `:149-161` does not pass it), so **OpenGL-only**. `RelWithDebInfo`, no installer, except `Release` with a package on `vkstorm-release`/tags. `LL_TESTS=OFF`. Compile and stage only; no runtime tests | windows-2022, ubuntu-24.04 (`:41-47`) |
| Vulkan qualification (shown in Actions as "Software Vulkan qualification") | `.github/workflows/software_vulkan.yml` (1,021 lines) | push to `vkstorm-vulkan`, `vkstorm_1.1.0`, `codex/vulkan-world-parity`; PRs into `vkstorm-vulkan`, `vkstorm-devel`, `vkstorm_1.1.0`; manual (`:3-13`) | Job `source-evidence` (ubuntu-24.04): 233 Python steps — `doc/vulkan/check_diligent_insertions.py --accept`, `check_milestone1_boundaries.py`, `doc/vulkan` unit tests, and `scripts/tests/test_*.py`, many compiling extracted production code with g++. Job `graphics` (`needs: source-evidence`): builds the GHI packages and a software ICD (SwiftShader/Lavapipe), standalone Diligent tests, then the viewer with `-DUSE_DILIGENTCORE=ON -DVS_VULKAN_DIAGNOSTICS=ON` `RelWithDebInfo` (`:669-683`), followed by geometry tests, OpenGL-reference parity baselines (Linux Lavapipe only, `:716-733`), presentation, connected-UI, R1/R2/R3 replays and evidence packaging | windows-2022 SwiftShader; ubuntu-24.04 SwiftShader; ubuntu-24.04 Lavapipe (`:516-528`) |
| Check PR | `check-pr.yaml` | PRs | Description length; `check_no_base_merges.py` for PRs into `vkstorm-*` | ubuntu |

So **OpenGL-only builds of this line are built in CI**, through `build.yaml`
on every push. They are compiled and staged but not exercised.
`software_vulkan.yml` builds only the native configuration. macOS is out of
scope: `DiligentCore.cmake:9-11` stops with a fatal error.

### CI results (from `gh run list` / `gh run view`, read on 2026-10-27)

| Workflow on `vkstorm-vulkan` | Runs listed | Success | Failure | Cancelled |
|---|---|---|---|---|
| `software_vulkan.yml` (since 2026-10-05) | 181 | 20 | 91 | 69 (+1 running) |
| `build.yaml` | 103 | 26 | 9 | 67 (+1 running) |

- The last successful Vulkan qualification on `vkstorm-vulkan` was
  2026-10-16 22:51 at `93da7c824a`, 353 commits before HEAD.
- The last run in which `graphics` jobs compiled the native viewer was
  `39086372280` at `51c41124ca` (2026-10-18 01:24). All three graphics jobs
  failed, but only at later replay steps: Lavapipe at "Production R2 geometry
  and appearance replay", and both SwiftShader jobs at "Production R3
  concurrent texture-cache instances".
- `116a410fc8` (`[skip ci]`) and everything after it have **not had a
  native-configuration viewer build in CI**. Every push and PR run from
  2026-10-22 to 2026-10-27 failed in `source-evidence`, mostly at
  `check_diligent_insertions.py --accept`, so `graphics` was skipped. The one
  exception is PR #225 head `082f137fa4` (run `40587007906`). There
  `source-evidence` passed, but all three `graphics` jobs failed at "Build
  standalone staged Diligent tests", before the viewer step.
- `build.yaml` (OpenGL-only) has succeeded on every `vkstorm-vulkan` push from
  `1d76d15703` (2026-10-25) to `8fda71152e` (2026-10-27). It failed at
  `b0b3d53aa3` (2026-10-18). It did not run on `116a410fc8`, so the compile and
  link breaks were found on the `codex/vulkan-build-repair` PR.
- The checkpoint document `doc/vulkan/ui-integration-checkpoint-2026-10-18.md`
  says no full rebuild was done for `116a410fc8`: "No full viewer rebuild,
  staging refresh, viewer launch or three-platform runtime qualification was
  performed for this checkpoint." It records the last complete local build at
  `fda4ef55ab`.

## History

### Timeline by week (632 non-merge commits; commit dates)

| Week (Mon) | Commits | C++ churn | Doc churn | Themes |
|---|---|---|---|---|
| 2026-09-28 (Oct 2–4) | 32 | 204 | 39,111 | Branding, defaults, login/skins, Inno Setup installers, version schema, CPU-AVX2 OpenJPEG. Mostly shared with release (30 patch-identical). DiligentCore GHI preparation and renderer selector (`07d3476e7c`, Oct 3); OpenGL audit and Vulkan design docs (#114, #117) |
| 2026-10-05 | 147 | 21,758 | 154,310 | Standalone Diligent tests and software-Vulkan CI (Oct 5–6); docs for staged integration (Oct 7, several `[skip ci]`); V3/V4 milestones: native window, login lifecycle, connected UI and nearby chat (`f1701ee9e9`, `be089272e4`, `05907fc6e4`, Oct 9); connected skin and account services (`12b2da0ba9`, `9004ad3126`, Oct 10); Win32 pointer and DPI work (Oct 11) |
| 2026-10-12 | 403 | 101,378 | 1,098,173 | R0 inventory and parity baselines (`7abe9a5f8b`, `86fd47d9b2`, Oct 12); R1 ownership (`88f6494ef2`, Oct 13); **R2 geometry/appearance** (`e0794c1e92`, 126 files, Oct 14); R3 textures, materials and terrain (Oct 15–17); **216 commits on Oct 17**, 75 on Oct 18; **`116a410fc8`** (Oct 18 17:02) |
| 2026-10-19 | 23 | 1,512 | 75,517 | No commits Oct 19–23. Oct 24–25: OpenGL-only build repair (PR #189), upstream inventory fixes (#179), one-way-merge enforcement (#198), FIRE-37058/37070/RLVa picks (#204), evidence re-pin (#205) |
| 2026-10-26 | 27 | 261 | 9,935 | Hook fixes (#210, #216); LTO/AVX2 and FIRE-36022 (#223); two OpenGL-path regressions fixed and six source-evidence test groups added (#225) |

Daily non-merge counts (`git log --no-merges --format=%cs … | sort | uniq -c`):
Oct 2: 11, 3: 19, 4: 2, 5: 8, 6: 26, 7: 13, 8: 14, 9: 17, 10: 39, 11: 30,
12: 41, 13: 26, 14: 5, 15: 34, 16: 6, **17: 216**, 18: 75, 24: 6, 25: 17,
26: 1, 27: 26.

### How the work was committed

- 550 of the 632 non-merge commits sit on the first-parent chain. They were
  committed directly to `vkstorm-vulkan`, not landed through PRs. Oct 17 alone
  has 214 such commits. The line's `AGENTS.md` explicitly allows this ("Develop
  Vulkan integration directly on `vkstorm-vulkan`; additional feature branches
  from it are unnecessary"), which differs from the repository-wide
  feature-branch rule. From Oct 24 onward, changes arrive through PRs #179–#225.
- Commit pattern: implementation commits alternate with "Pin …/Refresh source
  evidence" commits that regenerate the `doc/vulkan` catalogues. First words
  of subjects: Pin 146, Record 34, Refresh 30, Preserve 29, Wire 19, Fix 19,
  Keep 16, Restore 15, Qualify 15, Repin 11. A rough keyword classification
  gives evidence/pin/record ≈276, transpose/share ≈62, wire/connect/implement
  ≈37, build/CI ≈35, branding/skin ≈34, docs ≈13, other ≈175.
- Authors: Anne Skydancer 626, Ansariel 3, Trish_sl 2, Beq 1. The last three
  are upstream Firestorm fixes carried on the line. 71 messages carry a
  `Co-Authored-By: Claude` trailer.
- Merges: 27. Two "Merge earlier Vulkan … history" commits (Oct 10) and three
  "Integrate/Merge isolated R3 …" commits (Oct 17–18) are local integration
  merges. `9645c7098c` (2026-10-25 03:44 UTC) is "Merge branch
  'vkstorm-vulkan' into codex/fix-llurlmatch-test-vulkan", a base-into-feature
  merge. It predates the one-way-merge rule commit `47d4b677b4` (11:21 UTC the
  same day).

### Largest commits

By total lines (`git log --shortstat`). Most are evidence and pin commits:

| Commit | Date | Files | Lines ± | Subject (abridged) |
|---|---|---|---|---|
| `ff829da597` | 10-17 | 8 | 88,645 | Record exhaustive connected UI audit… |
| `b55f911e33` | 10-25 | 7 | 72,529 | Source evidence: re-pin to 90c45e70e6 |
| `86fd47d9b2` | 10-12 | 4 | 69,916 | Inventory direct settings observers, XUI bindings… for R0 |
| **`116a410fc8`** | 10-18 | **828** | **56,761** | Implement connected UI and R3 service checkpoint `[skip ci]` |
| `7abe9a5f8b` | 10-12 | 16 | 47,644 | Add R0 acceptance inventory and baseline capture |
| `e314755ca1` | 10-18 | 6 | 45,871 | Pin integrated native UI controllers… |
| `f01105bcb3` | 10-18 | 5 | 40,164 | Pin shared navigation region controller… |
| `fb600fb0bb` | 10-10 | 7 | 38,323 | Repin native connected UI source review… |

By C++ churn under `indra/`:

| Commit | Date | Files | C++ ± | Subject |
|---|---|---|---|---|
| **`116a410fc8`** | 10-18 | 828 | **43,722** (35 % of all C++ churn on the line) | Implement connected UI and R3 service checkpoint `[skip ci]` |
| `e0794c1e92` | 10-14 | 126 | 15,683 | Implement R2 native geometry and appearance production paths |
| `12b2da0ba9` | 10-10 | 192 | 5,779 | Wire native connected viewer UI and account services |
| `9004ad3126` | 10-10 | 105 | 3,539 | Integrate shared native connected UI and account-safe actions |
| `4a4b951e67` | 10-18 | 114 | 2,780 | Transpose shared land controllers… keyboard camera modes |
| `88f6494ef2` | 10-13 | 47 | 2,120 | Complete R1 native ownership and lifecycle contracts |
| `a9e852d2e8` | 10-18 | 43 | 2,067 | Wire native object-media controllers… |
| `908ac903d3` | 10-18 | 94 | 1,963 | Connect shared region controllers… tracker and beacon owners |

### `[skip ci]` commits

| Commit | Date | Files | Lines ± | C++ | Subject |
|---|---|---|---|---|---|
| `116a410fc8` | 10-18 | 828 | 56,761 | 43,722 | Implement connected UI and R3 service checkpoint |
| `5ca30b4970` | 10-10 | 4 | 51 | 0 | Record completed V4 three-platform software qualification |
| `109039d400` | 10-09 | 5 | 99 | 0 | Record completed V3 software qualification |
| `f4f2284fac` | 10-07 | 4 | 265 | 0 | docs(vulkan): plan staged viewer integration… |
| `417e32891a` | 10-07 | 2 | 21 | 0 | docs(vulkan): clarify UI-first incremental world… |
| `c4185a1d0e` | 10-07 | 2 | 48 | 0 | docs(vulkan): expand deliverable to full viewer rendering parity |
| `ded560cbf5` | 10-06 | 1 | 30 | 0 | docs(vulkan): accept standalone Diligent checklist item 3 |
| `7132ee5c00` | 10-06 | 1 | 66 | 0 | docs(vulkan): record successful software CI qualification |

Seven of the eight are documentation only. `116a410fc8` is the exception. It
touched 359 shared C++ files (≈40 % of the 885), 141 `vs*` C++ files, 21 files
in `ll*` libraries, and 326 evidence/test files. Its message says: "This is an
implementation checkpoint, not R3 completion or runtime qualification…
Rebuild and qualification are deferred until production gap closure." Both
regressions fixed this week trace back to it, as do the three
OpenGL-only build breaks.

### Checkpoint commits

`31cd67d773` (10-07, viewer presentation checkpoint, docs) ·
`fb600fb0bb` (10-10, qualification checkpoints) ·
`0316e0d336` (10-17, "Record R3 reboot checkpoint and pending runtime qualification") ·
`31acf4fa9b`, `08b4e303ca`, `f15a16ccec` (10-17, R3 material, sampling and
shared-fetch checkpoint pins) · `b0b3d53aa3` (10-18 04:50, "Record connected UI
pause checkpoint") · `116a410fc8` (10-18 17:02, implementation checkpoint).

## Risks and open questions

1. **Most native-driven shared changes ship in OpenGL builds.** 868 of 885
   shared files carry unguarded changes, and all 15,911 deleted lines reach
   both builds. The only automated check on the OpenGL configuration of this
   line is compile, link and stage (`build.yaml`, `LL_TESTS=OFF`). No test
   runs the OpenGL viewer from this line or compares it with release. Both
   regressions found this week were caught by native-side source-evidence
   tests, not by OpenGL checks.
2. **The native configuration at HEAD has not been compiled in CI.** The last
   CI native viewer build was at `51c41124ca`, before the 43,722-line C++
   checkpoint. Since then the `graphics` job has been skipped or has failed
   before the viewer step. The last local full build recorded in-tree
   (`fda4ef55ab`) is older still. Open question: does `USE_DILIGENTCORE=ON`
   build at `51d1f729a6`?
3. **The guard boundary is enforced only by a build.** The macro is defined
   PRIVATE to the viewer target. Library code cannot use it, so native
   behaviour in `llui`/`llrender`/`llwindow` must be runtime-gated
   (`isNativeUI()`, `mUseGL`). A mistake there is invisible to the
   preprocessor.
4. **"Shared" helpers make OpenGL depend on new code.** Capability names, tree,
   terrain, flexible, avatar and 2D primitive geometry, camera view, selection
   flow and pie click policy now live in `vs*` files that both renderers
   compile. The FlickrConnect regression shows the failure mode: a native
   requirement added to a shared table.
5. **Drift from release.** 57 release commits since the merge base have no
   patch-identical copy here. Several, including the SLua port, the camera
   default, statistics and reflection-probe fixes, and the hover-capture fix,
   are absent at HEAD. Bringing this line to release, or release to this line,
   needs a deliberate per-commit integration branch.
6. **The Preferences entry is reachable in OpenGL-only builds.** Selecting
   "Vulkan (in development)" makes the next startup stop with no OpenGL
   fallback.
7. **Localized XUI reformatting.** 108 locale files were rewritten in one
   commit. Equivalence with release display has not been checked.
8. **Stale documentation.** `3p/README.md` still describes the renderer as
   dependency preparation only.
9. **Evidence volume.** `doc/vulkan` holds about 39 MB and 1.38 M lines of
   churn, and `scripts/tests` has 713 files. 330 commits touch only
   docs/evidence. The required `check_diligent_insertions.py --accept` gate
   has failed on almost every run since Oct 22, which blocks the build jobs
   behind it.
10. **Open:**
    - Do the 17 "fully guarded" and 216 mixed files contain unguarded helpers
      that are dead in OpenGL builds, beyond the one fixed in `aae45e93da`?
    - Is `vsuiprimitive.h` pixel-equivalent to the deleted `gGL` sequences,
      for example the `translateUI` placement in `gl_arc_2d`/`gl_circle_2d`?
    - Is the Win32 DPI ordering change intended for OpenGL sessions?
