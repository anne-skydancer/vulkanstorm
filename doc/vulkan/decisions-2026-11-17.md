<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Vulkan line decisions (owner, 2026-11-17)

These decisions refine [direction-2026-10-29.md](direction-2026-10-29.md). Where they differ, these apply. The OpenGL viewer is unchanged by any of them.

**Precedence.** This page supersedes the DiligentCore, bgfx, VMA and wgpu statements in the earlier study files ([architecture.md](architecture.md), [resources-platform.md](resources-platform.md), [qualification-roadmap.md](qualification-roadmap.md), [framework-comparison.md](framework-comparison.md), [component-designs.md](component-designs.md)). Those files are kept as a record and are not rewritten.

## Owner decisions

### API and shaders

| Topic | Decision | Reason |
|---|---|---|
| API | Raw Vulkan through **ash**. Not wgpu. | Control, and FSR's host side in Rust over ash. |
| Shaders (V3) | **Slang**, the latest stable release, pinned to an exact version and updated deliberately. | One modern language that compiles to SPIR-V and shares code as modules. |
| DX12 | No DX12 route. Native Vulkan only. | We ship on Windows and Linux with Vulkan drivers. |

**On the API choice, honestly.** The study of Aurora's GPU hangs ([robustness-rules.md](robustness-rules.md)) and its audit did not single out wgpu. No hang was traced to wgpu's design or to the ash seam. The one wgpu defect found (A11, Intel descriptor pool) was a library bug, fixed in a fork; wgpu changed how a loss surfaces and what it hides (budget, breadcrumbs). The one confirmed driver hang (RDNA 4) is a driver bug any API would meet; the largest cluster (#2239) is unexplained. The VRAM and work-sizing causes need the same policy under ash. The choice rests on control and on our Windows and Linux only scope. It is a design choice, not a finding.

### Anti-aliasing and upscaling

- The same anti-aliasing options as Aurora.
- Plus **FSR, including upscaling**.
- No frame generation for now.

### Ray tracing

Ray tracing stays an option for the light work. See [ray-traced-shadows-option.md](ray-traced-shadows-option.md).

### Vulkan decisions

The owner accepted V1, V2 and V4 to V7 as recommended. V3 is the Slang row above.

| ID | Decision | Reason |
|---|---|---|
| V1 | Vulkan 1.3 floor, with these features required (descriptor indexing is a feature to enable): descriptor indexing, dynamic rendering and synchronization2. The "floor GPU" is the slowest GPU we support under this floor. | One modern model with no fallback paths. |
| V2 | **gpu-allocator** (pure Rust). VMA is excluded. | VMA is C++, which the no-FFI rule forbids. |
| V4 | Equivalence with the OpenGL viewer means the same look, measured per pixel within a stated tolerance, with every difference explained. Not pixel-identical. The OpenGL viewer itself stays identical. | Different APIs round differently. A stated tolerance is testable. |
| V5 | HDR display output later. Design for it, ship SDR first. | Avoids a late rework of the output stage. |
| V6 | GPU choice is automatic (discrete). An override exists only as a debug setting, with no Preferences entry. | Matches our rule on Preferences entries. |
| V7 | Wayland and X11. | Both are in use on Linux. |

### Engineering defaults (part of the accepted recommendation)

- A disk pipeline cache.
- Validation layers in development builds only.
- Async compute only where the prototype measures a gain.
- No mesh shaders for now.
- Vsync and frame limits map to present modes.
- Crash-diagnostic markers fall back to a `vkCmdFillBuffer` pass id where the vendor extensions are missing ([robustness-rules.md](robustness-rules.md), R3).

### Choices the Vulkan viewer shares with the Rust port

| Area | Choice |
|---|---|
| UI | XUI through egui (option E). It includes our own widget tree. |
| Font | Inter, through skrifa. |
| Sound | symphonia, kira and cpal. |
| Voice | webrtc 0.21. |
| Third-party C/C++ (VLC, CEF) | External processes over the SLPlugin protocol. |
| FFI | None. |

## How we apply them (implementation notes, follow from the decisions)

**Anti-aliasing set, as read from Aurora's code.** None, FXAA, SMAA 1x high, MSAA 2/4/8 with a multisampled depth prepass, TAA. Specular AA is a separate switch. TAA and FSR share the motion vectors (including skinned avatars), the jitter and the reactive/transparency masks.

**FSR host side.** The FSR 3.1 host code is Rust over ash, using AMD's MIT-licensed shaders as SPIR-V. The FidelityFX SDK is not linked. This follows from the no-FFI rule.

**Frame generation.** It is out for now (owner), so it must not be designed out: keep the motion vectors, a present path that can take interpolated frames, and the HUD-less colour target available.

**Slang path.**

1. Start with today's GLSL through `slangc -allow-glsl`.
2. Move shared code to Slang modules: lighting, shadow filtering, the stochastic core, atmosphere.
3. Convert leaf shaders as they are touched.

The OpenGL viewer's GLSL stays as it is.

**Mesa's Dozen.** The owner asked about it as a DX12 route. It was considered and not used: a translation layer adds a failure surface and we ship Vulkan drivers anyway.

**Prototype.** The prototype (10 to 20k draws) no longer picks the API. It shapes the ash design and runs the three robustness checks in [robustness-rules.md](robustness-rules.md).

## Licences to add later

Add each to the audit in `doc/licensing.md` when it is actually added to the tree. This document does not edit that file. Verified on crates.io on 2026-11-17 where marked; the others are from the project pages as known and are not re-checked online.

| Component | Role | Licence | Verified |
|---|---|---|---|
| ash | Vulkan bindings | MIT OR Apache-2.0 | crates.io |
| gpu-allocator | Memory allocation | MIT OR Apache-2.0 | crates.io |
| Slang | Build tool (shader compiler) | Apache-2.0 with LLVM exception | not checked online |
| egui | UI drawing | MIT OR Apache-2.0 | crates.io |
| skrifa | Font reading | MIT OR Apache-2.0 | crates.io |
| Inter | UI font | SIL OFL 1.1 | not checked online |
| symphonia | Audio decoding | MPL-2.0 (file-level copyleft; compatible with GPL-3.0) | crates.io |
| kira | Audio engine | MIT OR Apache-2.0 | crates.io |
| cpal | Audio output | Apache-2.0 | crates.io |
| webrtc 0.21 | Voice | MIT/Apache-2.0 | crates.io |
| AMD FidelityFX FSR shaders | Upscaling | MIT | not checked online |
| VLC, CEF | Separate processes, not linked | Their own licences | not checked online |
