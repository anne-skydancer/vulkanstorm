# Mesa/Zink audit: redundancy, modernisation and hero-probe parity

Audited 2026-09-29 against master `9a387f65b6` and the pinned package
`mesazink 26.3.0-devel-git.00e42c51b1`. This is a read-only source audit:
nothing was built, launched or measured for it. Statements marked
*(inference)* are reasoning that still needs a measurement; everything else
was verified in the viewer source, the Mesa source at the pinned commit, or
the released package binaries.

## Summary

- Hero-probe changes alone cannot give Zink parity with the AMD OpenGL ICD.
  The recorded gap (Zink about 20 presents/s, native about 34-38 on an
  RX 9070 XT; see `doc/mesa-zink-inworld-observations.md`) was measured with
  mirrors off, and with `RenderMirrors=0` the hero-probe code issues no GL
  work at all.
- Under Zink the viewer classifies the GPU vendor as `MISC`, so AMD
  featuretable entries are not applied. Existing native-vs-Zink comparisons
  therefore ran with different texture-upload settings.
- On Windows, Mesa's kopper always presents with `IMMEDIATE`, and WGL emulates
  vsync with a CPU sleep scaled by 1.75. These are the largest expected
  presentation levers.
- Master contains no development runtime hooks from the Zink work, but it
  carries stale investigation documents, a re-applicable timing-hook patch
  and some dead code. The Mesa package repository has stale source trees and
  one ineffective patch.

## 1. Redundancy and cruft

### Viewer (master)

`python scripts/tests/check_release_hooks.py` passes. The Tracy zones and
memory-gate changes from `77698308ea` and `939919e9ff` were reverted by
`bab6b573a2`. The checker scans only tracked `indra/` sources, so it does not
cover `scripts/`, `tools/` or `doc/`.

| Location | Finding | Recommendation |
|---|---|---|
| `doc/mesa-*.md`, `doc/zink_performance_baseline.md` | Investigation logs that reference removed worktrees, branches and settings (`USE_TRACY_MEMORY`, `test_profiler_memory_gate.py`, `WGL_ZINK_FRAME_LATENCY`, the `-wglinit1` package id). None is linked from any index. | Fold still-valid facts (renderer identity, loader-patch rationale, measurement method) into one indexed note; move the logs to `vkstorm-devel`. |
| `scripts/perf/notification-replay-clock.patch` | Development timing hook kept in patch form for re-application to `LLFrameTimer`/`LLAppViewer::doFrame`. Not compiled, but outside the checker's scope. | Move to `vkstorm-devel`. `tools/vulkan/diagnostic_replay_clock.h` can stay as test support. |
| Other `scripts/perf` harnesses | Standalone tools, not compiled into the viewer; their unit tests pass. | Permitted by `AGENTS.md`. |
| `indra/newview/llappviewerwin32.cpp:50` | Unused `llvkprobe.h` include; the comment at `:1231-1235` says Zink is not gated on `LLVKProbe`. | Remove. |
| `llcomputelod.cpp:177`, `llparticlecompute.cpp:154`, `pipeline.cpp:919, 8115` | `#if LL_WINDOWS && !LL_MESA`; `LL_MESA` is never defined. | Simplify to `#if LL_WINDOWS`. |
| `llwindowmesaheadless.*`, `LL_MESA_HEADLESS` guards, `BUILD_HEADLESS` | 2011-era OSMesa headless path; never built and unrelated to Zink. | Remove only in a deliberate upstream-divergence cleanup. |
| `autobuild.xml` `mesa` 7.11.1 | Nothing calls `use_prebuilt_binary(mesa)`. | Remove, or keep only for upstream parity. |
| `llappviewer.cpp:3787-3819` | Comments claim Vulkan-device validation that does not happen; logs "Render backend: Zink" even when the Mesa load failed and native GL is running. | Have `selectGLBackend()` return the effective provider and log that. |
| `llfloaterpreference.cpp`, `llvkdialogs.cpp`, `llvkwindowmgr.cpp`, `llappviewer.cpp` | The `RenderBackend` "not Vulkan/Zink means OpenGL" normalisation is repeated 7+ times, with inconsistent defaults and case handling. | One shared backend enum with parse/normalise. |
| `Copy3rdPartyLibs.cmake`, `newview/CMakeLists.txt`, `viewer_manifest.py` | Mesa file names and runtime directory hard-coded in 4+ places despite `MESAZINK_RUNTIME_DIR`/`MESAZINK_RUNTIME_FILES`. Windows stages a flat Mesa `opengl32.dll` in `sharedlibs/<cfg>/`. | Use the CMake variables; stage into a `mesa/` subdirectory. |
| `llappviewerwin32.cpp:1244-1247` | `MESA_LOADER_DRIVER_OVERRIDE` is not read by the WGL build. | Drop it on Windows; keep `GALLIUM_DRIVER`. |
| `llappviewerwin32.cpp:1254-1263` | `SetDllDirectoryW(mesa_dir)` is left set process-wide after a successful load. | Use `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` instead, or reset immediately. |
| `llappviewerwin32.cpp` vs `lllinuxzink.h` | Windows sets environment only if absent and never restores it; Linux overwrites and restores. | Use one policy on both platforms. |
| `llfloaterpreference.cpp:4042-4055` | The Zink option is offered on macOS and on `USE_MESAZINK=OFF` builds. | Gate it on the runtime files being present. |
| `viewer_manifest.py` (Windows) | `mesazink.txt` licence is staged on Linux only. | Stage it on Windows too. |
| `scripts/configure_firestorm.sh:104, 557` | `--zink` described as Windows-only. | Update the text. |
| Repository root `RenderDoc/` | Untracked, not ignored capture files. | Move out of the repository or ignore. |

