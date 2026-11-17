<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Robustness rules for the Vulkan viewer (2026-11-17)

Eleven rules for the new renderer, taken from a study of Aurora Viewer's GPU hangs and device losses (2026-11-17; Aurora at `3704a818`) and corrected by its audit. They are design rules for the Vulkan line. They change nothing in the OpenGL viewer.

## What the evidence shows, and what it does not

- Aurora filed about 207 "GPU device lost" reports in three weeks. Its causes are mixed.
- Confirmed causes: a driver hang on AMD RDNA 4, photo passes longer than the 2 s Windows TDR, video memory oversubscription, the BAR window running out, and a wgpu bug on Intel.
- The largest cluster (Aurora #2239, a hang with free VRAM) is **unexplained**. Nobody could name the pass, because the breadcrumb PR was closed unmerged.
- **No hang was traced to wgpu itself.** wgpu changed how a loss surfaces (panics, opaque out-of-memory errors) and what it does not expose (budget, breadcrumbs). It did not cause the hangs.
- The RDNA 4 hang (A1) is **one reporter group's matched on/off test**. The exact shader construct the driver mishandles is unknown. Any API would meet it.
- The panics and the issue counts came from a subagent reading wgpu 30.0.1 and the tracker. They were not re-run.
- Cost figures are Aurora's measurements or estimates until our prototype measures them.

The rules therefore rest on the confirmed classes and on published practice. They do not claim that a different API would have avoided Aurora's hangs.

## Rules

| Rule | Statement |
|---|---|
| R1 | Time-bounded submissions |
| R2 | Device loss is a normal path |
| R3 | Crash diagnostics from day one |
| R4 | Enforced VRAM budget |
| R5 | Staging in host memory |
| R6 | Validate GPU-visible counts at ingest; clamp dynamic indices |
| R7 | Bounded shader loops |
| R8 | Bounded indirect draws |
| R9 | No CPU waits on the GPU inside the frame |
| R10 | Vendor qualification of new compute passes |
| R11 | No silent CPU renderer; overlay layers off by default |

### R1 Time-bounded submissions

- **Rule.** No submission exceeds 250 ms of GPU time on the slowest supported GPU (an eighth of the 2 s TDR). Long jobs (photos, probe or shadow bakes, BVH builds, pipeline warm-up) are split into chunks sized from GPU timestamps of earlier chunks.
- **Why.** Photos drawn in one pass took 2.5 s on a Radeon 610M and lost the device (A4, confirmed by the project). Aurora's fix bounds each pass to 500 ms of measured GPU time. Microsoft documents the 2 s default TDR.
- **Test.** A timestamp per submission in the demo capture shows none above 250 ms. A forced-slow run (lowered GPU clock or an artificial cost multiplier) shows the job split, not lost.

### R2 Device loss is a normal path

- **Rule.** Every GPU wait has a finite timeout. On loss: destroy the device and everything made from it, then create a new device from the same physical device. Automatic recovery is allowed once per 10 minutes. A second loss in that window comes back at the safe level with the running feature off. No thread is left behind at exit (our clean-shutdown premise).
- **Why.** Aurora shipped recovery late and off by default (A17). In 11 reports the recovered device was lost again 7 to 20 s later (A15). Windows bug-checks after 5 TDRs in one minute; the link to the "system restarted" reports is plausible, not proven. The Vulkan spec says a lost device cannot be reset, only replaced.
- **Test.** Prototype check 1 below.

### R3 Crash diagnostics from day one

- **Rule.** Read `VK_EXT_device_fault` on loss. Put markers at pass boundaries (`VK_AMD_buffer_marker` or `VK_NV_device_diagnostic_checkpoints`), on by default if they cost 0.2 ms or less, otherwise a debug setting. The crash report names the last pass that completed and the first that did not.
- **Why.** The #2239 cluster has no root cause after about 40 analysed reports, because no breadcrumbs shipped (A2, confirmed absence). Aurora measured 0.18 ms per frame on an RX 9070 XT.
- **Test.** Prototype check 3 below.

### R4 Enforced VRAM budget

- **Rule.** Poll `VK_EXT_memory_budget` about once a second and use the lowest value of the last minute. Keep usage at or below 90 % of it. Evict streamed textures and meshes first. Render targets are allocated at a settings change, never lazily inside a frame. Every allocation failure is a handled result.
- **Why.** Oversubscription made WDDM page memory out, frames took about 1 s, then the device was lost (A3, confirmed as a mechanism). Lazily created render targets stayed invalid after one refusal (A5, confirmed by the fix). A session that is over budget is plausible for individual reports, not shown: no loss on 0.9.1 or later was over budget.
- **Test.** Prototype check 2 below.

### R5 Staging in host memory

- **Rule.** Staging buffers live in host-visible system memory unless Resizable BAR is present. Mapped device-local memory is capped at a quarter of its heap.
- **Why.** On NVIDIA without Resizable BAR, staging plus about 1,200 buffer writes a frame filled the 256 MB BAR window and lost the GPU (A7, confirmed by the fix; no reporter confirmation).
- **Test.** Run the demo with a simulated 256 MB BAR heap. No loss, no stall.

### R6 Validate GPU-visible counts at ingest; clamp dynamic indices

- **Rule.** Decoders check vertex, index, joint and material counts and offsets against their buffers before upload. Shaders clamp bindless and palette indices (`min(i, n - 1)`). `robustBufferAccess2` is on in development builds.
- **Why.** Aurora's GPU-culling ranges and bindless page indices had no bound (A8): a confirmed bug, but only a plausible cause of losses. Our own OpenGL viewer has the same class of gap (glTF joints, particle blend factors, terrain patch size), fixed as hotfixes. The Aurora study did not audit its other decoders.
- **Test.** A fuzz corpus of malformed meshes, glTF and network data through the decoders. A shader test with out-of-range indices under `robustBufferAccess2`.

### R7 Bounded shader loops

- **Rule.** A loop count is a constant, or `min(count, CONST)` inside the shader, not only on the CPU. Counters are integers (a float counter stalls at 2^24 or on NaN).
- **Why.** Our RLVa variable blur takes an unbounded loop count from a script command, with a float counter (a confirmed code path, not run). Aurora's own audit found its loops bounded, so this is a rule from our code, not from their hangs.
- **Test.** A lint in CI flags any non-constant `for` or `while` bound without `min(` or `clamp(`.

### R8 Bounded indirect draws

- **Rule.** `maxDrawCount` equals the capacity of the indirect buffer, and the producing compute pass clamps the count.
- **Why.** Same class as R6 (A8). Standard GPU-driven practice.
- **Test.** A unit test on the cull pass with an oversize count.

### R9 No CPU waits on the GPU inside the frame

- **Rule.** Readbacks (occlusion, picking, statistics) go through an N-frame ring of fences that are polled, never waited on. The only blocking wait is frame pacing, with a timeout.
- **Why.** No Aurora hang was traced to this class. It is here because our OpenGL viewer has a blocking occlusion fallback and an unbounded sync wait on the upload worker, and a wait that never returns breaks R2.
- **Test.** A Tracy capture shows no fence waits inside the frame scope.

### R10 Vendor qualification of new compute passes

- **Rule.** A pass that uses workgroup memory or subgroup operations is default-on only after a run on NVIDIA, AMD (at least one of RDNA 2, 3 or 4) and Intel. A kill switch exists as a debug setting.
- **Why.** The RDNA 4 hang (A1) was a workgroup-memory plus barrier shader made default without a vendor matrix. The matched test is one reporter group's, and the exact construct is unknown.
- **Test.** The qualification checklist in the PR. The kill switch is covered by a settings test.

### R11 No silent CPU renderer; overlay layers off by default

- **Rule.** Refuse llvmpipe and lavapipe, with a message. Disable known overlay and capture implicit layers unless the user allows them.
- **Why.** A heavy frame on llvmpipe tripped the watchdog (A12, confirmed by log). Overlay hooks were present in many reports and were kept out by default in Aurora, but hangs continued without them (A14, plausible as a contributor only).
- **Test.** A start-up log check. A run on a machine with a simulated missing feature ends in the message, not a fallback.

## The three prototype checks

The prototype (10 to 20k draws) runs these checks on the ash renderer. Each is a CI or demo test.

| # | Check | Pass condition |
|---|---|---|
| 1 | Recover from an injected GPU hang. A debug-only, bounded spin shader, plus injected `VK_ERROR_DEVICE_LOST`. | The session stays up. No panic, no leaked thread. The second loss inside 10 minutes comes back at the safe level. |
| 2 | Survive another process filling VRAM to the brim. | Textures lower, the frame keeps running, no device loss, no frozen world. |
| 3 | Breadcrumbs name the hung pass. Run check 1 and read the report. | The report names the last completed and the first unfinished pass. The cost is at most 0.2 ms per frame, or the markers become a debug setting. |

Also count the lines and `unsafe` blocks each check needs. The count goes into the prototype report.
