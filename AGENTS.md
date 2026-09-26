# Vulkanstorm development conventions

- Prefer correct, performant, maintainable code.
- Develop new features on a feature branch based on `vkstorm-devel`.
- Prefer incorporating new features from `vkstorm-devel` into `master` through pull requests.
- Handle patches and error fixes directly in `master` or `vkstorm-devel`, as appropriate to the affected code and intended target.
- The default development viewer build configuration is `RelWithDebInfo`.
- Use the same feature and dependency configuration as the Vulkanstorm Release viewer, except for the build type and installer generation.
- Do not generate an installer for a development viewer.
- Stage all files required to run the development viewer directly from its staged directory without running an installer, including runtime libraries, plugins, shaders, and application assets. Compiling the executable alone does not complete a development viewer build.
- Use the project's existing Autobuild workflow.
