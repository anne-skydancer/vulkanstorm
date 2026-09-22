# -*- cmake -*-
# <VulkanStorm>
# Mesa Zink (OpenGL-over-Vulkan) runtime, bundled as a prebuilt 3p package
# (3p-mesazink, built from the patched Mesa devel tree: AMD RX9000-series
# support + viewer crash-region fixes). Windows uses WGL DLLs; Linux uses
# a private GLVND GLX provider and matching Gallium library. Selected through
# RenderBackend=Zink; bundling is OFF by default outside the CI presets.
include(Prebuilt)

if ((WINDOWS OR LINUX) AND ADDRESS_SIZE EQUAL 64)
    option(USE_MESAZINK "Bundle the Mesa Zink OpenGL-over-Vulkan runtime" OFF)
else ()
    set(USE_MESAZINK OFF CACHE BOOL "Bundle the Mesa Zink OpenGL-over-Vulkan runtime" FORCE)
endif ()

if (USE_MESAZINK)
    use_prebuilt_binary(mesazink)
    if (WINDOWS)
        set(MESAZINK_RUNTIME_DIR "${AUTOBUILD_INSTALL_DIR}/bin/release")
        set(MESAZINK_RUNTIME_FILES opengl32.dll libgallium_wgl.dll)
    else ()
        set(MESAZINK_RUNTIME_DIR "${AUTOBUILD_INSTALL_DIR}/lib/release/mesa")
        set(MESAZINK_RUNTIME_FILES libGLX_vulkanstorm.so.0 libgallium_vulkanstorm.so)
    endif ()
    foreach(mesazink_file ${MESAZINK_RUNTIME_FILES})
        if (NOT EXISTS "${MESAZINK_RUNTIME_DIR}/${mesazink_file}")
            message(FATAL_ERROR "Missing Mesa Zink runtime file: ${mesazink_file} (re-run autobuild install)")
        endif ()
    endforeach()
endif ()
# </VulkanStorm>
