# -*- cmake -*-

project(llimage)

include(00-Common)
include(LLCommon)
include(LLImage)
include(JPEG)
include(LLKDU)
include(NeoJ2K)
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
    llpngwrapper.cpp
    )

set(llimage_HEADER_FILES
    CMakeLists.txt

    llimage.h
    llimagebmp.h
    llimagedimensionsinfo.h
    llimagedxt.h
    llimagefilter.h
    llimagefpstate.h
    llimagej2c.h
    llimagejpeg.h
    llimagepng.h
    llimagetga.h
    llimageworker.h
    llmapimagetype.h
    llpngwrapper.h
    )

list(APPEND llimage_SOURCE_FILES ${llimage_HEADER_FILES})

add_library (llimage ${llimage_SOURCE_FILES})
target_include_directories( llimage  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
if (USE_KDU)
    target_link_libraries(llimage llkdu)
elseif (USE_NEOJ2K)
    # <Vulkanstorm> the J2C codec is neo-j2k (cmake/NeoJ2K.cmake): fallbackCreateLLImageJ2CImpl() is its
    target_link_libraries(llimage llimagej2cneo)
else ()
    target_link_libraries(llimage llimagej2coj)
endif ()

target_link_libraries(llimage
        llfilesystem
        llmath
        llcommon
        ll::libpng
        ll::libjpeg
    )

# Add tests
if (LL_TESTS)
  SET(llimage_TEST_SOURCE_FILES
    llimageworker.cpp
    )
  LL_ADD_PROJECT_UNIT_TESTS(llimage "${llimage_TEST_SOURCE_FILES}")

  # INTEGRATION TESTS
  include(LLAddBuildTest)
  if (USE_NEOJ2K)
    LL_ADD_INTEGRATION_TEST(llimagej2c_decode "" "llimage;llimagej2cneo;llfilesystem;llmath;llcommon")
    # the test writes its fixtures through OpenJPEG's names, which neo-j2k's compatibility headers map
    target_include_directories(INTEGRATION_TEST_llimagej2c_decode PRIVATE ${NEOJ2K_COMPAT_INCLUDE_DIR})
    target_link_libraries(INTEGRATION_TEST_llimagej2c_decode ll::neoj2k)
    target_compile_definitions(INTEGRATION_TEST_llimagej2c_decode PRIVATE LL_J2C_NEOJ2K)
  elseif (NOT USE_KDU)
    LL_ADD_INTEGRATION_TEST(llimagej2c_decode "" "llimage;llimagej2coj;llfilesystem;llmath;llcommon")
  endif ()

  # <Vulkanstorm> A tool, not a test (tests/llimagej2c_corpus_tool.cpp): decodes a folder of codestreams through this
  # build's J2C codec, to compare the OpenJPEG and neo-j2k builds and to time them. Built on demand.
  add_executable(llimagej2c_corpus_tool EXCLUDE_FROM_ALL tests/llimagej2c_corpus_tool.cpp)
  target_link_libraries(llimagej2c_corpus_tool llimage llfilesystem llmath llcommon)
  if (WINDOWS)
    if (USE_NEOJ2K)
      set(j2c_codec_dll neoj2k.dll)
    else ()
      set(j2c_codec_dll openjp2.dll)
    endif ()
    add_custom_command(TARGET llimagej2c_corpus_tool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCH_PREBUILT_DIRS_RELEASE}/${j2c_codec_dll}" "$<TARGET_FILE_DIR:llimagej2c_corpus_tool>")
  endif ()
endif (LL_TESTS)
