# -*- cmake -*-
# Rust integration for the vkstorm-rust track: the target vkstorm_ffi, the C ABI
# over the vkstorm-core-rust crates (header vkstorm_ffi.h).
#
# By default it is the vkstorm-ffi autobuild package (autobuild.xml): a static
# library built by vkstorm-core-rust's CI with every rung (full), for the
# viewer's x86-64-v3 baseline, without OpenSim. Viewer builds need no Rust
# toolchain.
#
# Development only: VKS_CORE_RUST_DIR=<vkstorm-core-rust checkout> builds the
# same library from that checkout with cargo through corrosion instead, so the
# port can iterate without publishing a package. CI always uses the package.
include_guard(GLOBAL)

set(VKS_CORE_RUST_DIR "" CACHE PATH
    "Development only: build vkstorm-ffi from this vkstorm-core-rust checkout instead of the vkstorm-ffi package")

if (VKS_CORE_RUST_DIR)
  include(FetchContent)
  FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Corrosion)

  set(_vks_ffi_features full)
  # The grid profile is fixed at build time: OpenSim builds carry the legacy
  # UDP paths of the asset pipeline (and nothing else does).
  if (OPENSIM)
    list(APPEND _vks_ffi_features opensim)
  endif ()
  # Imports target `vkstorm_ffi` (a static library); corrosion adds the
  # system libraries Rust's std needs to the imported target itself.
  corrosion_import_crate(
    MANIFEST_PATH ${VKS_CORE_RUST_DIR}/Cargo.toml
    CRATES vkstorm-ffi
    PROFILE release
    FEATURES ${_vks_ffi_features}
  )
  target_include_directories(vkstorm_ffi INTERFACE ${VKS_CORE_RUST_DIR}/crates/vkstorm-ffi/include)

  # Match the viewer's CPU baseline in every crate of the build. vkstorm-core-rust's
  # .cargo/config.toml sets build.rustflags to x86-64-v3, and per-target rustc
  # flags reach only the top crate, so the baseline goes through RUSTFLAGS,
  # which replaces build.rustflags for the whole build, including without AVX.
  if (USE_AVX2_OPTIMIZATION)
    set(_vks_rustflags "-Ctarget-cpu=x86-64-v3")
  elseif (USE_AVX_OPTIMIZATION)
    set(_vks_rustflags "-Ctarget-cpu=x86-64-v2 -Ctarget-feature=+avx")
  else ()
    set(_vks_rustflags "-Ctarget-cpu=x86-64")
  endif ()
  corrosion_set_env_vars(vkstorm_ffi "RUSTFLAGS=${_vks_rustflags}")
else ()
  # The package is built for one configuration; say so instead of linking a
  # library that does not match.
  if (OPENSIM)
    message(FATAL_ERROR "The vkstorm-ffi package has no OpenSim build yet; set VKS_CORE_RUST_DIR to build it from source")
  endif ()
  if (NOT USE_AVX2_OPTIMIZATION)
    message(FATAL_ERROR "The vkstorm-ffi package targets x86-64-v3 (USE_AVX2_OPTIMIZATION); set VKS_CORE_RUST_DIR to build it for this baseline")
  endif ()

  include(Prebuilt)
  use_prebuilt_binary(vkstorm-ffi)
  find_library(VKSTORM_FFI_LIBRARY
      NAMES vkstorm_ffi.lib libvkstorm_ffi.a
      PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)
  add_library(vkstorm_ffi STATIC IMPORTED GLOBAL)
  set_target_properties(vkstorm_ffi PROPERTIES IMPORTED_LOCATION "${VKSTORM_FFI_LIBRARY}")
  target_include_directories(vkstorm_ffi SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)

  # The system libraries the static library leaves to the final link, as rustc
  # reported them when the package was built (kernel32.lib ... on Windows,
  # -lgcc_s ... on Linux). The CRT default library is the viewer's own (/MD).
  file(READ "${ARCH_PREBUILT_DIRS_RELEASE}/vkstorm_ffi.native-libs.txt" _vks_ffi_native)
  string(STRIP "${_vks_ffi_native}" _vks_ffi_native)
  separate_arguments(_vks_ffi_native UNIX_COMMAND "${_vks_ffi_native}")
  set(_vks_ffi_system_libs)
  foreach (_lib IN LISTS _vks_ffi_native)
    if (_lib MATCHES "^/defaultlib:")
      continue ()
    elseif (_lib MATCHES "^-l(.+)$")
      list(APPEND _vks_ffi_system_libs "${CMAKE_MATCH_1}")
    elseif (_lib MATCHES "^(.+)\\.lib$")
      list(APPEND _vks_ffi_system_libs "${CMAKE_MATCH_1}")
    else ()
      list(APPEND _vks_ffi_system_libs "${_lib}")
    endif ()
  endforeach ()
  target_link_libraries(vkstorm_ffi INTERFACE ${_vks_ffi_system_libs})
endif ()

# The HTTP layer (rung four) binds libcurl by hand and leaves its symbols to
# the final link: the viewer's prebuilt libcurl with its TLS stack.
include(OpenSSL)
include(NGHTTP2)
include(ZLIBNG)
include(CURL)
target_link_libraries(vkstorm_ffi INTERFACE ll::libcurl)
