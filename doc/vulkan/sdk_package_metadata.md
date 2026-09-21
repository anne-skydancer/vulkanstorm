# Vulkan SDK package metadata

The previous common archive (`vulkan_sdk-1.4.350-common-73f1373.tar.zst`)
omitted `autobuild-package.xml`. Autobuild 3.9.3 therefore marked the dependency
dirty on both Windows and Linux. This was a provenance/packaging defect, not a
Linux compiler or Vulkan runtime problem.

The replacement is the `v1.4.350-metadata2` release of
`anne-skydancer/3p-vulkan-sdk`, built from `6638a752b7b2d8233f32e4fc6daba17eec05be0d`
on Ubuntu 24.04 in Actions run `35612168685`. Its archive is
`vulkan_sdk-1.4.350-common-5.tar.zst` with SHA-256:

`8235bdb9d30764cd0eb26e39ff0976e293075dae99028b0e3fa39b032c246fe1`

It contains Autobuild-generated version metadata and a complete 56-file
payload manifest, licenses and the unchanged upstream component versions:
Vulkan-Headers `vulkan-sdk-1.4.350.1`, volk `1.4.350`, and VMA `v3.4.0`.
It remains a `common` headers/source package. The viewer compiles volk itself;
there is no separate precompiled Linux SDK library to supply or relabel.

The producer now has a valid common-platform Autobuild configuration and runs
`autobuild build` followed by `autobuild package --clean-only`, rather than
publishing a hand-created archive. Its validator checks metadata, payload
coverage and every packaged file against the assembled tree. A separate install
probe exercises normal Autobuild installation and verifies a clean dependency.

The initial `metadata1` fix wrongly installed a root-level `VERSION.txt`, which
collided with another dependency in the shared prefix. It is now a build-only
input; the installed version lives in `autobuild-package.xml`. The regression
test preinstalls another owner of `VERSION.txt`, reproduces the old collision,
and verifies the corrected SDK neither claims nor alters that file.

Validation: Linux and Windows passed package and coexistence checks. A fresh
Windows install of the viewer's real `soloud` and corrected `vulkan_sdk` packages
also passed, reported no dirty packages, and identified SoLoud as the owner of
`VERSION.txt`. No full viewer rebuild or runtime
rendering test was needed or performed for this manifest-only change.

NV-00/NV-01: the consumer-visible contract is the same include/source/license
layout and component versions for both backends/platforms; the change restores
package provenance and checksum verification. No UI/rendering behavior, GPU
ownership, synchronization or retirement changes. Existing release assets remain
intact; the viewer selects a new immutable release URL and SHA-256 together.
