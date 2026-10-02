# Clean release base, 2 October 2026

The reconstruction starts at the current published Firestorm release branch
`Firestorm_7.2.4`, commit `10bd3c9f930c76e1427ddd4ecece6cdf36b4406d`
(the `Firestorm_Release_7.2.4.80712` release tag). The 7.2.5 branch is a preview.
No old development branch was merged into this base.

The selected ports retain Vulkanstorm branding, the OpenGL/Mesa Zink selector
and graphics-settings persistence, SoLoud audio, VLC fixes and spatial music,
Markdown chat, logout/pink-screen fixes, PPLL, configurable teal avatar loading
clouds, the Viewer preferences tab name, and the AnsaStorm Modern default skin.
Skin widget dependencies are included. The skin directory is identical to the
pre-reset development version.

The OpenJPEG GPU, Mesa/Zink and SoLoud installable definitions are identical
to pre-reset master `7bf30e6ef8664bc038603a0341fb2e982ff9dea7`.
Other dependencies come from the upstream release base. Existing upstream
sorted-alpha rendering remains the fallback; PPLL is the only added OIT method.
Depth peeling and the rigged/world interleaving change are excluded. Native
Vulkan rendering, compute-mesh experiments, viewer performance experiments,
and development runtime probes are excluded.

The branch policy is in AGENTS.md. `vkstorm-release` is the default release
branch; `vkstorm-devel` starts at the same reconstruction commit. New features
use branches from development and reach release through qualified, tested PRs.
Only hotfixes, critical fixes and security patches may be committed directly.
CI retains upstream packaging and advances `latest` only after successful
Windows and Linux Release builds. Development builds stage complete runtime
assets and do not create an installer.

## Archive and recovery

`H:\vulkanstorm\archive-2026-10-02-clean-base` contains a verified complete
Git bundle, a copy of the Git metadata, ref/worktree inventories, staged and
unstaged patches, and copies of modified/untracked working files from existing
worktrees. Ignored build trees remain in their original worktrees. A second
verified remote-refresh bundle preserves the remote state fetched before the
branch replacement. This also preserves remote master `b615e891acb7973af0d1f31e9af567b1e7922d82`.

Recover history with `git clone <bundle-path> <recovery-directory>` and restore
working patches/files from the matching working-changes archive. The old
worktrees are historical checkouts, not the active release/development base.

## Qualification

Configuration, release-hook checks, graphics identity persistence, login-music
regression checks, audio backend selection, and publication tests passed.
All 48 offscreen PPLL cases passed on native NVIDIA OpenGL and the preserved
Mesa 26.3.0-devel package (git 4c18bbc637) using an RTX 5070 Ti.
All 47 retained Markdown tokenizer unit tests passed. The SoLoud adapter passed
for 2, 6 and 8 channels, and production VLC speaker-fill tests passed.
The device-free VLC audio engine tests passed with the preserved SoLoud header;
the guarded extension digest was updated for that exact header, with all
patch anchors still required to match uniquely. The optional VLC PCM bridge
headless test timed out. The production plugin uses `LL_VLC_PCM_AUDIO=0`.

The Windows Autobuild RelWithDebInfo build succeeded, with AVX2, LTO, SoLoud,
Mesa/Zink, OpenJPEG and the upstream open-source dependency configuration.
The development executable, runtime libraries, plugins, shaders, character
assets and complete retained skin are staged without an installer at
`worktrees/clean-release-base/build-vc170-64/newview/RelWithDebInfo`.
All retained character and skin runtime files were checked against their
source paths. Linux build validation remains delegated to the Release CI
matrix; `latest` must remain unchanged until both platform builds succeed.
These checks do not establish improved viewer performance. Interactive grid login, logout,
audio-device switching and visual skin/OIT qualification remain necessary.
