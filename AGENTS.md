# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code. Report measured results and qualification limits accurately.
- Prefer the `vs` filename prefix for new Vulkanstorm-owned viewer `.cpp` and `.h` files, for example `vsshadercache.cpp` and `vsshadercache.h`. Standalone tests and tools use descriptive names consistent with their directories.
- `vkstorm-release` is the feature-frozen release/default branch. Only security fixes and functionality patches are accepted; new improvements belong in development, not release. Qualify permitted patches before integration through PRs.
- `vkstorm-devel` experiments with improvements no longer accepted in `vkstorm-release`.
- `vkstorm-canary` targets Vulkanstorm 1.0.1, based on upstream 7.2.5. Rename it to `vkstorm_1.0.1` only once upstream 7.2.5 is released.
- Continue authorized Vulkan work on the existing `vkstorm-vulkan` branch. Once development is ready, rename it to `vkstorm_1.1.0`; native Vulkan will be that revision's default renderer.
- Keep native Vulkan dependency, rendering and software-device CI separate from production baseline CI. Software graphics qualification must not publish release artifacts or advance `latest`.
- Develop new features only on feature branches created from `vkstorm-devel`. Use the `codex/` prefix by default.
- Only hotfixes, critical fixes, or security patches may be directly committed to `vkstorm-release` or `vkstorm-devel`.
- Never merge either release or development into the other. Integrate selected changes through a dedicated feature/integration branch and PR. Before every merge verify the destination branch and source.
- Never merge legacy `master` or `origin/master` into any active branch.
- Keep development-only rendering hooks, capture harnesses, and profiling probes out of `vkstorm-release`. Run `scripts/tests/check_release_hooks.py` on release changes. Ordinary viewer debug facilities and standalone tools are permitted.
- The default development build is `RelWithDebInfo`, using the Release viewer's features and dependencies, without generating an installer.
- Use the existing Autobuild workflow. Stage runtime libraries, plugins, shaders, and application assets so the development viewer runs directly from its staged directory. Compiling the executable alone does not complete the build.
- Keep `latest` at the source commit of the latest successful CI Release build supplying both Windows and Linux binaries. Advance it only after both platform builds succeed.
- Pre-reset history is archived off-tree at `H:\vulkanstorm\archive-2026-10-02-clean-base`.
