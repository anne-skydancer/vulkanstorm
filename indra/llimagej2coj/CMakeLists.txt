# -*- cmake -*-

project(llimagej2coj)

# <FS:ND> when using KDU, why compile the OpenJPEG library?
if (NOT USE_KDU)

include(00-Common)
include(LLCommon)
include(LLImage)
include(OpenJPEG)

set(llimagej2coj_SOURCE_FILES
    llimagej2coj.cpp
    )

set(llimagej2coj_HEADER_FILES
    CMakeLists.txt

    llimagej2coj.h
    )

list(APPEND llimagej2coj_SOURCE_FILES ${llimagej2coj_HEADER_FILES})

add_library (llimagej2coj ${llimagej2coj_SOURCE_FILES})
target_include_directories( llimagej2coj  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
if (USE_RUST_J2C)
    target_compile_definitions(llimagej2coj PRIVATE LL_J2C_RUST_FRONTEND=1)
endif (USE_RUST_J2C)
target_link_libraries( llimagej2coj
        llcommon
        llimage
        ll::openjpeg
    )

endif()
