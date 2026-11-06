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
sorted-alpha rendering remains the fallback; the image-array A-buffer OIT (originally a PPLL) is the only added OIT method.
Depth peeling and the rigged/world interleaving change are excluded. Native
Vulkan rendering, compute-mesh experiments, viewer performance experiments,
and development runtime probes are excluded.

The branch policy is in AGENTS.md. `vkstorm-release` is the default release
branch; `vkstorm-devel` starts at the same reconstruction commit. New features
use branches from development and reach release through qualified, tested PRs.
Only hotfixes, critical fixes and security patches may be committed directly.
CI retains upstream packaging. Each Release platform advances its own tag
only after its own build succeeds: `latest-windows`, `latest-linux-LTS` (built
in Ubuntu 24.04) and `latest-linux` (built on GitHub's ubuntu-latest runner). Development builds stage complete runtime
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
matrix; each `latest-<platform>` tag must remain unchanged until its own
platform build succeeds.
These checks do not establish improved viewer performance. Interactive grid login, logout,
audio-device switching and visual skin/OIT qualification remain necessary.

## Alpha OIT A-buffer node

Each slot of the image-array A-buffer holds one fragment in an 8-byte RG32UI
texel (`alphaOITPackF.glsl`, linked into the four capture shaders and the
resolve): `x` is the premultiplied colour as R11F_G11F_B10F, `y` is the window
depth as a 24-bit unorm in the high bits and alpha as an 8-bit unorm in the
low byte, so the resolve sorts on the `y` words directly. It replaced a
16-byte RGBA32UI node (half-float RGBA, float depth, an unused zero word). At
8 slots a pixel needs 92 bytes instead of 156. Under the default 512 MiB
budget, 1440p now keeps all 8 slots (previously cut to 7), and 4K keeps 4
(previously 2). The tail accumulators are unchanged.

Precision. The colour keeps the half float's 5-bit exponent, so its range is
the same (up to 65024 for red and green, 64512 for blue). Larger values clamp
to that limit rather than becoming infinity. The mantissa is 6 bits for red and
green and 5 for blue. That gives relative steps of 1.6% and 3.1%, so a stored
value is off by at most 0.8% and 1.6%. These are roughly one 8-bit sRGB step of
a bright source texel; for mid and dark tones they are finer than the 8-bit
texture data. The rounding is relative and every contribution to the composite
is non-negative, so the composited colour carries the same relative bound. It
does not grow with the layer count. Alpha is stored as 8 bits, the precision of
the texture alpha the fragments come from. Depth uses 24 bits, the precision of
the opaque depth buffer (`GL_DEPTH_COMPONENT24`) it is tested against. The
capture still tests the full-precision depth, and the quantisation is
monotonic, so the resolve keeps every fragment the capture kept.

What this loses:
- The colour mantissa drops from 10 to 6/6/5 bits. Smooth bright gradients
  seen through alpha can shift by up to one 8-bit step. Blue is coarser than
  red and green, so a hue can move by up to 1.6%.
- Below 6.1e-5, colours are denormal, with an absolute step of about 1e-6.
  That is far below display precision, but exposure that scales very dark
  premultiplied colours up would show it.
- Alpha is rounded to 1/255. Alpha that does not come straight from an 8-bit
  texel (vertex alpha, material alpha factors) is off by up to 1/510 per layer
  in the transmittance. Over many layers this compounds in the background's
  visibility. Alpha at or below 1/510 stores as 0: the fragment still adds its
  premultiplied colour but no coverage.
- Two fragments closer than 1/16777215 in window depth can share a depth code.
  They are then ordered by alpha rather than by their float depth. With
  the near plane at 0.25 m, window depth passes 0.5 at about 0.5 m, where the
  float depth already has a 2^-24 spacing, so beyond that the order is the same
  as before to within one step.