### Mesa package (`3p-mesazink`)

- The pinned Windows release is byte-identical to the build in the local
  `mz-src`/`mz-package` trees (Mesa `00e42c51b1` plus all three patches), not
  to `3p-mesazink/mesa-src`. That tree sits on the abandoned `3e2092a295`
  bump with only two patches applied; its `build/` output, the
  `3e2092a295` tarball, `package-results.json` and a pre-loader-patch
  `00e42c51b1` tarball are stale.
- The release carries two Windows assets with identical DLLs and different
  archive hashes.
- `build.py` does not reconfigure Meson when options change and copies
  artifacts only when the destination looks older, so stale DLLs can be
  packaged. Only Linux has CI; the Windows package is built by hand. The
  version string is duplicated in the Linux workflow.
- The `autobuild.xml` description mentions an RX 9000-series patch that does
  not exist, and the null-guards patch header still says "PATCH 2/2".

| Patch | Assessment |
|---|---|
| `mesa-msvc-release.patch` | Needed: with `NDEBUG`, an assert-only variable in `vtn_cmat.c` trips MSVC warning-as-error C4189. Not fixed upstream; worth upstreaming. |
| `mesa-wgl-loader-init.patch` | Correct (zero-initialises `kopper_loader_info`); no functional effect on the AMD configuration. Not fixed upstream; worth upstreaming. |
| `mesa-zink-null-guards.patch` | Ineffective: every guard is unreachable or its callers already check for NULL. The real crash fix is upstream `3fe13b1c074` ("zink: don't draw or dispatch with a null pipeline"), which is not in the pin. Drop the patch and pick up that commit. |

The Windows build is lean and optimised: release, `-O2`, asserts compiled
out, Zink as the only Gallium driver, no LLVM. LTO is off because Mesa
rejects it without `allow-broken-lto`.

## 2. Modernisation

1. **Classify Zink explicitly in the viewer.** The Zink GL vendor string is
   "Mesa", so `llgl.cpp:1178-1206` sets `mGLVendorShort = "MISC"` and
   `mIsAMD`/`mIsNVIDIA` stay false on every GPU. The featuretable `list AMD`
   (`RenderGLMultiThreadedTextures 1`) and the NVIDIA upload-fence path in
   `llimagegl.cpp` are never applied under Zink. On Linux, the renderer-string
   check can set `mIsIntel`. Add an explicit Zink flag, record the underlying
   Vulkan vendor separately, and decide each vendor workaround for Zink
   deliberately, for example with a `list Zink` featuretable entry.
