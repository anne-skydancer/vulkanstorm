# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code. Report measured results and qualification limits accurately.
- At each completed checkpoint, commit and push the work, then continue toward the authorized outcome. Give a detailed completion report rather than handing off partial checkpoints.
- Carry authorized work through to the requested outcome. Milestones, diagnostic checkpoints and passing subsets of tests guide the work; they are not reasons to stop while requirements remain open.
- Provide the outcome and qualification recap at completion. Avoid mid-flight recaps or permission requests that turn internal checkpoints into handoffs; interrupt only for a concrete blocker requiring user input. Follow any higher-priority communication requirements without stopping the work.
- When asked to finish a roadmap stage, implement its entire scope and qualify its acceptance criteria. Distinguish implementation, runtime evidence and explicit qualification limits; do not substitute a diagnostic-only slice for required viewer integration.
- Full connected UI and keyboard navigation means 100% production implementation with no missing routes. Include menus, context menus, toolbars, floaters, nested controls, accelerators, focus and input methods, and the services those actions require. Construction, admission, enabled appearance, transport scaffolding or a passing subset is not completion. Preserve ordinary OpenGL permission, selection, availability, maturity and RLVa rules.
- Prefer the `vs` filename prefix for new Vulkanstorm-owned viewer `.cpp` and `.h` files, for example `vsshadercache.cpp` and `vsshadercache.h`. Standalone tests and tools use descriptive names consistent with their directories.
- `vkstorm-release` is the feature-frozen release/default branch. Only security fixes and functionality patches are accepted; new improvements belong in development, not release. Qualify permitted patches before integration through PRs.
- `vkstorm-devel` experiments with improvements no longer accepted in `vkstorm-release`.
- `vkstorm-canary` targets Vulkanstorm 1.0.1, based on upstream 7.2.5. Rename it to `vkstorm_1.0.1` only once upstream 7.2.5 is released.
- Continue authorized Vulkan work on the existing `vkstorm-vulkan` branch. Once development is ready, rename it to `vkstorm_1.1.0`; native Vulkan will be that revision's default renderer.
- Develop Vulkan integration directly on `vkstorm-vulkan`; additional feature branches from it are unnecessary. Integrate any earlier Vulkan work still outstanding into this branch after reviewing its contents and verifying the destination.
- Keep branch checkouts under `C:\Dev\vulkanstorm\worktrees`; the Vulkan checkout belongs at `C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan`. Preserve staged executables needed for an ongoing user test until that test is finished.
- Keep native Vulkan dependency, rendering and software-device CI separate from production baseline CI. Software graphics qualification must not publish release artifacts or advance `latest`.
- Develop new features only on feature branches created from `vkstorm-devel`. Use the `codex/` prefix by default.
- Only hotfixes, critical fixes, or security patches may be directly committed to `vkstorm-release` or `vkstorm-devel`.
- Never merge either release or development into the other. Integrate selected changes through a dedicated feature/integration branch and PR. Before every merge verify the destination branch and source.
- Merges flow one way: a feature or PR branch is merged into its base line through its PR. Never merge a base line (`vkstorm-*`) into a feature or PR branch, including GitHub's "Update branch", and never instruct an agent to. To bring base changes into a feature branch, cherry-pick them, or land the branch and follow up. `scripts/check_no_base_merges.py` enforces this in CI (Check PR) and, after `scripts/install-git-hooks.sh`, locally.
- Never merge legacy `master` or `origin/master` into any active branch.
- Keep development-only rendering hooks, capture harnesses, and profiling probes out of `vkstorm-release`. Run `scripts/tests/check_release_hooks.py` on release changes. Ordinary viewer debug facilities and standalone tools are permitted.
- The default development build is `RelWithDebInfo`, using the Release viewer's features and dependencies, without generating an installer.
- Use the existing Autobuild workflow. Stage runtime libraries, plugins, shaders, and application assets so the development viewer runs directly from its staged directory. Compiling the executable alone does not complete the build.
- Rebuild the existing Vulkan checkout in place at `C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan`, using its `build-vc170-64` directory as the canonical local build and runtime stage. Do not create additional per-fix build/stage copies or compile through rewritten projects in another checkout's build cache. Preserve any currently running test executable until the user finishes with it.
- Other branches may have in-branch builds when needed; those do not replace the canonical Vulkan `RelWithDebInfo` build in the existing `vkstorm-vulkan` checkout.
- Keep `latest` at the source commit of the latest successful CI Release build supplying both Windows and Linux binaries. Advance it only after both platform builds succeed.
- Pre-reset history is archived off-tree at `H:\vulkanstorm\archive-2026-10-02-clean-base`.

- `vkstorm-canary` follows upstream 7.2.5 development while retaining the selected Vulkanstorm release changes. Integrate upstream updates through a dedicated integration branch and PR into canary, resolving conflicts and validating the result. Do not reset canary to upstream or merge canary into release/development. Canary builds must not advance `latest`.
