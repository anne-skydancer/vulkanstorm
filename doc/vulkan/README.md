# Vulkan pipeline audit and design

Current `vkstorm-vulkan` objective: **Vulkan is a peer backend; OpenGL fallback
is not achievable.** The selected GHI candidate is **DiligentCore**. The first
deliverable is a minimal UI and connected in-world chat interface rendered
natively through its Vulkan backend, with **no world rendering**. See the
[first-deliverable requirements and acceptance evidence](qualification-roadmap.md#first-deliverable-for-vkstorm-vulkan).
This objective supersedes historical selection/fallback proposals below and in
the linked audit reports. No implementation or runtime qualification is claimed
by this documentation update.

GL/Zink is a **GL backend**, created for AMD GPUs affected by rendering
regressions caused by bugs in the AMD OpenGL ICD. Its use of Vulkan for driver
translation does not make it a native Vulkan viewer backend or a Vulkan fallback.

Current insertion-catalog baseline: **`e0545386296bf3ce0b722720b240008a6bef1dda`**, on **`vkstorm-vulkan`**. Target: **Windows and Linux**. DiligentCore is the selected graphics abstraction. The [current insertion catalog](diligent-insertion-catalog.md) defines implementation locations and acceptance accounting; it includes no native viewer implementation or runtime qualification.

Historical comparative audit baseline: **`1a490c3cb7ed60124169bf4bf6ad61a6ae1eeec5`**. Its mandate compared suitable architectures, including native Vulkan and general graphics abstractions. Those reports contain no runtime measurements, GPU captures, native implementation or build qualification.

The historical baseline belongs to `vkstorm-devel`. Publishing those findings on `vkstorm-release` and `vkstorm-canary` made the reference design available there; it did not certify those branches' renderer source parity with the audited baseline. The current insertion audit is scoped to its pinned feature-branch source.

The historical [paired in-depth design](framework-comparison.md) evaluated both DiligentCore and bgfx against the 73 listed records. It favored **DiligentCore for qualification**, based on explicit UAV/subresource transitions, GLSL/SPIR-V signatures and fence readback; bgfx has credible graphics storage support but additional shader-container, pass-boundary and frame-readback gates. The viewer retains scene/pass/material semantics; the library supplies device/resource allocation, descriptors, PSOs, commands, barriers and lifetime infrastructure. Remaining GL decoupling and custom shader/effect/auxiliary migration are substantial, not automatic.

Sustainable implementation/maintenance capacity excludes a bespoke Vulkan RHI as the primary implementation or default fallback. Native escapes must be exceptional and bounded. The user has selected DiligentCore as the GHI. Its pinned dependencies are prepared on `vkstorm-vulkan`; native rendering, compatibility and parity remain unqualified. GL/Zink remains the reference renderer. See [architecture comparison](architecture.md).

The shared rendering architecture and default functional path remain independent and fully support NVIDIA/AMD/Intel. **Vendor-origin supporting utilities are eligible** and should be used when qualified to improve outcomes, with equivalent independent functional fallback. Evaluate actual capabilities, licenses/build compatibility, quality and measured benefit; origin or vendor ID alone neither excludes nor selects a helper. AMD VMA is a supporting-utility candidate, not automatically adopted. Pinned DiligentCore source now verifies integrated optional FSR1 and NVIDIA DLSS Vulkan providers, with explicit input/state/glow-alpha and platform/build constraints in the comparison; bgfx also supplies an adaptable FSR1 compute example. Availability is not viewer integration or measured benefit.

Use the same qualified common abstraction on NVIDIA, AMD and Intel. Optional NVIDIA-only NVRHI requires an equivalent independent AMD/Intel path, isolated adapter dependencies, and tangible benefit exceeding duplicate adapter/testing costs; defer it until the common path qualifies. NVIDIA retains the common path. Equivalence means practical functionality, quality, lifecycle and comparable qualification goals, not identical FPS. Utility permission does not authorize a vendor-controlled universal rendering core.

Qualify Windows all three vendors and Linux Mesa AMD/Intel plus NVIDIA proprietary drivers, including hybrid/mixed-adapter selection. Shared core independence and qualified vendor utilities are compatible requirements. Actual runtime compatibility remains untested.


## Current DiligentCore insertion audit

The user selected DiligentCore, and `vkstorm-vulkan` now includes pinned package
recipes, runtime staging and a disabled Vulkan selector entry. None constitutes
native viewer rendering. The old 73-record assessment does not establish all
insertion points. Acceptance requires the [insertion catalog](diligent-insertion-catalog.md),
its [records](diligent-insertion-records.json) and [site ledger](diligent-insertion-sites.csv)
to reconcile the current tracked source and transitive rendering responsibilities.
The current ledger reconciles **28,263 source witnesses in 1,731 files**, all
**225 shader modules** and **690 historical registration rows**, with no
unmapped or unreviewed entries. These are source-accounting counts, not counts
of edits or features. Source-reviewed roots, callback obligations, interface
boundaries and typed exceptions support the location mapping.
Run `python doc/vulkan/check_diligent_insertions.py --accept` to check documented
acceptance status and reproducible accounting. A passing mechanical coverage
check alone is insufficient: unexplained in-scope paths and discovery gaps must
remain blockers. Native rendering and runtime parity remain unqualified.

## Reports and evidence

| Artifact | Contents |
|---|---|
| [Source audit](source-audit.md) | Frame/resource graphs, main and auxiliary pass routing, draw families, PPLL and GL state/lifetime contracts. |
| [Resources and platforms](resources-platform.md) | GL wrappers, uploads/publication, buffers, shaders/targets, media, capabilities, WSI/teardown and build/staging. |
| [Scene, UI and auxiliary views](scene-ui-auxiliary.md) | Assets/scene/culling, materials/avatars, UI/fonts/media, previews/bakes, picking and capture producers/consumers. |
| [Shader contracts](shader-contracts.md) | Shader assembly/fallback, 11 families, encodings, binding/vertex ABI and language/toolchain decisions. |
| [Both framework designs](framework-comparison.md) and [73-row paired API matrix](framework-contract-matrix.csv) | Concrete public API/backend routes, end-to-end adapters, ordering/ABI/readback/lifecycle/build/utility constraints and comparative qualification. |
| [Architecture](architecture.md) | Open option matrix, concrete framework evidence, shared interfaces, Vulkan capability assumptions and synchronization. |
| [Component designs](component-designs.md) | Compatible designs, coordinate/encoding conversion and cross-report design traceability. |
| [Qualification roadmap](qualification-roadmap.md) | Source-gap closure, candidate experiments, dependency-ordered implementation and parity/performance acceptance. |
| [Coverage ledger](coverage-ledger.csv) | Unique record IDs and source-contract/design locations; these are family/responsibility records, not counts of distinct renderer features. |
| [Shader inventory](shader-inventory.csv), [registration rows](shader-registration.csv), [GL candidate files](gl-source-inventory.csv) | Reproducible lexical evidence, not behavioral reachability or completion proof. |

## Coverage and limits

There are **73 listed contract/design records**: 27 core frame/draw records (F01–F10/D01–D17), 13 resource/platform/build records (R01–R09/P01–P03/B01), 22 scene/UI/auxiliary records (S01–S10/U01–U06/A01–A06), and 11 shader-family records (H01–H11). Across the full ledger, 71 records are active/conditional and two are dormant: D16 (empty old-sky pool) and H10 (disabled error-shader fallback). The core subset has 26 active/conditional records and one dormant record. All listed records have candidate designs or explicit no-output decisions for dormant records. These groups overlap semantically; 73 is **not** a count of independent features or exhaustively traced execution paths.

The lexical inventory contains **225 GLSL files**, **690 shader registration/variant rows**, **223 GL candidate source files**, and **13 draw-pool implementation files**. Parent scope cross-checks include 26 draw-pool `.cpp/.h`, 51 llrender, 42 llwindow and 233 llui source files. Counts establish inventory scope only. Critical source boundaries were traced, but all branches, all realized shader permutations, all callbacks and per-pixel arithmetic were not exhaustively proven.

The historical reports record six architecture decisions (U-D1–U-D6). Library adoption and revision are now settled by the selected, pinned DiligentCore preparation. Depth formats, shader binding/vertex ABI, history/probe reset behavior, media/bake ownership and cancellation, and measurement-dependent optimizations still need implementation qualification. The historical source-gap lists describe the earlier audit; the current insertion catalog supersedes their insertion-location accounting. Locating every required seam does not qualify realized shader permutations, bake arithmetic, glTF compatibility, asset/cache failure behavior or an optional proprietary pathing implementation. Runtime parity, performance, memory budgets, driver portability and failure handling remain unqualified.

The 73-record catalog is an earlier partial responsibility-level assessment. It is **not accepted as a complete DiligentCore insertion catalog**. Every necessary insertion point must have a source-backed mapping or justified exclusion; unresolved in-scope paths and discovery blind spots block acceptance. Runtime qualification is a separate requirement.

## Reproduce the inventory

From the repository root at the pinned baseline, run `python doc/vulkan/inventory.py` (the existing local interpreter may be used). The generator refuses tracked `indra` differences from the baseline, reads tracked source files and writes the three CSV inventories plus [inventory-summary.json](inventory-summary.json). It masks comments and literals for GL discovery, validates command names against the [pinned Khronos registry](gl-registry.md), and matches exact wrapper class names. GLSL declarations require global brace/parenthesis scope and include qualified interface blocks. Run `python doc/vulkan/test_inventory.py` for regression checks. It does not preprocess C++, evaluate shader macros, resolve runtime call graphs or prove source reachability. `direct_cpp_literals` records literal references, not all dynamically assembled uses. CSV hashes describe source bytes, not compiled shader modules; lexical declarations are not compiled ABI reflection.

Primary Khronos and framework references were accessed on 3 October 2026. Moving documentation links and framework main branches support the architectural assessment; implementation must pin immutable SDK/framework/tool revisions. Source links use the audited commit. The proposed API floor is Vulkan 1.3 with explicitly queried/enabled features, while the consulted normative documentation reports revision 1.4.365; these are different facts.

The comparative upstream snapshots are DiligentCore `bcb8b11eecd0899962c330b798ebe3e786b02bbb` and bgfx `abf165d8a78f962ad05da05f10adf0380bce286d`. DiligentCore is now the selected, pinned dependency; the bgfx snapshot remains historical comparison evidence. [framework-matrix.py](framework-matrix.py) regenerates the curated paired matrix and checks its IDs against the existing coverage ledger; it is design data, not automated behavioral analysis.