2. **Hook up the Windows present mode in kopper.**
   `zink_kopper_set_present_mode_for_interval` (`zink_kopper.c:36-38`) forces
   `VK_PRESENT_MODE_IMMEDIATE_KHR` under `DETECT_OS_WINDOWS` ("not hooked up
   yet"). With a non-zero swap interval, `stw_framebuffer_swap_locked`
   (`stw_framebuffer.c:708-762`) sleeps on the CPU before presenting, scaled by
   a 1.75 fudge factor, then presents without vsync. Map interval 1 to FIFO (or
   `FIFO_LATEST_READY`), skip `wait_swap_interval` for Zink, and route
   `wglSwapIntervalEXT` to `zink_kopper_set_swap_interval`. This is a small
   local Mesa patch and still unchanged on upstream main.
3. **Determine why Zink gets Composed Flip.** Zink presents through a real
   `VkSwapchainKHR` on the window (`VK_KHR_win32_surface`); no GDI or DXGI
   path is involved. It requests 2 images, opaque alpha, and extra usage flags
   (`SAMPLED`, `INPUT_ATTACHMENT`, `TRANSFER_SRC`), with no full-screen-exclusive
   chaining. Run `vkcube --present_mode 0` in the same window state under
   PresentMon. If it is also Composed Flip, the policy is in the AMD Vulkan
   ICD: try chaining `VkSurfaceFullScreenExclusiveInfoEXT`. If it gets
   Independent Flip, the cause is Zink-specific: try reduced usage flags and
   3 images. *(inference)*
4. **Rebase Mesa** to current main or the 26.3 branch point to pick up
   `3fe13b1c074`; keep the msvc and loader-init patches, drop null-guards.
5. **Build hygiene:** track Meson options in the checkout marker (or always
   reconfigure), copy artifacts unconditionally, set `-Dvideo-codecs=` on
   Windows, single-source the version string, add a Windows CI job, and
   consolidate onto one Mesa source tree.
6. **Known limits needing Mesa work:** the Mesa shader cache is disabled on
   Windows (`meson.build:1254-1256`); Zink disables EDS2 (and so EDS3 and
   vertex-input dynamic state) and push descriptors on AMD's proprietary
   driver (`zink_screen.c:2984, 3186`); Zink has no `VK_EXT_descriptor_heap`,
   present-wait or full-screen-exclusive support. Measure before pursuing.

## 3. Hero probes

### Gating

With `RenderMirrors=0` (the default at every featuretable level),
`LLHeroProbeManager::update()` and `renderProbes()` return early, the hero
render target is not allocated, `HERO_PROBES` is not defined in shaders, and
uniform/texture binding is skipped. Hero-probe optimisation therefore cannot
affect the mirrors-off measurements.

### Per-frame cost with mirrors on

`RenderHeroProbeUpdateRate` means faces per frame = 6 / rate (clamped); the
`settings.xml` description is wrong. At the defaults (resolution 1024,
rate 2) each frame performs:

- 3 full deferred scene renders at probe resolution, each with a full cull at
  main draw distance and no mirror clip plane;
- 6 extra sun-shadow cascade renders (2 per face);
- 6 blur draws, 30 mip-chain draws and 12 radiance draws;
- 42 `glCopyTexSubImage3D` calls into the cube array;
- 6 stray clears of the default framebuffer.

Resources: the cube array is an R11F_G11F_B10F mutable allocation of 4 cubes
(24 layers, about 134 MB at 1024, 537 MB at 2048), of which only cube 0
(output) and cube 3 (scratch) are used. `mMipChain` is ten separate RGBA16F
2D targets; `mRenderTarget` carries an unneeded depth buffer.

### Zink-specific costs

1. **Copies become blitter draws.** `st_CopyTexSubImage` calls `pipe->blit`.
   The formats differ (RGBA16F to R11F_G11F_B10F), so `try_copy_region`
   fails; `blit_native` fails on the RGB/RGBA mask mismatch; the fallback is
   `util_blitter_blit`, which saves state, switches framebuffer, draws and
   restores. That is about two extra render-pass boundaries per copy, around
   84 per frame. Zink barriers cover every mip and layer of a resource, so
   each copy transitions the whole cube array. The regular probe manager
   copies R11F_G11F_B10F to R11F_G11F_B10F and takes the `vkCmdCopyImage`
   path.
2. **Stray default-framebuffer clears.** `llviewerwindow.cpp:6697` (and,
   *(inference)*, `llviewerdisplay.cpp:2216`) clear FBO 0 because the
   preceding `LLRenderTarget::flush` restored it. Zink defers the clear and
   flushes it on the next framebuffer change through
   `zink_kopper_acquire(UINT64_MAX)`: a blocking swapchain acquire plus a
   clear-only render pass, up to 6 times per frame.
3. **Same-frame occlusion polling.** The default hero probe is pushed into
   `mProbes` twice (`llheroprobemanager.cpp:598, 612`), and `doOcclusion` is
   called from two blocks in `pipeline.cpp` (`:2849, 2869`). A query issued and
   polled in the same frame goes through `tc_get_query_result` (threaded
   context sync) to `zink_get_query_result`, which performs a mid-frame
   submit. This extends the duplicate-call note in `gl-core-zink-audit.md`.
4. **Extra scene renders** multiply Zink's per-draw overhead, including the
   redundant texture binds from `LLTexUnit::bindFast`. *(inference)*

### Candidates, in priority order

All are viewer-side and expected to be neutral or positive on native GL.

| # | Change | Expected effect | Risk |
|---|---|---|---|
| 1 | Allocate `mMipChain` as `GL_R11F_G11F_B10F` (`llheroprobemanager.cpp:122`), as the regular manager does | 42 copies per frame move from blitter draws to `vkCmdCopyImage` | Low |
| 2 | Remove the stray default-framebuffer clears in cube snapshots (confirm nothing reads window depth; similar clears at `llviewerwindow.cpp:6206, 6552`) | Removes up to 6 swapchain acquires and clear passes per frame | Low |
| 3 | Push the default probe once; drop the second hero `doOcclusion` block or skip polling a query issued this frame | Removes a threaded-context sync and mid-frame submit | Low |
| 4 | Replace radiance mip 0 (identity sample) with one `glCopyImageSubData` over 6 layers | 6 draws and 6 copies become one copy | Low-medium |
| 5 | Render the mip chain and radiance directly into the cube array (MRT or per-layer attachments); split scratch and output into separate textures, dropping unused cubes 1 and 2 | Removes all copies and their whole-image transitions; halves cube-array memory | Medium |
| 6 | Update only faces the mirror can see, pass the mirror plane as the user clip plane, share one sun-shadow set per frame | About 30-50% fewer face renders on both drivers *(inference)* | Medium |
| 7 | Run `generateRadiance` only for faces updated this frame | Up to half the radiance work | Low |
| 8 | Trim unused tiny mips; drop the `mRenderTarget` depth buffer; `glInvalidateFramebuffer` on hero scratch; `glTexStorage3D` for the cube array | Small | Low |

Also found: `RenderHeroProbeConservativeUpdateMultiplier` and
`RenderHeroProbeDistance` are read but have no effect, and `cubeFaces[6]` in
`llheroprobemanager.cpp` is unused. Only radiance mips 0-1 of the output cube
are written, yet `reflectionProbeF.glsl:723` can sample up to LOD 2.5, reading
unwritten mips with small weights. *(inference; needs visual confirmation)*

### Parity assessment

Candidates 1-7 can plausibly remove most of the Zink-only hero penalty with
mirrors on, but mirrors-on performance would then track the mirrors-off ratio
rather than reach parity. Parity depends on the base path: vendor
classification, presentation mode and flip model, redundant texture binds,
shadow and terrain passes, and end-of-frame synchronisation.

The regular reflection-probe manager, active in the mirrors-off baseline,
uses the same per-mip copy and radiance/irradiance loops
(`llreflectionmapmanager.cpp:900-1032`). Candidates 2 and 5 applied there
could move the measured gap.

## Recommended order

1. Add explicit Zink vendor classification.
2. Re-measure native and Zink with matched effective settings.
3. Patch the Windows present mode and vsync sleep in Mesa.
4. Run the `vkcube` flip-model test.
5. Apply hero candidates 1-3 and reflection-probe candidates 2 and 5.
6. Measure per-pass GPU time and Zink render-pass counts
   (`ZINK_QUERY_RENDER_PASSES`) with mirrors on and off.

Follow the measurement rules in `doc/mesa-zink-performance-archive-review.md`:
one change at a time, matched scenes, frame-time distributions, and visual
qualification of probes, water and transparency before accepting a change.
