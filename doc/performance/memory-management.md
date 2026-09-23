# Memory-management adaptation

Developed against vkstorm-devel 107366d729; the production changes are also carried onto a release branch based on master.

## Scope

Adapt selected policies from Alchemy develop 902e75e7db77aea86a675c72006510d5331979ae. Alchemy and Linden source retain their LGPL-2.1-only attribution. The complete texture-container replacement, immutable-storage rewrite, and reworked offscreen-object eviction are excluded from this change.

- Keep texture detail in a visible, unfocused viewer by default. Hidden/minimized behavior and genuine memory-pressure response remain active. TextureDiscardOnFocusLoss restores the old focus-loss policy when enabled.
- Saturate heap-headroom subtraction, read current Linux RSS with a peak-RSS fallback, and use the scarcest available physical/commit memory on Windows. Pressure recovery has a deadband and rate limits.
- SceneLoadAutomaticMemoryBudget defaults to true. It derives effective scene-loading limits from installed RAM (25%/60%) and the viewer heap cap. The legacy SceneLoadLowMemoryBound/HighMemoryBound values remain saved and take effect in manual mode; runtime pressure never overwrites them. This implements the user's selected migration policy.
- Limit ordinary texture face-priority traversal to 32 faces per visit. Carry size and visibility over a rotation, age old results out, and reset traversal when face membership changes. Retain existing texture containers, ownership/refcount thresholds, selected/sculpt handling, and >1024-face fallback.
- Record mutable GL allocations per mip and cube face, including explicit compressed uploads, generated non-square mip chains, arrays, storage-format padding estimates, and eventual deletion. Texture budget/display consumes this estimate once; vertex overhead estimate remains doubled. These are allocation estimates, not actual dedicated VRAM residency.
- Default the total disk texture-cache allowance to 6400 MB, providing approximately 4096 MB of texture-body capacity after the header/fast-cache reservation. Preferences explains this split. Existing saved user overrides remain effective.

## Compatibility contract

The GL texture object lifetime and all graphics allocation/upload commands remain as before. Accounting follows redefinitions of individual mip images instead of discarding the whole name's record. Generated mips update existing levels rather than double-counting. GL render-target textures are already counted in the texture total; the separate Render field must not be added again as independent usage.

The native Vulkan allocator is separate and is not represented by LLImageGL's counters. Shared CPU-memory/scene policies still apply to viewer code used with that backend. No Vulkan allocation/ownership contracts or native renderer files are changed.

The old visibility-based object eviction predicate stays disabled. RAM-scaled bounds affect active scene-loading consumers; this is not an implementation of Alchemy's new eviction policy.

## Verification

scripts/perf/test_memory_management.py compiles production policy headers and extracted production allocation/free/format methods. It checks pressure entry/recovery, automatic/manual scene limits and heap capping, face-scan convergence/visibility aging, non-square mips, level replacement, cube faces, arrays, compressed block dimensions, and idempotent deletion. The development branch separately retains and tests the baseline's opt-in Tracy allocation/GPU instrumentation guards.

Build configuration: RelWithDebInfo, Mesa/Zink on, Tracy CPU zones on, allocation and GPU tracing off, local-only/on-demand connection, AVX2/LTO on, PACKAGE off. A fresh build needs llvk_ui_shaders built before the existing manifest copy target. No installer is expected for RelWithDebInfo.

Initial user testing of build 81946 on Windows/AMD Zink reported successful operation and recovery of estimated free VRAM from 64 MB to approximately 900-1000 MB. This is an observation of the console estimate, not a measured physical-residency or controlled FPS improvement. The cache-default change followed that build and has been XML-validated.

Broader live validation remains necessary: native OpenGL and Zink on AMD/NVIDIA Windows/Linux; stationary texture settling, camera rotation, face/attachment changes, minimize and visible-focus transitions, and low-memory recovery. Compare sharpness and reloads as well as FPS, CPU zones, allocation estimates and actual GPU residency. The code tests do not establish a performance gain or visual parity.
