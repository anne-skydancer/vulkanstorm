# -*- cmake -*-
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The volumetric clouds' noise textures: one data file, clouds/cloud_noise.vkcn, from the
# `cloudnoise` package (https://github.com/anne-skydancer/3p-cloudnoise, TileableVolumeNoise, MIT).
# The file is required: CMake stops if it is missing or is not the file this source tree names, and the
# viewer checks the same hash again at start-up (llcloudnoise.cpp). A new package therefore needs a change
# here: change CLOUD_NOISE_SHA256 together with the autobuild.xml entry.
include_guard()
include(Prebuilt)

# SHA-256 of cloud_noise.vkcn, cloudnoise 1.0.0 (EXPECTED_SHA256 of 3p-cloudnoise). The single source of
# truth: scripts/clouds/check_cloud_noise.py and the installer checks of CI read it from this line.
set(CLOUD_NOISE_SHA256 "ca508a14700600161f9aa720ca4a4f6cec76c1645330dbab9bfe83945654843c")

use_prebuilt_binary(cloudnoise)

set(CLOUD_NOISE_FILE "${AUTOBUILD_INSTALL_DIR}/clouds/cloud_noise.vkcn")
if (NOT EXISTS "${CLOUD_NOISE_FILE}")
  message(FATAL_ERROR "The cloudnoise package did not provide ${CLOUD_NOISE_FILE}")
endif ()
file(SHA256 "${CLOUD_NOISE_FILE}" CLOUD_NOISE_ACTUAL_SHA256)
if (NOT CLOUD_NOISE_ACTUAL_SHA256 STREQUAL CLOUD_NOISE_SHA256)
  message(FATAL_ERROR
          "${CLOUD_NOISE_FILE} has SHA-256 ${CLOUD_NOISE_ACTUAL_SHA256}, but this source tree expects "
          "${CLOUD_NOISE_SHA256} (indra/cmake/CloudNoise.cmake). The cloudnoise package and CLOUD_NOISE_SHA256 "
          "must be changed together.")
endif ()
