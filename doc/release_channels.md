<!-- SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Release channels

Every published viewer build is a GitHub release on a tag of its own, one per
platform. A platform's tag advances only when its own build succeeds, so a
failed or skipped platform never holds back another.

## Platforms

Each platform is a workflow of its own: `build-windows.yaml` (Build Windows),
`build-linux.yaml` (Build Linux) and `build-linux-lts.yaml` (Build Linux LTS).
Each run has three jobs: `checks` (the repository checks of
`.github/actions/ci-checks`, and the plan of
`scripts/ci_publish_latest.cjs` `planPlatform()`), the build itself (check
`windows`, `linux` or `linux-lts`, the shared steps of
`.github/actions/viewer-build`), and, when the plan gives the run a tag,
`publish`. A platform publishes the moment its own build finishes,
whatever the other platforms' workflows do. `.github/build-platforms.json`
holds the platform data (runner, container, packages, tags).

One run per pull request or line: a new push cancels the previous run of the
same workflow. vkstorm-release pushes (they publish) and tags never cancel,
and every dispatch has a group of its own; each publish job also holds its
tag's lock. Pushes build the vkstorm-* lines and Second_Life tags; a feature
branch builds through its pull request.

| Platform | Built | Package | Floor |
|---|---|---|---|
| `windows` | windows-2025, MSVC; Inno Setup installer | `*_Setup.exe` | – |
| `linux` | built and linked directly on GitHub's `ubuntu-26.04` (the newest GitHub runner, bumped by hand), with the newest GCC its Ubuntu ships and the packages of `autobuild.xml` (built on ubuntu-26.04) | `*-ubuntu-latest.tar.xz` | that runner's glibc, reported by CI |
| `linux-lts` | built and linked inside `ubuntu:24.04` with GCC 14 and the 24.04-built packages of `scripts/linux_lts_packages.json` | `*-ubuntu24.04.tar.xz` | glibc 2.39 / GLIBCXX_3.4.33, enforced by CI: runs on Ubuntu 24.04 and newer |

The Release packages are Release, AVX2 and LTO builds. They no longer bundle
the Mesa/Zink runtime (`USE_MESAZINK` off): Zink was withdrawn from the
viewer, a Vulkan build will supersede it, and its code is kept (tag
`archive/zink/2026-10-29`).

Pull requests run all three workflows as RelWithDebInfo qualification
builds, never published, so that the on-demand `linux-lts` build never rots.
Pushes run Build Windows and Build Linux; Build Linux LTS runs on pull
requests and on dispatch only. A FreeBSD build, `freebsd-latest` with
`experimental-freebsd-latest` and `preview-freebsd-latest`, is planned. It
will be one more entry in `build-platforms.json` and one more workflow.

## Tags

| Tag | Source | When |
|---|---|---|
| `windows-latest` | vkstorm-release, `windows` | every push, automatically; takes GitHub's Latest badge |
| `linux-latest` | vkstorm-release, `linux` | every push, automatically |
| `linux-lts` | vkstorm-release, `linux-lts` | on demand: Build Linux LTS with `publish` |
| `stable` | the designated `windows-latest`, `linux-latest` and `linux-lts` releases, promoted together into one release; nothing is rebuilt | on demand: Promote stable |
| `oldstable` | the previous `stable` | moved by each stable promotion |
| `archive/stable-installers` | the outgoing `oldstable` packages, with `SHA256SUMS-<version>.txt` and a table row | added to by each stable promotion |
| `experimental-windows`, `experimental-linux-latest`, `experimental-linux-lts` | vkstorm-canary, one per platform, pre-releases | on demand: the platform's Build workflow with `publish` |
| `preview-windows`, `preview-linux-latest`, `preview-linux-lts` | vkstorm-devel, one per platform, pre-releases | on demand: the platform's Build workflow with `publish` |

Dispatch the platform's workflow (Build Windows, Build Linux or Build Linux
LTS). With `publish` set, it builds a Release package and publishes it to
that line's tag for the platform. Without it, it only builds.
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
