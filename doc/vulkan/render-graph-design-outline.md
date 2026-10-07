# Render graph and pass-plan design outline

Target: extend [component-designs.md](component-designs.md) (and cross-link
[shader-contracts.md](shader-contracts.md) and the
[framework contract matrix](framework-contract-matrix.csv)).
Status: *outline, not yet written.*

Grounding: direct code survey of `vkstorm-release` -- `llrender.h`,
`pipeline.h/.cpp` (`renderGeomDeferred`, `renderGeomPostDeferred`,
`renderGeomShadow`, `renderDeferredLighting`, `renderFinalize`,
`bindDeferredShader`), `lldrawpool.h`, `llspatialpartition.h`
(`LLDrawInfo`), `lldrawpoolterrain.cpp`, `lldrawpoolalpha.cpp`; plus
[qualification-roadmap.md](qualification-roadmap.md) and
[milestone1-source-acceptance.md](milestone1-source-acceptance.md)
(M1 gates, no-GL-fallback mandate, viewer-owned graph requirement -- the
roadmap explicitly mandates "viewer-owned graph and resource contracts", so
the pass-plan and render-graph sections are required downstream, not optional).

---

## 1. Purpose and scope

- Consolidate the code survey into design deltas: pass-plan data model,
  render-graph node taxonomy, PSO variant key space, contract additions.
- Explicit non-goals: dual-backend RHI (already rejected, PR #35); translating
  `LLRender` call-for-call.

## 2. The shared boundary: from globals to a frame plan

- **Startup-time backend selection** -- one branch at `LLVKSession::start()`,
  no per-call backend conditionals. Per the roadmap mandate, this is **not a
  fallback switch**: Vulkan is a peer backend; failed initialization or device
  loss terminates the Vulkan session with a clear error, and no OpenGL recovery
  may be promised. GL remains a separate backend that may only supply reference
  images in a separate run.
- **CPU layer (shared):** cull, `stateSort`, pool build, `LLDrawInfo`
  production, CPU-computed per-frame constants. "CPU facts only, never GPU
  objects."
- **Graphics layer (per backend):** separate implementations of one render
  contract; GL path stays untouched as reference oracle.
- **Frame mode as explicit data:** replace scattered globals
  (`sRenderingHUDs`, `sImpostorRender`, `sReflectionRender`,
  `gCubeSnapshot`, `sUnderWaterRender`, `sRenderDeferred`,
  `sShadowRender`) with a `FrameMode` parameter of the plan -- these decide
  node inclusion, uniform values, and shader variants simultaneously.
- **Milestone 1 alignment:** the M1 gate taxonomy (`G-BACKEND`, `G-UI`,
  `G-RESOURCE`, `G-WORLD`) is the enforced form of this boundary for the
  UI/chat slice. The world render-graph work in sections 3-9 is a **later
  deliverable** under the roadmap's own sequencing ("implement the UI/session
  slice before world parity work"); M1's closed admission set and
  accounted-for deferred paths remain the gate until then.

## 3. The PassPlan data model

- One `PassPlan` per (pool type x pass flavor x pass index).
- Fields (from survey):
  - `flavor`: Deferred | PostDeferred | Shadow | ForwardWithinDeferred
    (alpha) | FullscreenPost (RT-to-RT)
  - state: blend (from `LLRender::eBlendFactor` pairs + scene blend),
    color mask, depth state, cull -- all sourced from the `LLGLEnable` /
    `LLGLDepthTest` / `setColorMask` sites being replaced
  - `ShaderKey` + variant dimensions
  - `items`: sorted `LLDrawInfo` spans; existing comparators = batching
    policy
  - `FrameBindings`: G-buffer RTs, shadow maps, LUTs, probes, noise, exposure
  - `WorldUniforms`: the `bindDeferredShader` uniform block
- Data already API-neutral: `LLDrawInfo` (`mStart/mEnd/mCount/mOffset`,
  `eBlendFactor`, material fields) -- cite as the anchor evidence.
- Encode the rigged = non-rigged + 1 convention explicitly (PASS_* and
  `renderObjects(type+1)`).

## 4. Render-graph node taxonomy

- Node inventory mapped 1:1 to observed code:
  - per-pool deferred nodes (pool loop in `renderGeomDeferred`)
  - shadow nodes (`renderGeomShadow`)
  - deferred lighting subpasses: sun/SSAO lightmap -> blur (two-pass
    ping-pong) -> soften -> local lights (per-light cubes / fullscreen) ->
    spot/projector
  - post-deferred pool nodes with **conditional inter-pass effect insertion**
    (`doAtmospherics`/`doWaterHaze`/`doWaterExclusionMask`)
  - post chain (`renderFinalize`): SSR -> luminance -> exposure ->
    tonemap/CAS -> glow -> DoF -> FXAA/SMAA -> vignette/snapshot-frame
  - UI composite (uses the separate UI matrix stack)
- Dependency edges: alpha node needs **depth only** (not G-buffer color) --
  document explicitly to avoid over-serialization.
- Buffer lifetimes: ping-pong RT swaps become graph-managed transients;
  `flush()`-per-bind pattern is the in-flight-frame hazard (ties to Phase A
  P0s).

## 5. Deferred pass contract (generated from `bindDeferredShader`)

- G-buffer read layout = descriptor set layout (DIFFUSE/SPECULAR/NORMAL/
  EMISSIVE attachments, point filter, clamp; depth; light target with
  parameterized source and white-texture fallback -> failure-mode contract).
- Uniform inventory (viewport, 6 shadow matrices, clip planes, SSAO block
  incl. FS window-scale correction, bias w/ altitude term, sun/moon dirs+colors
  w/ auto-adjust, resolutions, delta modelview pair, normal matrix, probe LOD).
- Static vs dynamic split: `bindDeferredShaderFast` already separates
  per-frame-dynamic bindings (shadow maps, probes, LUTs) from static ones ->
  per-frame descriptor set vs pipeline-constant UBO.
- `unbindDeferredShader` has no Vulkan counterpart -- note its disappearance.

## 6. PSO variant key space

- Dimensions (enumerable from survey): frame mode (shadow / cube snapshot /
  HUD / impostor / OIT-eligible) x shader variant (terrain paint types,
  material `SHADER_COUNT*2`, HUD/impostor alternates) x rigged/non-rigged x
  vertex-format mask (`getVertexDataMask()` varies by render mode) x blend
  state.
- Pre-baking strategy; cache size estimate; qualification for
  `pipeline-cache` integration (Phase A item).

## 7. Local-light pass redesign (Vulkan side)

- Per-light uniform+draw -> instanced cube draws with per-instance light buffer.
- Equivalence hazards: `BT_ADD` accumulation order, per-light falloff math,
  CPU Gaussian kernel weights -> move to shared CPU layer.
- Camera-inside/outside-light branching; spot projector deferral.

## 8. OIT / PPLL parity target

- The fork's existing PPLL capture/resolve (`RenderAlphaSortMethod == 1`,
  `<FS> Vulkanstorm` block) is the actual Phase D parity target, not generic
  OIT. Governed by the roadmap's world PPLL parity gates for later
  deliverables -- reference those gates rather than only the setting.
