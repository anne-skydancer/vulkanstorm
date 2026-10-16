#!/bin/sh
# Runs as root through the pinned cross-platform-actions builder's custom hook.
set -eu

[ "$(uname -s)" = FreeBSD ]
case "$(freebsd-version -u)" in 15.*) ;; *) exit 1 ;; esac

export ASSUME_ALWAYS_YES=yes
pkg install -y ca_root_nss git git-lfs bash cmake ninja pkgconf \
    python311 py311-pip ccache autoconf automake libtool bison gmake \
    curl zip unzip zstd

# Keep Python tools isolated from pkg-managed Python packages. CMake and Ninja
# come from pkg: Linux PyPI wheels must never enter this native build image.
python3.11 -m venv /opt/vulkanstorm
/opt/vulkanstorm/bin/python -m pip install --disable-pip-version-check \
    autobuild==3.9.3 llsd==1.2.4
ln -sf /opt/vulkanstorm/bin/autobuild /usr/local/bin/autobuild

mkdir -p /usr/local/share/vulkanstorm-image
freebsd-version -ku > /usr/local/share/vulkanstorm-image/freebsd-version.txt
pkg query '%n %v %o' | sort > /usr/local/share/vulkanstorm-image/packages.txt
/opt/vulkanstorm/bin/python -m pip freeze > /usr/local/share/vulkanstorm-image/python-packages.txt
cc --version > /usr/local/share/vulkanstorm-image/compiler.txt
