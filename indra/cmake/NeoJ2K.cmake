# -*- cmake -*-
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The J2C codec of the viewer, chosen when CMake configures: USE_NEOJ2K=ON links neo-j2k (the `neoj2k`
# package, https://github.com/anne-skydancer/3p-neoj2k, BSD-2-Clause) as its own shared library, neoj2k.dll
# or libneoj2k.so; OFF (the default) keeps OpenJPEG (the `openjpeg` package). One codec per binary: the
# choice is the implementation behind fallbackCreateLLImageJ2CImpl() (llimage/llimagej2c.cpp), with no
# runtime routing and no per-file fallback. neo-j2k decodes and encodes (uploads, bakes, snapshots), so the
# OpenJPEG package is neither installed nor linked when it is ON.
include_guard(GLOBAL)
include(Prebuilt)

option(USE_NEOJ2K "Decode and encode JPEG 2000 with neo-j2k (3p-neoj2k) instead of OpenJPEG" OFF)

add_library(ll::neoj2k INTERFACE IMPORTED GLOBAL)

if (USE_NEOJ2K)
  if (USE_KDU)
    message(FATAL_ERROR "USE_NEOJ2K and USE_KDU are both on: one J2C codec per binary")
  endif ()

  use_prebuilt_binary(neoj2k)

  # The import library on Windows (neoj2k.lib, for neoj2k.dll), libneoj2k.so on Linux.
  find_library(NEOJ2K_LIBRARY
      NAMES neoj2k
      PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

  target_link_libraries(ll::neoj2k INTERFACE ${NEOJ2K_LIBRARY})
  target_include_directories(ll::neoj2k SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)

  # Headers with OpenJPEG's names (openjpeg.h, opj_config.h) that map onto neo-j2k's: for tests that write
  # their fixtures through OpenJPEG's API. Kept apart from the target so that no viewer code sees it.
  set(NEOJ2K_COMPAT_INCLUDE_DIR "${LIBS_PREBUILT_DIR}/include/neoj2k/compat" CACHE INTERNAL "OpenJPEG-named headers of neo-j2k (tests)")
endif ()
