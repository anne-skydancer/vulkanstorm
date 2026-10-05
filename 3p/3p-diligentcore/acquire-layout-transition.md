# Pinned swapchain acquisition fix

Base: DiligentCore `bcb8b11eecd0899962c330b798ebe3e786b02bbb`.
Overlay: `acquire-layout-transition.patch`, hashed with its exact resulting
source file in `3p/vulkan-dependencies.json` (LF-normalized hashes).
The package version suffix `acquire1` distinguishes it from the unpatched binary.

The initial Windows SwiftShader presentation run, using validation layers
`0afe79ff768afd7243b6f697b07b44ee3b2f59ea` with synchronization validation enabled,
reported `SYNC-HAZARD-WRITE-AFTER-READ`: the first swapchain image layout transition
could execute before the image-acquire semaphore's color/transfer wait stages.
The transition uses TOP_OF_PIPE as its source stage and writes image layout
state previously accessed by acquisition. The same issue recurred on recreation.

Wait at ALL_COMMANDS for the acquired image, covering layout transitions and
subsequent color/transfer use. This is a conservative dependency-level fix,
without viewer-owned Vulkan command machinery or validation suppression.
The relevant scope is defined by [VkSubmitInfo's semaphore wait stages](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubmitInfo.html).

Local prototype result on Windows x64 / SwiftShader LLVM 10: offscreen original
and replacement readbacks, native presentation/resize/minimize/restore/teardown,
core-validation probe, synchronization probe and bad-pixel probe all passed after
the fix. Before the fix, the presentation test failed on the reported hazards.
This is not fresh/cached CI acceptance, Linux qualification or a performance
measurement. The broader wait may reduce overlap; no performance improvement is
claimed. Refine wait/barrier scopes only with equivalent correctness evidence.

The dependency builder verifies the base revision, patch hash and resulting
source hash, rejecting unrelated source edits. Existing correctly patched source
is accepted idempotently. No changes to the production Autobuild package manifest
or release branch are made by this overlay.
