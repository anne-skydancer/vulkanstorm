# Inno Setup is a build tool, not a viewer runtime dependency.
include_guard()
set(_inno_default OFF)
# <Vulkanstorm> Velopack is the other installer: it is not defaulted over.
if (WINDOWS AND PACKAGE AND NOT USE_VELOPACK)
  set(_inno_default ON)
endif ()
option(USE_INNOSETUP "Package Windows installers with Inno Setup" ${_inno_default})
if (USE_INNOSETUP AND USE_VELOPACK)
  message(FATAL_ERROR "USE_INNOSETUP and USE_VELOPACK are both ON: choose one installer")
endif ()
