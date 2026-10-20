# -*- cmake -*-
include(Prebuilt)
include(Linking)
# The prebuilt freetype decodes colour bitmap fonts with libpng; nothing else
# in the viewer uses libpng (the PNG codec is Rust, see doc/rust_port.md).
include(PNG)

include_guard()
add_library( ll::freetype INTERFACE IMPORTED )

use_system_binary(freetype)
use_prebuilt_binary(freetype)
target_include_directories( ll::freetype SYSTEM INTERFACE  ${LIBS_PREBUILT_DIR}/include/freetype2/)

find_library(FREETYPE_LIBRARY
    NAMES
    freetype.lib
    libfreetype.a
    PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

target_link_libraries(ll::freetype INTERFACE ${FREETYPE_LIBRARY} ll::libpng)
