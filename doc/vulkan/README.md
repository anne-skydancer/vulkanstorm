# Vulkan pipeline audit and design

Audited source baseline: **`1a490c3cb7ed60124169bf4bf6ad61a6ae1eeec5`**. Target: **Windows and Linux**. User mandate: compare all suitable architectures, including native Vulkan and general graphics abstractions. This report changes no viewer code and includes no runtime measurements, GPU captures, native implementation or build qualification.

This baseline belongs to `vkstorm-devel`. Publishing the same findings on `vkstorm-release` and `vkstorm-canary` makes the reference design available there; it does not certify those branches' renderer source parity with the audited baseline.

The [paired in-depth design](framework-comparison.md) evaluates both DiligentCore and bgfx against all 73 records. It favors **DiligentCore for qualification**, based on explicit UAV/subresource transitions, GLSL/SPIR-V signatures and fence readback; bgfx has credible graphics storage support but additional shader-container, pass-boundary and frame-readback gates. The viewer retains scene/pass/material semantics; the library supplies device/resource allocation, descriptors, PSOs, commands, barriers and lifetime infrastructure. Remaining GL decoupling and custom shader/effect/auxiliary migration are substantial, not automatic.

Sustainable implementation/maintenance capacity excludes a bespoke Vulkan RHI as the primary implementation or default fallback. Native escapes must be exceptional and bounded. DiligentCore is not yet adopted or proven suitable; if no mature abstraction qualifies, the implementation decision is blocked by feasibility and GL/Zink remains operational. See [architecture comparison](architecture.md).

The shared rendering architecture and default functional path remain independent and fully support NVIDIA/AMD/Intel. **Vendor-origin supporting utilities are eligible** and should be used when qualified to improve outcomes, with equivalent independent functional fallback. Evaluate actual capabilities, licenses/build compatibility, quality and measured benefit; origin or vendor ID alone neither excludes nor selects a helper. AMD VMA is a supporting-utility candidate, not automatically adopted. Pinned DiligentCore source now verifies integrated optional FSR1 and NVIDIA DLSS Vulkan providers, with explicit input/state/glow-alpha and platform/build constraints in the comparison; bgfx also supplies an adaptable FSR1 compute example. Availability is not viewer integration or measured benefit.

Use the same qualified common abstraction on NVIDIA, AMD and Intel. Optional NVIDIA-only NVRHI requires an equivalent independent AMD/Intel path, isolated adapter dependencies, and tangible benefit exceeding duplicate adapter/testing costs; defer it until the common path qualifies. NVIDIA retains the common path. Equivalence means practical functionality, quality, lifecycle and comparable qualification goals, not identical FPS. Utility permission does not authorize a vendor-controlled universal rendering core.

Qualify Windows all three vendors and Linux Mesa AMD/Intel plus NVIDIA proprietary drivers, including hybrid/mixed-adapter selection. Shared core independence and qualified vendor utilities are compatible requirements. Actual runtime compatibility remains untested.

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

There are **73 listed contract/design records**: 27 core frame/draw records (F01â€“F10/D01â€“D17), 13 resource/platform/build records (R01â€“R09/P01â€“P03/B01), 22 scene/UI/auxiliary records (S01â€“S10/U01â€“U06/A01â€“A06), and 11 shader-family records (H01â€“H11). The core ledger marks 26 active/conditional records and one dormant empty old-sky record. All listed records have candidate designs or an explicit no-output decision for that dormant record. These groups overlap semantically; 73 is **not** a count of independent features or exhaustively traced execution paths.

The lexical inventory contains **225 GLSL files**, **690 shader registration/variant rows**, **282 GL candidate source files**, and **13 draw-pool implementation files**. Parent scope cross-checks include 26 draw-pool `.cpp/.h`, 51 llrender, 42 llwindow and 233 llui source files. Counts establish inventory scope only. Critical source boundaries were traced, but all branches, all realized shader permutations, all callbacks and per-pixel arithmetic were not exhaustively proven.

Six explicit architecture decisions remain open (U-D1â€“U-D6): library revision/adoption, depth formats, shader binding/vertex ABI, history/probe reset visibility, media/bake ownership and cancellation, and measurement-dependent optimizations. Additional source gaps include whole-tree raw-state reconciliation, realized shader routes/uniform producers, media plugin protocol, every preview subclass, full bake-mask arithmetic, glTF extensions, asset/cache failures, specialized debug/pathing branches and historical platform reachability. Each report preserves its own detailed gaps. Runtime parity, performance, memory budgets, driver portability and failure handling are entirely unqualified here.

Thus this is a complete inventory/contract/design catalog **for the listed records**, with a substantive end-to-end architecture assessment. It is not a certificate of exhaustive behavioral coverage or a qualified Vulkan renderer.

## Reproduce the inventory

From the repository root at the pinned baseline, run `python doc/vulkan/inventory.py` (the existing local interpreter may be used). The generator refuses tracked `indra` differences from the baseline, reads tracked source files, strips comments lexically while preserving line numbers and writes the three CSV inventories plus [inventory-summary.json](inventory-summary.json). It does not preprocess C++, evaluate shader macros, resolve runtime call graphs or prove source reachability. `direct_cpp_literals` records literal references, not all dynamically assembled uses. CSV hashes describe source bytes, not compiled shader modules.

Primary Khronos and framework references were accessed on 3 October 2026. Moving documentation links and framework main branches support the architectural assessment; implementation must pin immutable SDK/framework/tool revisions. Source links use the audited commit. The proposed API floor is Vulkan 1.3 with explicitly queried/enabled features, while the consulted normative documentation reports revision 1.4.365; these are different facts.

The comparative upstream snapshots are DiligentCore `bcb8b11eecd0899962c330b798ebe3e786b02bbb` and bgfx `abf165d8a78f962ad05da05f10adf0380bce286d`. These are source-analysis pins, not adopted dependencies. [framework-matrix.py](framework-matrix.py) regenerates the curated paired matrix and checks its IDs against the existing coverage ledger; it is design data, not automated behavioral analysis.
