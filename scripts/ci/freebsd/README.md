# FreeBSD 15.x build image

The `FreeBSD runner image` workflow builds a FreeBSD 15.0 RELEASE amd64
QCOW2 image using Packer and the pinned cross-platform-actions/freebsd-builder.
Vulkanstorm supplies only the builder's supported `resources/custom.sh` hook;
upstream installation, SSH setup, runner account, and cleanup remain intact.
The GitHub Actions runner runs on Ubuntu. Build commands run natively in the
FreeBSD guest; there is no native GitHub runner installed in the image.

## Build and qualification

Push this feature branch, open a PR changing these paths, or run the workflow
manually after it is integrated. The workflow installs QEMU and Packer, builds
a 32 GiB virtual disk, checks its structure, and boots the result through a
pinned cross-platform-actions/action revision. The installer ISO checksum is
provided by the pinned upstream version configuration.

The fresh guest must report FreeBSD 15.0 RELEASE amd64, execute as a non-root
runner, run Autobuild 3.9.3 and LLSD, and configure, compile, link and test a
C++20 threaded program with CMake/Ninja in RelWithDebInfo. Evidence must return
to the Linux host. Only then is `freebsd-15.0-x86-64-image` uploaded, containing
the QCOW2 and SHA256SUMS. The separate evidence artifact includes source and
builder commits, Packer logs, package inventories, tool versions and test logs.
Artifacts expire after 14 days; retain any selected image before that deadline.

The image includes native Clang from FreeBSD base, CMake, Ninja, Python 3.11,
Git/LFS, pkgconf, ccache and common build utilities. Python's Autobuild and
LLSD are isolated in `/opt/vulkanstorm`; `autobuild` is on the default PATH.
Package versions are recorded, not frozen: rebuilding against a changed pkg
repository can produce a different image. Use the exact image checksum for
qualification and establish a package snapshot before requiring reproducible
rebuilds.

## Use in a repository build

Download a successful run's image artifact and check `sha256sum --check
SHA256SUMS`. For long-term use, attach both files to an explicitly versioned
image release (separate from the viewer's `latest` tag), then pass the QCOW2
asset URL as the action's `image_url`. Pin the action commit, version `15.0`,
architecture `x86-64`, and image release. On self-hosted Linux, the same image
can be served locally, as the image qualification workflow demonstrates.

These images inherit upstream's passwordless sudo and known CI passwords.
They are disposable CI guests, intended for an isolated hypervisor network,
not publicly accessible servers. Keep registration tokens and credentials out
of the image. Use a fresh disk copy for each build.

## Qualification boundary and next porting work

This qualifies an image and native toolchain, not Vulkanstorm. The current
viewer has no FreeBSD CMake platform configuration or `freebsd64` Autobuild
dependency entries. `dependency_audit.py` inventories that gap without
installing incompatible Linux archives. Some packages are common assets or
optional dependencies and need individual review.

The next feature work must establish FreeBSD platform recognition in Autobuild
and CMake, build the native dependency set, then configure Vulkanstorm using
its existing Autobuild environment with Release-equivalent features,
RelWithDebInfo and `PACKAGE=OFF`. Qualification requires staging libraries,
plugins, shaders and assets and running from that staged directory. The image
smoke test does not satisfy that requirement. Vulkan rendering requires a
separate environment with suitable GPU access.

To advance to another 15.x release, update the pinned builder and VM action as
needed, the build command, workflow version/image name and smoke version
assertion together; requalify before promotion.
