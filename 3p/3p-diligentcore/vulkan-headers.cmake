# Diligent normally creates this target from its older bundled headers. Supply
# the separately pinned 3p-vulkan headers before its ThirdParty directory runs.
if(NOT TARGET Vulkan::Headers)
  if(NOT EXISTS "${VULKAN_HEADERS_SOURCE_DIR}/include/vulkan/vulkan.h")
    message(FATAL_ERROR "The pinned 3p-vulkan headers are required")
  endif()
  add_library(Vulkan-Headers INTERFACE)
  add_library(Vulkan::Headers ALIAS Vulkan-Headers)
  target_include_directories(Vulkan-Headers INTERFACE "${VULKAN_HEADERS_SOURCE_DIR}/include")
  set(Vulkan-Headers_SOURCE_DIR "${VULKAN_HEADERS_SOURCE_DIR}")
endif()
