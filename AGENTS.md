# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code. Report measured results and qualification limits accurately.
- `vkstorm-release` is the release/default branch. Incorporate new features only through PRs after qualification and testing on their feature branches.
- Develop new features only on feature branches created from `vkstorm-devel`. Use the `codex/` prefix by default.
- Only hotfixes, critical fixes, or security patches may be directly committed to `vkstorm-release` or `vkstorm-devel`.
- Never merge either release or development into the other. Integrate selected changes through a dedicated feature/integration branch and PR. Before every merge verify the destination branch and source.
- Never merge legacy `master` or `origin/master` into any active branch.
- Keep development-only rendering hooks, capture harnesses, and profiling probes out of `vkstorm-release`. Run `scripts/tests/check_release_hooks.py` on release changes. Ordinary viewer debug facilities and standalone tools are permitted.
- The default development build is `RelWithDebInfo`, using the Release viewer's features and dependencies, without generating an installer.
- Use the existing Autobuild workflow. Stage runtime libraries, plugins, shaders, and application assets so the development viewer runs directly from its staged directory. Compiling the executable alone does not complete the build.
- Keep `latest` at the source commit of the latest successful CI Release build supplying both Windows and Linux binaries. Advance it only after both platform builds succeed.
- Pre-reset history is archived off-tree at `H:\vulkanstorm\archive-2026-10-02-clean-base`.

- `vkstorm-canary` follows upstream 7.2.5 development while retaining the selected Vulkanstorm release changes. Integrate upstream updates through a dedicated integration branch and PR into canary, resolving conflicts and validating the result. Do not reset canary to upstream or merge canary into release/development. Canary builds must not advance `latest`.
