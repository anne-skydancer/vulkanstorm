# Vulkan GHI dependency preparation. This does not enable a native renderer.
include_guard(GLOBAL)
include(Linking)
option(USE_DILIGENTCORE "Stage the DiligentCore Vulkan GHI and Vulkan loader" OFF)

if(NOT USE_DILIGENTCORE)
  return()
endif()
if(NOT (WINDOWS OR LINUX) OR NOT ADDRESS_SIZE EQUAL 64)
  message(FATAL_ERROR "DiligentCore packaging supports Windows/Linux x64 only")
endif()

# The separate Autobuild configuration is generated from pinned sources by
# scripts/build_vulkan_dependencies.py. Never substitute an unpublished URL.
foreach(package vulkan diligentcore)
  set(metadata "${AUTOBUILD_INSTALL_DIR}/metadata/${package}.json")
  if(NOT EXISTS "${metadata}")
    message(FATAL_ERROR "Missing ${package}; install the pinned Vulkan GHI packages with scripts/build_vulkan_dependencies.py --install-dir ${AUTOBUILD_INSTALL_DIR}")
  endif()
  file(READ "${CMAKE_CURRENT_LIST_DIR}/../../3p/vulkan-dependencies.json" lock)
  file(READ "${metadata}" installed)
  string(JSON expected GET "${lock}" ${package} version)
  string(JSON actual GET "${installed}" version)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${package}: expected ${expected}, installed ${actual}")
  endif()
  string(JSON expected GET "${lock}" ${package} sources)
  string(JSON actual GET "${installed}" sources)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${package}: installed source pins do not match the dependency lock")
  endif()
endforeach()

add_library(ll::vulkan SHARED IMPORTED GLOBAL)
add_library(ll::diligentcore SHARED IMPORTED GLOBAL)
set_target_properties(ll::vulkan PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${AUTOBUILD_INSTALL_DIR}/include")
set_target_properties(ll::diligentcore PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${AUTOBUILD_INSTALL_DIR}/include"
  INTERFACE_COMPILE_DEFINITIONS "DILIGENT_VK_SHARED=1")
if(WINDOWS)
  # CEF owns its own loader in bin/release. Keep GHI package ownership separate.
  set(DILIGENTCORE_RUNTIME_DIR "${AUTOBUILD_INSTALL_DIR}/bin/release/vulkan-ghi")
  set(DILIGENTCORE_RUNTIME_FILES GraphicsEngineVk_64r.dll vulkan-1.dll)
  set_target_properties(ll::vulkan PROPERTIES
    IMPORTED_LOCATION "${DILIGENTCORE_RUNTIME_DIR}/vulkan-1.dll"
    IMPORTED_IMPLIB "${AUTOBUILD_INSTALL_DIR}/lib/release/vulkan-ghi/vulkan-1.lib")
  set_target_properties(ll::diligentcore PROPERTIES
    IMPORTED_LOCATION "${DILIGENTCORE_RUNTIME_DIR}/GraphicsEngineVk_64r.dll"
    IMPORTED_IMPLIB "${AUTOBUILD_INSTALL_DIR}/lib/release/vulkan-ghi/GraphicsEngineVk_64r.lib")
  target_compile_definitions(ll::diligentcore INTERFACE PLATFORM_WIN32=1)
else()
  set(DILIGENTCORE_RUNTIME_DIR "${AUTOBUILD_INSTALL_DIR}/lib/release/vulkan-ghi")
  set(DILIGENTCORE_RUNTIME_FILES libGraphicsEngineVk.so libvulkan.so.1 libvulkan.so)
  set_target_properties(ll::vulkan PROPERTIES
    IMPORTED_LOCATION "${DILIGENTCORE_RUNTIME_DIR}/libvulkan.so.1")
  set_target_properties(ll::diligentcore PROPERTIES
    IMPORTED_LOCATION "${DILIGENTCORE_RUNTIME_DIR}/libGraphicsEngineVk.so")
  target_compile_definitions(ll::diligentcore INTERFACE PLATFORM_LINUX=1)
endif()
foreach(runtime ${DILIGENTCORE_RUNTIME_FILES})
  if(NOT EXISTS "${DILIGENTCORE_RUNTIME_DIR}/${runtime}")
    message(FATAL_ERROR "Missing Vulkan GHI runtime: ${runtime}")
  endif()
endforeach()
