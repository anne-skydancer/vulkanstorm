# -*- cmake -*-
# Construct the viewer version number based on the indra/VIEWER_VERSION file

if (NOT DEFINED VIEWER_SHORT_VERSION) # will be true in indra/, false in indra/newview/
    # <FS:Ansariel> Use own version file to avoid LL's year-as-major-version thing
    # set(VIEWER_VERSION_BASE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/newview/VIEWER_VERSION.txt")
    set(VIEWER_VERSION_BASE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/newview/VIEWER_VERSION_FS.txt")

    if ( EXISTS ${VIEWER_VERSION_BASE_FILE} )
        file(STRINGS ${VIEWER_VERSION_BASE_FILE} VIEWER_SHORT_VERSION LIMIT_COUNT 1)
        string(STRIP "${VIEWER_SHORT_VERSION}" VIEWER_SHORT_VERSION)
        if (NOT VIEWER_SHORT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-canary)?$")
            message(FATAL_ERROR "Invalid viewer version: ${VIEWER_SHORT_VERSION}")
        endif ()
        set(VIEWER_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(VIEWER_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(VIEWER_VERSION_PATCH "${CMAKE_MATCH_3}")
        set(VIEWER_VERSION_SUFFIX "${CMAKE_MATCH_4}")

        # The build number is the source commit count, including local worktrees.
        # Autobuild IDs may be timestamps, so they must not override Git history.
        find_program(GIT git REQUIRED)
        execute_process(COMMAND ${GIT} rev-list --count HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _version_git_result
            OUTPUT_VARIABLE VIEWER_VERSION_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (NOT _version_git_result EQUAL 0 OR NOT VIEWER_VERSION_REVISION MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Cannot obtain the source Git commit count")
        endif ()
        message(STATUS "Building '${VIEWER_CHANNEL}' Version ${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
    else ( EXISTS ${VIEWER_VERSION_BASE_FILE} )
        message(SEND_ERROR "Cannot get viewer version from '${VIEWER_VERSION_BASE_FILE}'") 
    endif ( EXISTS ${VIEWER_VERSION_BASE_FILE} )

    # <FS:PP>
    set(VIEWER_VERSION_LL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/newview/VIEWER_VERSION.txt")
    if (EXISTS ${VIEWER_VERSION_LL_FILE})
        file(STRINGS ${VIEWER_VERSION_LL_FILE} VIEWER_VERSION_LL LIMIT_COUNT 1)
        string(STRIP "${VIEWER_VERSION_LL}" VIEWER_VERSION_LL)
        message(STATUS "Upstream viewer version: ${VIEWER_VERSION_LL}")
    endif ()
    # </FS:PP>

    set(VIEWER_CHANNEL_VERSION_DEFINES
        "LL_VIEWER_CHANNEL=${VIEWER_CHANNEL}"
        "LL_VIEWER_VERSION_MAJOR=${VIEWER_VERSION_MAJOR}"
        "LL_VIEWER_VERSION_MINOR=${VIEWER_VERSION_MINOR}"
        "LL_VIEWER_VERSION_PATCH=${VIEWER_VERSION_PATCH}"
        "LL_VIEWER_VERSION_SUFFIX=\"${VIEWER_VERSION_SUFFIX}\""
        "LL_VIEWER_VERSION_BUILD=${VIEWER_VERSION_REVISION}"
        "FS_VIEWER_VERSION_GITHASH=${VIEWER_VERSION_GITHASH}"
        "LLBUILD_CONFIG=\"${CMAKE_BUILD_TYPE}\""
        )
endif (NOT DEFINED VIEWER_SHORT_VERSION)
