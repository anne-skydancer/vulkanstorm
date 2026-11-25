# -*- cmake -*-
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The J2C codec of the viewer, chosen when CMake configures: USE_NEOJ2K=ON links neo-j2k (BSD-2-Clause), by default
# as a static library built from the pinned neo-j2k release source (NEOJ2K_STATIC, below), so that the viewer needs no
# neoj2k.dll or libneoj2k.so; NEOJ2K_STATIC=OFF links the shared library of the `neoj2k` package instead
# (https://github.com/anne-skydancer/3p-neoj2k: neoj2k.dll or libneoj2k.so); USE_NEOJ2K=OFF keeps OpenJPEG (the `openjpeg` package), which stays available until llimagej2coj is retired. One codec per binary: the
# choice is the implementation behind fallbackCreateLLImageJ2CImpl() (llimage/llimagej2c.cpp), with no
# runtime routing and no per-file fallback. neo-j2k decodes and encodes (uploads, bakes, snapshots), so the
# OpenJPEG package is neither installed nor linked when it is ON.
include_guard(GLOBAL)
include(Prebuilt)

# 3p-neoj2k ships windows64, linux64 and linux64-lts packages only: neo-j2k is the default there, OpenJPEG elsewhere.
if (WINDOWS OR LINUX)
  set(_neoj2k_default ON)
else ()
  set(_neoj2k_default OFF)
endif ()
option(USE_NEOJ2K "Decode and encode JPEG 2000 with neo-j2k (3p-neoj2k) instead of OpenJPEG" ${_neoj2k_default})
if (USE_NEOJ2K AND NOT (WINDOWS OR LINUX))
  message(FATAL_ERROR "USE_NEOJ2K: the 3p-neoj2k package exists for Windows and Linux only; configure with -DUSE_NEOJ2K:BOOL=OFF")
endif ()

option(NEOJ2K_STATIC "With USE_NEOJ2K, link neo-j2k statically (built from the release source below) instead of the package's shared library" ON)

add_library(ll::neoj2k INTERFACE IMPORTED GLOBAL)

if (USE_NEOJ2K AND NEOJ2K_STATIC)
  # The neo-j2k release (v0.1.3: the 3p-neoj2k package's source too), built here as its static library (no link-time
  # code generation in it, nothing exported, the library of the package without the DLL boundary): the same options
  # as 3p-neoj2k's build-cmd.sh, with the library alone. Bumping the release means the URL and its hash.
  include(FetchContent)
  set(NJ2K_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(NJ2K_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(NJ2K_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(NJ2K_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(NJ2K_BUILD_BENCH OFF CACHE BOOL "" FORCE)
  set(NJ2K_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
  set(NJ2K_INSTALL OFF CACHE BOOL "" FORCE)
  set(NJ2K_PROFILE_OPENSIM OFF CACHE BOOL "" FORCE)
  set(NJ2K_WERROR OFF CACHE BOOL "" FORCE)
  set(NJ2K_SIMD AVX2 CACHE STRING "" FORCE)
  FetchContent_Declare(neoj2k_source
      URL https://github.com/anne-skydancer/neo-j2k/archive/refs/tags/v0.1.3.tar.gz
      URL_HASH SHA256=1a68eb98421714793298efaa2c335f3220c9afa608c22acd8192e9749a188d72
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(neoj2k_source)

  # The viewer's directory-wide warnings-as-errors (00-Common.cmake) must not reach neo-j2k's own sources.
  foreach (_nj2k_target neoj2k_static neoj2k_cpu_check_static)
    get_target_property(_nj2k_options ${_nj2k_target} COMPILE_OPTIONS)
    if (_nj2k_options)
      list(FILTER _nj2k_options EXCLUDE REGEX "^(/WX|-Werror)$")
      set_property(TARGET ${_nj2k_target} PROPERTY COMPILE_OPTIONS "${_nj2k_options}")
    endif ()
  endforeach ()

  # The licence files the manifest stages (licenses/neoj2k.txt, licenses/openjpeg.txt), which the shared package carries
  # in its LICENSES/: written from the release source, the same text (3p-neoj2k's build-cmd.sh).
  file(READ "${neoj2k_source_SOURCE_DIR}/LICENSE" _nj2k_licence)
  file(READ "${neoj2k_source_SOURCE_DIR}/LICENSE.OpenJPEG" _nj2k_openjpeg_licence)
  set(_nj2k_rule "----------------------------------------------------------------------------")
  file(WRITE "${LIBS_PREBUILT_DIR}/LICENSES/neoj2k.txt"
      "${_nj2k_licence}

${_nj2k_rule}
OpenJPEG (code ported into neo-j2k): licence and notices
${_nj2k_rule}

${_nj2k_openjpeg_licence}")
  file(WRITE "${LIBS_PREBUILT_DIR}/LICENSES/openjpeg.txt" "${_nj2k_openjpeg_licence}")

  target_link_libraries(ll::neoj2k INTERFACE neoj2k_static)
  set(NEOJ2K_COMPAT_INCLUDE_DIR "${neoj2k_source_SOURCE_DIR}/include/neoj2k/compat" CACHE INTERNAL "OpenJPEG-named headers of neo-j2k (tests)")
elseif (USE_NEOJ2K)
  use_prebuilt_binary(neoj2k)

  # The import library on Windows (neoj2k.lib, for neoj2k.dll), libneoj2k.so on Linux. Exact file names, as for SoLoud and
  # OpenJPEG: a bare `neoj2k` is looked up in a listing of the directory that CMake may have cached before this package
  # was extracted (an earlier find_library in the same run), and then is not found.
  find_library(NEOJ2K_LIBRARY NAMES neoj2k.lib libneoj2k.so
      PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

  target_link_libraries(ll::neoj2k INTERFACE ${NEOJ2K_LIBRARY})
  target_include_directories(ll::neoj2k SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)

  # Headers with OpenJPEG's names (openjpeg.h, opj_config.h) that map onto neo-j2k's: for tests that write
  # their fixtures through OpenJPEG's API. Kept apart from the target so that no viewer code sees it.
  set(NEOJ2K_COMPAT_INCLUDE_DIR "${LIBS_PREBUILT_DIR}/include/neoj2k/compat" CACHE INTERNAL "OpenJPEG-named headers of neo-j2k (tests)")
endif ()
