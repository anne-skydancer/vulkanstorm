#!/usr/bin/env bash
# Run from the Vulkanstorm checkout after checking out the pinned builder below.
set -euo pipefail
builder="$PWD/.freebsd-builder"
expected=6b1eb482e85a4e9d5bb2cf769412bb053f40c9c0
test "$(git -C "$builder" rev-parse HEAD)" = "$expected"
cp scripts/ci/freebsd/patch_autobuild.py "$builder/resources/vulkanstorm-patch-autobuild.py"
package_names=$(sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d' scripts/ci/freebsd/system-packages.txt)
if grep -Ev '^[A-Za-z0-9_+.-]+$' <<< "$package_names"; then
    echo 'Invalid system package name' >&2
    exit 1
fi
# Packer transfers only the hook script, so embed the validated package list.
{
    printf '#!/bin/sh\nVKSTORM_SYSTEM_PACKAGES="%s"\n' "$package_names"
    cat scripts/ci/freebsd/provision.sh
} > "$builder/resources/custom.sh"
# The VM action synchronizes /home/runner, including RUNNER_TEMP. Keep the
# installer ISO, Packer cache, and QCOW2 outside that tree.
build_root=$(mktemp -d /var/tmp/vulkanstorm-freebsd.XXXXXX)
mv "$builder" "$build_root/builder"
builder="$build_root/builder"
image_directory="$build_root/image"
if [[ -n "${GITHUB_ENV:-}" ]]; then
    printf 'FREEBSD_IMAGE_DIRECTORY=%s\n' "$image_directory" >> "$GITHUB_ENV"
fi
mkdir -p freebsd-image-evidence
printf '%s\n' "$expected" > freebsd-image-evidence/builder-commit.txt
git rev-parse HEAD > freebsd-image-evidence/source-commit.txt
(
    cd "$builder"
    ./build.sh 15.0 x86-64 -var headless=true -var disk_size=32G
) 2>&1 | tee freebsd-image-evidence/packer.txt
mkdir -p "$image_directory"
mv "$builder/output/freebsd-15.0-x86-64.qcow2" "$image_directory/"
qemu-img check "$image_directory/freebsd-15.0-x86-64.qcow2"
qemu-img info --output=json "$image_directory/freebsd-15.0-x86-64.qcow2" \
    > freebsd-image-evidence/disk.json
(cd "$image_directory" && sha256sum freebsd-15.0-x86-64.qcow2 > SHA256SUMS)
printf 'Image directory: %s\n' "$image_directory"
