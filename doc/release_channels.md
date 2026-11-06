<!-- SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Release channels

Every published viewer build is a GitHub release on a tag of its own, one per
platform. A platform's tag advances only when its own build succeeds, so a
failed or skipped platform never holds back another.

## Platforms

`.github/build-platforms.json` defines the CI platforms. `scripts/ci_publish_latest.cjs`
plans `build.yaml`'s matrix and publication from it. `build.yaml` calls
`viewer-platform.yaml` once per platform. That workflow builds the platform
(check `<platform> / build`) and, when the plan gives it a tag, publishes it
(`<platform> / publish <tag>`). Each publish job waits only for its own
platform's build.

The builds run one after another, never side by side: `windows`, then
`linux-lts`, then `linux`; a release push (no `linux-lts`) builds `windows`,
then `linux`. The order is sequencing only: a later build runs whether an
earlier one succeeded, failed or was not selected. A run therefore takes
about the sum of its builds, not the longest one.

| Platform | Built | Package | Floor |
|---|---|---|---|
| `windows` | windows-2025, MSVC; Inno Setup installer | `*_Setup.exe` | – |
| `linux` | built and linked directly on GitHub's `ubuntu-26.04` (the newest GitHub runner, bumped by hand), with the newest GCC its Ubuntu ships and the packages of `autobuild.xml` (built on ubuntu-26.04) | `*-ubuntu-latest.tar.xz` | that runner's glibc, reported by CI |
| `linux-lts` | built and linked inside `ubuntu:24.04` with GCC 14 and the 24.04-built packages of `scripts/linux_lts_packages.json` | `*-ubuntu24.04.tar.xz` | glibc 2.39 / GLIBCXX_3.4.33, enforced by CI: runs on Ubuntu 24.04 and newer |

The Release packages are Release, AVX2 and LTO builds. They no longer bundle
the Mesa/Zink runtime (`USE_MESAZINK` off): Zink was withdrawn from the
viewer, a Vulkan build will supersede it, and its code is kept (tag
`archive/zink/2026-10-29`).

Pull requests build all three platforms as RelWithDebInfo qualification
builds, never published, so that the on-demand `linux-lts` build never rots.
Pushes build `windows` and `linux`. A FreeBSD build, `freebsd-latest` with
`experimental-freebsd-latest` and `preview-freebsd-latest`, is planned. It
will be one more entry in `build-platforms.json`.

## Tags

| Tag | Source | When |
|---|---|---|
| `windows-latest` | vkstorm-release, `windows` | every push, automatically; takes GitHub's Latest badge |
| `linux-latest` | vkstorm-release, `linux` | every push, automatically |
| `linux-lts` | vkstorm-release, `linux-lts` | on demand: Build, `linux_lts` + `publish` |
| `stable` | the designated `windows-latest`, `linux-latest` and `linux-lts` releases, promoted together into one release; nothing is rebuilt | on demand: Promote stable |
| `oldstable` | the previous `stable` | moved by each stable promotion |
| `archive/stable-installers` | the outgoing `oldstable` packages, with `SHA256SUMS-<version>.txt` and a table row | added to by each stable promotion |
| `experimental-windows`, `experimental-linux-latest`, `experimental-linux-lts` | vkstorm-canary, one per platform, pre-releases | on demand: Build with `publish`, platforms selected |
| `preview-windows`, `preview-linux-latest`, `preview-linux-lts` | vkstorm-devel, one per platform, pre-releases | on demand: Build with `publish`, platforms selected |

A dispatch of Build selects its platforms with the `windows`, `linux` and
`linux_lts` inputs. With `publish` set, it builds Release packages and
publishes each selected platform to its own tag. Without it, it only builds.
No other line publishes. vkstorm-canary and vkstorm-devel never move
`windows-latest`, `linux-latest`, `linux-lts` or `stable`.

Each release carries its packages and `SHA256SUMS.txt`. The CI summary sits
between `<!-- vulkanstorm-<tag>-start -->` and `<!-- vulkanstorm-<tag>-end -->`,
with the run id that published it; notes outside those markers are kept. A
late older run never replaces a newer one, and an older source commit never
moves a tag backwards.

`freebsd-image-*` releases are CI infrastructure: the FreeBSD guest image of
`scripts/ci/freebsd`. They are never a viewer build. Nothing publishes to them,
promotes them, or offers them for download.

## Updates

The viewer's update check does not read these tags. It queries the grid's
viewer version manager (VVM, `llvvmquery.cpp`) with the viewer's channel,
version and platform. Downloads are offered from the releases above:
`windows-latest` for Windows, `linux-latest` for Linux, and `linux-lts` for
Linux systems older than the ubuntu-26.04 runner (Ubuntu 24.04 and newer).

## Promoting stable

Run **Promote stable** on vkstorm-release. Its inputs name the releases to take
the Windows, Linux and Linux LTS packages from; they default to
`windows-latest`, `linux-latest` and `linux-lts`. Before anything is written,
the run reads every package. Then it:

1. adds the outgoing `oldstable` packages to `archive/stable-installers`, with
   `SHA256SUMS-<version>.txt` and a row in its table;
2. gives `oldstable` the current `stable` packages and notes, and moves the
   `oldstable` tag to `stable`'s commit;
3. gives `stable` the promoted packages, and moves the `stable` tag to the
   Windows package's source commit. The release names the source release and
   commit of each package.

Every step uploads before it replaces, so a rerun continues where a failed
run stopped. Edit the stable release notes by hand afterwards.

## When the token cannot move a tag (403)

The workflow token cannot move or create a tag whose move crosses a change
to `.github/workflows` ("Resource not accessible by integration" on
`git.updateRef`). This happens on the first publication after a workflow
change. Finish such a publication by hand, once the build has succeeded:

1. move the tag to the build's commit:
   `git push --force origin <commit>:refs/tags/<tag>` (or create it);
2. in the release, rewrite the `<!-- vulkanstorm-<tag>-start/end -->` block
   with the commit, the run URL and the file list, and rename any
   `*.upload-<run>` assets to their names;
3. delete the superseded packages (`_Setup.exe`, `.tar.xz`) and keep any
   unrelated files.

For a stable promotion, do the same for `oldstable` and `stable` in the order
given above.
