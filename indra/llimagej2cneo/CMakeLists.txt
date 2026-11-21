# -*- cmake -*-
# SPDX-License-Identifier: GPL-3.0-or-later

project(llimagej2cneo)

# The J2C codec when USE_NEOJ2K is on (cmake/NeoJ2K.cmake); llimagej2coj, the OpenJPEG one, is built otherwise.
if (USE_NEOJ2K)

include(00-Common)
include(LLCommon)
include(LLImage)
include(NeoJ2K)

set(llimagej2cneo_SOURCE_FILES
    llimagej2cneo.cpp
    )

set(llimagej2cneo_HEADER_FILES
    CMakeLists.txt

    llimagej2cneo.h
    )

list(APPEND llimagej2cneo_SOURCE_FILES ${llimagej2cneo_HEADER_FILES})

add_library (llimagej2cneo ${llimagej2cneo_SOURCE_FILES})
# llj2creduce.h and llj2cdecodeguard.h are the codec-independent rules of llimagej2coj: header-only, shared
target_include_directories(llimagej2cneo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../llimagej2coj)
target_link_libraries( llimagej2cneo
        llcommon
        llimage
        ll::neoj2k
    )

# Standalone checks of the codec-independent rules (llj2creduce.h, llj2cdecodeguard.h, llj2cdecoderlimits.h) and of
# the JP2 extension rule, against neo-j2k: the OpenJPEG ones of llimagej2coj/tests, compiled unchanged against
# neo-j2k's OpenJPEG-compatible headers (cmake/NeoJ2K.cmake, NEOJ2K_COMPAT_INCLUDE_DIR), which map opj_* onto
# nj2k_*. Not part of the viewer build; build them by name and run them (the build and CI do):
#   llj2c_reduce_neoj2k_test llj2c_decodeguard_neoj2k_test llj2c_decoderlimits_neoj2k_test llj2c_jp2_file_neoj2k_test
if (LL_TESTS)
  set(llj2c_neoj2k_tests
      reduce:llj2creduce_openjpeg_test.cpp
      decodeguard:llj2cdecodeguard_openjpeg_test.cpp
      decoderlimits:llj2cdecoderlimits_openjpeg_test.cpp
      jp2_file:llj2c_jp2_file_openjpeg_test.cpp)
  foreach(entry IN LISTS llj2c_neoj2k_tests)
    string(REPLACE ":" ";" entry "${entry}")
    list(GET entry 0 name)
    list(GET entry 1 source)
    add_executable(llj2c_${name}_neoj2k_test EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/../llimagej2coj/tests/${source})
    target_include_directories(llj2c_${name}_neoj2k_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../llimagej2coj ${NEOJ2K_COMPAT_INCLUDE_DIR})
    target_compile_definitions(llj2c_${name}_neoj2k_test PRIVATE LL_J2C_NEOJ2K)
    target_link_libraries(llj2c_${name}_neoj2k_test ll::neoj2k)
    if (WINDOWS)
      add_custom_command(TARGET llj2c_${name}_neoj2k_test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCH_PREBUILT_DIRS_RELEASE}/neoj2k.dll" "$<TARGET_FILE_DIR:llj2c_${name}_neoj2k_test>")
    elseif (LINUX)
      # CMAKE_SKIP_RPATH (00-Common.cmake) leaves the executable no path to libneoj2k.so.0.1; these tests run
      # directly, not through LL_TEST_COMMAND's LD_LIBRARY_PATH, so they carry the prebuilt directory themselves.
      target_link_options(llj2c_${name}_neoj2k_test PRIVATE "LINKER:-rpath,${ARCH_PREBUILT_DIRS_RELEASE}")
    endif ()
  endforeach()
  # the rule alone, with no codec
  add_executable(llj2c_reduce_rule_test EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/../llimagej2coj/tests/llj2creduce_test.cpp)
  target_include_directories(llj2c_reduce_rule_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../llimagej2coj)
endif (LL_TESTS)

endif (USE_NEOJ2K)
