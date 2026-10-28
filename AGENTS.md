# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code. Report measured results and qualification limits accurately.
- `vkstorm-release` is the release/default branch. Incorporate new features only through PRs after qualification and testing on their feature branches.
- Develop new features only on feature branches created from `vkstorm-devel`. Use the `codex/` prefix by default.
- Apply hotfixes and patches directly on `vkstorm-release` in the main checkout (`C:\Users\annes\source\repos\vulkanstorm`), compile them as `Release` with LTO enabled, and produce an installer. Do not create or retain redundant worktrees or staged viewer copies for release patches. PRs are for feature/integration branches unless a concrete reason warrants an exception. Only hotfixes, critical fixes, or security patches may be directly committed to `vkstorm-devel`.
- Never merge either release or development into the other. Integrate selected changes through a dedicated feature/integration branch and PR. Before every merge verify the destination branch and source.
- Merges flow one way: a feature or PR branch is merged into its base line through its PR. Never merge a base line (`vkstorm-*`) into a feature or PR branch, including GitHub's "Update branch", and never instruct an agent to. To bring base changes into a feature branch, cherry-pick them, or land the branch and follow up. `scripts/check_no_base_merges.py` enforces this in CI (Check PR) and, after `scripts/install-git-hooks.sh`, locally.
- Never merge legacy `master` or `origin/master` into any active branch.
- Keep development-only rendering hooks, capture harnesses, and profiling probes out of `vkstorm-release`. Run `scripts/tests/check_release_hooks.py` on release changes. Ordinary viewer debug facilities and standalone tools are permitted.
- Feature work uses a feature branch and `RelWithDebInfo`, using the Release viewer's features and dependencies, without generating an installer. Release patches use `Release`.
- Use the existing Autobuild workflow. Stage runtime libraries, plugins, shaders, and application assets so the development viewer runs directly from its staged directory. Compiling the executable alone does not complete the build.
- Keep `latest` at the source commit of the latest successful CI Release build supplying both Windows and Linux binaries. Advance it only after both platform builds succeed.
- Pre-reset history is archived off-tree at `H:\vulkanstorm\archive-2026-10-02-clean-base`.

- `vkstorm-canary` follows upstream 7.2.5 development while retaining the selected Vulkanstorm release changes. Integrate upstream updates through a dedicated integration branch and PR into canary, resolving conflicts and validating the result. Do not reset canary to upstream or merge canary into release/development. Canary builds must not advance `latest`.
