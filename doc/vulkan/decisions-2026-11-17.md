<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Vulkan line decisions (owner, 2026-11-17)

These decisions refine [direction-2026-10-29.md](direction-2026-10-29.md). Where they differ, these apply. The OpenGL viewer is unchanged by any of them.

## API and shaders

| Topic | Decision | Reason |
|---|---|---|
| API | Raw Vulkan through **ash**. Not wgpu. | Control, and FSR's host side in Rust over ash. |
| Shaders | **Slang**, the latest stable release, pinned to an exact version and updated deliberately. | One modern language that compiles to SPIR-V and shares code as modules. |
| DX12 | No DX12 route. Native Vulkan only. Mesa's Dozen was considered and rejected. | We ship on Windows and Linux with Vulkan drivers. A translation layer adds a failure surface. |

**On the API choice, honestly.** The study of Aurora's GPU hangs ([robustness-rules.md](robustness-rules.md)) and its audit did **not** single out wgpu. No hang was traced to wgpu, the worst case is a driver bug that any API would meet, and the VRAM and work-sizing causes need the same policy under ash. What wgpu confirmably cost Aurora is how a loss surfaces and what it hides (budget, breadcrumbs). The choice rests on control and on our Windows and Linux only scope. It is a design choice, not a finding.

**Slang path.**

1. Start with today's GLSL through `slangc -allow-glsl`.
2. Move shared code to Slang modules: lighting, shadow filtering, the stochastic core, atmosphere.
3. Convert leaf shaders as they are touched.

The OpenGL viewer's GLSL stays as it is.

The prototype (10 to 20k draws) no longer picks the API. It shapes the ash design and runs the three robustness checks in [robustness-rules.md](robustness-rules.md).

## Anti-aliasing and upscaling

- Aurora's set: None, FXAA, SMAA 1x high, MSAA 2/4/8 with a multisampled depth prepass, TAA. Specular AA is a separate switch.
- Plus **FSR 3.1 including upscaling**.
- The FSR host code is Rust over ash, using AMD's MIT-licensed shaders as SPIR-V. The FidelityFX SDK is not linked.
- Frame generation is out for now and must not be designed out.
- TAA and FSR share the motion vectors (including skinned avatars), the jitter and the reactive/transparency masks.

## Vulkan decisions

| ID | Decision | Reason |
|---|---|---|
| V1 | Vulkan 1.3 floor. Descriptor indexing, dynamic rendering and synchronization2 are required. | One modern model with no fallback paths. |
| V2 | **gpu-allocator** (pure Rust). VMA is excluded. | VMA is C++, which the no-FFI rule forbids. |
| V4 | Equivalence with the OpenGL viewer means the same look, measured per pixel within a stated tolerance, with every difference explained. Not pixel-identical. The OpenGL viewer itself stays identical. | Different APIs round differently. A stated tolerance is testable. |
| V5 | HDR display output later. Design for it, ship SDR first. | Avoids a late rework of the output stage. |
| V6 | GPU choice is automatic (discrete). An override exists only as a debug setting, with no Preferences entry. | Matches our rule on Preferences entries. |
| V7 | Wayland and X11. | Both are in use on Linux. |

## Engineering defaults

- A disk pipeline cache.
- Validation layers in development builds only.
- Async compute only where the prototype measures a gain.
- No mesh shaders for now.
- Vsync and frame limits map to present modes.

## Ray tracing

Ray tracing stays an option for the light work. See [ray-traced-shadows-option.md](ray-traced-shadows-option.md).

## Choices the Vulkan viewer shares with the port

| Area | Choice |
|---|---|
| UI | The existing XUI files, drawn through egui. We keep our own widget tree. |
| Font | Inter, through skrifa. |
| Sound | symphonia, kira and cpal. |
| Voice | webrtc 0.21. |
| Third-party C/C++ (VLC, CEF) | External processes over the SLPlugin protocol. |
| FFI | None. |

## Licences to add later

Add each to the audit in `doc/licensing.md` when it is actually added to the tree. This document does not edit that file.

| Component | Role |
|---|---|
| ash | Vulkan bindings |
| gpu-allocator | Memory allocation |
| Slang | Build tool (shader compiler) |
| egui | UI drawing |
