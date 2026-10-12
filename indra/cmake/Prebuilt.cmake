# -*- cmake -*-
include_guard()

include(FindAutobuild)
if(INSTALL_PROPRIETARY)
  include(FindSCP)
endif(INSTALL_PROPRIETARY)

set(PREBUILD_TRACKING_DIR ${AUTOBUILD_INSTALL_DIR}/cmake_tracking)
# For the library installation process;
# see cmake/Prebuild.cmake for the counterpart code.
if ("${CMAKE_SOURCE_DIR}/../autobuild.xml" IS_NEWER_THAN "${PREBUILD_TRACKING_DIR}/sentinel_installed")
  file(MAKE_DIRECTORY ${PREBUILD_TRACKING_DIR})
  file(WRITE ${PREBUILD_TRACKING_DIR}/sentinel_installed "0")
endif ("${CMAKE_SOURCE_DIR}/../autobuild.xml" IS_NEWER_THAN "${PREBUILD_TRACKING_DIR}/sentinel_installed")

# The use_prebuilt_binary macro handles automated installation of package
# dependencies using autobuild.  The goal is that 'autobuild install' should
# only be run when we know we need to install a new package.  This should be
# the case in a clean checkout, or if autobuild.xml has been updated since the
# last run (encapsulated by the file ${PREBUILD_TRACKING_DIR}/sentinel_installed),
# or if a previous attempt to install the package has failed (the exit status
# of previous attempts is serialized in the file
# ${PREBUILD_TRACKING_DIR}/${_binary}_installed)
macro (use_prebuilt_binary _binary)
    if( NOT DEFINED ${_binary}_installed )
        set( ${_binary}_installed "")
    endif()

    if("${${_binary}_installed}" STREQUAL "" AND EXISTS "${PREBUILD_TRACKING_DIR}/${_binary}_installed")
        file(READ ${PREBUILD_TRACKING_DIR}/${_binary}_installed "${_binary}_installed")
        if(DEBUG_PREBUILT)
            message(STATUS "${_binary}_installed: \"${${_binary}_installed}\"")
        endif(DEBUG_PREBUILT)
    endif("${${_binary}_installed}" STREQUAL "" AND EXISTS "${PREBUILD_TRACKING_DIR}/${_binary}_installed")

    if(${PREBUILD_TRACKING_DIR}/sentinel_installed IS_NEWER_THAN ${PREBUILD_TRACKING_DIR}/${_binary}_installed OR NOT ${${_binary}_installed} EQUAL 0)
        if(DEBUG_PREBUILT)
            message(STATUS "cd ${CMAKE_SOURCE_DIR} && ${AUTOBUILD_EXECUTABLE} install
        --install-dir=${AUTOBUILD_INSTALL_DIR}
        ${_binary} ")
        endif(DEBUG_PREBUILT)
        # Software Vulkan CI retains the exact package URL/checksum and retries
        # only transient server responses. Integrity and other failures stay fatal.
        set(_vs_prebuilt_attempts 1)
        if(VS_VULKAN_DIAGNOSTICS AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
            set(_vs_prebuilt_attempts 3)
        endif()
        foreach(_vs_prebuilt_attempt RANGE 1 ${_vs_prebuilt_attempts})
            execute_process(COMMAND "${AUTOBUILD_EXECUTABLE}"
                    install
                    --install-dir=${AUTOBUILD_INSTALL_DIR}
                    ${_binary}
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    RESULT_VARIABLE ${_binary}_installed
                    OUTPUT_VARIABLE _vs_prebuilt_output
                    ERROR_VARIABLE _vs_prebuilt_error
                    )
            if(NOT "${_vs_prebuilt_output}" STREQUAL "")
                message("${_vs_prebuilt_output}")
            endif()
            if(NOT "${_vs_prebuilt_error}" STREQUAL "")
                message("${_vs_prebuilt_error}")
            endif()
            if(${_binary}_installed EQUAL 0)
                break()
            endif()
            string(TOLOWER "${_vs_prebuilt_output}\n${_vs_prebuilt_error}" _vs_prebuilt_failure)
            if(_vs_prebuilt_attempt LESS _vs_prebuilt_attempts AND
               _vs_prebuilt_failure MATCHES "http error 5[0-9][0-9]" AND
               NOT _vs_prebuilt_failure MATCHES "(^|[^a-z])(checksum|hash)([^a-z]|$).*(mismatch|failed)|corrupt|failed to (unpack|extract)")
                math(EXPR _vs_prebuilt_delay "10 * ${_vs_prebuilt_attempt}")
                message(STATUS "Transient server error downloading ${_binary}; retrying after ${_vs_prebuilt_delay}s")
                execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${_vs_prebuilt_delay})
            else()
                break()
            endif()
        endforeach()
        file(WRITE ${PREBUILD_TRACKING_DIR}/${_binary}_installed "${${_binary}_installed}")
    endif(${PREBUILD_TRACKING_DIR}/sentinel_installed IS_NEWER_THAN ${PREBUILD_TRACKING_DIR}/${_binary}_installed OR NOT ${${_binary}_installed} EQUAL 0)

    if(NOT ${_binary}_installed EQUAL 0)
        message(FATAL_ERROR
                "Failed to download or unpack prebuilt '${_binary}'."
                " Process returned ${${_binary}_installed}.")
    endif (NOT ${_binary}_installed EQUAL 0)
endmacro (use_prebuilt_binary _binary)

#Sadly we need a macro here, otherwise the return() will not properly work
macro ( use_system_binary package )
  if( USE_CONAN )
    target_link_libraries( ll::${package} INTERFACE CONAN_PKG::${package} )
    foreach( extra_pkg "${ARGN}" )
      if( extra_pkg )
        target_link_libraries( ll::${package} INTERFACE CONAN_PKG::${extra_pkg} )
      endif()
    endforeach()
    return()
  endif()
endmacro()

