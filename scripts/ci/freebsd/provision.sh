#!/bin/sh
# Runs as root through the pinned cross-platform-actions builder's custom hook.
set -eu

[ "$(uname -s)" = FreeBSD ]
case "$(freebsd-version -u)" in 15.*) ;; *) exit 1 ;; esac

export ASSUME_ALWAYS_YES=yes
pkg install -y ca_root_nss git git-lfs bash cmake ninja pkgconf \
    python312 py312-pip ccache autoconf automake libtool bison gmake \
    curl zip unzip zstd
: "${VKSTORM_SYSTEM_PACKAGES:?Use the generated builder hook}"
# Deliberate word splitting of package names validated by build-image.sh.
# shellcheck disable=SC2086
pkg install -y $VKSTORM_SYSTEM_PACKAGES

# Keep Python tools isolated from pkg-managed Python packages. CMake and Ninja
# come from pkg: Linux PyPI wheels must never enter this native build image.
python3.12 -m venv /opt/vulkanstorm
/opt/vulkanstorm/bin/python -m pip install --disable-pip-version-check \
    autobuild==3.10.2 llsd==1.2.4
fetch -o /tmp/vulkanstorm-patch-autobuild.py \
    "http://${PACKER_HTTP_ADDR:?}/resources/vulkanstorm-patch-autobuild.py"
/opt/vulkanstorm/bin/python /tmp/vulkanstorm-patch-autobuild.py
ln -sf /opt/vulkanstorm/bin/autobuild /usr/local/bin/autobuild

mkdir -p /usr/local/share/vulkanstorm-image
freebsd-version -ku > /usr/local/share/vulkanstorm-image/freebsd-version.txt
pkg query '%n %v %o' | sort > /usr/local/share/vulkanstorm-image/packages.txt
/opt/vulkanstorm/bin/python -m pip freeze > /usr/local/share/vulkanstorm-image/python-packages.txt
cc --version > /usr/local/share/vulkanstorm-image/compiler.txt
sha256 /tmp/vulkanstorm-patch-autobuild.py > /usr/local/share/vulkanstorm-image/autobuild-patch.txt
rm /tmp/vulkanstorm-patch-autobuild.py
