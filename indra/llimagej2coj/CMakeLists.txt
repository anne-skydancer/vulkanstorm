# -*- cmake -*-
# JPEG 2000 codec for the viewer: decoding and encoding in Rust
# (vkstorm-j2c from vkstorm-core-rust, through the vkstorm-ffi static library).

project(llimagej2coj)

if (NOT USE_KDU)

include(00-Common)
include(LLCommon)
include(LLImage)
include(Rust)

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
target_link_libraries( llimagej2coj
        llcommon
        llimage
        vkstorm_ffi
    )

if (LL_TESTS)
  LL_ADD_INTEGRATION_TEST(llimagej2coj "" "llimagej2coj;llimage;llfilesystem;llmath;llcommon")
  set_property(TARGET INTEGRATION_TEST_llimagej2coj APPEND PROPERTY
      COMPILE_DEFINITIONS "VKS_J2C_TEST_TEXTURE_DIR=\"${CMAKE_SOURCE_DIR}/newview/skins/default/textures\"")
endif (LL_TESTS)

endif()
