# -*- cmake -*-

project(llimage)

include(00-Common)
include(LLCommon)
include(LLImage)
include(JPEG)
include(Rust)
include(LLKDU)
include(ZLIBNG)
include(LLAddBuildTest)
include(bugsplat)
include(Tut)

set(llimage_SOURCE_FILES
    llimagebmp.cpp
    llimage.cpp
    llimagedimensionsinfo.cpp
    llimagedxt.cpp
    llimagefilter.cpp
    llimagej2c.cpp
    llimagejpeg.cpp
    llimagepng.cpp
    llimagetga.cpp
    llimageworker.cpp
    )

set(llimage_HEADER_FILES
    CMakeLists.txt

    llimage.h
    llimagebmp.h
    llimagedimensionsinfo.h
    llimagedxt.h
    llimagefilter.h
    llimagej2c.h
    llimagejpeg.h
    llimagepng.h
    llimagetga.h
    llimageworker.h
    llmapimagetype.h
    )

list(APPEND llimage_SOURCE_FILES ${llimage_HEADER_FILES})

add_library (llimage ${llimage_SOURCE_FILES})
target_include_directories( llimage  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
if (USE_KDU)
    target_link_libraries(llimage llkdu)
else (USE_KDU)
    target_link_libraries(llimage llimagej2coj)
endif (USE_KDU)

# PNG, TGA, BMP and DXT are Rust (vkstorm-image through vkstorm_ffi); JPEG
# stays on libjpeg-turbo (see doc/rust_port.md).
target_link_libraries(llimage
        llfilesystem
        llmath
        llcommon
        ll::libjpeg
        vkstorm_ffi
    )

# Add tests
if (LL_TESTS)
  SET(llimage_TEST_SOURCE_FILES
    llimageworker.cpp
    )
  LL_ADD_PROJECT_UNIT_TESTS(llimage "${llimage_TEST_SOURCE_FILES}")
  # The Rust raster codecs through LLImagePNG, LLImageTGA, LLImageBMP and
  # LLImageDXT over the default skin's textures.
  LL_ADD_INTEGRATION_TEST(llimagecodecs "" "llimage;llfilesystem;llmath;llcommon")
  set_property(TARGET INTEGRATION_TEST_llimagecodecs APPEND PROPERTY
      COMPILE_DEFINITIONS
      "VKS_IMAGE_TEST_SKIN_DIR=\"${CMAKE_SOURCE_DIR}/newview/skins/default\""
      "VKS_IMAGE_TEST_MANIFEST=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/llimagecodecs_manifest.txt\""
      "VKS_IMAGE_TEST_CHARACTER_DIR=\"${CMAKE_SOURCE_DIR}/newview/character\"")
endif (LL_TESTS)



