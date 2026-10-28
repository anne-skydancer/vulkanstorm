# -*- cmake -*-
include(Prebuilt)

set(NDOF ON CACHE BOOL "Use NDOF space navigator joystick library.")

include_guard()
add_library( ll::ndof INTERFACE IMPORTED )

if (NDOF)
  if (WINDOWS OR DARWIN)
    use_prebuilt_binary(libndofdev)
  elseif (LINUX)
    use_prebuilt_binary(open-libndofdev)
  endif (WINDOWS OR DARWIN)

  if (LINUX)
    set(NDOF_LIBRARY "")
    foreach(_ndof_directory IN LISTS ARCH_PREBUILT_DIRS_RELEASE)
      if (EXISTS "${_ndof_directory}/libndofdev.a")
        set(NDOF_LIBRARY "${_ndof_directory}/libndofdev.a")
        break()
      endif ()
    endforeach()
    if (NOT NDOF_LIBRARY)
      message(FATAL_ERROR "Could not find NDOF library: libndofdev.a in ${ARCH_PREBUILT_DIRS_RELEASE}")
    endif ()
  else ()
    find_library(NDOF_LIBRARY
        NAMES
        libndofdev
        ndofdev
        PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)
  endif (LINUX)

  target_link_libraries(ll::ndof INTERFACE ${NDOF_LIBRARY})

  target_compile_definitions(ll::ndof INTERFACE LIB_NDOF=1)
endif (NDOF)
