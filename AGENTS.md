# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code.
- Develop new features on a feature branch based on `vkstorm-devel`.
- Never merge `vkstorm-devel` into `master`, or `master` into `vkstorm-devel`, including their remote-tracking refs. Integrate selected changes through a dedicated feature/integration branch and pull request instead of merging these branches into each other.
- Never merge `master` (including `origin/master`) into another branch, including when resolving PR conflicts. Permitted integration direction is a dedicated feature/integration branch into `master`, never `vkstorm-devel` into `master`. Before every merge, verify the checked-out destination branch and the source. An instruction to fast-forward `master` authorizes only a fast-forward update of `master`, not a merge of `master` into another branch.
- Handle patches and error fixes directly in `master` or `vkstorm-devel`, as appropriate to the affected code and intended target.
- Keep development-only rendering hooks, capture harnesses, and profiling probes out of `master`. Audit inherited development commits before integrating a feature branch; run `scripts/tests/check_release_hooks.py` for master changes. Ordinary viewer debug facilities and standalone tools are permitted.
- The default development viewer build configuration is `RelWithDebInfo`.
- Use the same feature and dependency configuration as the Vulkanstorm Release viewer, except for the build type and installer generation.
- Do not generate an installer for a development viewer.
- Stage all files required to run the development viewer directly from its staged directory without running an installer, including runtime libraries, plugins, shaders, and application assets. Compiling the executable alone does not complete a development viewer build.
- Use the project's existing Autobuild workflow.
- Keep the `latest` tag pointing to the commit of the latest successful CI Release build providing Windows and Linux binaries. Update the tag after both platform builds succeed so it identifies the source used for those binaries.