- Eligibility rules (main RT only, excludes mirrors/probes/cube/HUDs/impostors)
  become frame-plan data.
- Rationale: PPLL is natively Vulkan-shaped (atomic counter + linked list +
  resolve) -- candidate for early Vulkan implementation.

## 9. Contract-matrix additions (new coverage IDs)

1. `LLTexUnit` texture-env combiner vocabulary (`TB_*`/`TBO_*`/`TBS_*`)
   -> explicit blend shader code (silent-parity-risk category).
2. FS SSAO window-height scale correction (HiDPI/macOS hazard) -> shared
   CPU-computed uniform.
3. HDR gate is GL-version-dependent (`mGLVersion > 4.05` decides tonemap vs
   gammaCorrect) -> pin HDR-on as canonical for the contract.
4. Water-sign/clip logic as frame-plan data (pre/post-water, underwater,
   opaque-water clip).
5. Rigged/non-rigged "+1" ordering convention.
6. Cube-snapshot mode as first-class frame mode in the equivalence harness
   (mismatched-mode comparisons).
7. In-draw CPU side effects inventory (texture boost, spot-light priority
   update) -> move to shared prepare phase; Vulkan path would silently skip
   them.

## 10. Phase plan deltas

- Phase A: add pipeline-cache + descriptor-set-layout hardening items
  (sections 5, 6).
- Phase B: PassPlan extraction + world-uniforms refactor (the code comment in
  `prepare_alpha_shader` already requests it).
- Phase C: deferred skeleton nodes per section 4; post chain as first
  equivalence target (RT-to-RT, no scene input).
- Phase D: OIT parity per section 8.
- Validation: per-node byte-exact readback at the seams defined in sections
  4-5; GL reference path pinned per 9.3.

## 11. Resolved questions

1. **Conditional atmospherics -- pre-declared graph variants + conditional
   activation.** The insertion logic in `renderGeomPostDeferred` depends on
   a closed set of three booleans (`sUnderWaterRender`,
   `gCubeSnapshot`/probe level, HUD mode), all known at frame start,
   yielding at most 3-4 orderings. Declare two topologies (above/below water)
   plus a per-node skip flag driven by `FrameMode`. No mid-frame topology
   mutation is needed.
2. **Occlusion -- previous-frame result at the existing call site.** GL
   occlusion is already coarse and single-site: `doOcclusion(camera)` fires
   once, lazily before `POOL_GRASS`, consumed at pool-type granularity (the
   pool ordering comment is explicitly designed around hierarchical Z). A
   same-frame Vulkan query would force a mid-recording GPU-CPU sync; a
   previous-frame visibility result drops into the same decision point with
   identical granularity. Promote it into the frame plan as a per-pool-type
   visibility mask.
3. **UI split -- split along the line the codebase already draws.** HUD
   attachments are world-pipeline content (pools, shared shaders,
   `RENDER_TYPE_HUD`, toggled in `renderDeferredLighting`) and remain
   world-graph nodes. 2D UI (`LLRender2DUtils`, separate `pushUIMatrix`
   stack, composites after `renderFinalize`) is owned by the new Vulkan UI
   stack (`LLVKContext`/`LLVKSession`); the world graph's UI-composite node
   only consumes its output texture. Do not unify the two.
4. **DiligentCore vs bgfx -- DiligentCore chosen and in active runtime
   qualification.** Adopted as the Vulkan-side GHI; qualification runs via
   `.github/workflows/software_vulkan.yml` on `vkstorm-vulkan`: standalone
   `diligent_render_test` suite (CTest +
   `run_software_vulkan_tests.py`, evidence artifacts, headless mode) on a
   Windows/Linux x SwiftShader/Lavapipe software-GPU matrix, plus a staged
   development viewer built with `-DUSE_DILIGENTCORE=ON`. Architectural
   containment has source-accounting checks through
   `check_diligent_insertions.py` and `check_milestone1_boundaries.py` in CI;
   these checks do not enforce runtime gates. Hardware-GPU
   compatibility/performance measurements (NVIDIA/AMD/Intel) remain
   unmeasured and are not an implementation acceptance requirement. Assume
   AMD/NVIDIA hosts are unavailable. The immediate implementation gap is the
   native viewer path, covered by [viewer-integration-plan.md](viewer-integration-plan.md);
   software-device harness qualification does not close that gap.
