# Native Vulkan tree

All work here MUST follow
[Native Vulkan development invariants](../../doc/vulkan/native_vulkan_invariants.md).
Read that contract before changing implementation. It also governs supporting
native-renderer changes outside this directory.

Implement native, backend-exclusive rendering from neutral scene/layout/asset
inputs, not GL call translation, a shared low-level RHI or GL draw callbacks.
Keep view/material/history contracts explicit and retire GPU resources by proven
completion. Do not change the GL oracle to make a native comparison pass.

PRs must identify affected NV rule IDs and provide the contract's review record:
reference inputs, consumer-visible changes, GPU safety, validation and limits.
Consult the [approved reverse engineering](../../doc/vulkan/reverse-engineering/README.md)
for evidence; old design drafts do not override the invariants.

These instructions govern development, not a claim that existing bring-up code
already satisfies the contract. Changes to the governing rules require an
explicit reviewed amendment.
