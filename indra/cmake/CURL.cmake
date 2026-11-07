# -*- cmake -*-
include(Prebuilt)
include(Linking)

include_guard()
add_library( ll::libcurl INTERFACE IMPORTED )

use_system_binary(libcurl)
use_prebuilt_binary(curl)

find_library(CURL_LIBRARY
    NAMES
    libcurl.lib
    libcurl.a
    PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

target_link_libraries(ll::libcurl INTERFACE ${CURL_LIBRARY} ll::openssl ll::nghttp2 ll::zlib-ng)

if (WINDOWS)
  # <Vulkanstorm> A static curl 8 also needs these (its CURLTargets.cmake:
  # bcrypt advapi32 crypt32 ws2_32 iphlpapi). </Vulkanstorm>
  target_link_libraries(ll::libcurl INTERFACE bcrypt advapi32 crypt32 ws2_32 iphlpapi)
endif (WINDOWS)

target_include_directories( ll::libcurl SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)
