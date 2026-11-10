# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code. Report measured results and qualification limits accurately.
- `vkstorm-release` is the release/default branch. Incorporate new features only through PRs after qualification and testing on their feature branches.
- Develop new features only on feature branches created from `vkstorm-devel`. Use the `codex/` prefix by default.
- Product lines start from the tip of `vkstorm-release`, to build on what is proven rather than on the newest development: `vkstorm-rust`, and the new `vkstorm-vulkan` once the Rust port is complete (`doc/vulkan/direction-2026-10-29.md`). Feature work for such a line branches from that line and returns to it through PRs; `vkstorm-devel` is not its base.
- Only hotfixes, critical fixes, or security patches may be directly committed to `vkstorm-release` or `vkstorm-devel`.
- Never merge either release or development into the other. Integrate selected changes through a dedicated feature/integration branch and PR. Before every merge verify the destination branch and source.
- Merges flow one way: a feature or PR branch is merged into its base line through its PR. Never merge a base line (`vkstorm-*`) into a feature or PR branch, including GitHub's "Update branch", and never instruct an agent to. To bring base changes into a feature branch, cherry-pick them, or land the branch and follow up. `scripts/check_no_base_merges.py` enforces this in CI (Check PR) and, after `scripts/install-git-hooks.sh`, locally.
- Never merge legacy `master` or `origin/master` into any active branch.
- Keep development-only rendering hooks, capture harnesses, and profiling probes out of `vkstorm-release`. Run `scripts/tests/check_release_hooks.py` on release changes. Ordinary viewer debug facilities and standalone tools are permitted.
- The default development build is `RelWithDebInfo`, using the Release viewer's features and dependencies, without generating an installer.
- Use the existing Autobuild workflow. Stage runtime libraries, plugins, shaders, and application assets so the development viewer runs directly from its staged directory. Compiling the executable alone does not complete the build.
- Each release platform has its own tag, advanced only by that platform's successful CI Release build of vkstorm-release: `windows-latest` and `linux-latest` on every push, `linux-lts` (ubuntu:24.04) on demand. `stable` is promoted on demand from those three by Promote stable, and the previous stable becomes `oldstable`. Canary publishes `experimental-*` and devel publishes `preview-*`, on demand; neither moves `windows-latest`, `linux-latest`, `linux-lts`, `stable` or `oldstable`. On a 403 from a publish job, move the tag by hand as doc/release_channels.md describes.
- Pre-reset history is archived off-tree at `H:\vulkanstorm\archive-2026-10-02-clean-base`.
- Licence: Vulkanstorm is licensed under `GPL-3.0-or-later` (`LICENSE`, `NOTICE.md`, `doc/licensing.md`; decided 2026-11-08, the LGPL-2.1 option is dropped). Linden Lab and Firestorm code stays LGPL-2.1, with its headers unchanged, and is combined into Vulkanstorm, which as a whole is licensed GPL-3.0-or-later (LGPL-2.1 is compatible with inclusion in a GPL-3.0 work). New files carry `SPDX-License-Identifier: GPL-3.0-or-later`; Linden Lab and Firestorm headers stay unchanged. A new linked or bundled library must be compatible with GPL-3.0 (no proprietary, GPL-2.0-only or extra-restriction licences); add it to the audit in `doc/licensing.md`. Proprietary SDKs (Vivox, KDU, FMOD, Havok, BugSplat, Discord Social SDK) are not built or shipped.

- `vkstorm-canary` follows upstream 7.2.5 development while retaining the selected Vulkanstorm release changes. Integrate upstream updates through a dedicated integration branch and PR into canary, resolving conflicts and validating the result. Do not reset canary to upstream or merge canary into release/development. Canary builds never move `windows-latest`, `linux-latest`, `linux-lts`, `stable` or `oldstable`; they publish only `experimental-*`.

## Working rules for agents

These complete the rules above for every agent (planner, surveyor, coder, reviewer, auditor, checker) and for anyone else working in this repo.

- Git: never force-push, on any branch. Bring base changes into a branch only by cherry-picking (`-x`) or by re-cutting the branch: a new branch from the base tip with the PR's commits cherry-picked, and a new PR that supersedes the old one. Once a PR is open, add commits only when the owner asks.
- Worktrees go in `<repo>\work\<name>`, never beside the repository.
- Compiled fixture and integration tests run with `TMPDIR` (POSIX) or `TEMP`/`TMP` (Windows) pointing to the repo's `.fixture-tmp` folder, which is excluded from Defender.
- Smoke launches use a scratch APPDATA/LOCALAPPDATA with no saved credentials, so they can never log in. Changes that touch rendering, shutdown or login need a Release build that reaches the world (owner-assisted) before they are called safe.
- Before adding cleanup, frees, caching or shared/static state to a function, check every caller and every thread that runs it (worker threads with shared GL contexts call the same `shutdown()` paths).
- OpenGL behaviour outside the change must stay identical. Renderer-free logic gets equivalence tests where a GLSL twin exists.
- Light and shadow work: everything new together, at the default tier, costs at most today's PCSS-on frame + 5 % (measured against the recorded baseline). Every stage's PR reports its own cost and the running total.
- No Preferences entries unless the owner asks. Structural parts of a method are debug settings. Tuning panels (such as the Sky tab) and diagnostics live on `vkstorm-devel` only and are left out of every integration into `vkstorm-release`.
- Bugs are hotfixes (see above), applied to every line the bug exists on. PRs are for features, studies and integrations.
- CI and workflow changes are validated by `ci-lint` only, never by a viewer build; documentation-only changes are not linted and do not require a viewer build.
- Review threads (Copilot or others) get a reply, with the fixing commit or the reason to decline, and are then resolved; none is left silent or open.
