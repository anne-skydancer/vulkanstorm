# CI only: retain optimization/LTO/debug information and the ordinary feature set.
# MSBuild ignores compiler launchers. The workflows use Ninja Multi-Config on
# Windows, preserving the existing Release/RelWithDebInfo staging layout.
find_program(VS_CI_SCCACHE sccache REQUIRED)
set(CMAKE_C_COMPILER_LAUNCHER "${VS_CI_SCCACHE}" CACHE STRING "CI compiler cache" FORCE)
set(CMAKE_CXX_COMPILER_LAUNCHER "${VS_CI_SCCACHE}" CACHE STRING "CI compiler cache" FORCE)
# MSVC PCH is not cacheable; GCC PCH requires relaxed hashing. Keep strict
# content/argument hashing instead. This affects compile acceleration, not features.
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "Use content-addressed CI objects" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW CACHE STRING "Cacheable MSVC debug info" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "Debug info in objects" FORCE)
