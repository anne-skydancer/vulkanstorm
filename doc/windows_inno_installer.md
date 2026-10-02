# Windows installer recovery

Windows package builds use Inno Setup by default (`USE_INNOSETUP=ON`). The compiler is a build-machine tool; it adds no viewer runtime dependency. The manifest locates Inno Setup 7 or 6 in Program Files. CI installs it before packaging. Development builds keep `PACKAGE=OFF` and `USE_INNOSETUP=OFF` and stage the complete runtime without an installer.

The template and manifest methods were recovered from pre-reset commit `7bf30e6ef8664bc038603a0341fb2e982ff9dea7`, preserved in `H:\vulkanstorm\archive-2026-10-02-clean-base`. They were applied selectively without restoring the old rendering work or executable-copy dependency cycle.

The installer uses an explicit manifest payload, LGPL license page, optional desktop shortcut and URL registration, and the Vulkanstorm executable name. Its numeric Windows file version uses a zero fourth component because the Git commit count exceeds the 16-bit resource-version limit; the displayed product version retains the complete count. Upgrades do not indiscriminately delete the chosen installation directory.

Local qualification: Inno Setup 7 compiled a 192,903,986-byte unsigned installer from the previously verified complete Windows RelWithDebInfo runtime (7.2.4.80724). The final template also compiled against a small fixture after removing the broad directory cleanup and restoring the current signing hook. Payload selection tests, Linux launch/install fixtures, shell syntax and the release-hook policy check passed. This does not establish fresh Release CI success, installer installation/upgrade/uninstallation behavior, or live viewer performance. Both CI platform builds must qualify the PR before integration. Version 1.0.0 rollout remains gated on the requested preceding CI validation.
