# -*- cmake -*-
# Rust integration for the vkstorm-rust track: builds the crates under
# indra/rust with cargo through corrosion and exposes them as CMake targets.
include_guard()

option(USE_RUST_J2C "Decode JPEG 2000 textures with the Rust decoder (vkstorm-j2c on hayro-jpeg2000), keeping OpenJPEG as fallback" ON)

if (USE_RUST_J2C)
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
endif (USE_RUST_J2C)
