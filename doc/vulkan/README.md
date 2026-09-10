# Vulkan documentation

## Governing contract and reference

- [Native viewer roadmap](native_viewer_roadmap.md), reset at PR #41 on
  2026-09-10: function/helper contracts first, native design second, implementation
  and verification third. This replaces the older native implementation sequence.
- [Native Vulkan development invariants](native_vulkan_invariants.md) govern
  native renderer work and integration changes across the repository.
- [Approved OpenGL reverse engineering](reverse-engineering/README.md) documents
  the pinned reference behavior and native parity requirements.

The invariants supersede conflicting native-renderer architecture proposals in
older drafts. They do not change the timing/platform policy of the separate
[OpenGL modernization strategy](opengl_modernization_strategy.md).

## Historical design and implementation context

- [Original pipeline design](design_overview.md)
- [Migration strategy](migration_strategy.md)
- [Bootstrap](phase1_bootstrap.md)
- [UI plan](phase3_v2_ui_plan.md) and [greenfield UI design](phase3_v2_m0_design.md)
- [Text design](llvktext_design.md)
- [Capability probe design](capability_probe_design.md)
- [Shared assets](shared_assets.md)

These documents record earlier decisions, proposals and implementation states.
Read them with the approved report and governing contract; a draft or historical
parity claim is not current runtime evidence.
