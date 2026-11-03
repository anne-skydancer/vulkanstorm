# -*- cmake -*-

project(media_plugin_libvlc)

include(00-Common)
include(LLCommon)
include(LLImage)
include(LLWindow)
include(Linking)
include(PluginAPI)
include(MediaPluginBase)   # <FS:Zi> Linux volume catcher

include(LibVLCPlugin)

### media_plugin_libvlc


set(media_plugin_libvlc_SOURCE_FILES
    media_plugin_libvlc.cpp
    )

add_library(media_plugin_libvlc
    SHARED
    ${media_plugin_libvlc_SOURCE_FILES}
    )

target_link_libraries(media_plugin_libvlc
        media_plugin_base
        ll::libvlc
)

if (WINDOWS)
  set_target_properties(
    media_plugin_libvlc
    PROPERTIES
    LINK_FLAGS "/MANIFEST:NO /NODEFAULTLIB:LIBCMT"
    )

  # <Vulkanstorm> vlc_cache_gen writes libvlc's plugin cache (plugins.dat) into
  # the staged llplugin/plugins (viewer_manifest.py): without it libvlc opens
  # every plugin DLL at each start and the parcel music takes half a minute to
  # start after a fresh install or build.
  add_executable(vlc_cache_gen vlc_cache_gen.cpp)
  target_include_directories(vlc_cache_gen SYSTEM PRIVATE "${AUTOBUILD_INSTALL_DIR}/include")
  target_link_libraries(vlc_cache_gen PRIVATE ll::libvlc)
  # </Vulkanstorm>
endif (WINDOWS)

if (DARWIN)
  # Don't prepend 'lib' to the executable name, and don't embed a full path in the library's install name
  set_target_properties(
    media_plugin_libvlc
    PROPERTIES
    PREFIX ""
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@executable_path/../Frameworks"
    LINK_FLAGS "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/../base/media_plugin_base.exp"
  )

endif (DARWIN)

if( LINUX )
  set(CMAKE_SHARED_LINKER_FLAGS  "-Wl,--build-id -Wl,-rpath,'$ORIGIN:$ORIGIN/../../lib'")
endif()
