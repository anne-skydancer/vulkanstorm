# -*- cmake -*-

project(llimagej2coj)

# <FS:ND> when using KDU, why compile the OpenJPEG library?
# <Vulkanstorm> neither when the codec is neo-j2k (USE_NEOJ2K, llimagej2cneo)
if (NOT USE_KDU AND NOT USE_NEOJ2K)

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
    llj2cdecodeguard.h
    llj2cdecoderlimits.h
    llj2creduce.h
    )

list(APPEND llimagej2coj_SOURCE_FILES ${llimagej2coj_HEADER_FILES})

add_library (llimagej2coj ${llimagej2coj_SOURCE_FILES})
target_link_libraries( llimagej2coj
        llcommon
        llimage
        ll::openjpeg
    )

endif()
