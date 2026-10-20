# -*- cmake -*-
# Rust integration for the vkstorm-rust track: builds the crates under
# indra/rust with cargo through corrosion and exposes them as CMake targets.
include_guard()

# JPEG 2000 (and later rungs) are Rust on this track; there is no option to
# select another implementation.
if (TRUE)
  include(FetchContent)
  FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Corrosion)

  # Imports target `vkstorm-ffi` (a static library) built in release profile.
  corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_SOURCE_DIR}/rust/Cargo.toml
    CRATES vkstorm-ffi
    PROFILE release
  )

  # corrosion adds the system libraries Rust's std needs (kernel32, ntdll,
  # userenv, ws2_32, dbghelp on Windows) to the imported target itself.

  # Match the viewer's CPU baseline: the Release configuration targets AVX2.
  if (USE_AVX2_OPTIMIZATION)
    corrosion_add_target_rustflags(vkstorm_ffi "-Ctarget-cpu=x86-64-v3")
  elseif (USE_AVX_OPTIMIZATION)
    corrosion_add_target_rustflags(vkstorm_ffi "-Ctarget-cpu=x86-64-v2" "-Ctarget-feature=+avx")
  endif ()
endif ()
