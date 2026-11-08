<img align="left" width="100" height="100" src="doc/vulkanstorm_256.png" alt="Vulkanstorm viewer icon"/>

**[Vulkanstorm](https://github.com/anne-skydancer/vulkanstorm) is a free client for 3D virtual worlds such as Second Life and OpenSim, where users can create, connect and chat with others from around the world.**

This repository contains Vulkanstorm, a viewer derived from Firestorm and the Second Life viewer.

## Open Source

Firestorm is a third party viewer derived from the official [Second Life](https://github.com/secondlife/viewer) client. The client codebase has been open source since 2007 and is available under the LGPL license.

## Licence

Vulkanstorm is licensed under the [GNU General Public License, version 3 or (at your option) any later version](LICENSE) (`GPL-3.0-or-later`).

- Code from Linden Lab and Firestorm, and Rust ports of it: released by them under LGPL-2.1 only, and used here under the ordinary GPL as section 3 of LGPL-2.1 allows. Their file headers are kept unchanged ([LICENSE-LGPL-2.1](LICENSE-LGPL-2.1) is the text they refer to).
- Original Vulkanstorm code: `GPL-3.0-or-later`.
- Third-party components keep their own licences, all compatible with GPL-3.0.

[NOTICE.md](NOTICE.md) explains the groups; [doc/licensing.md](doc/licensing.md) audits the libraries the viewer links with. Contributions are accepted under the same terms ([CONTRIBUTING.md](CONTRIBUTING.md)).

## Download

Vulkanstorm builds are published on this repository's [releases](https://github.com/anne-skydancer/vulkanstorm/releases), one tag per platform ([doc/release_channels.md](doc/release_channels.md)):

- `stable`: the current stable release, Windows, Linux and Linux LTS; `oldstable` is the previous one.
- `windows-latest` and `linux-latest`: the latest successful builds of `vkstorm-release`.
- `linux-lts`: the Linux build for older systems, built in Ubuntu 24.04 (glibc 2.39); it runs on Ubuntu 24.04 and newer.

Firestorm's own builds for Windows, Mac and Linux are on the [Firestorm website](https://www.firestormviewer.org/choose-your-platform/).

## Build Instructions

Build instructions for each operating system can be found using the links below and in the official [wiki](https://wiki.firestormviewer.org).

- [Windows](doc/building_windows.md)
- [Mac](doc/building_macos.md)
- [Linux](doc/building_linux.md)

> [!NOTE]
> We do not provide support for compiling the viewer or issues resulting from using a self-compiled viewer. However, there is a self-compilers group within Second Life that can be joined to ask questions related to compiling the viewer: [Firestorm Self Compilers](https://tinyurl.com/firestorm-self-compilers)

## Contribute

Help make Firestorm better! You can get involved with improvements by filing bugs and suggesting enhancements via [JIRA](https://jira.firestormviewer.org) or [creating pull requests](CONTRIBUTING.md).

## Community Respect

This section is guided by the [TPV Policy](https://secondlife.com/corporate/third-party-viewers) and the [Second Life Code of Conduct](https://github.com/secondlife/viewer?tab=coc-ov-file).

Firestorm code is made available during ongoing development, with the **master** branch representing the current nightly build. Developers and self-compilers are encouraged to work on their own forks and contribute back via pull requests, as detailed in the [contributing guide](CONTRIBUTING.md).

If you intend to use our code for your own viewer beyond personal use, please only use code from official release branches (for example, `Firestorm_7.1.13`), rather than from pre-release/preview or nightly builds.
