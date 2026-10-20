# -*- cmake -*-
# JPEG 2000 front end on the Rust decoder (vkstorm-j2c via vkstorm-ffi), with
# the OpenJPEG implementation as fallback and encoder.

project(llimagej2crust)

include(00-Common)
include(LLCommon)
include(LLImage)
include(Rust)

set(llimagej2crust_SOURCE_FILES
    llimagej2crust.cpp
    )

set(llimagej2crust_HEADER_FILES
    CMakeLists.txt

    llimagej2crust.h
    vkstorm_ffi.h
    )

list(APPEND llimagej2crust_SOURCE_FILES ${llimagej2crust_HEADER_FILES})

add_library (llimagej2crust ${llimagej2crust_SOURCE_FILES})
target_include_directories( llimagej2crust  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions( llimagej2crust  INTERFACE   LL_J2C_RUST=1)
target_link_libraries( llimagej2crust
        llimagej2coj
        llcommon
        llimage
        vkstorm_ffi
    )

if (LL_TESTS)
  LL_ADD_INTEGRATION_TEST(llimagej2crust "" "llimagej2crust;llimagej2coj;llimage;llfilesystem;llmath;llcommon")
  set_property(TARGET INTEGRATION_TEST_llimagej2crust APPEND PROPERTY
      COMPILE_DEFINITIONS "VKS_J2C_TEST_TEXTURE_DIR=\"${CMAKE_SOURCE_DIR}/newview/skins/default/textures\"")
endif (LL_TESTS)
